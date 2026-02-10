// render - Parse a RIB file and render it using libri
#include "rib_parse.h"
#include "ri.h"
#include "ri_callbacks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global verbose level (incremented via -v command line option)
static int g_verbose = 0;
// Global progress flag (set via -p command line option)
static int g_progress = 0;

// Wrapper functions that call the actual Ri* functions
// These adapt the callback interface to the varargs Ri* API

static void render_Begin(RtToken name) {
    RiBegin(name);
    if (g_verbose > 0) {
        RtInt level = g_verbose;
        RiOption("statistics", "endofframe", &level, RI_NULL);
    }
    if (g_progress) {
        RtInt show = 1;
        RiOption("progress", "show", &show, RI_NULL);
    }
}

static void render_End(void) {
    RiEnd();
}

static void render_FrameBegin(RtInt frame) {
    RiFrameBegin(frame);
}

static void render_FrameEnd(void) {
    RiFrameEnd();
}

static void render_Declare(const char* name, const char* declaration) {
    RiDeclare(name, declaration);
}

static void render_Format(RtInt xres, RtInt yres, RtFloat aspect) {
    RiFormat(xres, yres, aspect);
}

static void render_Display(RtToken name, RtToken type, RtToken mode) {
    RiDisplay(name, type, mode, RI_NULL);
}

static void render_Projection(RtToken name, RtToken* tokens, RtPointer* values, int count) {
    RiProjectionV(name, tokens, values, count);
}

static void render_PixelSamples(RtFloat x, RtFloat y) {
    RiPixelSamples(x, y);
}

static void render_PixelFilter(RtToken filtername, RtFloat xw, RtFloat yw) {
    RtFilterFunc func = RiBoxFilter;
    if (filtername) {
        if (strcmp(filtername, "triangle") == 0) {
            func = RiTriangleFilter;
        } else if (strcmp(filtername, "gaussian") == 0) {
            func = RiGaussianFilter;
        }
    }
    RiPixelFilter(func, xw, yw);
}

static void render_DepthOfField(RtFloat fstop, RtFloat focal, RtFloat dist) {
    RiDepthOfField(fstop, focal, dist);
}

static void render_Shutter(RtFloat open, RtFloat close) {
    RiShutter(open, close);
}

static void render_ShadingRate(RtFloat size) {
    RiShadingRate(size);
}

static void render_Option(RtToken name, RtToken* tokens, RtPointer* values, int count) {
    RiOptionV(name, tokens, values, count);
}

static void render_Hider(RtToken type, RtToken* tokens, RtPointer* values, int count) {
    RiHiderV(type, tokens, values, count);
}

static void render_AttributeBegin(void) {
    RiAttributeBegin();
}

static void render_AttributeEnd(void) {
    RiAttributeEnd();
}

static void render_TransformBegin(void) {
    RiTransformBegin();
}

static void render_TransformEnd(void) {
    RiTransformEnd();
}

static void render_MotionBegin(RtInt n, RtFloat* times) {
    RiMotionBeginV(n, times);
}

static void render_MotionEnd(void) {
    RiMotionEnd();
}

static void render_Identity(void) {
    RiIdentity();
}

static void render_Transform(RtMatrix m) {
    RiTransform(m);
}

static void render_ConcatTransform(RtMatrix m) {
    RiConcatTransform(m);
}

static void render_Translate(RtFloat dx, RtFloat dy, RtFloat dz) {
    RiTranslate(dx, dy, dz);
}

static void render_Rotate(RtFloat angle, RtFloat dx, RtFloat dy, RtFloat dz) {
    RiRotate(angle, dx, dy, dz);
}

static void render_Scale(RtFloat sx, RtFloat sy, RtFloat sz) {
    RiScale(sx, sy, sz);
}

