#define _POSIX_C_SOURCE 200809L
#include "rib_parse.h"
#include "ri_callbacks.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

// Maximum sizes
#define MAX_TOKEN_LEN 4096
#define MAX_ARRAY_SIZE 65536
#define MAX_PARAMS 64
#define MAX_ERROR_LEN 256

// Token types
typedef enum {
    TOK_EOF,
    TOK_NUMBER,
    TOK_STRING,
    TOK_COMMAND,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_ERROR
} RibTokenType;

// Token structure
typedef struct {
    RibTokenType type;
    union {
        double number;
        char string[MAX_TOKEN_LEN];
    } value;
} RibToken;

// Parser structure
struct RibParser {
    FILE* input;
    RiCallbacks* callbacks;
    RibToken current_token;
    int line_number;
    int peek_char;
    int has_peek;
    char error[MAX_ERROR_LEN];

    // Temporary storage for arrays
    float* float_array;
    int float_array_capacity;
};

// Forward declarations
static void next_token(RibParser* p);
static int parse_command(RibParser* p);
static void set_error(RibParser* p, const char* msg);

// --- Lexer ---

static int peek_char(RibParser* p) {
    if (p->has_peek) {
        return p->peek_char;
    }
    p->peek_char = fgetc(p->input);
    p->has_peek = 1;
    return p->peek_char;
}

static int read_char(RibParser* p) {
    int c;
    if (p->has_peek) {
        c = p->peek_char;
        p->has_peek = 0;
    } else {
        c = fgetc(p->input);
    }
    if (c == '\n') {
        p->line_number++;
    }
    return c;
}

static void skip_whitespace_and_comments(RibParser* p) {
    while (1) {
        int c = peek_char(p);
        if (c == EOF) break;

        // Skip whitespace
        if (isspace(c)) {
            read_char(p);
            continue;
        }

        // Skip comments (# to end of line)
        if (c == '#') {
            while ((c = read_char(p)) != EOF && c != '\n') {
                // Skip to end of line
            }
            continue;
        }

        break;
    }
}

static void read_string(RibParser* p) {
    // Current char is opening quote
    read_char(p); // consume the "

    int i = 0;
    int c;
    while ((c = read_char(p)) != EOF && c != '"') {
        if (c == '\\') {
            // Handle escape sequences
            c = read_char(p);
            if (c == EOF) break;
            switch (c) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case '\\': c = '\\'; break;
                case '"': c = '"'; break;
                default: break; // Keep the character as-is
            }
        }
        if (i < MAX_TOKEN_LEN - 1) {
            p->current_token.value.string[i++] = (char)c;
        }
    }
    p->current_token.value.string[i] = '\0';
    p->current_token.type = TOK_STRING;
}

static void read_number_or_command(RibParser* p) {
    char buf[MAX_TOKEN_LEN];
    int i = 0;
    int c;

    // Read until whitespace or special char
    while ((c = peek_char(p)) != EOF) {
        if (isspace(c) || c == '[' || c == ']' || c == '"' || c == '#') {
            break;
        }
        read_char(p);
        if (i < MAX_TOKEN_LEN - 1) {
            buf[i++] = (char)c;
        }
    }
    buf[i] = '\0';

    // Try to parse as number
    char* endptr;
    double num = strtod(buf, &endptr);
    if (*endptr == '\0' && i > 0) {
        // Successfully parsed as number
        p->current_token.type = TOK_NUMBER;
        p->current_token.value.number = num;
    } else {
        // It's a command/identifier
        p->current_token.type = TOK_COMMAND;
        strncpy(p->current_token.value.string, buf, MAX_TOKEN_LEN - 1);
        p->current_token.value.string[MAX_TOKEN_LEN - 1] = '\0';
    }
}

static void next_token(RibParser* p) {
    skip_whitespace_and_comments(p);

    int c = peek_char(p);
    if (c == EOF) {
        p->current_token.type = TOK_EOF;
        return;
    }

    if (c == '"') {
        read_string(p);
        return;
    }

    if (c == '[') {
        read_char(p);
        p->current_token.type = TOK_LBRACKET;
        return;
    }

    if (c == ']') {
        read_char(p);
        p->current_token.type = TOK_RBRACKET;
        return;
    }

    // Number or command
    read_number_or_command(p);
}

// --- Helpers ---

static void set_error(RibParser* p, const char* msg) {
    snprintf(p->error, MAX_ERROR_LEN, "Line %d: %s", p->line_number, msg);
}

static int expect_number(RibParser* p, double* out) {
    if (p->current_token.type != TOK_NUMBER) {
        set_error(p, "Expected number");
        return -1;
    }
    *out = p->current_token.value.number;
    next_token(p);
    return 0;
}

