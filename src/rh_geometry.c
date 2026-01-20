#define _POSIX_C_SOURCE 200809L
#include "rh_geometry.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

// --- Primvar Helper Functions ---

// Get component count for a given type
int rh_type_component_count(RiVarType type) {
    switch (type) {
        case RI_TYPE_FLOAT:   return 1;
        case RI_TYPE_INTEGER: return 1;
        case RI_TYPE_STRING:  return 1;
        case RI_TYPE_COLOR:   return 3;
        case RI_TYPE_POINT:   return 3;
        case RI_TYPE_VECTOR:  return 3;
        case RI_TYPE_NORMAL:  return 3;
        case RI_TYPE_HPOINT:  return 4;
        case RI_TYPE_MATRIX:  return 16;
        default:              return 1;
    }
}

// Get byte size for a single element of a given type
static size_t rh_type_element_size(RiVarType type) {
    switch (type) {
        case RI_TYPE_FLOAT:   return sizeof(float);
        case RI_TYPE_INTEGER: return sizeof(int);
        case RI_TYPE_STRING:  return sizeof(char*);
        case RI_TYPE_COLOR:   return 3 * sizeof(float);
        case RI_TYPE_POINT:   return 3 * sizeof(float);
        case RI_TYPE_VECTOR:  return 3 * sizeof(float);
        case RI_TYPE_NORMAL:  return 3 * sizeof(float);
        case RI_TYPE_HPOINT:  return 4 * sizeof(float);
        case RI_TYPE_MATRIX:  return 16 * sizeof(float);
        default:              return sizeof(float);
    }
}

// Copy a primitive variable (deep copy of data)
void rh_primvar_copy(RhPrimVar* dst, const RhPrimVar* src) {
    strncpy(dst->name, src->name, sizeof(dst->name) - 1);
    dst->name[sizeof(dst->name) - 1] = '\0';
    dst->sclass = src->sclass;
    dst->type = src->type;
    dst->count = src->count;

    // Deep copy the data
    size_t element_size = rh_type_element_size(src->type);
    size_t total_size = element_size * src->count;

    if (src->type == RI_TYPE_STRING) {
        // For strings, copy each string pointer and the string itself
        dst->data = malloc(src->count * sizeof(char*));
        if (dst->data) {
            char** dst_strings = (char**)dst->data;
            char** src_strings = (char**)src->data;
            for (int i = 0; i < src->count; i++) {
                if (src_strings[i]) {
                    dst_strings[i] = strdup(src_strings[i]);
                } else {
                    dst_strings[i] = NULL;
                }
            }
        }
    } else {
        dst->data = malloc(total_size);
        if (dst->data) {
            memcpy(dst->data, src->data, total_size);
        }
    }
}

// Free a single primitive variable's data
void rh_primvar_free(RhPrimVar* pv) {
    if (pv && pv->data) {
        if (pv->type == RI_TYPE_STRING) {
            // Free each string
            char** strings = (char**)pv->data;
            for (int i = 0; i < pv->count; i++) {
                free(strings[i]);
            }
        }
        free(pv->data);
        pv->data = NULL;
    }
}

// Free an array of primitive variables
void rh_primvar_array_free(RhPrimVar* primvars, int count) {
    if (primvars) {
        for (int i = 0; i < count; i++) {
            rh_primvar_free(&primvars[i]);
        }
        free(primvars);
    }
}

// --- Grid Implementation ---

RhMicroGrid* rh_grid_create(int width, int height) {
    RhMicroGrid* g = (RhMicroGrid*)malloc(sizeof(RhMicroGrid));
    if (!g) return NULL;
    g->width = width;
    g->height = height;
    int count = width * height;
    g->positions = (RhVec3*)malloc(count * sizeof(RhVec3));
    g->colors = (RhColor*)malloc(count * sizeof(RhColor));
    g->opacities = (RhColor*)malloc(count * sizeof(RhColor));
    g->normals = (RhVec3*)malloc(count * sizeof(RhVec3));
    g->u_coords = (RhFloat*)malloc(count * sizeof(RhFloat));
    g->v_coords = (RhFloat*)malloc(count * sizeof(RhFloat));
    g->primvars = NULL;
    g->num_primvars = 0;

    if (!g->positions || !g->colors || !g->opacities || !g->normals ||
        !g->u_coords || !g->v_coords) {
        rh_grid_destroy(g);
        return NULL;
    }
    return g;
}

void rh_grid_destroy(RhMicroGrid* g) {
    if (g) {
        if (g->positions) free(g->positions);
        if (g->colors) free(g->colors);
        if (g->opacities) free(g->opacities);
        if (g->normals) free(g->normals);
        if (g->u_coords) free(g->u_coords);
        if (g->v_coords) free(g->v_coords);
        rh_primvar_array_free(g->primvars, g->num_primvars);
        free(g);
    }
}

// --- Primitive Implementation ---

RhPrimitive rh_prim_create_sphere(RhFloat radius, RhFloat zmin, RhFloat zmax, RhFloat tmin, RhFloat tmax) {
    RhPrimitive p;
    p.type = RH_PRIM_SPHERE;
    p.data.sphere.radius = radius;
    p.data.sphere.zmin = zmin;
    p.data.sphere.zmax = zmax;
    p.data.sphere.theta_min = tmin;
    p.data.sphere.theta_max = tmax;
    p.u_min = 0.0f; p.u_max = 1.0f;
    p.v_min = 0.0f; p.v_max = 1.0f;
    p.primvars = NULL; p.num_primvars = 0;
    return p;
}

