/**
 * ri_declare.c - Variable declaration management
 *
 * Handles RiDeclare and the declaration lookup table.
 */

#include "ri_internal.h"

// --- Helper Functions ---

// Helper to parse storage class from string
static RiStorageClass ri_parse_storage_class(const char* str) {
    if (strcmp(str, "constant") == 0) return RI_CLASS_CONSTANT;
    if (strcmp(str, "uniform") == 0) return RI_CLASS_UNIFORM;
    if (strcmp(str, "varying") == 0) return RI_CLASS_VARYING;
    if (strcmp(str, "vertex") == 0) return RI_CLASS_VERTEX;
    return RI_CLASS_UNIFORM;  // Default
}

// Helper to parse variable type from string
static RiVarType ri_parse_var_type(const char* str) {
    if (strcmp(str, "float") == 0) return RI_TYPE_FLOAT;
    if (strcmp(str, "integer") == 0) return RI_TYPE_INTEGER;
    if (strcmp(str, "string") == 0) return RI_TYPE_STRING;
    if (strcmp(str, "color") == 0) return RI_TYPE_COLOR;
    if (strcmp(str, "point") == 0) return RI_TYPE_POINT;
    if (strcmp(str, "vector") == 0) return RI_TYPE_VECTOR;
    if (strcmp(str, "normal") == 0) return RI_TYPE_NORMAL;
    if (strcmp(str, "hpoint") == 0) return RI_TYPE_HPOINT;
    if (strcmp(str, "matrix") == 0) return RI_TYPE_MATRIX;
    return RI_TYPE_FLOAT;  // Default
}

// Internal helper to add a declaration
static void ri_add_declaration(const char* name, RiStorageClass sclass, RiVarType type, int array_size) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx || ctx->num_declarations >= MAX_DECLARATIONS) return;

    // Check if already declared (update existing)
    for (int i = 0; i < ctx->num_declarations; i++) {
        if (strcmp(ctx->declarations[i].name, name) == 0) {
            ctx->declarations[i].sclass = sclass;
            ctx->declarations[i].type = type;
            ctx->declarations[i].array_size = array_size;
            return;
        }
    }

    // Add new declaration
    RiDeclaration* decl = &ctx->declarations[ctx->num_declarations++];
    strncpy(decl->name, name, sizeof(decl->name) - 1);
    decl->name[sizeof(decl->name) - 1] = '\0';
    decl->sclass = sclass;
    decl->type = type;
    decl->array_size = array_size;
}

// --- Public Functions ---

// Install predefined standard variable declarations
void ri_install_standard_declarations(void) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    // Position variables (Table 5.1 from RISpec)
    ri_add_declaration("P", RI_CLASS_VERTEX, RI_TYPE_POINT, 1);
    ri_add_declaration("Pz", RI_CLASS_VERTEX, RI_TYPE_POINT, 1);  // Actually 1 float, but encoded as point
    ri_add_declaration("Pw", RI_CLASS_VERTEX, RI_TYPE_HPOINT, 1);

    // Normal variables
    ri_add_declaration("N", RI_CLASS_VARYING, RI_TYPE_NORMAL, 1);
    ri_add_declaration("Np", RI_CLASS_UNIFORM, RI_TYPE_NORMAL, 1);

    // Color and opacity
    ri_add_declaration("Cs", RI_CLASS_VARYING, RI_TYPE_COLOR, 1);
    ri_add_declaration("Os", RI_CLASS_VARYING, RI_TYPE_COLOR, 1);

    // Texture coordinates
    ri_add_declaration("s", RI_CLASS_VARYING, RI_TYPE_FLOAT, 1);
    ri_add_declaration("t", RI_CLASS_VARYING, RI_TYPE_FLOAT, 1);
    ri_add_declaration("st", RI_CLASS_VARYING, RI_TYPE_FLOAT, 2);  // 2-float array

    // Light parameters
    ri_add_declaration("intensity", RI_CLASS_UNIFORM, RI_TYPE_FLOAT, 1);
    ri_add_declaration("lightcolor", RI_CLASS_UNIFORM, RI_TYPE_COLOR, 1);
    ri_add_declaration("from", RI_CLASS_UNIFORM, RI_TYPE_POINT, 1);
    ri_add_declaration("to", RI_CLASS_UNIFORM, RI_TYPE_POINT, 1);

    // Shader parameters
    ri_add_declaration("Ka", RI_CLASS_UNIFORM, RI_TYPE_FLOAT, 1);
    ri_add_declaration("Kd", RI_CLASS_UNIFORM, RI_TYPE_FLOAT, 1);
    ri_add_declaration("Ks", RI_CLASS_UNIFORM, RI_TYPE_FLOAT, 1);
    ri_add_declaration("roughness", RI_CLASS_UNIFORM, RI_TYPE_FLOAT, 1);
    ri_add_declaration("specularcolor", RI_CLASS_UNIFORM, RI_TYPE_COLOR, 1);
    ri_add_declaration("texturename", RI_CLASS_UNIFORM, RI_TYPE_STRING, 1);

    // Spotlight parameters
    ri_add_declaration("coneangle", RI_CLASS_UNIFORM, RI_TYPE_FLOAT, 1);
    ri_add_declaration("conedeltaangle", RI_CLASS_UNIFORM, RI_TYPE_FLOAT, 1);
    ri_add_declaration("beamdistribution", RI_CLASS_UNIFORM, RI_TYPE_FLOAT, 1);
}