static int expect_string(RibParser* p, char* out, int maxlen) {
    if (p->current_token.type != TOK_STRING) {
        set_error(p, "Expected string");
        return -1;
    }
    int srclen = (int)strlen(p->current_token.value.string);
    int copylen = srclen < maxlen - 1 ? srclen : maxlen - 1;
    memcpy(out, p->current_token.value.string, copylen);
    out[copylen] = '\0';
    next_token(p);
    return 0;
}

// Read a float array enclosed in [ ]
static int read_float_array(RibParser* p, int* count) {
    if (p->current_token.type != TOK_LBRACKET) {
        set_error(p, "Expected '['");
        return -1;
    }
    next_token(p); // consume [

    *count = 0;
    while (p->current_token.type == TOK_NUMBER) {
        if (*count >= p->float_array_capacity) {
            // Grow array
            int new_cap = p->float_array_capacity * 2;
            if (new_cap < 256) new_cap = 256;
            float* new_arr = (float*)realloc(p->float_array, new_cap * sizeof(float));
            if (!new_arr) {
                set_error(p, "Out of memory");
                return -1;
            }
            p->float_array = new_arr;
            p->float_array_capacity = new_cap;
        }
        p->float_array[(*count)++] = (float)p->current_token.value.number;
        next_token(p);
    }

    if (p->current_token.type != TOK_RBRACKET) {
        set_error(p, "Expected ']'");
        return -1;
    }
    next_token(p); // consume ]
    return 0;
}

// --- Command Parsers ---

static int parse_Format(RibParser* p) {
    double xres, yres, aspect;
    if (expect_number(p, &xres) < 0) return -1;
    if (expect_number(p, &yres) < 0) return -1;
    if (expect_number(p, &aspect) < 0) return -1;
    p->callbacks->Format((RtInt)xres, (RtInt)yres, (RtFloat)aspect);
    return 0;
}

static int parse_Display(RibParser* p) {
    char name[256], type[64], mode[64];
    if (expect_string(p, name, sizeof(name)) < 0) return -1;
    if (expect_string(p, type, sizeof(type)) < 0) return -1;
    if (expect_string(p, mode, sizeof(mode)) < 0) return -1;
    p->callbacks->Display(name, type, mode);
    return 0;
}

static int parse_Projection(RibParser* p) {
    char name[64];
    if (expect_string(p, name, sizeof(name)) < 0) return -1;

    // Parse optional parameters
    RtToken tokens[MAX_PARAMS];
    RtPointer values[MAX_PARAMS];
    float param_values[MAX_PARAMS];
    int param_count = 0;

    while (p->current_token.type == TOK_STRING) {
        tokens[param_count] = strdup(p->current_token.value.string);
        next_token(p);

        if (p->current_token.type == TOK_NUMBER) {
            param_values[param_count] = (float)p->current_token.value.number;
            values[param_count] = &param_values[param_count];
            next_token(p);
        } else if (p->current_token.type == TOK_LBRACKET) {
            int count;
            if (read_float_array(p, &count) < 0) return -1;
            // For simplicity, just use the first value
            if (count > 0) {
                param_values[param_count] = p->float_array[0];
                values[param_count] = &param_values[param_count];
            }
        }
        param_count++;
        if (param_count >= MAX_PARAMS) break;
    }

    p->callbacks->Projection(name, tokens, values, param_count);

    // Free allocated tokens
    for (int i = 0; i < param_count; i++) {
        free(tokens[i]);
    }
    return 0;
}

static int parse_PixelSamples(RibParser* p) {
    double x, y;
    if (expect_number(p, &x) < 0) return -1;
    if (expect_number(p, &y) < 0) return -1;
    p->callbacks->PixelSamples((RtFloat)x, (RtFloat)y);
    return 0;
}

static int parse_PixelFilter(RibParser* p) {
    char name[64];
    double xw, yw;
    if (expect_string(p, name, sizeof(name)) < 0) return -1;
    if (expect_number(p, &xw) < 0) return -1;
    if (expect_number(p, &yw) < 0) return -1;
    p->callbacks->PixelFilter(name, (RtFloat)xw, (RtFloat)yw);
    return 0;
}

static int parse_DepthOfField(RibParser* p) {
    double fstop, focal, dist;
    if (expect_number(p, &fstop) < 0) return -1;
    if (expect_number(p, &focal) < 0) return -1;
    if (expect_number(p, &dist) < 0) return -1;
    p->callbacks->DepthOfField((RtFloat)fstop, (RtFloat)focal, (RtFloat)dist);
    return 0;
}

static int parse_Shutter(RibParser* p) {
    double open, close;
    if (expect_number(p, &open) < 0) return -1;
    if (expect_number(p, &close) < 0) return -1;
    if (p->callbacks->Shutter) {
        p->callbacks->Shutter((RtFloat)open, (RtFloat)close);
    }
    return 0;
}

