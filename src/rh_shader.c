#include "rh_shader.h"
#include <math.h>
#include <stddef.h>

// Helper: Standard Lighting Loop (Simulating 'illuminance' or simple additive)
// For MVP, we'll just pass lights array in void* or access global?
// To keep it pure, we should pass lights in params or context. 
// Let's assume context has access to a "Light Evaluator" or simple list.
// For now, I'll cheat and make it opaque, but we need to calculate L.

// We need the light struct definition here or a helper to evaluate lights.
// Let's import the light definition from somewhere or redefine a common one.
// Actually, `ri.c` defines `RhLight`. We should move `RhLight` to `rh_shader.h` or `ri.h` 
// if shaders need to iterate them.
// Or, we provide `rh_shader_illuminance` callback in context.

#include "rh_math.h"

// Temporary Light Struct for this file (must match ri.c RhLight exactly!)
typedef struct {
    char type[32];
    RhVec3 position;
    RhVec3 direction;
    RhColor color;
    float intensity;
    RhMat4 transform;  // Must include this to match ri.c RhLight struct size
    // Spotlight parameters
    float coneangle;
    float conedeltaangle;
    float beamdistribution;
} RhLight_ShaderView;

static void calculate_lights(RhShaderContext* ctx, RhColor* ambient_out, RhColor* diff, RhColor* spec, float roughness) {
    *ambient_out = (RhColor){0,0,0};
    *diff = (RhColor){0,0,0};
    *spec = (RhColor){0,0,0};

    if (!ctx->light_list) return;

    RhLight_ShaderView* lights = (RhLight_ShaderView*)ctx->light_list;

    RhVec3 V = rh_vec3_normalize(rh_vec3_mul(ctx->I, -1.0f)); // View vector
    RhVec3 Nn = rh_vec3_normalize(ctx->N);

    for (int i = 0; i < ctx->num_lights; i++) {
        RhLight_ShaderView* l = &lights[i];

        RhColor lc;
        lc.r = l->color.r * l->intensity;
        lc.g = l->color.g * l->intensity;
        lc.b = l->color.b * l->intensity;

        // Ambient light - contributes uniformly
        if (l->type[0] == 'a') { // "ambientlight"
            ambient_out->r += lc.r;
            ambient_out->g += lc.g;
            ambient_out->b += lc.b;
            continue;
        }

        RhVec3 L;
        float attenuation = 1.0f;

        if (l->type[0] == 'd') { // "distantlight"
            L = l->direction;
        } else if (l->type[0] == 's') { // "spotlight"
            L = rh_vec3_normalize(rh_vec3_sub(l->position, ctx->P));

            // Spotlight cone attenuation
            // Angle between light direction and vector to surface
            float cos_angle = -rh_vec3_dot(l->direction, L);
            float angle = acosf(rh_max(-1.0f, rh_min(1.0f, cos_angle))) * (180.0f / RH_PI);

            float inner_angle = l->coneangle;
            float outer_angle = l->coneangle + l->conedeltaangle;

            if (angle > outer_angle) {
                attenuation = 0.0f;
            } else if (angle > inner_angle) {
                // Smooth falloff in penumbra
                float t = (angle - inner_angle) / (outer_angle - inner_angle);
                attenuation = 1.0f - t;
                attenuation *= attenuation; // Quadratic falloff
            }

            // Beam distribution (cosine power)
            if (attenuation > 0.0f && l->beamdistribution > 0.0f) {
                attenuation *= powf(rh_max(0.0f, cos_angle), l->beamdistribution);
            }
        } else { // "pointlight" or other
            L = rh_vec3_normalize(rh_vec3_sub(l->position, ctx->P));
        }

        float n_dot_l = rh_vec3_dot(Nn, L);

        if (n_dot_l > 0.0f && attenuation > 0.0f) {
            float contrib = n_dot_l * attenuation;

            // Diffuse
            diff->r += lc.r * contrib;
            diff->g += lc.g * contrib;
            diff->b += lc.b * contrib;

            // Specular (Phong)
            if (roughness > 0.0f) {
                RhVec3 R = rh_vec3_sub(rh_vec3_mul(Nn, 2.0f * n_dot_l), L);
                float r_dot_v = rh_vec3_dot(R, V);
                if (r_dot_v > 0.0f) {
                    float spec_power = 1.0f / roughness;
                    float s = powf(r_dot_v, spec_power) * attenuation;
                    spec->r += lc.r * s;
                    spec->g += lc.g * s;
                    spec->b += lc.b * s;
                }
            }
        }
    }
}

void rh_shader_surface_constant(RhShaderContext* ctx, void* params) {
    (void)params;
    ctx->Oi = ctx->Os;
    ctx->Ci = ctx->Cs; // Ci = Cs * Os usually? 
    // Premultiplied alpha? RenderMan usually does premult.
    ctx->Ci.r *= ctx->Oi.r;
    ctx->Ci.g *= ctx->Oi.g;
    ctx->Ci.b *= ctx->Oi.b;
}

