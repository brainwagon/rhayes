#ifndef RH_GEOMETRY_H
#define RH_GEOMETRY_H

#include "rh_math.h"
#include "rh_image.h"
#include <stdbool.h>

// --- Micropolygon Grid ---

typedef struct {
    int width;   // u dimension (vertices)
    int height;  // v dimension (vertices)
    RhVec3* positions; // World or Screen space positions
    RhColor* colors;   // Shaded colors
    // Normals would be here for shading
    RhVec3* normals;
} RhMicroGrid;

RhMicroGrid* rh_grid_create(int width, int height);
void rh_grid_destroy(RhMicroGrid* g);

// --- Primitives ---

typedef enum {
    RH_PRIM_SPHERE,
    RH_PRIM_CYLINDER,
    RH_PRIM_CONE,
    RH_PRIM_PARABOLOID,
    RH_PRIM_POLYGON,
    RH_PRIM_PATCH_BICUBIC,
    RH_PRIM_DISK,
    RH_PRIM_TORUS,
    RH_PRIM_HYPERBOLOID
} RhPrimitiveType;

typedef struct {
    RhFloat radius;
    RhFloat zmin, zmax;
    RhFloat theta_min, theta_max;
} RhSphere;

typedef struct {
    RhFloat radius;
    RhFloat zmin, zmax;
    RhFloat theta_max;
} RhCylinder;

typedef struct {
    RhFloat height;
    RhFloat radius;
    RhFloat theta_max;
} RhCone;

typedef struct {
    RhFloat rmax;
    RhFloat zmin, zmax;
    RhFloat theta_max;
} RhParaboloid;

typedef struct {
    int count;
    RhVec3* vertices;
} RhPolygon;

typedef struct {
    RhVec3 cp[16]; // 4x4 Control Points
    RhMat4 u_basis;
    RhMat4 v_basis;
} RhPatchBicubic;

typedef struct {
    RhFloat height;
    RhFloat radius;
    RhFloat theta_max;
} RhDisk;

typedef struct {
    RhFloat majorradius;
    RhFloat minorradius;
    RhFloat phimin, phimax;
    RhFloat theta_max;
} RhTorus;

typedef struct {
    RhVec3 point1;
    RhVec3 point2;
    RhFloat theta_max;
} RhHyperboloid;

// Generic Primitive Container
typedef struct {
    RhPrimitiveType type;
    union {
        RhSphere sphere;
        RhCylinder cylinder;
        RhCone cone;
        RhParaboloid paraboloid;
        RhPolygon polygon;
        RhPatchBicubic patch;
        RhDisk disk;
        RhTorus torus;
        RhHyperboloid hyperboloid;
    } data;
    
    // Parametric domain [0, 1]
    RhFloat u_min, u_max;
    RhFloat v_min, v_max;
} RhPrimitive;

RhPrimitive rh_prim_create_sphere(RhFloat radius, RhFloat zmin, RhFloat zmax, RhFloat tmin, RhFloat tmax);
RhPrimitive rh_prim_create_cylinder(RhFloat radius, RhFloat zmin, RhFloat zmax, RhFloat tmax);
RhPrimitive rh_prim_create_cone(RhFloat height, RhFloat radius, RhFloat tmax);
RhPrimitive rh_prim_create_paraboloid(RhFloat rmax, RhFloat zmin, RhFloat zmax, RhFloat tmax);
RhPrimitive rh_prim_create_disk(RhFloat height, RhFloat radius, RhFloat tmax);
RhPrimitive rh_prim_create_torus(RhFloat majorradius, RhFloat minorradius, RhFloat phimin, RhFloat phimax, RhFloat tmax);
RhPrimitive rh_prim_create_hyperboloid(RhVec3 point1, RhVec3 point2, RhFloat tmax);

// Creates a polygon (copies vertices)
RhPrimitive rh_prim_create_polygon(int count, const RhVec3* vertices);

// Creates a bicubic patch (copies control points)
RhPrimitive rh_prim_create_patch_bicubic(const RhVec3* cp, RhMat4 u_basis, RhMat4 v_basis);

// Frees internal data if necessary (e.g. polygon vertices)
void rh_prim_free_data(RhPrimitive* p);

// Calculate 3D bounding box (Object Space)
RhBounds3 rh_prim_bound(const RhPrimitive* p);

// Check if primitive is diceable (small enough on screen)
// For MVP, we'll just check if the parametric domain is small enough or recurse to a depth.
// Real REYES projects bounds to screen.
bool rh_prim_diceable(const RhPrimitive* p, const RhMat4* world_view_proj, RhFloat threshold);

// Split a primitive into sub-primitives (usually 2 or 4)
// Returns number of children, writes to 'children' array (caller allocates, max 4)
int rh_prim_split(const RhPrimitive* p, RhPrimitive* children);

// Dice primitive into a Grid
// grid_size: e.g. 8 means 8x8 vertices
void rh_prim_dice(const RhPrimitive* p, int u_res, int v_res, RhMicroGrid* grid);

#endif // RH_GEOMETRY_H