static int parse_MotionBegin(RibParser* p) {
    // MotionBegin [t0 t1 ...]
    int count;
    if (read_float_array(p, &count) < 0) return -1;
    if (count < 2) {
        set_error(p, "MotionBegin requires at least 2 time values");
        return -1;
    }
    if (p->callbacks->MotionBegin) {
        p->callbacks->MotionBegin(count, p->float_array);
    }
    return 0;
}

static int cmd_MotionEnd(RibParser* p) {
    if (p->callbacks->MotionEnd) {
        p->callbacks->MotionEnd();
    }
    return 0;
}

static int parse_FrameBegin(RibParser* p) {
    double frame_num;
    if (expect_number(p, &frame_num) < 0) return -1;
    if (p->callbacks->FrameBegin)
        p->callbacks->FrameBegin((RtInt)frame_num);
    return 0;
}

static int cmd_FrameEnd(RibParser* p) {
    if (p->callbacks->FrameEnd)
        p->callbacks->FrameEnd();
    return 0;
}

static int parse_ShadingRate(RibParser* p) {
    double size;
    if (expect_number(p, &size) < 0) return -1;
    p->callbacks->ShadingRate((RtFloat)size);
    return 0;
}

static int parse_Orientation(RibParser* p) {
    char orientation[64];
    if (expect_string(p, orientation, sizeof(orientation)) < 0) return -1;
    if (p->callbacks->Orientation) {
        p->callbacks->Orientation(orientation);
    }
    return 0;
}

static int cmd_ReverseOrientation(RibParser* p) {
    if (p->callbacks->ReverseOrientation) {
        p->callbacks->ReverseOrientation();
    }
    return 0;
}

static int parse_Sides(RibParser* p) {
    double n;
    if (expect_number(p, &n) < 0) return -1;
    if (p->callbacks->Sides) {
        p->callbacks->Sides((RtInt)n);
    }
    return 0;
}

static int parse_Matte(RibParser* p) {
    double n;
    if (expect_number(p, &n) < 0) return -1;
    if (p->callbacks->Matte) {
        p->callbacks->Matte((RtBoolean)n);
    }
    return 0;
}

static int parse_Attribute(RibParser* p) {
    char name[64];
    if (expect_string(p, name, sizeof(name)) < 0) return -1;

    RtToken tokens[MAX_PARAMS];
    RtPointer values[MAX_PARAMS];
    float* arrays[MAX_PARAMS];
    float scalars[MAX_PARAMS];
    char* strings[MAX_PARAMS];
    int param_count = 0;

    while (p->current_token.type == TOK_STRING) {
        tokens[param_count] = strdup(p->current_token.value.string);
        arrays[param_count] = NULL;
        strings[param_count] = NULL;
        next_token(p);

        if (p->current_token.type == TOK_NUMBER) {
            scalars[param_count] = (float)p->current_token.value.number;
            values[param_count] = &scalars[param_count];
            next_token(p);
        } else if (p->current_token.type == TOK_STRING) {
            strings[param_count] = strdup(p->current_token.value.string);
            values[param_count] = strings[param_count];
            next_token(p);
        } else if (p->current_token.type == TOK_LBRACKET) {
            int count;
            if (read_float_array(p, &count) < 0) return -1;
            arrays[param_count] = (float*)malloc(count * sizeof(float));
            memcpy(arrays[param_count], p->float_array, count * sizeof(float));
            values[param_count] = arrays[param_count];
        }
        param_count++;
        if (param_count >= MAX_PARAMS) break;
    }

    if (p->callbacks->Attribute) {
        p->callbacks->Attribute(name, tokens, values, param_count);
    }

    for (int i = 0; i < param_count; i++) {
        free(tokens[i]);
        if (arrays[i]) free(arrays[i]);
        if (strings[i]) free(strings[i]);
    }
    return 0;
}

static int parse_Option(RibParser* p) {
    char name[64];
    if (expect_string(p, name, sizeof(name)) < 0) return -1;

    RtToken tokens[MAX_PARAMS];
    RtPointer values[MAX_PARAMS];
    float* arrays[MAX_PARAMS];
    float scalars[MAX_PARAMS];
    char* strings[MAX_PARAMS];
    int param_count = 0;

    while (p->current_token.type == TOK_STRING) {
        tokens[param_count] = strdup(p->current_token.value.string);
        arrays[param_count] = NULL;
        strings[param_count] = NULL;
        next_token(p);

        if (p->current_token.type == TOK_NUMBER) {
            scalars[param_count] = (float)p->current_token.value.number;
            values[param_count] = &scalars[param_count];
            next_token(p);
        } else if (p->current_token.type == TOK_STRING) {
            strings[param_count] = strdup(p->current_token.value.string);
            values[param_count] = strings[param_count];
            next_token(p);
        } else if (p->current_token.type == TOK_LBRACKET) {
            int count;
            if (read_float_array(p, &count) < 0) return -1;
            arrays[param_count] = (float*)malloc(count * sizeof(float));
            memcpy(arrays[param_count], p->float_array, count * sizeof(float));
            values[param_count] = arrays[param_count];
        }
        param_count++;
        if (param_count >= MAX_PARAMS) break;
    }

    p->callbacks->Option(name, tokens, values, param_count);

    for (int i = 0; i < param_count; i++) {
        free(tokens[i]);
        if (arrays[i]) free(arrays[i]);
        if (strings[i]) free(strings[i]);
    }
    return 0;
}

