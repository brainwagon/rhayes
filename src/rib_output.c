#include "rib_output.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// RIB Output Context
typedef struct {
    FILE* output;
    int binary_mode;
    int indent_level;
    int owns_file;      // 1 if we opened the file, 0 if user provided stream
    int in_world;       // Track if we're inside WorldBegin/End
    int object_counter; // For generating object handles
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

// Helper: Write parameter list (token-value pairs)
static void write_params(RtToken* tokens, RtPointer* values, int count) {
    if (!g_rib_ctx || !g_rib_ctx->output || count <= 0) return;

    for (int i = 0; i < count; i++) {
        RtToken token = tokens[i];
        if (!token) break;

        fprintf(g_rib_ctx->output, " \"%s\" ", token);

        // Determine array size based on token type
        // For now, handle common cases
        if (strcmp(token, "P") == 0 || strcmp(token, "Pw") == 0) {
            // Position array - need vertex count from context
            // This is handled specially by specific primitives (Polygon, Patch)
            // For generic params, we just note it's a point array
            fprintf(g_rib_ctx->output, "[...]"); // Placeholder - specific handlers override
        } else if (strcmp(token, "fov") == 0 ||
                   strcmp(token, "intensity") == 0 ||
                   strcmp(token, "Ka") == 0 ||
                   strcmp(token, "Kd") == 0 ||
                   strcmp(token, "Ks") == 0 ||
                   strcmp(token, "roughness") == 0) {
            // Single float
            float* fval = (float*)values[i];
            fprintf(g_rib_ctx->output, "%g", *fval);
        } else if (strcmp(token, "from") == 0 ||
                   strcmp(token, "to") == 0 ||
                   strcmp(token, "lightcolor") == 0) {
            // 3-float array (point or color)
            float* fvals = (float*)values[i];
            write_float_array(fvals, 3);
        } else {
            // Default: assume single float
            float* fval = (float*)values[i];
            fprintf(g_rib_ctx->output, "%g", *fval);
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

    // For Polygon, we need to handle "P" specially - it's an array of nvertices*3 floats
    for (int i = 0; i < count; i++) {
        RtToken token = tokens[i];
        if (!token) break;

        fprintf(g_rib_ctx->output, " \"%s\" ", token);

        if (strcmp(token, "P") == 0) {
            float* pts = (float*)values[i];
            write_float_array(pts, nvertices * 3);
        } else if (strcmp(token, "N") == 0) {
            float* norms = (float*)values[i];
            write_float_array(norms, nvertices * 3);
        } else if (strcmp(token, "st") == 0) {
            float* st = (float*)values[i];
            write_float_array(st, nvertices * 2);
        } else {
            // Default: single float
            float* fval = (float*)values[i];
            fprintf(g_rib_ctx->output, "%g", *fval);
        }
    }
    fprintf(g_rib_ctx->output, "\n");
}

static void ribout_Patch(RtToken type, RtToken* tokens, RtPointer* values, int count) {
    if (!g_rib_ctx || !g_rib_ctx->output) return;
    write_indent();
    fprintf(g_rib_ctx->output, "Patch \"%s\"", type ? type : "bilinear");

    // Patch needs special handling for control points
    // bicubic has 16 points, bilinear has 4
    int num_pts = (type && strcmp(type, "bicubic") == 0) ? 16 : 4;

    for (int i = 0; i < count; i++) {
        RtToken token = tokens[i];
        if (!token) break;

        fprintf(g_rib_ctx->output, " \"%s\" ", token);

        if (strcmp(token, "P") == 0) {
            float* pts = (float*)values[i];
            write_float_array(pts, num_pts * 3);
        } else {
            float* fval = (float*)values[i];
            fprintf(g_rib_ctx->output, "%g", *fval);
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

    // Handle light parameters
    for (int i = 0; i < count; i++) {
        RtToken token = tokens[i];
        if (!token) break;

        fprintf(g_rib_ctx->output, " \"%s\" ", token);

        if (strcmp(token, "from") == 0 || strcmp(token, "to") == 0 ||
            strcmp(token, "lightcolor") == 0) {
            float* fvals = (float*)values[i];
            write_float_array(fvals, 3);
        } else {
            // Single float (intensity, coneangle, etc.)
            float* fval = (float*)values[i];
            fprintf(g_rib_ctx->output, "%g", *fval);
        }
    }
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

// --- Callback Table ---

RiCallbacks ri_output_callbacks = {
    .Begin = ribout_Begin,
    .End = ribout_End,
    .Format = ribout_Format,
    .Display = ribout_Display,
    .Projection = ribout_Projection,
    .PixelSamples = ribout_PixelSamples,
    .PixelFilter = ribout_PixelFilter,
    .DepthOfField = ribout_DepthOfField,
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
    .Basis = ribout_Basis,
    .Color = ribout_Color,
    .Opacity = ribout_Opacity,
    .Surface = ribout_Surface,
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
};

RiCallbacks* rib_output_get_callbacks(void) {
    return &ri_output_callbacks;
}
