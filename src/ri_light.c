/**
 * ri_light.c - Lighting functions
 *
 * Handles RiLightSource and RiIlluminate.
 */

#include "ri_internal.h"
#include "rh_sl_vm.h"
#include "xpt.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    const char* name;
    RhLight* l;
    RhVec3 to_world;
    RtToken* tokens;
    RtPointer* values;
    int count;
    bool found;
} LightSearch;

static bool light_search_cb(bool is_builtin, const char* dir, void* user) {
    LightSearch* s = (LightSearch*)user;
    if (is_builtin) {
        if (strcmp(s->name, "ambientlight") == 0 ||
            strcmp(s->name, "pointlight") == 0 ||
            strcmp(s->name, "distantlight") == 0 ||
            strcmp(s->name, "spotlight") == 0) {
            xpt_debug("ri.light", "shader: loaded builtin light '%s'", s->name);
            s->found = true;
            return true;
        }
    } else {
        RhSLProgram* prog = sl_try_load_from_dir(s->name, dir);
        if (prog && prog->shader_type == RH_SL_SHADER_LIGHT) {
            RhSLShader* shader = rh_sl_shader_create(prog);
            if (shader) {
                /* Generic parameter loop (like ri_set_sl_surface) */
                for (int i = 0; i < s->count; i++) {
                    if (!s->tokens[i]) break;
                    float* fval = (float*)s->values[i];
                    if (rh_sl_shader_set_param(shader, s->tokens[i], fval, 16) == 0)
                        continue;
                    rh_sl_shader_set_string_param(shader, s->tokens[i],
                                                  (const char*)s->values[i]);
                }
                /* Override from/to with camera-space-transformed values */
                float from_arr[3] = {s->l->position.x, s->l->position.y, s->l->position.z};
                rh_sl_shader_set_param(shader, "from", from_arr, 3);
                float to_arr[3] = {s->to_world.x, s->to_world.y, s->to_world.z};
                rh_sl_shader_set_param(shader, "to", to_arr, 3);

                s->l->light_shader = rh_sl_vm_shader_exec;
                s->l->light_shader_params = shader;
                s->found = true;
                return true;
            }
        }
    }
    return false;
}

RtToken RiLightSourceV(RtToken name, RtToken* tokens, RtPointer* values, int count) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx || ctx->num_lights >= MAX_LIGHTS) return RI_NULL;

    RhLight* l = &ctx->lights[ctx->num_lights];
    ctx->num_lights++;

    strncpy(l->type, name, 31);

    // Defaults
    l->intensity = 1.0f;
    l->color = (RhColor){1.0f, 1.0f, 1.0f};
    l->position = rh_vec3_create(0.0f, 0.0f, 0.0f); // "From"
    RhVec3 to = rh_vec3_create(0.0f, 0.0f, 1.0f);   // "To"
    // Spotlight defaults (radians)
    l->coneangle = (float)(30.0 * M_PI / 180.0);
    l->conedeltaangle = (float)(5.0 * M_PI / 180.0);
    l->beamdistribution = 2.0f;
    // Shadow map defaults
    l->shadowmap = NULL;
    l->shadow_samples = 16;
    l->shadow_bias = 0.05f;  // Bias in world units (5cm default)
    l->shadow_blur = 1.0f;
    l->light_shader = NULL;
    l->light_shader_params = NULL;

    // Store shadow map filename temporarily for loading after parsing
    const char* shadowmap_file = NULL;

    // Parse arguments from token/value arrays
    for (int i = 0; i < count; i++) {
        if (!tokens[i]) continue;
        if (strcmp(tokens[i], "intensity") == 0) {
            RtFloat* val = (RtFloat*)values[i];
            l->intensity = *val;
        } else if (strcmp(tokens[i], "lightcolor") == 0) {
            RtColor* col = (RtColor*)values[i];
            l->color.r = (*col)[0];
            l->color.g = (*col)[1];
            l->color.b = (*col)[2];
        } else if (strcmp(tokens[i], "from") == 0) {
            RtPoint* p = (RtPoint*)values[i];
            l->position = rh_vec3_create((*p)[0], (*p)[1], (*p)[2]);
        } else if (strcmp(tokens[i], "to") == 0) {
            RtPoint* p = (RtPoint*)values[i];
            to = rh_vec3_create((*p)[0], (*p)[1], (*p)[2]);
        } else if (strcmp(tokens[i], "coneangle") == 0) {
            RtFloat* val = (RtFloat*)values[i];
            l->coneangle = *val;
        } else if (strcmp(tokens[i], "conedeltaangle") == 0) {
            RtFloat* val = (RtFloat*)values[i];
            l->conedeltaangle = *val;
        } else if (strcmp(tokens[i], "beamdistribution") == 0) {
            RtFloat* val = (RtFloat*)values[i];
            l->beamdistribution = *val;
        } else if (strcmp(tokens[i], "shadowmap") == 0) {
            shadowmap_file = (const char*)values[i];
        } else if (strcmp(tokens[i], "shadowsamples") == 0) {
            RtFloat* val = (RtFloat*)values[i];
            l->shadow_samples = (int)(*val);
            if (l->shadow_samples < 1) l->shadow_samples = 1;
            if (l->shadow_samples > 64) l->shadow_samples = 64;
        } else if (strcmp(tokens[i], "shadowbias") == 0) {
            RtFloat* val = (RtFloat*)values[i];
            l->shadow_bias = *val;
        } else if (strcmp(tokens[i], "shadowblur") == 0) {
            RtFloat* val = (RtFloat*)values[i];
            l->shadow_blur = *val;
            if (l->shadow_blur < 0.0f) l->shadow_blur = 0.0f;
        }
    }

    // Load shadow map if specified
    if (shadowmap_file) {
        l->shadowmap = ri_shadowmap_read(shadowmap_file);
        if (!l->shadowmap) {
            xpt_warn("ri.light", "Failed to load shadow map '%s' for light", shadowmap_file);
        }
    }

    // Calculate direction (toward light for distantlight, used differently for spotlight)
    l->direction = rh_vec3_normalize(rh_vec3_sub(l->position, to));

    // Transform to world space
    l->transform = ri_curr()->transform;
    l->position = rh_mat4_mul_point(ri_curr()->transform, l->position);
    RhVec3 to_world = rh_mat4_mul_point(ri_curr()->transform, to);
    l->direction = rh_vec3_normalize(rh_vec3_sub(l->position, to_world));

    // Search for light shader using the configured search path.
    {
        LightSearch s = {name, l, to_world, tokens, values, count, false};
        ri_iterate_searchpath(light_search_cb, &s);
    }

    return RI_NULL;
}

RtToken RiLightSource(RtToken name, ...) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx || ctx->num_lights >= MAX_LIGHTS) return RI_NULL;

    RtToken tokens[16];
    RtPointer values[16];
    int count = 0;

    va_list ap;
    va_start(ap, name);
    RtToken token;
    while ((token = va_arg(ap, RtToken)) != RI_NULL && count < 16) {
        tokens[count] = token;
        values[count] = va_arg(ap, RtPointer);
        count++;
    }
    va_end(ap);

    return RiLightSourceV(name, tokens, values, count);
}

void RiIlluminate(RtToken light, RtBoolean onoff) {
    (void)light; (void)onoff;
}