static int parse_Hider(RibParser* p) {
    char type[64];
    if (expect_string(p, type, sizeof(type)) < 0) return -1;

    RtToken tokens[MAX_PARAMS];
    RtPointer values[MAX_PARAMS];
    float* arrays[MAX_PARAMS];
    float scalars[MAX_PARAMS];
    int param_count = 0;

    while (p->current_token.type == TOK_STRING) {
        tokens[param_count] = strdup(p->current_token.value.string);
        arrays[param_count] = NULL;
        next_token(p);

        if (p->current_token.type == TOK_NUMBER) {
            scalars[param_count] = (float)p->current_token.value.number;
            values[param_count] = &scalars[param_count];
            next_token(p);
        } else if (p->current_token.type == TOK_LBRACKET) {
            int count;
            if (read_float_array(p, &count) < 0) return -1;
            arrays[param_count] = (float*)malloc(count * sizeof(float));
            memcpy(arrays[param_count], p->float_array, count * sizeof(float));
            values[param_count] = arrays[param_count];
        }
        param_count++;
        if (param_count >= MAX_PARAMS) break;
    }

    if (p->callbacks->Hider) {
        p->callbacks->Hider(type, tokens, values, param_count);
    }

    for (int i = 0; i < param_count; i++) {
        free(tokens[i]);
        if (arrays[i]) free(arrays[i]);
    }
    return 0;
}

static int parse_Translate(RibParser* p) {
    double x, y, z;
    if (expect_number(p, &x) < 0) return -1;
    if (expect_number(p, &y) < 0) return -1;
    if (expect_number(p, &z) < 0) return -1;
    p->callbacks->Translate((RtFloat)x, (RtFloat)y, (RtFloat)z);
    return 0;
}

static int parse_Rotate(RibParser* p) {
    double angle, x, y, z;
    if (expect_number(p, &angle) < 0) return -1;
    if (expect_number(p, &x) < 0) return -1;
    if (expect_number(p, &y) < 0) return -1;
    if (expect_number(p, &z) < 0) return -1;
    p->callbacks->Rotate((RtFloat)angle, (RtFloat)x, (RtFloat)y, (RtFloat)z);
    return 0;
}

static int parse_Scale(RibParser* p) {
    double x, y, z;
    if (expect_number(p, &x) < 0) return -1;
    if (expect_number(p, &y) < 0) return -1;
    if (expect_number(p, &z) < 0) return -1;
    p->callbacks->Scale((RtFloat)x, (RtFloat)y, (RtFloat)z);
    return 0;
}

static int parse_Transform(RibParser* p) {
    int count;
    if (read_float_array(p, &count) < 0) return -1;
    if (count != 16) {
        set_error(p, "Transform requires 16 values");
        return -1;
    }
    RtMatrix m;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            m[i][j] = p->float_array[i * 4 + j];
        }
    }
    p->callbacks->Transform(m);
    return 0;
}

static int parse_ConcatTransform(RibParser* p) {
    int count;
    if (read_float_array(p, &count) < 0) return -1;
    if (count != 16) {
        set_error(p, "ConcatTransform requires 16 values");
        return -1;
    }
    RtMatrix m;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            m[i][j] = p->float_array[i * 4 + j];
        }
    }
    p->callbacks->ConcatTransform(m);
    return 0;
}

static int parse_Color(RibParser* p) {
    int count;
    if (read_float_array(p, &count) < 0) return -1;
    if (count < 3) {
        set_error(p, "Color requires at least 3 values");
        return -1;
    }
    RtColor c;
    c[0] = p->float_array[0];
    c[1] = p->float_array[1];
    c[2] = p->float_array[2];
    p->callbacks->Color(c);
    return 0;
}

static int parse_Opacity(RibParser* p) {
    int count;
    if (read_float_array(p, &count) < 0) return -1;
    if (count < 3) {
        set_error(p, "Opacity requires at least 3 values");
        return -1;
    }
    RtColor o;
    o[0] = p->float_array[0];
    o[1] = p->float_array[1];
    o[2] = p->float_array[2];
    p->callbacks->Opacity(o);
    return 0;
}

