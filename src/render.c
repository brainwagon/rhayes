// render - Parse a RIB file and render it using libri
#include "rib_parse.h"
#include "ri.h"
#include "ri_callbacks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Wrapper functions that call the actual Ri* functions
// These adapt the callback interface to the varargs Ri* API

static void render_Begin(RtToken name) {
    RiBegin(name);
}

static void render_End(void) {
    RiEnd();
}

static void render_Format(RtInt xres, RtInt yres, RtFloat aspect) {
    RiFormat(xres, yres, aspect);
}

static void render_Display(RtToken name, RtToken type, RtToken mode) {
    RiDisplay(name, type, mode, RI_NULL);
}

static void render_Projection(RtToken name, RtToken* tokens, RtPointer* values, int count) {
    if (count > 0 && tokens[0] && strcmp(tokens[0], "fov") == 0) {
        RiProjection(name, "fov", values[0], RI_NULL);
    } else {
        RiProjection(name, RI_NULL);
    }
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

static void render_ShadingRate(RtFloat size) {
    RiShadingRate(size);
}

static void render_Option(RtToken name, RtToken* tokens, RtPointer* values, int count) {
    // Build varargs-style call to RiOption
    if (count == 0) {
        RiOption(name, RI_NULL);
    } else if (count == 1) {
        RiOption(name, tokens[0], values[0], RI_NULL);
    } else if (count >= 2) {
        RiOption(name, tokens[0], values[0], tokens[1], values[1], RI_NULL);
    }
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
    (void)tokens; (void)values; (void)count;
    RiSurface(name, RI_NULL);
}

static void render_WorldBegin(void) {
    RiWorldBegin();
}

static void render_WorldEnd(void) {
    RiWorldEnd();
}

static void render_Sphere(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                          RtToken* tokens, RtPointer* values, int count) {
    (void)tokens; (void)values; (void)count;
    RiSphere(radius, zmin, zmax, tmax, RI_NULL);
}

static void render_Cylinder(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                            RtToken* tokens, RtPointer* values, int count) {
    (void)tokens; (void)values; (void)count;
    RiCylinder(radius, zmin, zmax, tmax, RI_NULL);
}

static void render_Cone(RtFloat height, RtFloat radius, RtFloat tmax,
                        RtToken* tokens, RtPointer* values, int count) {
    (void)tokens; (void)values; (void)count;
    RiCone(height, radius, tmax, RI_NULL);
}

static void render_Disk(RtFloat height, RtFloat radius, RtFloat tmax,
                        RtToken* tokens, RtPointer* values, int count) {
    (void)tokens; (void)values; (void)count;
    RiDisk(height, radius, tmax, RI_NULL);
}

static void render_Torus(RtFloat majorrad, RtFloat minorrad, RtFloat phimin,
                         RtFloat phimax, RtFloat tmax,
                         RtToken* tokens, RtPointer* values, int count) {
    (void)tokens; (void)values; (void)count;
    RiTorus(majorrad, minorrad, phimin, phimax, tmax, RI_NULL);
}

static void render_Paraboloid(RtFloat rmax, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                              RtToken* tokens, RtPointer* values, int count) {
    (void)tokens; (void)values; (void)count;
    RiParaboloid(rmax, zmin, zmax, tmax, RI_NULL);
}

static void render_Hyperboloid(RtPoint p1, RtPoint p2, RtFloat tmax,
                               RtToken* tokens, RtPointer* values, int count) {
    (void)tokens; (void)values; (void)count;
    RiHyperboloid(p1, p2, tmax, RI_NULL);
}

static void render_Polygon(RtInt nvertices, RtToken* tokens, RtPointer* values, int count) {
    if (count > 0 && tokens[0] && strcmp(tokens[0], "P") == 0) {
        RiPolygon(nvertices, "P", values[0], RI_NULL);
    }
}

static void render_Patch(RtToken type, RtToken* tokens, RtPointer* values, int count) {
    if (count > 0 && tokens[0] && strcmp(tokens[0], "P") == 0) {
        RiPatch(type, "P", values[0], RI_NULL);
    }
}

static void render_Geometry(RtToken type, RtToken* tokens, RtPointer* values, int count) {
    (void)tokens; (void)values; (void)count;
    RiGeometry(type, RI_NULL);
}

static void render_LightSource(RtToken name, RtToken* tokens, RtPointer* values, int count) {
    // Build varargs call based on parsed parameters
    if (count == 0) {
        RiLightSource(name, RI_NULL);
        return;
    }

    // Simplified: just pass the first few common parameters
    // A full implementation would need to handle all parameters
    RtToken t1 = count > 0 ? tokens[0] : NULL;
    RtPointer v1 = count > 0 ? values[0] : NULL;
    RtToken t2 = count > 1 ? tokens[1] : NULL;
    RtPointer v2 = count > 1 ? values[1] : NULL;
    RtToken t3 = count > 2 ? tokens[2] : NULL;
    RtPointer v3 = count > 2 ? values[2] : NULL;

    if (count == 1 && t1) {
        RiLightSource(name, t1, v1, RI_NULL);
    } else if (count == 2 && t1 && t2) {
        RiLightSource(name, t1, v1, t2, v2, RI_NULL);
    } else if (count >= 3 && t1 && t2 && t3) {
        RiLightSource(name, t1, v1, t2, v2, t3, v3, RI_NULL);
    } else {
        RiLightSource(name, RI_NULL);
    }
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
    .Format = render_Format,
    .Display = render_Display,
    .Projection = render_Projection,
    .PixelSamples = render_PixelSamples,
    .PixelFilter = render_PixelFilter,
    .DepthOfField = render_DepthOfField,
    .ShadingRate = render_ShadingRate,
    .Option = render_Option,
    .AttributeBegin = render_AttributeBegin,
    .AttributeEnd = render_AttributeEnd,
    .TransformBegin = render_TransformBegin,
    .TransformEnd = render_TransformEnd,
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
    if (argc < 2) {
        fprintf(stderr, "Usage: render <file.rib>\n");
        fprintf(stderr, "Parses a RIB file and renders it.\n");
        return 1;
    }

    const char* filename = argv[1];

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
