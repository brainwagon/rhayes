/**
 * ri_light.c - Lighting functions
 *
 * Handles RiLightSource and RiIlluminate.
 */

#include "ri_internal.h"
#include "rh_sl_vm.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
    // Spotlight defaults
    l->coneangle = 30.0f;
    l->conedeltaangle = 5.0f;
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
        l->shadowmap = rh_shadowmap_read(shadowmap_file);
        if (!l->shadowmap) {
            fprintf(stderr, "Warning: Failed to load shadow map '%s' for light\n", shadowmap_file);
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
    // BUILTIN = use C evaluate_light() (light_shader stays NULL).
    // Directory elements = try loading .slo/.sl VM shader.
    {
        const char* spath = ctx->shader_searchpath;
        char dir_buf[256];
        bool found = false;

        while (*spath && !found) {
            const char* end = spath;
            while (*end && *end != ':') end++;
            size_t slen = (size_t)(end - spath);

            if (slen > 0) {
                if (slen == 7 && memcmp(spath, "BUILTIN", 7) == 0) {
                    /* BUILTIN: recognized light types use C fallback */
                    if (strcmp(name, "ambientlight") == 0 ||
                        strcmp(name, "pointlight") == 0 ||
                        strcmp(name, "distantlight") == 0 ||
                        strcmp(name, "spotlight") == 0) {
                        fprintf(stderr, "shader: loaded builtin light '%s'\n", name);
                        found = true;
                    }
                } else {
                    /* Directory element: try .slo/.sl */
                    if (slen == 1 && spath[0] == '.') {
                        dir_buf[0] = '\0';
                    } else {
                        if (slen >= 255) slen = 254;
                        memcpy(dir_buf, spath, slen);
                        if (dir_buf[slen - 1] != '/') {
                            dir_buf[slen] = '/';
                            dir_buf[slen + 1] = '\0';
                        } else {
                            dir_buf[slen] = '\0';
                        }
                    }
                    RhSLProgram* prog = sl_try_load_from_dir(name, dir_buf);
                    if (prog && prog->shader_type == RH_SL_SHADER_LIGHT) {
                        RhSLShader* shader = rh_sl_shader_create(prog);
                        if (shader) {
                            float fval;
                            fval = l->intensity;
                            rh_sl_shader_set_param(shader, "intensity", &fval, 1);
                            float lc[3] = {l->color.r, l->color.g, l->color.b};
                            rh_sl_shader_set_param(shader, "lightcolor", lc, 3);
                            float from_arr[3] = {l->position.x, l->position.y, l->position.z};
                            rh_sl_shader_set_param(shader, "from", from_arr, 3);
                            float to_arr[3] = {to_world.x, to_world.y, to_world.z};
                            rh_sl_shader_set_param(shader, "to", to_arr, 3);
                            fval = l->coneangle * (float)(M_PI / 180.0);
                            rh_sl_shader_set_param(shader, "coneangle", &fval, 1);
                            fval = l->conedeltaangle * (float)(M_PI / 180.0);
                            rh_sl_shader_set_param(shader, "conedeltaangle", &fval, 1);
                            fval = l->beamdistribution;
                            rh_sl_shader_set_param(shader, "beamdistribution", &fval, 1);

                            l->light_shader = rh_sl_vm_shader_exec;
                            l->light_shader_params = shader;
                            found = true;
                        }
                    }
                }
            }

            spath = (*end == ':') ? end + 1 : end;
        }
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
