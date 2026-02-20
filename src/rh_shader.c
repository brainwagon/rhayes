#include "rh_shader.h"
#include "rh_texture.h"
#include "rh_shadow.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Primvar Lookup Helper ---

RhPrimVar* rh_shader_get_primvar(RhShaderContext* ctx, const char* name) {
    if (!ctx || !name || !ctx->primvars) return NULL;

    for (int i = 0; i < ctx->num_primvars; i++) {
        if (strcmp(ctx->primvars[i].name, name) == 0) {
            return &ctx->primvars[i];
        }
    }
    return NULL;
}

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

// Temporary Light Struct for this file (must match ri_internal.h RhLight exactly!)
typedef struct {
    char type[32];
    RhVec3 position;
    RhVec3 direction;
    RhColor color;
    float intensity;
    RhMat4 transform;  // Must include this to match RhLight struct size
    // Spotlight parameters
    float coneangle;
    float conedeltaangle;
    float beamdistribution;
    // Shadow map parameters
    RhShadowMap* shadowmap;
    int shadow_samples;
    float shadow_bias;
    float shadow_blur;
    // VM light shader
    RhShaderFunc light_shader;
    void* light_shader_params;
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

        // Ambient light - contributes uniformly (no shadows for ambient)
        if (l->type[0] == 'a') { // "ambientlight"
            if (l->light_shader) {
                // VM ambient light shader
                RhShaderContext ctx_light;
                memset(&ctx_light, 0, sizeof(ctx_light));
                ctx_light.Ps = ctx->P;
                ctx_light.N = ctx->N;
                l->light_shader(&ctx_light, l->light_shader_params);
                ambient_out->r += ctx_light.Cl_out.r;
                ambient_out->g += ctx_light.Cl_out.g;
                ambient_out->b += ctx_light.Cl_out.b;
            } else {
                ambient_out->r += lc.r;
                ambient_out->g += lc.g;
                ambient_out->b += lc.b;
            }
            continue;
        }

        RhVec3 L;
        RhColor light_cl;

        if (l->light_shader) {
            // Execute VM light shader (light params are in camera space).
            // Pass transform_ctx so the shader VM can handle coordinate transforms
            // (e.g. solar blocks transform their axis to camera space).
            RhShaderContext ctx_light;
            memset(&ctx_light, 0, sizeof(ctx_light));
            ctx_light.Ps = ctx->P;
            ctx_light.N = ctx->N;
            ctx_light.P_world = ctx->P_world;
            ctx_light.transform_ctx = ctx->transform_ctx;
            l->light_shader(&ctx_light, l->light_shader_params);

            // Check if light contributes (L == 0 means outside cone or ambient)
            float Llen2 = ctx_light.L_out.x * ctx_light.L_out.x
                        + ctx_light.L_out.y * ctx_light.L_out.y
                        + ctx_light.L_out.z * ctx_light.L_out.z;
            if (Llen2 < 1e-24f) continue;

            // L from illuminate/solar block is light-to-surface.
            // Negate to get surface-to-light for N.L calculations.
            float inv_len = 1.0f / sqrtf(Llen2);
            L.x = -ctx_light.L_out.x * inv_len;
            L.y = -ctx_light.L_out.y * inv_len;
            L.z = -ctx_light.L_out.z * inv_len;
            light_cl.r = ctx_light.Cl_out.r;
            light_cl.g = ctx_light.Cl_out.g;
            light_cl.b = ctx_light.Cl_out.b;
        } else {
            float attenuation = 1.0f;

            if (l->type[0] == 'd') { // "distantlight"
                L = l->direction;
            } else if (l->type[0] == 's') { // "spotlight"
                RhVec3 toLight = rh_vec3_sub(l->position, ctx->P);
                float dist2 = rh_vec3_dot(toLight, toLight);
                if (dist2 < 1e-12f) continue;
                L = rh_vec3_mul(toLight, 1.0f / sqrtf(dist2));
                attenuation /= dist2;

                // Spotlight cone attenuation
                float cos_angle = rh_vec3_dot(l->direction, L);
                float angle = acosf(rh_max(-1.0f, rh_min(1.0f, cos_angle)));

                float inner_angle = l->coneangle;
                float outer_angle = l->coneangle + l->conedeltaangle;

                if (angle > outer_angle) {
                    attenuation = 0.0f;
                } else if (angle > inner_angle) {
                    float t = (angle - inner_angle) / (outer_angle - inner_angle);
                    attenuation = 1.0f - t;
                    attenuation *= attenuation;
                }

                if (attenuation > 0.0f && l->beamdistribution > 0.0f) {
                    attenuation *= powf(rh_max(0.0f, cos_angle), l->beamdistribution);
                }
            } else { // "pointlight" or other
                RhVec3 toLight = rh_vec3_sub(l->position, ctx->P);
                float dist2 = rh_vec3_dot(toLight, toLight);
                if (dist2 < 1e-12f) continue;
                L = rh_vec3_mul(toLight, 1.0f / sqrtf(dist2));
                attenuation /= dist2;
            }

            // Shadow map lookup (if light has shadow map)
            float shadow_factor = 0.0f;
            if (l->shadowmap && attenuation > 0.0f) {
                if (rh_shadow_in_frustum(l->shadowmap, ctx->P_world)) {
                    shadow_factor = rh_shadow_pcf_lookup(
                        l->shadowmap,
                        ctx->P_world,
                        l->shadow_samples,
                        l->shadow_bias,
                        l->shadow_blur
                    );
                }
            }

            float light_visibility = 1.0f - shadow_factor;
            attenuation *= light_visibility;

            if (attenuation <= 0.0f) continue;

            light_cl.r = lc.r * attenuation;
            light_cl.g = lc.g * attenuation;
            light_cl.b = lc.b * attenuation;
        }

        float n_dot_l = rh_vec3_dot(Nn, L);

        if (n_dot_l > 0.0f) {
            // Diffuse
            diff->r += light_cl.r * n_dot_l;
            diff->g += light_cl.g * n_dot_l;
            diff->b += light_cl.b * n_dot_l;

            // Specular (Phong)
            if (roughness > 0.0f) {
                RhVec3 R = rh_vec3_sub(rh_vec3_mul(Nn, 2.0f * n_dot_l), L);
                float r_dot_v = rh_vec3_dot(R, V);
                if (r_dot_v > 0.0f) {
                    float spec_power = 1.0f / roughness;
                    float s = powf(r_dot_v, spec_power);
                    spec->r += light_cl.r * s;
                    spec->g += light_cl.g * s;
                    spec->b += light_cl.b * s;
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
    RhPaintedPlasticParams* p = (RhPaintedPlasticParams*)params;
    float Ka = p ? p->Ka : 1.0f;
    float Kd = p ? p->Kd : 0.5f;
    float Ks = p ? p->Ks : 0.5f;
    float r = p ? p->roughness : 0.1f;
    RhColor Cspec = p ? p->specular_color : (RhColor){1,1,1};

    // Sample texture if available, otherwise use Cs
    RhColor surface_color;
    if (p && p->texture) {
        RhTexture* tex = (RhTexture*)p->texture;
        surface_color = rh_texture_sample(tex, ctx->u, ctx->v, ctx->du, ctx->dv);
    } else {
        surface_color = ctx->Cs;
    }

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

// --- Diagnostic Shaders ---

// Simple hash function for pointer values
static unsigned int hash_ptr(void* ptr) {
    unsigned long val = (unsigned long)ptr;
    val = ((val >> 16) ^ val) * 0x45d9f3b;
    val = ((val >> 16) ^ val) * 0x45d9f3b;
    val = (val >> 16) ^ val;
    return (unsigned int)val;
}

static RhColor hash_to_color(unsigned int hash) {
    RhColor c;
    c.r = ((hash >> 0) & 0xFF) / 255.0f;
    c.g = ((hash >> 8) & 0xFF) / 255.0f;
    c.b = ((hash >> 16) & 0xFF) / 255.0f;
    return c;
}

void rh_shader_surface_randomgrid(RhShaderContext* ctx, void* params) {
    (void)params;
    unsigned int hash = hash_ptr(ctx->grid_ptr);
    RhColor color = hash_to_color(hash);
    ctx->Oi = ctx->Os;
    ctx->Ci = color;
    ctx->Ci.r *= ctx->Oi.r;
    ctx->Ci.g *= ctx->Oi.g;
    ctx->Ci.b *= ctx->Oi.b;
}

void rh_shader_surface_random(RhShaderContext* ctx, void* params) {
    (void)params;
    unsigned int hash = hash_ptr(ctx->grid_ptr);
    hash ^= (unsigned int)ctx->vertex_index * 2654435761u;
    RhColor color = hash_to_color(hash);
    ctx->Oi = ctx->Os;
    ctx->Ci = color;
    ctx->Ci.r *= ctx->Oi.r;
    ctx->Ci.g *= ctx->Oi.g;
    ctx->Ci.b *= ctx->Oi.b;
}

// --- Atmosphere Shaders ---

static float clamp01(float x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

void rh_shader_atmosphere_depthcue(RhShaderContext* ctx, void* params) {
    RhDepthcueParams* p = (RhDepthcueParams*)params;
    float mindist = 0.0f, maxdist = 1.0f;
    RhColor background = {0.0f, 0.0f, 0.0f};

    if (p) {
        mindist = p->mindistance;
        maxdist = p->maxdistance;
        background = p->background;
    }

    float depth = ctx->P.z;
    float range = maxdist - mindist;
    float f = (range > 0.0f) ? clamp01((depth - mindist) / range) : 0.0f;

    // Un-premultiply Ci (surface shaders premultiply Ci *= Oi)
    RhColor Ci;
    Ci.r = (ctx->Oi.r > 1e-6f) ? ctx->Ci.r / ctx->Oi.r : 0.0f;
    Ci.g = (ctx->Oi.g > 1e-6f) ? ctx->Ci.g / ctx->Oi.g : 0.0f;
    Ci.b = (ctx->Oi.b > 1e-6f) ? ctx->Ci.b / ctx->Oi.b : 0.0f;

    // Blend toward background color
    float inv_f = 1.0f - f;
    Ci.r = Ci.r * inv_f + background.r * f;
    Ci.g = Ci.g * inv_f + background.g * f;
    Ci.b = Ci.b * inv_f + background.b * f;

    // Re-premultiply
    ctx->Ci.r = Ci.r * ctx->Oi.r;
    ctx->Ci.g = Ci.g * ctx->Oi.g;
    ctx->Ci.b = Ci.b * ctx->Oi.b;
}

void rh_shader_atmosphere_fog(RhShaderContext* ctx, void* params) {
    RhFogParams* p = (RhFogParams*)params;
    float dist = 1.0f;
    RhColor background = {0.0f, 0.0f, 0.0f};

    if (p) {
        dist = p->distance;
        background = p->background;
    }

    float depth = ctx->P.z;
    float f = (dist > 0.0f) ? clamp01(1.0f - expf(-depth / dist)) : 0.0f;

    // Un-premultiply Ci
    RhColor Ci;
    Ci.r = (ctx->Oi.r > 1e-6f) ? ctx->Ci.r / ctx->Oi.r : 0.0f;
    Ci.g = (ctx->Oi.g > 1e-6f) ? ctx->Ci.g / ctx->Oi.g : 0.0f;
    Ci.b = (ctx->Oi.b > 1e-6f) ? ctx->Ci.b / ctx->Oi.b : 0.0f;

    // Blend toward background color
    float inv_f = 1.0f - f;
    Ci.r = Ci.r * inv_f + background.r * f;
    Ci.g = Ci.g * inv_f + background.g * f;
    Ci.b = Ci.b * inv_f + background.b * f;

    // Re-premultiply
    ctx->Ci.r = Ci.r * ctx->Oi.r;
    ctx->Ci.g = Ci.g * ctx->Oi.g;
    ctx->Ci.b = Ci.b * ctx->Oi.b;
}