static int parse_Atmosphere(RibParser* p) {
    char name[64];
    if (expect_string(p, name, sizeof(name)) < 0) return -1;

    RtToken tokens[MAX_PARAMS];
    RtPointer values[MAX_PARAMS];
    float param_values[MAX_PARAMS];
    char* string_values[MAX_PARAMS];
    int param_count = 0;
    int string_count = 0;

    while (p->current_token.type == TOK_STRING) {
        tokens[param_count] = strdup(p->current_token.value.string);
        next_token(p);

        if (p->current_token.type == TOK_NUMBER) {
            param_values[param_count] = (float)p->current_token.value.number;
            values[param_count] = &param_values[param_count];
            next_token(p);
        } else if (p->current_token.type == TOK_LBRACKET) {
            // Array parameter (e.g., "background" [r g b])
            next_token(p);  // consume '['
            float* arr = &p->float_array[0];
            int arr_count = 0;
            while (p->current_token.type == TOK_NUMBER && arr_count < 16) {
                arr[arr_count++] = (float)p->current_token.value.number;
                next_token(p);
            }
            if (p->current_token.type == TOK_RBRACKET) {
                next_token(p);  // consume ']'
            }
            // Allocate persistent storage for this array
            float* arr_copy = (float*)malloc(arr_count * sizeof(float));
            memcpy(arr_copy, arr, arr_count * sizeof(float));
            values[param_count] = arr_copy;
            // Track for cleanup using string_values slots
            string_values[string_count] = (char*)arr_copy;
            string_count++;
        } else if (p->current_token.type == TOK_STRING) {
            string_values[string_count] = strdup(p->current_token.value.string);
            values[param_count] = string_values[string_count];
            string_count++;
            next_token(p);
        }
        param_count++;
        if (param_count >= MAX_PARAMS) break;
    }

    p->callbacks->Atmosphere(name, tokens, values, param_count);

    for (int i = 0; i < param_count; i++) {
        free(tokens[i]);
    }
    for (int i = 0; i < string_count; i++) {
        free(string_values[i]);
    }
    return 0;
}

static int parse_Surface(RibParser* p) {
    char name[64];
    if (expect_string(p, name, sizeof(name)) < 0) return -1;

    RtToken tokens[MAX_PARAMS];
    RtPointer values[MAX_PARAMS];
    float param_values[MAX_PARAMS];
    char* string_values[MAX_PARAMS];
    int param_count = 0;
    int string_count = 0;

    while (p->current_token.type == TOK_STRING) {
        tokens[param_count] = strdup(p->current_token.value.string);
        next_token(p);

        if (p->current_token.type == TOK_NUMBER) {
            param_values[param_count] = (float)p->current_token.value.number;
            values[param_count] = &param_values[param_count];
            next_token(p);
        } else if (p->current_token.type == TOK_STRING) {
            string_values[string_count] = strdup(p->current_token.value.string);
            values[param_count] = string_values[string_count];
            string_count++;
            next_token(p);
        }
        param_count++;
        if (param_count >= MAX_PARAMS) break;
    }

    p->callbacks->Surface(name, tokens, values, param_count);

    for (int i = 0; i < param_count; i++) {
        free(tokens[i]);
    }
    for (int i = 0; i < string_count; i++) {
        free(string_values[i]);
    }
    return 0;
}

static int parse_Sphere(RibParser* p) {
    double radius, zmin, zmax, tmax;
    if (expect_number(p, &radius) < 0) return -1;
    if (expect_number(p, &zmin) < 0) return -1;
    if (expect_number(p, &zmax) < 0) return -1;
    if (expect_number(p, &tmax) < 0) return -1;

    p->callbacks->Sphere((RtFloat)radius, (RtFloat)zmin, (RtFloat)zmax, (RtFloat)tmax,
                         NULL, NULL, 0);
    return 0;
}

static int parse_Cylinder(RibParser* p) {
    double radius, zmin, zmax, tmax;
    if (expect_number(p, &radius) < 0) return -1;
    if (expect_number(p, &zmin) < 0) return -1;
    if (expect_number(p, &zmax) < 0) return -1;
    if (expect_number(p, &tmax) < 0) return -1;

    p->callbacks->Cylinder((RtFloat)radius, (RtFloat)zmin, (RtFloat)zmax, (RtFloat)tmax,
                           NULL, NULL, 0);
    return 0;
}

static int parse_Cone(RibParser* p) {
    double height, radius, tmax;
    if (expect_number(p, &height) < 0) return -1;
    if (expect_number(p, &radius) < 0) return -1;
    if (expect_number(p, &tmax) < 0) return -1;

    p->callbacks->Cone((RtFloat)height, (RtFloat)radius, (RtFloat)tmax,
                       NULL, NULL, 0);
    return 0;
}

static int parse_Disk(RibParser* p) {
    double height, radius, tmax;
    if (expect_number(p, &height) < 0) return -1;
    if (expect_number(p, &radius) < 0) return -1;
    if (expect_number(p, &tmax) < 0) return -1;

    p->callbacks->Disk((RtFloat)height, (RtFloat)radius, (RtFloat)tmax,
                       NULL, NULL, 0);
    return 0;
}