RhPrimitive rh_prim_create_cylinder(RhFloat radius, RhFloat zmin, RhFloat zmax, RhFloat tmax) {
    RhPrimitive p;
    p.type = RH_PRIM_CYLINDER;
    p.data.cylinder.radius = radius;
    p.data.cylinder.zmin = zmin;
    p.data.cylinder.zmax = zmax;
    p.data.cylinder.theta_max = tmax;
    p.u_min = 0.0f; p.u_max = 1.0f;
    p.v_min = 0.0f; p.v_max = 1.0f;
    p.primvars = NULL; p.num_primvars = 0;
    return p;
}

RhPrimitive rh_prim_create_cone(RhFloat height, RhFloat radius, RhFloat tmax) {
    RhPrimitive p;
    p.type = RH_PRIM_CONE;
    p.data.cone.height = height;
    p.data.cone.radius = radius;
    p.data.cone.theta_max = tmax;
    p.u_min = 0.0f; p.u_max = 1.0f;
    p.v_min = 0.0f; p.v_max = 1.0f;
    p.primvars = NULL; p.num_primvars = 0;
    return p;
}

RhPrimitive rh_prim_create_paraboloid(RhFloat rmax, RhFloat zmin, RhFloat zmax, RhFloat tmax) {
    RhPrimitive p;
    p.type = RH_PRIM_PARABOLOID;
    p.data.paraboloid.rmax = rmax;
    p.data.paraboloid.zmin = zmin;
    p.data.paraboloid.zmax = zmax;
    p.data.paraboloid.theta_max = tmax;
    p.u_min = 0.0f; p.u_max = 1.0f;
    p.v_min = 0.0f; p.v_max = 1.0f;
    p.primvars = NULL; p.num_primvars = 0;
    return p;
}

RhPrimitive rh_prim_create_disk(RhFloat height, RhFloat radius, RhFloat tmax) {
    RhPrimitive p;
    p.type = RH_PRIM_DISK;
    p.data.disk.height = height;
    p.data.disk.radius = radius;
    p.data.disk.theta_max = tmax;
    p.u_min = 0.0f; p.u_max = 1.0f;
    p.v_min = 0.0f; p.v_max = 1.0f;
    p.primvars = NULL; p.num_primvars = 0;
    return p;
}

RhPrimitive rh_prim_create_torus(RhFloat majorradius, RhFloat minorradius, RhFloat phimin, RhFloat phimax, RhFloat tmax) {
    RhPrimitive p;
    p.type = RH_PRIM_TORUS;
    p.data.torus.majorradius = majorradius;
    p.data.torus.minorradius = minorradius;
    p.data.torus.phimin = phimin;
    p.data.torus.phimax = phimax;
    p.data.torus.theta_max = tmax;
    p.u_min = 0.0f; p.u_max = 1.0f;
    p.v_min = 0.0f; p.v_max = 1.0f;
    p.primvars = NULL; p.num_primvars = 0;
    return p;
}

RhPrimitive rh_prim_create_hyperboloid(RhVec3 point1, RhVec3 point2, RhFloat tmax) {
    RhPrimitive p;
    p.type = RH_PRIM_HYPERBOLOID;
    p.data.hyperboloid.point1 = point1;
    p.data.hyperboloid.point2 = point2;
    p.data.hyperboloid.theta_max = tmax;
    p.u_min = 0.0f; p.u_max = 1.0f;
    p.v_min = 0.0f; p.v_max = 1.0f;
    p.primvars = NULL; p.num_primvars = 0;
    return p;
}

RhPrimitive rh_prim_create_polygon(int count, const RhVec3* vertices) {
    RhPrimitive p;
    p.type = RH_PRIM_POLYGON;
    p.data.polygon.count = count;
    p.data.polygon.vertices = (RhVec3*)malloc(count * sizeof(RhVec3));
    if (p.data.polygon.vertices && vertices) {
        memcpy(p.data.polygon.vertices, vertices, count * sizeof(RhVec3));
    }
    p.u_min = 0.0f; p.u_max = 1.0f;
    p.v_min = 0.0f; p.v_max = 1.0f;
    p.primvars = NULL; p.num_primvars = 0;
    return p;
}

RhPrimitive rh_prim_create_patch_bicubic(const RhVec3* cp, RhMat4 u_basis, RhMat4 v_basis) {
    RhPrimitive p;
    p.type = RH_PRIM_PATCH_BICUBIC;
    if (cp) memcpy(p.data.patch.cp, cp, 16 * sizeof(RhVec3));
    p.data.patch.u_basis = u_basis;
    p.data.patch.v_basis = v_basis;
    p.u_min = 0.0f; p.u_max = 1.0f;
    p.v_min = 0.0f; p.v_max = 1.0f;
    p.primvars = NULL; p.num_primvars = 0;
    return p;
}

void rh_prim_free_data(RhPrimitive* p) {
    if (p->type == RH_PRIM_POLYGON) {
        if (p->data.polygon.vertices) {
            free(p->data.polygon.vertices);
            p->data.polygon.vertices = NULL;
        }
    }
    // Free any attached primvars
    rh_primvar_array_free(p->primvars, p->num_primvars);
    p->primvars = NULL;
    p->num_primvars = 0;
}