void rh_shader_surface_matte(RhShaderContext* ctx, void* params) {
    RhMatteParams* p = (RhMatteParams*)params;
    float Ka = p ? p->Ka : 1.0f;
    float Kd = p ? p->Kd : 1.0f;

    RhColor ambient = {0,0,0};
    RhColor diffuse = {0,0,0};
    RhColor specular = {0,0,0}; // Unused for matte

    calculate_lights(ctx, &ambient, &diffuse, &specular, 0.0f);

    // If no ambient lights defined, use a default ambient approximation
    if (ambient.r == 0.0f && ambient.g == 0.0f && ambient.b == 0.0f) {
        ambient.r = ambient.g = ambient.b = 0.2f;
    }

    RhColor final;
    // Ambient
    final.r = ctx->Cs.r * Ka * ambient.r;
    final.g = ctx->Cs.g * Ka * ambient.g;
    final.b = ctx->Cs.b * Ka * ambient.b;

    // Diffuse
    final.r += ctx->Cs.r * Kd * diffuse.r;
    final.g += ctx->Cs.g * Kd * diffuse.g;
    final.b += ctx->Cs.b * Kd * diffuse.b;

    ctx->Oi = ctx->Os;
    ctx->Ci = final;

    // Premult
    ctx->Ci.r *= ctx->Oi.r;
    ctx->Ci.g *= ctx->Oi.g;
    ctx->Ci.b *= ctx->Oi.b;
}

void rh_shader_surface_plastic(RhShaderContext* ctx, void* params) {
    RhPlasticParams* p = (RhPlasticParams*)params;
    float Ka = p ? p->Ka : 1.0f;
    float Kd = p ? p->Kd : 0.5f;
    float Ks = p ? p->Ks : 0.5f;
    float r = p ? p->roughness : 0.1f;
    RhColor Cspec = p ? p->specular_color : (RhColor){1,1,1};

    RhColor ambient = {0,0,0};
    RhColor diff = {0,0,0};
    RhColor spec = {0,0,0};

    calculate_lights(ctx, &ambient, &diff, &spec, r);

    // Default ambient if no ambient lights
    if (ambient.r == 0.0f && ambient.g == 0.0f && ambient.b == 0.0f) {
        ambient.r = ambient.g = ambient.b = 0.2f;
    }

    RhColor final;
    // Ambient
    final.r = ctx->Cs.r * Ka * ambient.r;
    final.g = ctx->Cs.g * Ka * ambient.g;
    final.b = ctx->Cs.b * Ka * ambient.b;

    // Diffuse
    final.r += ctx->Cs.r * Kd * diff.r;
    final.g += ctx->Cs.g * Kd * diff.g;
    final.b += ctx->Cs.b * Kd * diff.b;

    // Specular
    final.r += Cspec.r * Ks * spec.r;
    final.g += Cspec.g * Ks * spec.g;
    final.b += Cspec.b * Ks * spec.b;

    ctx->Oi = ctx->Os;
    ctx->Ci = final;

    ctx->Ci.r *= ctx->Oi.r;
    ctx->Ci.g *= ctx->Oi.g;
    ctx->Ci.b *= ctx->Oi.b;
}

void rh_shader_surface_metal(RhShaderContext* ctx, void* params) {
    RhMetalParams* p = (RhMetalParams*)params;
    float Ka = p ? p->Ka : 1.0f;
    float Ks = p ? p->Ks : 1.0f;
    float r = p ? p->roughness : 0.1f;

    RhColor ambient = {0,0,0};
    RhColor diff = {0,0,0}; // Unused - metal has no diffuse
    RhColor spec = {0,0,0};

    calculate_lights(ctx, &ambient, &diff, &spec, r);

    // Default ambient if no ambient lights
    if (ambient.r == 0.0f && ambient.g == 0.0f && ambient.b == 0.0f) {
        ambient.r = ambient.g = ambient.b = 0.2f;
    }

    RhColor final;
    // Metal: specular color is the surface color (Cs)
    // Ci = Cs * (Ka * ambient + Ks * specular)
    final.r = ctx->Cs.r * (Ka * ambient.r + Ks * spec.r);
    final.g = ctx->Cs.g * (Ka * ambient.g + Ks * spec.g);
    final.b = ctx->Cs.b * (Ka * ambient.b + Ks * spec.b);

    ctx->Oi = ctx->Os;
    ctx->Ci = final;

    ctx->Ci.r *= ctx->Oi.r;
    ctx->Ci.g *= ctx->Oi.g;
    ctx->Ci.b *= ctx->Oi.b;
}