static int parse_Torus(RibParser* p) {
    double majorrad, minorrad, phimin, phimax, tmax;
    if (expect_number(p, &majorrad) < 0) return -1;
    if (expect_number(p, &minorrad) < 0) return -1;
    if (expect_number(p, &phimin) < 0) return -1;
    if (expect_number(p, &phimax) < 0) return -1;
    if (expect_number(p, &tmax) < 0) return -1;

    p->callbacks->Torus((RtFloat)majorrad, (RtFloat)minorrad,
                        (RtFloat)phimin, (RtFloat)phimax, (RtFloat)tmax,
                        NULL, NULL, 0);
    return 0;
}

static int parse_Paraboloid(RibParser* p) {
    double rmax, zmin, zmax, tmax;
    if (expect_number(p, &rmax) < 0) return -1;
    if (expect_number(p, &zmin) < 0) return -1;
    if (expect_number(p, &zmax) < 0) return -1;
    if (expect_number(p, &tmax) < 0) return -1;

    p->callbacks->Paraboloid((RtFloat)rmax, (RtFloat)zmin, (RtFloat)zmax, (RtFloat)tmax,
                             NULL, NULL, 0);
    return 0;
}

static int parse_Hyperboloid(RibParser* p) {
    double x1, y1, z1, x2, y2, z2, tmax;
    if (expect_number(p, &x1) < 0) return -1;
    if (expect_number(p, &y1) < 0) return -1;
    if (expect_number(p, &z1) < 0) return -1;
    if (expect_number(p, &x2) < 0) return -1;
    if (expect_number(p, &y2) < 0) return -1;
    if (expect_number(p, &z2) < 0) return -1;
    if (expect_number(p, &tmax) < 0) return -1;

    RtPoint p1 = {(RtFloat)x1, (RtFloat)y1, (RtFloat)z1};
    RtPoint p2 = {(RtFloat)x2, (RtFloat)y2, (RtFloat)z2};
    p->callbacks->Hyperboloid(p1, p2, (RtFloat)tmax, NULL, NULL, 0);
    return 0;
}

static int parse_Polygon(RibParser* p) {
    RtToken tokens[MAX_PARAMS];
    RtPointer values[MAX_PARAMS];
    float* arrays[MAX_PARAMS];
    int param_count = 0;
    int nvertices = 0;

    while (p->current_token.type == TOK_STRING) {
        tokens[param_count] = strdup(p->current_token.value.string);
        next_token(p);

        if (p->current_token.type == TOK_LBRACKET) {
            int count;
            if (read_float_array(p, &count) < 0) return -1;

            arrays[param_count] = (float*)malloc(count * sizeof(float));
            memcpy(arrays[param_count], p->float_array, count * sizeof(float));
            values[param_count] = arrays[param_count];

            // Infer nvertices from "P" array
            if (strcmp(tokens[param_count], "P") == 0) {
                nvertices = count / 3;
            }
        }
        param_count++;
        if (param_count >= MAX_PARAMS) break;
    }

    p->callbacks->Polygon(nvertices, tokens, values, param_count);

    for (int i = 0; i < param_count; i++) {
        free(tokens[i]);
        free(arrays[i]);
    }
    return 0;
}

static int parse_Patch(RibParser* p) {
    char type[64];
    if (expect_string(p, type, sizeof(type)) < 0) return -1;

    RtToken tokens[MAX_PARAMS];
    RtPointer values[MAX_PARAMS];
    float* arrays[MAX_PARAMS];
    int param_count = 0;

    while (p->current_token.type == TOK_STRING) {
        tokens[param_count] = strdup(p->current_token.value.string);
        next_token(p);

        if (p->current_token.type == TOK_LBRACKET) {
            int count;
            if (read_float_array(p, &count) < 0) return -1;

            arrays[param_count] = (float*)malloc(count * sizeof(float));
            memcpy(arrays[param_count], p->float_array, count * sizeof(float));
            values[param_count] = arrays[param_count];
        }
        param_count++;
        if (param_count >= MAX_PARAMS) break;
    }

    p->callbacks->Patch(type, tokens, values, param_count);

    for (int i = 0; i < param_count; i++) {
        free(tokens[i]);
        free(arrays[i]);
    }
    return 0;
}

static int parse_Geometry(RibParser* p) {
    char type[64];
    if (expect_string(p, type, sizeof(type)) < 0) return -1;

    // Parse optional parameters
    RtToken tokens[MAX_PARAMS];
    RtPointer values[MAX_PARAMS];
    int param_count = 0;

    p->callbacks->Geometry(type, tokens, values, param_count);
    return 0;
}