RhBounds3 rh_prim_bound(const RhPrimitive* p) {
    RhBounds3 b = {{0,0,0}, {0,0,0}};
    if (p->type == RH_PRIM_SPHERE) {
        RhFloat r = p->data.sphere.radius;
        b.min = rh_vec3_create(-r, -r, p->data.sphere.zmin);
        b.max = rh_vec3_create(r, r, p->data.sphere.zmax);
    } else if (p->type == RH_PRIM_CYLINDER) {
        RhFloat r = p->data.cylinder.radius;
        b.min = rh_vec3_create(-r, -r, p->data.cylinder.zmin);
        b.max = rh_vec3_create(r, r, p->data.cylinder.zmax);
    } else if (p->type == RH_PRIM_CONE) {
        RhFloat r = p->data.cone.radius;
        b.min = rh_vec3_create(-r, -r, 0); // Cone starts at z=0
        b.max = rh_vec3_create(r, r, p->data.cone.height);
    } else if (p->type == RH_PRIM_PARABOLOID) {
        RhFloat r = p->data.paraboloid.rmax; // Approx max radius
        b.min = rh_vec3_create(-r, -r, p->data.paraboloid.zmin);
        b.max = rh_vec3_create(r, r, p->data.paraboloid.zmax);
    } else if (p->type == RH_PRIM_POLYGON) {
        if (p->data.polygon.count > 0) {
            b.min = p->data.polygon.vertices[0];
            b.max = p->data.polygon.vertices[0];
            for (int i = 1; i < p->data.polygon.count; i++) {
                RhVec3 v = p->data.polygon.vertices[i];
                b.min.x = rh_min(b.min.x, v.x); b.min.y = rh_min(b.min.y, v.y); b.min.z = rh_min(b.min.z, v.z);
                b.max.x = rh_max(b.max.x, v.x); b.max.y = rh_max(b.max.y, v.y); b.max.z = rh_max(b.max.z, v.z);
            }
        }
    } else if (p->type == RH_PRIM_PATCH_BICUBIC) {
        // Convex Hull property: Bezier/B-Spline curve lies within convex hull of control points.
        // We can just bound the control points.
        b.min = p->data.patch.cp[0];
        b.max = p->data.patch.cp[0];
        for (int i = 1; i < 16; i++) {
             RhVec3 v = p->data.patch.cp[i];
             b.min.x = rh_min(b.min.x, v.x); b.min.y = rh_min(b.min.y, v.y); b.min.z = rh_min(b.min.z, v.z);
             b.max.x = rh_max(b.max.x, v.x); b.max.y = rh_max(b.max.y, v.y); b.max.z = rh_max(b.max.z, v.z);
        }
    } else if (p->type == RH_PRIM_DISK) {
        RhFloat r = p->data.disk.radius;
        RhFloat h = p->data.disk.height;
        b.min = rh_vec3_create(-r, -r, h);
        b.max = rh_vec3_create(r, r, h);
    } else if (p->type == RH_PRIM_TORUS) {
        RhFloat R = p->data.torus.majorradius;
        RhFloat r = p->data.torus.minorradius;
        b.min = rh_vec3_create(-(R+r), -(R+r), -r);
        b.max = rh_vec3_create(R+r, R+r, r);
    } else if (p->type == RH_PRIM_HYPERBOLOID) {
        // Bound both endpoints plus rotation
        RhVec3 p1 = p->data.hyperboloid.point1;
        RhVec3 p2 = p->data.hyperboloid.point2;
        RhFloat r1 = sqrtf(p1.x*p1.x + p1.y*p1.y);
        RhFloat r2 = sqrtf(p2.x*p2.x + p2.y*p2.y);
        RhFloat rmax = rh_max(r1, r2);
        b.min = rh_vec3_create(-rmax, -rmax, rh_min(p1.z, p2.z));
        b.max = rh_vec3_create(rmax, rmax, rh_max(p1.z, p2.z));
    }
    return b;
}

bool rh_prim_diceable(const RhPrimitive* p, const RhMat4* mvp, RhFloat threshold) {
    // If Polygon has > 4 vertices, it MUST split (into smaller polys).
    if (p->type == RH_PRIM_POLYGON && p->data.polygon.count > 4) {
        return false;
    }

    // Determine if primitive is small enough to be diced.
    // For this implementation, let's just use the parametric size.
    // In a real system, we'd project the bounds to screen space area.
    (void)mvp; // Unused for this simplified check
    (void)threshold;

    // Simple heuristic: if UV range is small enough?
    // Or just stop after N splits.
    // Let's implement this logic in the renderer loop (main.c) or here based on recursion depth logic passed in?
    // For now, return false so we always split until explicit stop in main.
    // Actually, let's look at the UV area.
    
    RhFloat u_len = p->u_max - p->u_min;
    RhFloat v_len = p->v_max - p->v_min;
    
    // If we've split enough (e.g., 1/16th of domain), dice.
    if (u_len * v_len < 0.001f) return true; // Arbitrary small number
    
    return false;
}

