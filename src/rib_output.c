#include "rib_output.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define RIBOUT_MAX_DECLS 128

typedef struct {
    char name[64];
    char decl_str[128];  /* e.g. "float", "uniform color" */
} RibOutDecl;

// RIB Output Context
typedef struct {
    FILE* output;
    int binary_mode;
    int indent_level;
    int owns_file;      // 1 if we opened the file, 0 if user provided stream
    int in_world;       // Track if we're inside WorldBegin/End
    int object_counter; // For generating object handles
    RibOutDecl user_decls[RIBOUT_MAX_DECLS];
    int num_user_decls;
} RibOutputContext;

static RibOutputContext* g_rib_ctx = NULL;

// Helper: Write indentation
static void write_indent(void) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    for (int i = 0; i < g_rib_ctx->indent_level; i++) {
        fprintf(g_rib_ctx->output, "    ");
    }
}

// Helper: Write a float array
static void write_float_array(const float* arr, int count) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    fprintf(g_rib_ctx->output, "[");
    for (int i = 0; i < count; i++) {
        if (i > 0) fprintf(g_rib_ctx->output, " ");
        fprintf(g_rib_ctx->output, "%g", arr[i]);
    }
    fprintf(g_rib_ctx->output, "]");
}

// Helper: Write a 4x4 matrix
static void write_matrix(RtMatrix m) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    fprintf(g_rib_ctx->output, "[");
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (i > 0 || j > 0) fprintf(g_rib_ctx->output, " ");
            fprintf(g_rib_ctx->output, "%g", m[i][j]);
        }
    }
    fprintf(g_rib_ctx->output, "]");
}

// Builtin parameter table (name, float-components, is_string)
typedef struct { const char* name; int components; int is_string; } RibOutBuiltin;
static const RibOutBuiltin ribout_builtins[] = {
    {"P", 3, 0}, {"Pz", 1, 0}, {"Pw", 4, 0},
    {"N", 3, 0}, {"Np", 3, 0}, {"Cs", 3, 0}, {"Os", 3, 0},
    {"s", 1, 0}, {"t", 1, 0}, {"st", 2, 0},
    {"intensity", 1, 0}, {"lightcolor", 3, 0},
    {"from", 3, 0}, {"to", 3, 0},
    {"Ka", 1, 0}, {"Kd", 1, 0}, {"Ks", 1, 0},
    {"roughness", 1, 0}, {"specularcolor", 3, 0},
    {"texturename", 1, 1}, {"mapname", 1, 1}, {"filename", 1, 1},
    {"coneangle", 1, 0}, {"conedeltaangle", 1, 0}, {"beamdistribution", 1, 0},
    {"fov", 1, 0}, {"background", 3, 0},
    {"mindistance", 1, 0}, {"maxdistance", 1, 0}, {"distance", 1, 0},
    {NULL, 0, 0}
};

static const RibOutBuiltin* ribout_find_builtin(const char* name) {
    for (int i = 0; ribout_builtins[i].name; i++)
        if (strcmp(ribout_builtins[i].name, name) == 0)
            return &ribout_builtins[i];
    return NULL;
}

static const char* ribout_lookup_user_decl(const char* name) {
    if (!g_rib_ctx) return NULL;
    for (int i = 0; i < g_rib_ctx->num_user_decls; i++)
        if (strcmp(g_rib_ctx->user_decls[i].name, name) == 0)
            return g_rib_ctx->user_decls[i].decl_str;
    return NULL;
}

/* Return the base type from a decl string (last space-separated word). */
static const char* ribout_decl_type(const char* s) {
    const char* last = s;
    while (*s) {
        while (*s == ' ') s++;
        if (*s) { last = s; while (*s && *s != ' ') s++; }
    }
    return last;
}

static int ribout_decl_components(const char* decl_str) {
    const char* t = ribout_decl_type(decl_str);
    if (strncmp(t, "color",  5) == 0 || strncmp(t, "point",  5) == 0 ||
        strncmp(t, "vector", 6) == 0 || strncmp(t, "normal", 6) == 0) return 3;
    if (strncmp(t, "hpoint", 6) == 0) return 4;
    if (strncmp(t, "matrix", 6) == 0) return 16;
    return 1;
}

static int ribout_decl_is_string(const char* decl_str) {
    return strncmp(ribout_decl_type(decl_str), "string", 6) == 0;
}