static int parse_LightSource(RibParser* p) {
    char name[64];
    if (expect_string(p, name, sizeof(name)) < 0) return -1;

    RtToken tokens[MAX_PARAMS];
    RtPointer values[MAX_PARAMS];
    float* arrays[MAX_PARAMS];
    float scalars[MAX_PARAMS];
    char* string_values[MAX_PARAMS];
    int param_count = 0;

    for (int i = 0; i < MAX_PARAMS; i++) string_values[i] = NULL;

    while (p->current_token.type == TOK_STRING) {
        tokens[param_count] = strdup(p->current_token.value.string);
        next_token(p);

        if (p->current_token.type == TOK_NUMBER) {
            scalars[param_count] = (float)p->current_token.value.number;
            values[param_count] = &scalars[param_count];
            arrays[param_count] = NULL;
            next_token(p);
        } else if (p->current_token.type == TOK_LBRACKET) {
            int count;
            if (read_float_array(p, &count) < 0) return -1;

            arrays[param_count] = (float*)malloc(count * sizeof(float));
            memcpy(arrays[param_count], p->float_array, count * sizeof(float));
            values[param_count] = arrays[param_count];
        } else if (p->current_token.type == TOK_STRING) {
            // String value parameter (e.g., shadowmap filename)
            string_values[param_count] = strdup(p->current_token.value.string);
            values[param_count] = string_values[param_count];
            arrays[param_count] = NULL;
            next_token(p);
        } else {
            arrays[param_count] = NULL;
        }
        param_count++;
        if (param_count >= MAX_PARAMS) break;
    }

    p->callbacks->LightSource(name, tokens, values, param_count);

    for (int i = 0; i < param_count; i++) {
        free(tokens[i]);
        if (arrays[i]) free(arrays[i]);
        if (string_values[i]) free(string_values[i]);
    }
    return 0;
}

static int parse_Illuminate(RibParser* p) {
    char light[64];
    double onoff;
    if (expect_string(p, light, sizeof(light)) < 0) return -1;
    if (expect_number(p, &onoff) < 0) return -1;

    p->callbacks->Illuminate(light, (RtBoolean)onoff);
    return 0;
}

static int parse_Basis(RibParser* p) {
    char ubasis[64], vbasis[64];
    double ustep, vstep;

    if (expect_string(p, ubasis, sizeof(ubasis)) < 0) return -1;
    if (expect_number(p, &ustep) < 0) return -1;
    if (expect_string(p, vbasis, sizeof(vbasis)) < 0) return -1;
    if (expect_number(p, &vstep) < 0) return -1;

    p->callbacks->Basis(ubasis, (RtInt)ustep, vbasis, (RtInt)vstep);
    return 0;
}

static int parse_ObjectBegin(RibParser* p) {
    // ObjectBegin may have an optional handle number
    if (p->current_token.type == TOK_NUMBER) {
        next_token(p); // Skip the handle number
    }
    p->callbacks->ObjectBegin();
    return 0;
}

static int parse_ObjectInstance(RibParser* p) {
    double handle;
    if (expect_number(p, &handle) < 0) return -1;
    p->callbacks->ObjectInstance((RtInt)handle);
    return 0;
}

// --- Main Command Dispatcher ---

typedef struct {
    const char* name;
    int (*handler)(RibParser* p);
} CommandEntry;

static int cmd_WorldBegin(RibParser* p) { p->callbacks->WorldBegin(); return 0; }
static int cmd_WorldEnd(RibParser* p) { p->callbacks->WorldEnd(); return 0; }
static int cmd_AttributeBegin(RibParser* p) { p->callbacks->AttributeBegin(); return 0; }
static int cmd_AttributeEnd(RibParser* p) { p->callbacks->AttributeEnd(); return 0; }
static int cmd_TransformBegin(RibParser* p) { p->callbacks->TransformBegin(); return 0; }
static int cmd_TransformEnd(RibParser* p) { p->callbacks->TransformEnd(); return 0; }
static int cmd_Identity(RibParser* p) { p->callbacks->Identity(); return 0; }
static int cmd_ObjectEnd(RibParser* p) { p->callbacks->ObjectEnd(); return 0; }

static int parse_Declare(RibParser* p) {
    char name[64], declaration[256];
    if (expect_string(p, name, sizeof(name)) < 0) return -1;
    if (expect_string(p, declaration, sizeof(declaration)) < 0) return -1;
    if (p->callbacks->Declare) {
        p->callbacks->Declare(name, declaration);
    }
    return 0;
}