RtToken RiDeclare(const char* name, const char* declaration) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx || !name || !declaration) return NULL;

    // Parse declaration string: [class] [type] ['[' n ']']
    // Examples: "varying float", "uniform point", "vertex float[3]"

    char decl_copy[256];
    strncpy(decl_copy, declaration, sizeof(decl_copy) - 1);
    decl_copy[sizeof(decl_copy) - 1] = '\0';

    RiStorageClass sclass = RI_CLASS_UNIFORM;  // Default storage class
    RiVarType type = RI_TYPE_FLOAT;            // Default type
    int array_size = 1;                        // Default: not an array

    // Tokenize the declaration string
    char* tokens[4];
    int token_count = 0;
    char* tok = strtok(decl_copy, " \t");
    while (tok && token_count < 4) {
        tokens[token_count++] = tok;
        tok = strtok(NULL, " \t");
    }

    if (token_count == 0) {
        // Empty declaration - use defaults
    } else if (token_count == 1) {
        // Just type (e.g., "float", "point")
        // Check for array notation
        char* bracket = strchr(tokens[0], '[');
        if (bracket) {
            *bracket = '\0';
            array_size = atoi(bracket + 1);
            if (array_size < 1) array_size = 1;
        }
        type = ri_parse_var_type(tokens[0]);
    } else {
        // Two or more tokens: [class] type [array]
        // First token could be class or type
        // Try parsing first token as class
        if (strcmp(tokens[0], "constant") == 0 ||
            strcmp(tokens[0], "uniform") == 0 ||
            strcmp(tokens[0], "varying") == 0 ||
            strcmp(tokens[0], "vertex") == 0) {
            sclass = ri_parse_storage_class(tokens[0]);
            // Second token is type
            if (token_count >= 2) {
                char* bracket = strchr(tokens[1], '[');
                if (bracket) {
                    *bracket = '\0';
                    array_size = atoi(bracket + 1);
                    if (array_size < 1) array_size = 1;
                }
                type = ri_parse_var_type(tokens[1]);
            }
        } else {
            // First token is type
            char* bracket = strchr(tokens[0], '[');
            if (bracket) {
                *bracket = '\0';
                array_size = atoi(bracket + 1);
                if (array_size < 1) array_size = 1;
            }
            type = ri_parse_var_type(tokens[0]);
        }

        // Check for separate array specification (e.g., "float" "[3]")
        for (int i = 1; i < token_count; i++) {
            if (tokens[i][0] == '[') {
                array_size = atoi(tokens[i] + 1);
                if (array_size < 1) array_size = 1;
            }
        }
    }

    // Add the declaration
    ri_add_declaration(name, sclass, type, array_size);

    // Return the name as the token (for chained usage)
    return (RtToken)name;
}

// Lookup a declaration by name
const RiDeclaration* ri_lookup_declaration(const char* name) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx || !name) return NULL;

    for (int i = 0; i < ctx->num_declarations; i++) {
        if (strcmp(ctx->declarations[i].name, name) == 0) {
            return &ctx->declarations[i];
        }
    }
    return NULL;
}

// Get total float count for a declaration (components * array_size)
int ri_declaration_float_count(const RiDeclaration* decl) {
    if (!decl) return 0;
    return rh_type_component_count(decl->type) * decl->array_size;
}

/* Parse an inline token like "float sphere" or "uniform color Kd".
 * Finds the last space: bare_name = last word, decl_string = prefix.
 * Returns 1 if inline type found, 0 if plain bare name.
 * Caller provides writable output buffers. */
int ri_parse_inline_token(const char* token,
                           char* bare_name_out, int bare_name_size,
                           char* decl_string_out, int decl_size) {
    if (!token || !bare_name_out || bare_name_size < 1) return 0;
    const char* last_sp = NULL;
    for (const char* p = token; *p; p++)
        if (*p == ' ') last_sp = p;
    if (!last_sp) {
        /* Plain bare name */
        int len = (int)strlen(token);
        int cp = len < bare_name_size - 1 ? len : bare_name_size - 1;
        memcpy(bare_name_out, token, cp); bare_name_out[cp] = '\0';
        if (decl_string_out && decl_size > 0) decl_string_out[0] = '\0';
        return 0;
    }
    /* Name = text after last space */
    int nlen = (int)strlen(last_sp + 1);
    int ncp = nlen < bare_name_size - 1 ? nlen : bare_name_size - 1;
    memcpy(bare_name_out, last_sp + 1, ncp); bare_name_out[ncp] = '\0';
    /* Decl = text before last space */
    if (decl_string_out && decl_size > 0) {
        int dlen = (int)(last_sp - token);
        int dcp = dlen < decl_size - 1 ? dlen : decl_size - 1;
        memcpy(decl_string_out, token, dcp); decl_string_out[dcp] = '\0';
    }
    return 1;
}