static void render_Basis(RtToken ubasis, RtInt ustep, RtToken vbasis, RtInt vstep) {
    // Map basis names to matrices
    RtMatrix* umat = &RiBezierBasis;
    RtMatrix* vmat = &RiBezierBasis;

    if (ubasis) {
        if (strcmp(ubasis, "bspline") == 0) umat = &RiBSplineBasis;
        else if (strcmp(ubasis, "catmull-rom") == 0) umat = &RiCatmullRomBasis;
        else if (strcmp(ubasis, "hermite") == 0) umat = &RiHermiteBasis;
        else if (strcmp(ubasis, "power") == 0) umat = &RiPowerBasis;
    }
    if (vbasis) {
        if (strcmp(vbasis, "bspline") == 0) vmat = &RiBSplineBasis;
        else if (strcmp(vbasis, "catmull-rom") == 0) vmat = &RiCatmullRomBasis;
        else if (strcmp(vbasis, "hermite") == 0) vmat = &RiHermiteBasis;
        else if (strcmp(vbasis, "power") == 0) vmat = &RiPowerBasis;
    }

    RiBasis(*umat, ustep, *vmat, vstep);
}

static void render_Color(RtColor color) {
    RiColor(color);
}

static void render_Opacity(RtColor opacity) {
    RiOpacity(opacity);
}

static void render_Surface(RtToken name, RtToken* tokens, RtPointer* values, int count) {
    RiSurfaceV(name, tokens, values, count);
}

static void render_Atmosphere(RtToken name, RtToken* tokens, RtPointer* values, int count) {
    RiAtmosphereV(name, tokens, values, count);
}

static void render_Orientation(RtToken orientation) {
    RiOrientation(orientation);
}

static void render_ReverseOrientation(void) {
    RiReverseOrientation();
}

static void render_Sides(RtInt nsides) {
    RiSides(nsides);
}

static void render_Attribute(RtToken name, RtToken* tokens, RtPointer* values, int count) {
    RiAttributeV(name, tokens, values, count);
}

static void render_WorldBegin(void) {
    RiWorldBegin();
}

static void render_WorldEnd(void) {
    RiWorldEnd();
}

static void render_Sphere(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                          RtToken* tokens, RtPointer* values, int count) {
    RiSphereV(radius, zmin, zmax, tmax, tokens, values, count);
}

static void render_Cylinder(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                            RtToken* tokens, RtPointer* values, int count) {
    RiCylinderV(radius, zmin, zmax, tmax, tokens, values, count);
}

static void render_Cone(RtFloat height, RtFloat radius, RtFloat tmax,
                        RtToken* tokens, RtPointer* values, int count) {
    RiConeV(height, radius, tmax, tokens, values, count);
}

static void render_Disk(RtFloat height, RtFloat radius, RtFloat tmax,
                        RtToken* tokens, RtPointer* values, int count) {
    RiDiskV(height, radius, tmax, tokens, values, count);
}

static void render_Torus(RtFloat majorrad, RtFloat minorrad, RtFloat phimin,
                         RtFloat phimax, RtFloat tmax,
                         RtToken* tokens, RtPointer* values, int count) {
    RiTorusV(majorrad, minorrad, phimin, phimax, tmax, tokens, values, count);
}

static void render_Paraboloid(RtFloat rmax, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                              RtToken* tokens, RtPointer* values, int count) {
    RiParaboloidV(rmax, zmin, zmax, tmax, tokens, values, count);
}

static void render_Hyperboloid(RtPoint p1, RtPoint p2, RtFloat tmax,
                               RtToken* tokens, RtPointer* values, int count) {
    RiHyperboloidV(p1, p2, tmax, tokens, values, count);
}

static void render_Polygon(RtInt nvertices, RtToken* tokens, RtPointer* values, int count) {
    RiPolygonV(nvertices, tokens, values, count);
}

static void render_Patch(RtToken type, RtToken* tokens, RtPointer* values, int count) {
    RiPatchV(type, tokens, values, count);
}