int rh_prim_split(const RhPrimitive* p, RhPrimitive* children) {
    if (p->type == RH_PRIM_POLYGON) {
        int n = p->data.polygon.count;
        RhVec3* v = p->data.polygon.vertices;
        
        if (n > 4) {
            // Decompose N-gon into 2 smaller polygons
            // Split at index k ~ n/2
            // Poly 1: 0, 1, ... k
            // Poly 2: 0, k, k+1, ... n-1
            // Vert 0 is pivot.
            int k = n / 2 + 1; // e.g. 5 -> 3. P1: 0,1,2,3 (4 verts). P2: 0,3,4 (3 verts).
            
            // Child 0
            int c1 = k + 1;
            RhVec3* v1 = (RhVec3*)malloc(c1 * sizeof(RhVec3));
            for(int i=0; i<=k; i++) v1[i] = v[i];
            children[0] = rh_prim_create_polygon(c1, v1);
            free(v1); // create_polygon makes a copy
            
            // Child 1
            int c2 = n - k + 1;
            RhVec3* v2 = (RhVec3*)malloc(c2 * sizeof(RhVec3));
            v2[0] = v[0];
            for(int i=0; i<n-k; i++) v2[i+1] = v[k+i];
            children[1] = rh_prim_create_polygon(c2, v2);
            free(v2);
            
            return 2;
        } else {
            // Bilinear Patch Split (Quad or Triangle)
            // We split parametrically.
            // Vertices are at u=0,v=0 (0), u=1,v=0 (1), u=1,v=1 (2), u=0,v=1 (3)
            // If Triangle: v3==v2 or v3==v0? Usually 0,1,2.
            // Let's assume standard Quad order: 0(0,0), 1(1,0), 2(1,1), 3(0,1).
            
            // Helper for linear interp
            #define LERP(a,b,t) rh_vec3_add(rh_vec3_mul(a, 1.0f-t), rh_vec3_mul(b, t))
            
            RhVec3 v0 = v[0];
            RhVec3 v1 = v[1];
            RhVec3 v2 = (n>2) ? v[2] : v1;
            RhVec3 v3 = (n>3) ? v[3] : v0;
            
            // We always split at parametric 0.5 relative to current?
            // Wait, rh_prim_split is usually parametric.
            // But for polygons, we usually physically split the geometry.
            // Since we re-create the polygon vertices, let's physically split.
            
            // Split U direction (Left/Right)
             // Edge 0-1 split at 01. Edge 3-2 split at 32. Center 01-32 split at C.
             // But wait, are we splitting U or V?
             // Use parametric logic.
             RhFloat u_range = p->u_max - p->u_min;
             RhFloat v_range = p->v_max - p->v_min;
             
             if (u_range >= v_range) {
                 // Split U
                 RhVec3 v01 = LERP(v0, v1, 0.5f);
                 RhVec3 v32 = LERP(v3, v2, 0.5f);
                 
                 RhVec3 c1_verts[4] = {v0, v01, v32, v3};
                 RhVec3 c2_verts[4] = {v01, v1, v2, v32};
                 
                 children[0] = rh_prim_create_polygon(4, c1_verts);
                 children[1] = rh_prim_create_polygon(4, c2_verts);
                 
                 // Inherit UVs (if we tracked them per vert, but we track global)
                 children[0].u_min = p->u_min; children[0].u_max = (p->u_min + p->u_max)*0.5f;
                 children[1].u_min = (p->u_min + p->u_max)*0.5f; children[1].u_max = p->u_max;
                 children[0].v_min = p->v_min; children[0].v_max = p->v_max;
                 children[1].v_min = p->v_min; children[1].v_max = p->v_max;
             } else {
                 // Split V
                 RhVec3 v03 = LERP(v0, v3, 0.5f);
                 RhVec3 v12 = LERP(v1, v2, 0.5f);
                 
                 RhVec3 c1_verts[4] = {v0, v1, v12, v03};
                 RhVec3 c2_verts[4] = {v03, v12, v2, v3};
                 
                 children[0] = rh_prim_create_polygon(4, c1_verts);
                 children[1] = rh_prim_create_polygon(4, c2_verts);
                 
                 children[0].v_min = p->v_min; children[0].v_max = (p->v_min + p->v_max)*0.5f;
                 children[1].v_min = (p->v_min + p->v_max)*0.5f; children[1].v_max = p->v_max;
                 children[0].u_min = p->u_min; children[0].u_max = p->u_max;
                 children[1].u_min = p->u_min; children[1].u_max = p->u_max;
             }
             return 2;
        }
    }

    // Split in half along largest parametric dimension
    RhFloat u_len = p->u_max - p->u_min;
    RhFloat v_len = p->v_max - p->v_min;

    if (u_len >= v_len) {
        // Split U
        RhFloat u_mid = (p->u_min + p->u_max) * 0.5f;
        
        children[0] = *p;
        children[0].u_max = u_mid;
        
        children[1] = *p;
        children[1].u_min = u_mid;
        
        return 2;
    } else {
        // Split V
        RhFloat v_mid = (p->v_min + p->v_max) * 0.5f;
        
        children[0] = *p;
        children[0].v_max = v_mid;
        
        children[1] = *p;
        children[1].v_min = v_mid;
        
        return 2;
    }
}