// Helper: Write parameter list (token-value pairs)
static void write_params(RtToken* tokens, RtPointer* values, int count) {
    if (!g_rib_ctx || !g_rib_ctx->output || count <= 0) return;
    for (int i = 0; i < count; i++) {
        RtToken token = tokens[i];
        if (!token) break;
        const RibOutBuiltin* bi = ribout_find_builtin(token);
        if (bi) {
            /* Standard pre-declared: emit bare name */
            fprintf(g_rib_ctx->output, " \"%s\" ", token);
            if (bi->is_string) {
                const char* sval = (const char*)values[i];
                fprintf(g_rib_ctx->output, "\"%s\"", sval ? sval : "");
            } else if (bi->components == 1) {
                fprintf(g_rib_ctx->output, "%g", *(float*)values[i]);
            } else {
                write_float_array((float*)values[i], bi->components);
            }
        } else {
            const char* ud = ribout_lookup_user_decl(token);
            /* User-declared: emit inline type; unknown: emit bare name */
            if (ud)
                fprintf(g_rib_ctx->output, " \"%s %s\" ", ud, token);
            else
                fprintf(g_rib_ctx->output, " \"%s\" ", token);
            int comps = ud ? ribout_decl_components(ud) : 1;
            int is_str = ud ? ribout_decl_is_string(ud) : 0;
            if (is_str) {
                const char* sval = (const char*)values[i];
                fprintf(g_rib_ctx->output, "\"%s\"", sval ? sval : "");
            } else if (comps == 1) {
                fprintf(g_rib_ctx->output, "%g", *(float*)values[i]);
            } else {
                write_float_array((float*)values[i], comps);
            }
        }
    }
}

// --- Initialization ---

int rib_output_begin(const char* filename, int binary_mode) {
    if (g_rib_ctx) {
        rib_output_end();
    }

    g_rib_ctx = (RibOutputContext*)calloc(1, sizeof(RibOutputContext));
    if (!g_rib_ctx) return -1;

    if (filename) {
        g_rib_ctx->output = fopen(filename, binary_mode ? "wb" : "w");
        if (!g_rib_ctx->output) {
            free(g_rib_ctx);
            g_rib_ctx = NULL;
            return -1;
        }
        g_rib_ctx->owns_file = 1;
    } else {
        g_rib_ctx->output = stdout;
        g_rib_ctx->owns_file = 0;
    }

    g_rib_ctx->binary_mode = binary_mode;
    g_rib_ctx->indent_level = 0;
    g_rib_ctx->in_world = 0;
    g_rib_ctx->object_counter = 0;

    return 0;
}

int rib_output_begin_stream(FILE* stream, int binary_mode) {
    if (g_rib_ctx) {
        rib_output_end();
    }

    g_rib_ctx = (RibOutputContext*)calloc(1, sizeof(RibOutputContext));
    if (!g_rib_ctx) return -1;

    g_rib_ctx->output = stream;
    g_rib_ctx->owns_file = 0;
    g_rib_ctx->binary_mode = binary_mode;
    g_rib_ctx->indent_level = 0;
    g_rib_ctx->in_world = 0;
    g_rib_ctx->object_counter = 0;

    return 0;
}

void rib_output_end(void) {
    if (!g_rib_ctx) return;

    if (g_rib_ctx->owns_file && g_rib_ctx->output) {
        fclose(g_rib_ctx->output);
    }

    free(g_rib_ctx);
    g_rib_ctx = NULL;
}

// --- RIB Output Callback Functions ---

static void ribout_Begin(RtToken name) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    fprintf(g_rib_ctx->output, "##RenderMan RIB-Structure 1.1\n");
    fprintf(g_rib_ctx->output, "version 3.03\n");
    (void)name; // Name is typically RI_NULL for file output
}

static void ribout_End(void) {
    // Nothing to output for RiEnd - cleanup happens in rib_output_end()
}

static void ribout_FrameBegin(RtInt frame) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "FrameBegin %d\n", frame);
    g_rib_ctx->indent_level++;
}

static void ribout_FrameEnd(void) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    if (g_rib_ctx->indent_level > 0) g_rib_ctx->indent_level--;
    write_indent();
    fprintf(g_rib_ctx->output, "FrameEnd\n");
}