static void render_Geometry(RtToken type, RtToken* tokens, RtPointer* values, int count) {
    RiGeometryV(type, tokens, values, count);
}

static void render_LightSource(RtToken name, RtToken* tokens, RtPointer* values, int count) {
    RiLightSourceV(name, tokens, values, count);
}

static void render_Illuminate(RtToken light, RtBoolean onoff) {
    RiIlluminate(light, onoff);
}

static void render_ObjectBegin(void) {
    RiObjectBegin();
}

static void render_ObjectEnd(void) {
    RiObjectEnd();
}

static void render_ObjectInstance(RtInt handle) {
    RiObjectInstance((RtObjectHandle)(intptr_t)handle);
}

// The render callback table
RiCallbacks ri_render_callbacks = {
    .Begin = render_Begin,
    .End = render_End,
    .FrameBegin = render_FrameBegin,
    .FrameEnd = render_FrameEnd,
    .Declare = render_Declare,
    .Format = render_Format,
    .Display = render_Display,
    .Projection = render_Projection,
    .PixelSamples = render_PixelSamples,
    .PixelFilter = render_PixelFilter,
    .DepthOfField = render_DepthOfField,
    .Shutter = render_Shutter,
    .ShadingRate = render_ShadingRate,
    .Option = render_Option,
    .Hider = render_Hider,
    .AttributeBegin = render_AttributeBegin,
    .AttributeEnd = render_AttributeEnd,
    .TransformBegin = render_TransformBegin,
    .TransformEnd = render_TransformEnd,
    .MotionBegin = render_MotionBegin,
    .MotionEnd = render_MotionEnd,
    .Identity = render_Identity,
    .Transform = render_Transform,
    .ConcatTransform = render_ConcatTransform,
    .Translate = render_Translate,
    .Rotate = render_Rotate,
    .Scale = render_Scale,
    .Basis = render_Basis,
    .Color = render_Color,
    .Opacity = render_Opacity,
    .Surface = render_Surface,
    .Atmosphere = render_Atmosphere,
    .Orientation = render_Orientation,
    .ReverseOrientation = render_ReverseOrientation,
    .Sides = render_Sides,
    .Attribute = render_Attribute,
    .WorldBegin = render_WorldBegin,
    .WorldEnd = render_WorldEnd,
    .Sphere = render_Sphere,
    .Cylinder = render_Cylinder,
    .Cone = render_Cone,
    .Disk = render_Disk,
    .Torus = render_Torus,
    .Paraboloid = render_Paraboloid,
    .Hyperboloid = render_Hyperboloid,
    .Polygon = render_Polygon,
    .Patch = render_Patch,
    .Geometry = render_Geometry,
    .LightSource = render_LightSource,
    .Illuminate = render_Illuminate,
    .ObjectBegin = render_ObjectBegin,
    .ObjectEnd = render_ObjectEnd,
    .ObjectInstance = render_ObjectInstance,
};

int main(int argc, char** argv) {
    const char* filename = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            g_verbose++;
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--progress") == 0) {
            g_progress = 1;
        } else if (argv[i][0] != '-') {
            filename = argv[i];
        }
    }

    if (!filename) {
        fprintf(stderr, "Usage: render [-v] [-p] <file.rib>\n");
        fprintf(stderr, "  -v, --verbose   Enable statistics output (use twice for detailed stats)\n");
        fprintf(stderr, "  -p, --progress  Show progress bar during rendering\n");
        fprintf(stderr, "Parses a RIB file and renders it.\n");
        return 1;
    }

    RibParser* parser = rib_parser_create(&ri_render_callbacks);
    if (!parser) {
        fprintf(stderr, "Error: Failed to create parser\n");
        return 1;
    }

    int result = rib_parser_parse_file(parser, filename);

    if (result != 0) {
        const char* err = rib_parser_get_error(parser);
        if (err) {
            fprintf(stderr, "Parse error: %s\n", err);
        }
    }

    rib_parser_destroy(parser);
    return result;
}