static RhVec3 evaluate_patch_bicubic(const RhPrimitive* p, RhFloat u, RhFloat v) {
    // P(u,v) = U * M_u * G * M_v^T * V^T
    // Where U = [u^3 u^2 u 1], V = [v^3 v^2 v 1]
    
    // Basis Matrices
    const RhMat4* Mu = &p->data.patch.u_basis;
    const RhMat4* Mv = &p->data.patch.v_basis;
    
    // U Vector
    float u2 = u * u;
    float u3 = u2 * u;
    float U[4] = {u3, u2, u, 1.0f};
    
    // V Vector
    float v2 = v * v;
    float v3 = v2 * v;
    float V[4] = {v3, v2, v, 1.0f};
    
    // U * Mu (1x4 * 4x4 = 1x4)
    float U_Mu[4];
    for(int i=0; i<4; i++) {
        U_Mu[i] = U[0] * Mu->m[0][i] + U[1] * Mu->m[1][i] + U[2] * Mu->m[2][i] + U[3] * Mu->m[3][i];
    }
    
    // V * Mv (1x4 * 4x4 = 1x4) (Using Mv here, we'll use Mv^T logic later, wait.
    // Formula usually: U * M * G * M^T * V^T
    // Let's compute Coeffs_V = Mv^T * V^T = (V * Mv)^T.
    // So let's compute V_Mv = V * Mv.
    float V_Mv[4];
    for(int i=0; i<4; i++) {
        V_Mv[i] = V[0] * Mv->m[0][i] + V[1] * Mv->m[1][i] + V[2] * Mv->m[2][i] + V[3] * Mv->m[3][i];
    }
    
    // Now we have blending weights U_Mu and V_Mv.
    // P = Sum(i=0..3, j=0..3) { U_Mu[i] * V_Mv[j] * G[i][j] }
    // G is 4x4 array of points.
    // p->data.patch.cp is linear array of 16 points. Row major?
    // Usually G rows are u, cols are v.
    // cp[0] is (0,0), cp[1] is (0,1) ...
    // Wait, RI Spec says: "Patch arrays are specified such that u varies faster than v."
    // So cp[0]=(u0,v0), cp[1]=(u1,v0)...
    // So row index is v, col index is u.
    // G[j][i] where j is row (v), i is col (u).
    // G[v][u].
    // Index = v * 4 + u.
    
    RhVec3 pos = {0,0,0};
    for(int j=0; j<4; j++) { // v row
        for(int i=0; i<4; i++) { // u col
            float weight = U_Mu[i] * V_Mv[j];
            RhVec3 pt = p->data.patch.cp[j * 4 + i];
            pos.x += pt.x * weight;
            pos.y += pt.y * weight;
            pos.z += pt.z * weight;
        }
    }
    
    return pos;
}

static RhVec3 evaluate_sphere(const RhPrimitive* p, RhFloat u, RhFloat v) {
    // Standard RenderMan sphere parameterization:
    // z = zmin + v * (zmax - zmin)
    // theta = tmin + u * (tmax - tmin)
    
    RhFloat r = p->data.sphere.radius;
    RhFloat zmin = p->data.sphere.zmin;
    RhFloat zmax = p->data.sphere.zmax;
    RhFloat tmin = p->data.sphere.theta_min;
    RhFloat tmax = p->data.sphere.theta_max;

    RhFloat z = zmin + v * (zmax - zmin);
    RhFloat theta = tmin + u * (tmax - tmin);
    
    // x^2 + y^2 + z^2 = r^2
    // radius at z: r_z = sqrt(r^2 - z^2)
    RhFloat r2 = r*r - z*z;
    RhFloat r_z = (r2 > 0) ? sqrtf(r2) : 0.0f;
    
    RhFloat theta_rad = theta * (RH_PI / 180.0f);
    
    RhVec3 pos;
    pos.x = r_z * cosf(theta_rad);
    pos.y = r_z * sinf(theta_rad);
    pos.z = z;
    
    return pos;
}

static RhVec3 evaluate_cylinder(const RhPrimitive* p, RhFloat u, RhFloat v) {
    // Cylinder:
    // x = r * cos(theta)
    // y = r * sin(theta)
    // z = zmin + v * (zmax - zmin)
    
    RhFloat r = p->data.cylinder.radius;
    RhFloat zmin = p->data.cylinder.zmin;
    RhFloat zmax = p->data.cylinder.zmax;
    RhFloat tmax = p->data.cylinder.theta_max;
    
    RhFloat z = zmin + v * (zmax - zmin);
    RhFloat theta = u * tmax;
    RhFloat theta_rad = theta * (RH_PI / 180.0f);
    
    RhVec3 pos;
    pos.x = r * cosf(theta_rad);
    pos.y = r * sinf(theta_rad);
    pos.z = z;
    return pos;
}

static RhVec3 evaluate_cone(const RhPrimitive* p, RhFloat u, RhFloat v) {
    // Cone:
    // z = v * height  (Assuming 0 to height)
    // r(z) = radius * (1 - z/height)
    // BUT: standard parameterization usually goes from z=0 (base) to z=height (apex)
    // or zmin to zmax if specified? RenderMan RiCone takes height.
    // RiCone(height, radius, thetamax). Base at z=0, apex at z=height.
    
    RhFloat h = p->data.cone.height;
    RhFloat r_base = p->data.cone.radius;
    RhFloat tmax = p->data.cone.theta_max;
    
    RhFloat z = v * h; // v: 0..1 -> z: 0..h
    RhFloat theta = u * tmax;
    RhFloat theta_rad = theta * (RH_PI / 180.0f);
    
    RhFloat r_z = r_base * (1.0f - z / h);
    
    RhVec3 pos;
    pos.x = r_z * cosf(theta_rad);
    pos.y = r_z * sinf(theta_rad);
    pos.z = z;
    return pos;
}