static void ribout_Declare(const char* name, const char* declaration) {
    if (!g_rib_ctx || !name || !declaration) return;
    /* Skip names already in the builtin table */
    if (ribout_find_builtin(name)) return;
    if (g_rib_ctx->num_user_decls >= RIBOUT_MAX_DECLS) return;
    /* Update existing entry if already stored */
    for (int i = 0; i < g_rib_ctx->num_user_decls; i++) {
        if (strcmp(g_rib_ctx->user_decls[i].name, name) == 0) {
            int dcp = (int)strlen(declaration);
            if (dcp > 127) dcp = 127;
            memcpy(g_rib_ctx->user_decls[i].decl_str, declaration, dcp);
            g_rib_ctx->user_decls[i].decl_str[dcp] = '\0';
            return;
        }
    }
    RibOutDecl* d = &g_rib_ctx->user_decls[g_rib_ctx->num_user_decls++];
    int ncp = (int)strlen(name);        if (ncp > 63)  ncp = 63;
    int dcp = (int)strlen(declaration); if (dcp > 127) dcp = 127;
    memcpy(d->name,     name,        ncp); d->name[ncp]     = '\0';
    memcpy(d->decl_str, declaration, dcp); d->decl_str[dcp] = '\0';
}

static void ribout_Format(RtInt xres, RtInt yres, RtFloat aspect) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Format %d %d %g\n", xres, yres, aspect);
}

static void ribout_Display(RtToken name, RtToken type, RtToken mode) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Display \"%s\" \"%s\" \"%s\"\n",
            name ? name : "", type ? type : "", mode ? mode : "");
}

static void ribout_Projection(RtToken name, RtToken* tokens, RtPointer* values, int count) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Projection \"%s\"", name ? name : "");
    write_params(tokens, values, count);
    fprintf(g_rib_ctx->output, "\n");
}

static void ribout_PixelSamples(RtFloat xsamples, RtFloat ysamples) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "PixelSamples %g %g\n", xsamples, ysamples);
}

static void ribout_PixelFilter(RtToken filtername, RtFloat xwidth, RtFloat ywidth) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "PixelFilter \"%s\" %g %g\n",
            filtername ? filtername : "box", xwidth, ywidth);
}

static void ribout_DepthOfField(RtFloat fstop, RtFloat focallength, RtFloat focaldistance) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "DepthOfField %g %g %g\n", fstop, focallength, focaldistance);
}

static void ribout_Clipping(RtFloat nearclip, RtFloat farclip) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Clipping %g %g\n", nearclip, farclip);
}

static void ribout_ShadingRate(RtFloat size) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "ShadingRate %g\n", size);
}

static void ribout_Option(RtToken name, RtToken* tokens, RtPointer* values, int count) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Option \"%s\"", name ? name : "");
    write_params(tokens, values, count);
    fprintf(g_rib_ctx->output, "\n");
}

static void ribout_Attribute(RtToken name, RtToken* tokens, RtPointer* values, int count) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Attribute \"%s\"", name ? name : "");
    write_params(tokens, values, count);
    fprintf(g_rib_ctx->output, "\n");
}

static void ribout_AttributeBegin(void) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "AttributeBegin\n");
    g_rib_ctx->indent_level++;
}

static void ribout_AttributeEnd(void) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    if (g_rib_ctx->indent_level > 0) g_rib_ctx->indent_level--;
    write_indent();
    fprintf(g_rib_ctx->output, "AttributeEnd\n");
}

static void ribout_TransformBegin(void) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "TransformBegin\n");
    g_rib_ctx->indent_level++;
}

static void ribout_TransformEnd(void) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    if (g_rib_ctx->indent_level > 0) g_rib_ctx->indent_level--;
    write_indent();
    fprintf(g_rib_ctx->output, "TransformEnd\n");
}

static void ribout_Identity(void) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Identity\n");
}

static void ribout_Transform(RtMatrix transform) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Transform ");
    write_matrix(transform);
    fprintf(g_rib_ctx->output, "\n");
}

static void ribout_ConcatTransform(RtMatrix transform) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "ConcatTransform ");
    write_matrix(transform);
    fprintf(g_rib_ctx->output, "\n");
}

static void ribout_Translate(RtFloat dx, RtFloat dy, RtFloat dz) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Translate %g %g %g\n", dx, dy, dz);
}

static void ribout_Rotate(RtFloat angle, RtFloat dx, RtFloat dy, RtFloat dz) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Rotate %g %g %g %g\n", angle, dx, dy, dz);
}

static void ribout_Scale(RtFloat sx, RtFloat sy, RtFloat sz) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Scale %g %g %g\n", sx, sy, sz);
}

static void ribout_CoordinateSystem(RtToken name) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "CoordinateSystem \"%s\"\n", name);
}

static void ribout_Basis(RtToken ubasis, RtInt ustep, RtToken vbasis, RtInt vstep) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Basis \"%s\" %d \"%s\" %d\n",
            ubasis ? ubasis : "bezier", ustep,
            vbasis ? vbasis : "bezier", vstep);
}

static void ribout_Color(RtColor color) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Color [%g %g %g]\n", color[0], color[1], color[2]);
}

