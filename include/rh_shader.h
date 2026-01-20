#ifndef RH_SHADER_H
#define RH_SHADER_H

#include "rh_math.h"
#include "rh_image.h"
#include "ri.h"

// Shader Inputs/Outputs (Surface Global Variables in RSL)
typedef struct {
    // Inputs (ReadOnly usually)
    RhVec3 P;  // Surface Position (Camera Space)
    RhVec3 N;  // Surface Normal (Camera Space)
    RhVec3 I;  // Incident Vector (Camera -> Point)

    RhColor Cs; // Surface Color
    RhColor Os; // Surface Opacity

    RhFloat u, v; // Texture coords
    RhFloat du, dv;

    // Outputs
    RhColor Ci; // Incident Color (Final Shaded Color)
    RhColor Oi; // Incident Opacity

    // Light Loop Helpers
    // In a real RSL compiler, illuminance() loops lights.
    // Here we might need access to the light list.
    void* light_list;
    int num_lights;

    // Grid/vertex identification for diagnostic shaders
    void* grid_ptr;      // Pointer to current grid (for hashing)
    int vertex_index;    // Index of vertex within grid

    // User-defined primitive variables (primvars)
    RhPrimVar* primvars;  // Array of primitive variables from grid
    int num_primvars;     // Number of primitive variables

} RhShaderContext;

// Primvar lookup helper for shaders
RhPrimVar* rh_shader_get_primvar(RhShaderContext* ctx, const char* name);

typedef void (*RhShaderFunc)(RhShaderContext* ctx, void* params);

// Standard Surface Shaders
void rh_shader_surface_constant(RhShaderContext* ctx, void* params);
void rh_shader_surface_matte(RhShaderContext* ctx, void* params);
void rh_shader_surface_plastic(RhShaderContext* ctx, void* params);
void rh_shader_surface_metal(RhShaderContext* ctx, void* params);
void rh_shader_surface_paintedplastic(RhShaderContext* ctx, void* params);
void rh_shader_surface_shinymetal(RhShaderContext* ctx, void* params);

// Diagnostic Shaders
void rh_shader_surface_randomgrid(RhShaderContext* ctx, void* params);
void rh_shader_surface_random(RhShaderContext* ctx, void* params);

// Parameter Structures for Standard Shaders
typedef struct {
    float Ka;
    float Kd;
} RhMatteParams;

typedef struct {
    float Ka;
    float Kd;
    float Ks;
    float roughness;
    RhColor specular_color;
} RhPlasticParams;

typedef struct {
    float Ka;
    float Ks;
    float roughness;
} RhMetalParams;

typedef struct {
    float Ka;
    float Kd;
    float Ks;
    float roughness;
    RhColor specular_color;
    char texturename[256];  // Path to texture file
    void* texture;          // Cached RhTexture pointer (set by RiSurface)
} RhPaintedPlasticParams;

typedef struct {
    float Ka;
    float Ks;
    float Kr;              // Reflection coefficient
    float roughness;
    char texturename[256]; // Placeholder for environment map (not implemented yet)
} RhShinyMetalParams;

#endif // RH_SHADER_H