static RhVec3 evaluate_paraboloid(const RhPrimitive* p, RhFloat u, RhFloat v) {
    // Paraboloid:
    // r(z) = rmax * sqrt(z / zmax)
    // z = zmin + v * (zmax - zmin)
    
    RhFloat rmax = p->data.paraboloid.rmax;
    RhFloat zmin = p->data.paraboloid.zmin;
    RhFloat zmax = p->data.paraboloid.zmax;
    RhFloat tmax = p->data.paraboloid.theta_max;
    
    RhFloat z = zmin + v * (zmax - zmin);
    RhFloat theta = u * tmax;
    RhFloat theta_rad = theta * (RH_PI / 180.0f);
    
    // Safety for z/zmax
    RhFloat r_z = 0.0f;
    if (zmax != 0.0f) {
        float ratio = z / zmax;
        if (ratio < 0) ratio = 0; 
        r_z = rmax * sqrtf(ratio);
    }
    
    RhVec3 pos;
    pos.x = r_z * cosf(theta_rad);
    pos.y = r_z * sinf(theta_rad);
    pos.z = z;
    return pos;
}

static RhVec3 evaluate_polygon(const RhPrimitive* p, RhFloat u, RhFloat v) {
    // Bilinear Patch Interpolation
    // Map u,v [0..1] to the patch defined by vertices.
    // Assuming vertices are at corners.
    // But p->u_min/max might be small slice.
    // Wait, the polygon vertices IN THE PRIMITIVE define the corners of THIS patch (because we split vertices).
    // So for the current primitive, u goes 0..1 relative to ITS vertices.
    // BUT rh_prim_dice passes global u,v interpolated from p->u_min...
    // The splitter logic modified vertices. So the vertices currently in p represent the sub-patch.
    // Thus we should interpolate 0..1 relative to the vertices provided in p.
    // However, dice loop interpolates u from p->u_min to p->u_max.
    // The 'u' passed here is global.
    // IF we updated vertices during split, then 'evaluate' should treat u,v as 0..1 local coords.
    // BUT the dice loop logic is:
    //   v_step = j / (res-1) -> 0..1
    //   v = lerp(v_min, v_max, v_step)
    //   evaluate(..., v)
    // So 'v' is global.
    // If we split geometry, the vertices represent the sub-patch corresponding to [u_min, u_max].
    // This is conflicting.
    // If we split geometry (Polygons), we implicitly reset the parameterization 0..1 for the new quad?
    // In `rh_prim_split` for polygon, we set children u_min/max.
    // If we simply interpolate vertices using local UV, we need to map global UV 'u' back to 0..1 range of this primitive.
    
    RhFloat u_local = (p->u_max == p->u_min) ? 0.0f : (u - p->u_min) / (p->u_max - p->u_min);
    RhFloat v_local = (p->v_max == p->v_min) ? 0.0f : (v - p->v_min) / (p->v_max - p->v_min);
    
    RhVec3* vert = p->data.polygon.vertices;
    RhVec3 v0 = vert[0];
    RhVec3 v1 = vert[1];
    RhVec3 v2 = (p->data.polygon.count > 2) ? vert[2] : v1;
    RhVec3 v3 = (p->data.polygon.count > 3) ? vert[3] : v0;
    
    // Bilinear:
    // P(u,v) = (1-u)(1-v)V0 + u(1-v)V1 + u*v*V2 + (1-u)v*V3
    // Using local UV.
    
    RhVec3 p0 = rh_vec3_add(rh_vec3_mul(v0, 1.0f - u_local), rh_vec3_mul(v1, u_local));
    RhVec3 p1 = rh_vec3_add(rh_vec3_mul(v3, 1.0f - u_local), rh_vec3_mul(v2, u_local));
    return rh_vec3_add(rh_vec3_mul(p0, 1.0f - v_local), rh_vec3_mul(p1, v_local));
}

static RhVec3 evaluate_disk(const RhPrimitive* p, RhFloat u, RhFloat v) {
    // Disk:
    // x = r * cos(theta), y = r * sin(theta), z = height
    // u maps to theta: 0..theta_max
    // v maps to radius: 0..radius

    RhFloat height = p->data.disk.height;
    RhFloat radius = p->data.disk.radius;
    RhFloat tmax = p->data.disk.theta_max;

    RhFloat r = v * radius;
    RhFloat theta = u * tmax;
    RhFloat theta_rad = theta * (RH_PI / 180.0f);

    RhVec3 pos;
    pos.x = r * cosf(theta_rad);
    pos.y = r * sinf(theta_rad);
    pos.z = height;
    return pos;
}