void rh_shader_surface_paintedplastic(RhShaderContext* ctx, void* params) {
    // Paintedplastic is like plastic but with texture-mapped surface color.
    // Without texture mapping, we just use Cs as the surface color (fallback).
    RhPaintedPlasticParams* p = (RhPaintedPlasticParams*)params;
    float Ka = p ? p->Ka : 1.0f;
    float Kd = p ? p->Kd : 0.5f;
    float Ks = p ? p->Ks : 0.5f;
    float r = p ? p->roughness : 0.1f;
    RhColor Cspec = p ? p->specular_color : (RhColor){1,1,1};

    // In full implementation, we would sample texture here using ctx->u, ctx->v
    // For now, just use Cs
    RhColor surface_color = ctx->Cs;

    RhColor ambient = {0,0,0};
    RhColor diff = {0,0,0};
    RhColor spec = {0,0,0};

    calculate_lights(ctx, &ambient, &diff, &spec, r);

    // Default ambient if no ambient lights
    if (ambient.r == 0.0f && ambient.g == 0.0f && ambient.b == 0.0f) {
        ambient.r = ambient.g = ambient.b = 0.2f;
    }

    RhColor final;
    // Ambient
    final.r = surface_color.r * Ka * ambient.r;
    final.g = surface_color.g * Ka * ambient.g;
    final.b = surface_color.b * Ka * ambient.b;

    // Diffuse
    final.r += surface_color.r * Kd * diff.r;
    final.g += surface_color.g * Kd * diff.g;
    final.b += surface_color.b * Kd * diff.b;

    // Specular (white highlight for plastic)
    final.r += Cspec.r * Ks * spec.r;
    final.g += Cspec.g * Ks * spec.g;
    final.b += Cspec.b * Ks * spec.b;

    ctx->Oi = ctx->Os;
    ctx->Ci = final;

    ctx->Ci.r *= ctx->Oi.r;
    ctx->Ci.g *= ctx->Oi.g;
    ctx->Ci.b *= ctx->Oi.b;
}

void rh_shader_surface_shinymetal(RhShaderContext* ctx, void* params) {
    // Shinymetal is like metal but with environment reflection.
    // Without environment maps, we approximate with a fresnel-like reflection term.
    RhShinyMetalParams* p = (RhShinyMetalParams*)params;
    float Ka = p ? p->Ka : 1.0f;
    float Ks = p ? p->Ks : 1.0f;
    float Kr = p ? p->Kr : 0.5f;
    float r = p ? p->roughness : 0.1f;

    RhColor ambient = {0,0,0};
    RhColor diff = {0,0,0}; // Unused - metal has no diffuse
    RhColor spec = {0,0,0};

    calculate_lights(ctx, &ambient, &diff, &spec, r);

    // Default ambient if no ambient lights
    if (ambient.r == 0.0f && ambient.g == 0.0f && ambient.b == 0.0f) {
        ambient.r = ambient.g = ambient.b = 0.2f;
    }

    // Compute reflection approximation without environment map
    // Use fresnel-like effect: more reflection at grazing angles
    RhVec3 V = rh_vec3_normalize(rh_vec3_mul(ctx->I, -1.0f));
    RhVec3 Nn = rh_vec3_normalize(ctx->N);
    float n_dot_v = rh_vec3_dot(Nn, V);
    if (n_dot_v < 0.0f) n_dot_v = 0.0f;

    // Schlick's fresnel approximation: F = F0 + (1 - F0) * (1 - cos(theta))^5
    // For metal, F0 is high (close to 1), so reflection is strong
    float fresnel = 0.9f + 0.1f * powf(1.0f - n_dot_v, 5.0f);

    // Simple fake reflection: blend towards a sky-like color at grazing angles
    // This approximates environment reflection without actual environment map
    RhColor reflection_color;
    float t = 1.0f - n_dot_v; // More reflection at grazing angles
    t = t * t; // Sharpen the falloff
    // Blend between surface color and a "sky" color (light blue-ish)
    RhColor sky = {0.6f, 0.7f, 0.9f};
    reflection_color.r = ctx->Cs.r * (1.0f - t) + sky.r * t;
    reflection_color.g = ctx->Cs.g * (1.0f - t) + sky.g * t;
    reflection_color.b = ctx->Cs.b * (1.0f - t) + sky.b * t;

    RhColor final;
    // Metal base: specular color is surface color
    final.r = ctx->Cs.r * (Ka * ambient.r + Ks * spec.r);
    final.g = ctx->Cs.g * (Ka * ambient.g + Ks * spec.g);
    final.b = ctx->Cs.b * (Ka * ambient.b + Ks * spec.b);

    // Add reflection component
    final.r += Kr * fresnel * reflection_color.r;
    final.g += Kr * fresnel * reflection_color.g;
    final.b += Kr * fresnel * reflection_color.b;

    ctx->Oi = ctx->Os;
    ctx->Ci = final;

    ctx->Ci.r *= ctx->Oi.r;
    ctx->Ci.g *= ctx->Oi.g;
    ctx->Ci.b *= ctx->Oi.b;
}