static const CommandEntry commands[] = {
    {"Atmosphere", parse_Atmosphere},
    {"Attribute", parse_Attribute},
    {"AttributeBegin", cmd_AttributeBegin},
    {"AttributeEnd", cmd_AttributeEnd},
    {"Basis", parse_Basis},
    {"Color", parse_Color},
    {"ConcatTransform", parse_ConcatTransform},
    {"Cone", parse_Cone},
    {"Cylinder", parse_Cylinder},
    {"Declare", parse_Declare},
    {"DepthOfField", parse_DepthOfField},
    {"Disk", parse_Disk},
    {"Display", parse_Display},
    {"Format", parse_Format},
    {"FrameBegin", parse_FrameBegin},
    {"FrameEnd", cmd_FrameEnd},
    {"Geometry", parse_Geometry},
    {"Hider", parse_Hider},
    {"Hyperboloid", parse_Hyperboloid},
    {"Identity", cmd_Identity},
    {"Illuminate", parse_Illuminate},
    {"LightSource", parse_LightSource},
    {"Matte", parse_Matte},
    {"MotionBegin", parse_MotionBegin},
    {"MotionEnd", cmd_MotionEnd},
    {"ObjectBegin", parse_ObjectBegin},
    {"ObjectEnd", cmd_ObjectEnd},
    {"ObjectInstance", parse_ObjectInstance},
    {"Opacity", parse_Opacity},
    {"Option", parse_Option},
    {"Orientation", parse_Orientation},
    {"Paraboloid", parse_Paraboloid},
    {"Patch", parse_Patch},
    {"PixelFilter", parse_PixelFilter},
    {"PixelSamples", parse_PixelSamples},
    {"Polygon", parse_Polygon},
    {"Projection", parse_Projection},
    {"ReverseOrientation", cmd_ReverseOrientation},
    {"Rotate", parse_Rotate},
    {"Scale", parse_Scale},
    {"ShadingRate", parse_ShadingRate},
    {"Shutter", parse_Shutter},
    {"Sides", parse_Sides},
    {"Sphere", parse_Sphere},
    {"Surface", parse_Surface},
    {"Torus", parse_Torus},
    {"Transform", parse_Transform},
    {"TransformBegin", cmd_TransformBegin},
    {"TransformEnd", cmd_TransformEnd},
    {"Translate", parse_Translate},
    {"WorldBegin", cmd_WorldBegin},
    {"WorldEnd", cmd_WorldEnd},
    {NULL, NULL}
};

static int parse_command(RibParser* p) {
    if (p->current_token.type != TOK_COMMAND) {
        set_error(p, "Expected command");
        return -1;
    }

    const char* cmd = p->current_token.value.string;

    // Skip version and structure declarations
    if (strcmp(cmd, "version") == 0 ||
        strncmp(cmd, "##", 2) == 0) {
        // Skip to end of line
        while (p->current_token.type != TOK_EOF) {
            int c = peek_char(p);
            if (c == '\n' || c == EOF) break;
            read_char(p);
        }
        next_token(p);
        return 0;
    }

    // Find command handler
    for (int i = 0; commands[i].name != NULL; i++) {
        if (strcmp(cmd, commands[i].name) == 0) {
            next_token(p); // consume command
            return commands[i].handler(p);
        }
    }

    // Unknown command - skip it
    char cmd_truncated[64];
    int cmd_len = (int)strlen(cmd);
    int copy_len = cmd_len < 63 ? cmd_len : 63;
    memcpy(cmd_truncated, cmd, copy_len);
    cmd_truncated[copy_len] = '\0';
    fprintf(stderr, "Warning: Unknown command: %s (skipping) at line %d\n",
            cmd_truncated, p->line_number);
    next_token(p);
    return 0;
}

// --- Public API ---

RibParser* rib_parser_create(RiCallbacks* callbacks) {
    if (!callbacks) return NULL;

    RibParser* p = (RibParser*)calloc(1, sizeof(RibParser));
    if (!p) return NULL;

    p->callbacks = callbacks;
    p->line_number = 1;
    p->float_array_capacity = 256;
    p->float_array = (float*)malloc(p->float_array_capacity * sizeof(float));
    if (!p->float_array) {
        free(p);
        return NULL;
    }

    return p;
}

void rib_parser_destroy(RibParser* parser) {
    if (!parser) return;
    if (parser->float_array) free(parser->float_array);
    free(parser);
}

int rib_parser_parse_file(RibParser* parser, const char* filename) {
    if (!parser || !filename) return -1;

    FILE* f = fopen(filename, "r");
    if (!f) {
        snprintf(parser->error, MAX_ERROR_LEN, "Cannot open file: %s", filename);
        return -1;
    }

    int result = rib_parser_parse_stream(parser, f);
    fclose(f);
    return result;
}

int rib_parser_parse_stream(RibParser* parser, FILE* stream) {
    if (!parser || !stream) return -1;

    parser->input = stream;
    parser->line_number = 1;
    parser->has_peek = 0;
    parser->error[0] = '\0';

    // Call Begin
    parser->callbacks->Begin(NULL);

    // Get first token
    next_token(parser);

    // Parse commands until EOF
    while (parser->current_token.type != TOK_EOF) {
        if (parse_command(parser) < 0) {
            parser->callbacks->End();
            return -1;
        }
    }

    // Call End
    parser->callbacks->End();
    return 0;
}

const char* rib_parser_get_error(RibParser* parser) {
    if (!parser || parser->error[0] == '\0') return NULL;
    return parser->error;
}

int rib_parser_get_line(RibParser* parser) {
    return parser ? parser->line_number : 0;
}