static RhVec3 evaluate_torus(const RhPrimitive* p, RhFloat u, RhFloat v) {
    // Torus:
    // x = (R + r * cos(phi)) * cos(theta)
    // y = (R + r * cos(phi)) * sin(theta)
    // z = r * sin(phi)
    // u maps to theta: 0..theta_max
    // v maps to phi: phimin..phimax

    RhFloat R = p->data.torus.majorradius;
    RhFloat r = p->data.torus.minorradius;
    RhFloat phimin = p->data.torus.phimin;
    RhFloat phimax = p->data.torus.phimax;
    RhFloat tmax = p->data.torus.theta_max;

    RhFloat theta = u * tmax;
    RhFloat phi = phimin + v * (phimax - phimin);
    RhFloat theta_rad = theta * (RH_PI / 180.0f);
    RhFloat phi_rad = phi * (RH_PI / 180.0f);

    RhFloat tube_r = R + r * cosf(phi_rad);

    RhVec3 pos;
    pos.x = tube_r * cosf(theta_rad);
    pos.y = tube_r * sinf(theta_rad);
    pos.z = r * sinf(phi_rad);
    return pos;
}

static RhVec3 evaluate_hyperboloid(const RhPrimitive* p, RhFloat u, RhFloat v) {
    // Hyperboloid:
    // Linear interpolation between point1 and point2, then rotate around Z axis
    // v maps to lerp between point1 and point2
    // u maps to theta: 0..theta_max

    RhVec3 p1 = p->data.hyperboloid.point1;
    RhVec3 p2 = p->data.hyperboloid.point2;
    RhFloat tmax = p->data.hyperboloid.theta_max;

    // Interpolate between points
    RhFloat x = p1.x + v * (p2.x - p1.x);
    RhFloat y = p1.y + v * (p2.y - p1.y);
    RhFloat z = p1.z + v * (p2.z - p1.z);

    // Rotate around Z axis
    RhFloat theta = u * tmax;
    RhFloat theta_rad = theta * (RH_PI / 180.0f);
    RhFloat cos_t = cosf(theta_rad);
    RhFloat sin_t = sinf(theta_rad);

    RhVec3 pos;
    pos.x = x * cos_t - y * sin_t;
    pos.y = x * sin_t + y * cos_t;
    pos.z = z;
    return pos;
}

// Public function to evaluate a single surface point
RhVec3 rh_prim_eval_point(const RhPrimitive* p, RhFloat u, RhFloat v) {
    switch (p->type) {
        case RH_PRIM_SPHERE: return evaluate_sphere(p, u, v);
        case RH_PRIM_CYLINDER: return evaluate_cylinder(p, u, v);
        case RH_PRIM_CONE: return evaluate_cone(p, u, v);
        case RH_PRIM_PARABOLOID: return evaluate_paraboloid(p, u, v);
        case RH_PRIM_POLYGON: return evaluate_polygon(p, u, v);
        case RH_PRIM_PATCH_BICUBIC: return evaluate_patch_bicubic(p, u, v);
        case RH_PRIM_DISK: return evaluate_disk(p, u, v);
        case RH_PRIM_TORUS: return evaluate_torus(p, u, v);
        case RH_PRIM_HYPERBOLOID: return evaluate_hyperboloid(p, u, v);
        default: return rh_vec3_create(0, 0, 0);
    }
}