static void ribout_Opacity(RtColor opacity) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Opacity [%g %g %g]\n", opacity[0], opacity[1], opacity[2]);
}

static void ribout_Surface(RtToken name, RtToken* tokens, RtPointer* values, int count) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Surface \"%s\"", name ? name : "");
    write_params(tokens, values, count);
    fprintf(g_rib_ctx->output, "\n");
}

static void ribout_Atmosphere(RtToken name, RtToken* tokens, RtPointer* values, int count) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Atmosphere \"%s\"", name ? name : "");
    write_params(tokens, values, count);
    fprintf(g_rib_ctx->output, "\n");
}

static void ribout_Orientation(RtToken orientation) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Orientation \"%s\"\n", orientation ? orientation : "rh");
}

static void ribout_ReverseOrientation(void) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "ReverseOrientation\n");
}

static void ribout_Sides(RtInt nsides) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Sides %d\n", nsides);
}

static void ribout_Matte(RtBoolean onoff) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Matte %d\n", onoff);
}

static void ribout_WorldBegin(void) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "WorldBegin\n");
    g_rib_ctx->indent_level++;
    g_rib_ctx->in_world = 1;
}

static void ribout_WorldEnd(void) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    if (g_rib_ctx->indent_level > 0) g_rib_ctx->indent_level--;
    write_indent();
    fprintf(g_rib_ctx->output, "WorldEnd\n");
    g_rib_ctx->in_world = 0;
}

static void ribout_Sphere(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                          RtToken* tokens, RtPointer* values, int count) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Sphere %g %g %g %g", radius, zmin, zmax, tmax);
    write_params(tokens, values, count);
    fprintf(g_rib_ctx->output, "\n");
}

static void ribout_Cylinder(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                            RtToken* tokens, RtPointer* values, int count) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Cylinder %g %g %g %g", radius, zmin, zmax, tmax);
    write_params(tokens, values, count);
    fprintf(g_rib_ctx->output, "\n");
}

static void ribout_Cone(RtFloat height, RtFloat radius, RtFloat tmax,
                        RtToken* tokens, RtPointer* values, int count) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Cone %g %g %g", height, radius, tmax);
    write_params(tokens, values, count);
    fprintf(g_rib_ctx->output, "\n");
}

static void ribout_Disk(RtFloat height, RtFloat radius, RtFloat tmax,
                        RtToken* tokens, RtPointer* values, int count) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Disk %g %g %g", height, radius, tmax);
    write_params(tokens, values, count);
    fprintf(g_rib_ctx->output, "\n");
}

static void ribout_Torus(RtFloat majorrad, RtFloat minorrad, RtFloat phimin,
                         RtFloat phimax, RtFloat tmax,
                         RtToken* tokens, RtPointer* values, int count) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Torus %g %g %g %g %g", majorrad, minorrad, phimin, phimax, tmax);
    write_params(tokens, values, count);
    fprintf(g_rib_ctx->output, "\n");
}

static void ribout_Paraboloid(RtFloat rmax, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                              RtToken* tokens, RtPointer* values, int count) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Paraboloid %g %g %g %g", rmax, zmin, zmax, tmax);
    write_params(tokens, values, count);
    fprintf(g_rib_ctx->output, "\n");
}

static void ribout_Hyperboloid(RtPoint point1, RtPoint point2, RtFloat tmax,
                               RtToken* tokens, RtPointer* values, int count) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Hyperboloid %g %g %g %g %g %g %g",
            point1[0], point1[1], point1[2],
            point2[0], point2[1], point2[2], tmax);
    write_params(tokens, values, count);
    fprintf(g_rib_ctx->output, "\n");
}

static void ribout_Polygon(RtInt nvertices, RtToken* tokens, RtPointer* values, int count) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Polygon");

    // P/N/st need vertex-count-aware sizes; all other tokens use write_params
    for (int i = 0; i < count; i++) {
        RtToken token = tokens[i];
        if (!token) break;

        if (strcmp(token, "P") == 0) {
            fprintf(g_rib_ctx->output, " \"P\" ");
            write_float_array((float*)values[i], nvertices * 3);
        } else if (strcmp(token, "N") == 0) {
            fprintf(g_rib_ctx->output, " \"N\" ");
            write_float_array((float*)values[i], nvertices * 3);
        } else if (strcmp(token, "st") == 0) {
            fprintf(g_rib_ctx->output, " \"st\" ");
            write_float_array((float*)values[i], nvertices * 2);
        } else {
            write_params(&tokens[i], &values[i], 1);
        }
    }
    fprintf(g_rib_ctx->output, "\n");
}

