#ifndef RI_H
#define RI_H

#include "rh_config.h"

// --- Standard Types ---

typedef short RtBoolean;
typedef int RtInt;
typedef float RtFloat;
typedef char* RtToken;
typedef void* RtPointer;

typedef RtFloat RtColor[3];
typedef RtFloat RtPoint[3];
typedef RtFloat RtVector[3];
typedef RtFloat RtNormal[3];
typedef RtFloat RtHpoint[4];
typedef RtFloat RtMatrix[4][4];
typedef RtFloat RtBound[6];

typedef RtPointer RtObjectHandle;
typedef RtPointer RtLightHandle;
typedef void RtVoid;
typedef RtFloat (*RtFilterFunc)(RtFloat, RtFloat, RtFloat, RtFloat);

#define RI_FALSE 0
#define RI_TRUE  1
#define RI_INFINITY 1.0e30
#define RI_EPSILON 1.0e-5
#define RI_NULL 0

// --- API Prototypes ---

// 1. Relationship to the external world
void RiBegin(RtToken name);
void RiEnd(void);
RtPointer RiGetContext(void);
void RiContext(RtPointer ctx);

// Variable declarations
// Associates a token with a type and storage class for parameter list parsing.
// declaration format: "[class] [type] ['[' n ']']"
// class: constant, uniform, varying, vertex (default: uniform)
// type: float, integer, string, color, point, vector, normal, hpoint, matrix (default: float)
// Example: RiDeclare("temperature", "vertex float");
//          RiDeclare("Cs", "varying color");
RtToken RiDeclare(const char* name, const char* declaration);

// 2. Options (Scene Description)
void RiOption(RtToken name, ...);
void RiFormat(RtInt xresolution, RtInt yresolution, RtFloat pixelaspectratio);
void RiPixelSamples(RtFloat xsamples, RtFloat ysamples);
void RiPixelFilter(RtFilterFunc filterfunc, RtFloat xwidth, RtFloat ywidth);
void RiDepthOfField(RtFloat fstop, RtFloat focallength, RtFloat focaldistance);
void RiProjection(RtToken name, ...); // Varargs for params (fov, etc.)
void RiDisplay(RtToken name, RtToken type, RtToken mode, ...);

// Standard Filter Functions
RtFloat RiBoxFilter(RtFloat x, RtFloat y, RtFloat xwidth, RtFloat ywidth);
RtFloat RiTriangleFilter(RtFloat x, RtFloat y, RtFloat xwidth, RtFloat ywidth);
RtFloat RiGaussianFilter(RtFloat x, RtFloat y, RtFloat xwidth, RtFloat ywidth);

// 3. Graphics State (Attributes & Transforms)
void RiAttributeBegin(void);
void RiAttributeEnd(void);
void RiTransformBegin(void);
void RiTransformEnd(void);

// 4. Transformations
void RiIdentity(void);
void RiTransform(RtMatrix transform);
void RiConcatTransform(RtMatrix transform);
void RiTranslate(RtFloat dx, RtFloat dy, RtFloat dz);
void RiRotate(RtFloat angle, RtFloat dx, RtFloat dy, RtFloat dz);
void RiScale(RtFloat sx, RtFloat sy, RtFloat sz);

// Basis
void RiBasis(RtMatrix ubasis, RtInt ustep, RtMatrix vbasis, RtInt vstep);

// Standard Basis Matrices
extern RtMatrix RiBezierBasis;
extern RtMatrix RiBSplineBasis;
extern RtMatrix RiCatmullRomBasis;
extern RtMatrix RiHermiteBasis;
extern RtMatrix RiPowerBasis;

// 5. Attributes
void RiColor(RtColor color);
void RiOpacity(RtColor color);
void RiShadingRate(RtFloat size);

// 6. Scene Structure
void RiWorldBegin(void);
void RiWorldEnd(void);

// 7. Primitives
void RiSphere(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax, ...);
void RiCylinder(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax, ...);
void RiCone(RtFloat height, RtFloat radius, RtFloat tmax, ...);
void RiParaboloid(RtFloat rmax, RtFloat zmin, RtFloat zmax, RtFloat tmax, ...);
void RiDisk(RtFloat height, RtFloat radius, RtFloat tmax, ...);
void RiTorus(RtFloat majorradius, RtFloat minorradius, RtFloat phimin, RtFloat phimax, RtFloat tmax, ...);
void RiHyperboloid(RtPoint point1, RtPoint point2, RtFloat tmax, ...);
void RiPolygon(RtInt nvertices, ...); // Expects RI_P, point_array, RI_NULL
void RiPatch(RtToken type, ...); // Expects "bicubic" or "bilinear", then params
void RiGeometry(RtToken type, ...);

// Surface
void RiSurface(RtToken name, ...);
void RiSurfaceV(RtToken name, RtToken* tokens, RtPointer* values, int count);

// 8. Lighting
RtToken RiLightSource(RtToken name, ...);
void RiIlluminate(RtToken light, RtBoolean onoff);

// 9. Retained Geometry
RtObjectHandle RiObjectBegin(void);
void RiObjectEnd(void);
void RiObjectInstance(RtObjectHandle handle);

// Standard Tokens
extern RtToken RI_P;
extern RtToken RI_CZ;
extern RtToken RI_INTENSITY;
extern RtToken RI_LIGHTCOLOR;
extern RtToken RI_FROM;
extern RtToken RI_TO;

#endif // RI_H