void rh_prim_dice(const RhPrimitive* p, int u_res, int v_res, RhMicroGrid* grid) {
    // Fill the grid with vertices
    // We iterate u_res and v_res steps over the primitive's current u_min/max range
    
    for (int j = 0; j < v_res; j++) {
        RhFloat v_step = (RhFloat)j / (RhFloat)(v_res - 1); // 0..1
        RhFloat v = rh_lerp(p->v_min, p->v_max, v_step);
        
        for (int i = 0; i < u_res; i++) {
            RhFloat u_step = (RhFloat)i / (RhFloat)(u_res - 1); // 0..1
            RhFloat u = rh_lerp(p->u_min, p->u_max, u_step);
            
            RhVec3 pos = {0,0,0};
            
            switch (p->type) {
                case RH_PRIM_SPHERE: pos = evaluate_sphere(p, u, v); break;
                case RH_PRIM_CYLINDER: pos = evaluate_cylinder(p, u, v); break;
                case RH_PRIM_CONE: pos = evaluate_cone(p, u, v); break;
                case RH_PRIM_PARABOLOID: pos = evaluate_paraboloid(p, u, v); break;
                case RH_PRIM_POLYGON: pos = evaluate_polygon(p, u, v); break;
                case RH_PRIM_PATCH_BICUBIC: pos = evaluate_patch_bicubic(p, u, v); break;
                case RH_PRIM_DISK: pos = evaluate_disk(p, u, v); break;
                case RH_PRIM_TORUS: pos = evaluate_torus(p, u, v); break;
                case RH_PRIM_HYPERBOLOID: pos = evaluate_hyperboloid(p, u, v); break;
            }
            
            // Calculate Normals
            // For analytical surfaces, we could compute derivative, but for MVP let's use Finite Differences
            // or just use the center-difference from neighbors in the grid?
            // Since we are inside the 'dice' loop, we have u,v.
            // Let's compute analytical normals if possible, or fallback to position approximation.
            // For Sphere at origin: Normal = Normalize(Pos).
            // For Cylinder: Normal = Normalize(x, y, 0).
            // For others it varies.
            
            // To be robust and generic (like REYES), we should compute derivatives dPdu and dPdv
            // and Cross(dPdu, dPdv).
            // Let's approximate dPdu and dPdv by sampling a tiny epsilon away.
            RhFloat eps = 1e-4f;
            
            // Re-evaluate slightly offset (clamped to domain? ignore domain for derivative check)
            // Just use the math functions again.
            
            RhVec3 p_plus_u, p_plus_v;
            
             switch (p->type) {
                case RH_PRIM_SPHERE:
                    p_plus_u = evaluate_sphere(p, u + eps, v);
                    p_plus_v = evaluate_sphere(p, u, v + eps);
                    break;
                case RH_PRIM_CYLINDER:
                    p_plus_u = evaluate_cylinder(p, u + eps, v);
                    p_plus_v = evaluate_cylinder(p, u, v + eps);
                    break;
                case RH_PRIM_CONE:
                    p_plus_u = evaluate_cone(p, u + eps, v);
                    p_plus_v = evaluate_cone(p, u, v + eps);
                    break;
                case RH_PRIM_PARABOLOID:
                    p_plus_u = evaluate_paraboloid(p, u + eps, v);
                    p_plus_v = evaluate_paraboloid(p, u, v + eps);
                    break;
                case RH_PRIM_POLYGON:
                    p_plus_u = evaluate_polygon(p, u + eps, v);
                    p_plus_v = evaluate_polygon(p, u, v + eps);
                    break;
                case RH_PRIM_PATCH_BICUBIC:
                    p_plus_u = evaluate_patch_bicubic(p, u + eps, v);
                    p_plus_v = evaluate_patch_bicubic(p, u, v + eps);
                    break;
                case RH_PRIM_DISK:
                    p_plus_u = evaluate_disk(p, u + eps, v);
                    p_plus_v = evaluate_disk(p, u, v + eps);
                    break;
                case RH_PRIM_TORUS:
                    p_plus_u = evaluate_torus(p, u + eps, v);
                    p_plus_v = evaluate_torus(p, u, v + eps);
                    break;
                case RH_PRIM_HYPERBOLOID:
                    p_plus_u = evaluate_hyperboloid(p, u + eps, v);
                    p_plus_v = evaluate_hyperboloid(p, u, v + eps);
                    break;
            }
            
            RhVec3 dPdu = rh_vec3_div(rh_vec3_sub(p_plus_u, pos), eps);
            RhVec3 dPdv = rh_vec3_div(rh_vec3_sub(p_plus_v, pos), eps);

            RhVec3 norm = rh_vec3_cross(dPdu, dPdv); // Standard: dPdu x dPdv

            // Check if cross product is degenerate (e.g., at poles of sphere, center of disk)
            RhFloat norm_len_sq = norm.x*norm.x + norm.y*norm.y + norm.z*norm.z;
            if (norm_len_sq < 1e-8f) {
                // Fallback: use analytic normals for known primitives
                if (p->type == RH_PRIM_SPHERE) {
                    // For sphere centered at origin: N = normalize(P)
                    norm = rh_vec3_normalize(pos);
                } else if (p->type == RH_PRIM_CYLINDER) {
                    // For cylinder along z-axis: N = normalize(x, y, 0)
                    norm = rh_vec3_normalize(rh_vec3_create(pos.x, pos.y, 0.0f));
                } else if (p->type == RH_PRIM_CONE) {
                    // For cone along z-axis, normal depends on cone angle
                    norm = rh_vec3_normalize(rh_vec3_create(pos.x, pos.y, 0.0f));
                } else if (p->type == RH_PRIM_DISK) {
                    // Disk normal is always +Z (or -Z for negative thetamax)
                    norm = rh_vec3_create(0.0f, 0.0f, 1.0f);
                } else if (p->type == RH_PRIM_TORUS) {
                    // Torus normal points outward from tube center
                    RhFloat R = p->data.torus.majorradius;
                    RhFloat theta_rad = u * p->data.torus.theta_max * (RH_PI / 180.0f);
                    RhVec3 ring_center = rh_vec3_create(R * cosf(theta_rad), R * sinf(theta_rad), 0.0f);
                    norm = rh_vec3_normalize(rh_vec3_sub(pos, ring_center));
                } else {
                    // Last resort: use any non-zero derivative as normal direction
                    RhFloat dPdu_len_sq = dPdu.x*dPdu.x + dPdu.y*dPdu.y + dPdu.z*dPdu.z;
                    RhFloat dPdv_len_sq = dPdv.x*dPdv.x + dPdv.y*dPdv.y + dPdv.z*dPdv.z;
                    if (dPdv_len_sq > dPdu_len_sq) {
                        norm = rh_vec3_normalize(dPdv);
                    } else if (dPdu_len_sq > 1e-8f) {
                        norm = rh_vec3_normalize(dPdu);
                    } else {
                        norm = rh_vec3_create(0.0f, 0.0f, 1.0f); // Default up
                    }
                }
            } else {
                norm = rh_vec3_normalize(norm);
            }
            
            int idx = j * grid->width + i;
            grid->positions[idx] = pos;
            grid->normals[idx] = norm;
            grid->u_coords[idx] = u;
            grid->v_coords[idx] = v;

            // Default color (white)
            grid->colors[idx] = (RhColor){1.0f, 1.0f, 1.0f};
        }
    }
}