static void ribout_Patch(RtToken type, RtToken* tokens, RtPointer* values, int count) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Patch \"%s\"", type ? type : "bilinear");

    // bicubic has 16 control points, bilinear has 4
    int num_pts = (type && strcmp(type, "bicubic") == 0) ? 16 : 4;

    for (int i = 0; i < count; i++) {
        RtToken token = tokens[i];
        if (!token) break;

        if (strcmp(token, "P") == 0) {
            fprintf(g_rib_ctx->output, " \"P\" ");
            write_float_array((float*)values[i], num_pts * 3);
        } else {
            write_params(&tokens[i], &values[i], 1);
        }
    }
    fprintf(g_rib_ctx->output, "\n");
}

static void ribout_Geometry(RtToken type, RtToken* tokens, RtPointer* values, int count) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Geometry \"%s\"", type ? type : "");
    write_params(tokens, values, count);
    fprintf(g_rib_ctx->output, "\n");
}

static void ribout_LightSource(RtToken name, RtToken* tokens, RtPointer* values, int count) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "LightSource \"%s\"", name ? name : "");
    write_params(tokens, values, count);
    fprintf(g_rib_ctx->output, "\n");
}

static void ribout_Illuminate(RtToken light, RtBoolean onoff) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Illuminate \"%s\" %d\n", light ? light : "", onoff);
}

static void ribout_ObjectBegin(void) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    g_rib_ctx->object_counter++;
    fprintf(g_rib_ctx->output, "ObjectBegin %d\n", g_rib_ctx->object_counter);
    g_rib_ctx->indent_level++;
}

static void ribout_ObjectEnd(void) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    if (g_rib_ctx->indent_level > 0) g_rib_ctx->indent_level--;
    write_indent();
    fprintf(g_rib_ctx->output, "ObjectEnd\n");
}

static void ribout_ObjectInstance(RtInt handle) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "ObjectInstance %d\n", handle);
}

static void ribout_ReadArchive(RtToken filename) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "ReadArchive \"%s\"\n", filename);
}

// --- Callback Table ---

RiCallbacks ri_output_callbacks = {
    .Begin = ribout_Begin,
    .End = ribout_End,
    .FrameBegin = ribout_FrameBegin,
    .FrameEnd = ribout_FrameEnd,
    .Declare = ribout_Declare,
    .Format = ribout_Format,
    .Display = ribout_Display,
    .Projection = ribout_Projection,
    .PixelSamples = ribout_PixelSamples,
    .PixelFilter = ribout_PixelFilter,
    .DepthOfField = ribout_DepthOfField,
    .Clipping = ribout_Clipping,
    .ShadingRate = ribout_ShadingRate,
    .Option = ribout_Option,
    .AttributeBegin = ribout_AttributeBegin,
    .AttributeEnd = ribout_AttributeEnd,
    .TransformBegin = ribout_TransformBegin,
    .TransformEnd = ribout_TransformEnd,
    .Identity = ribout_Identity,
    .Transform = ribout_Transform,
    .ConcatTransform = ribout_ConcatTransform,
    .Translate = ribout_Translate,
    .Rotate = ribout_Rotate,
    .Scale = ribout_Scale,
    .CoordinateSystem = ribout_CoordinateSystem,
    .Basis = ribout_Basis,
    .Color = ribout_Color,
    .Opacity = ribout_Opacity,
    .Surface = ribout_Surface,
    .Atmosphere = ribout_Atmosphere,
    .Orientation = ribout_Orientation,
    .ReverseOrientation = ribout_ReverseOrientation,
    .Sides = ribout_Sides,
    .Matte = ribout_Matte,
    .Attribute = ribout_Attribute,
    .WorldBegin = ribout_WorldBegin,
    .WorldEnd = ribout_WorldEnd,
    .Sphere = ribout_Sphere,
    .Cylinder = ribout_Cylinder,
    .Cone = ribout_Cone,
    .Disk = ribout_Disk,
    .Torus = ribout_Torus,
    .Paraboloid = ribout_Paraboloid,
    .Hyperboloid = ribout_Hyperboloid,
    .Polygon = ribout_Polygon,
    .Patch = ribout_Patch,
    .Geometry = ribout_Geometry,
    .LightSource = ribout_LightSource,
    .Illuminate = ribout_Illuminate,
    .ObjectBegin = ribout_ObjectBegin,
    .ObjectEnd = ribout_ObjectEnd,
    .ObjectInstance = ribout_ObjectInstance,
    .ReadArchive = ribout_ReadArchive,
};

RiCallbacks* rib_output_get_callbacks(void) {
    return &ri_output_callbacks;
}
