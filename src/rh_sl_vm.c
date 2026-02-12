#include "ri_internal.h"
#include "rh_sl_vm.h"
#include "rh_shadow.h"
#include "rh_noise.h"
#include "rh_texture.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Light struct (must match RhLight in ri_internal.h exactly)         */
/* ------------------------------------------------------------------ */

typedef struct {
    char type[32];
    RhVec3 position;
    RhVec3 direction;
    RhColor color;
    float intensity;
    RhMat4 transform;
    float coneangle;
    float conedeltaangle;
    float beamdistribution;
    RhShadowMap* shadowmap;
    int shadow_samples;
    float shadow_bias;
    float shadow_blur;
    RhShaderFunc light_shader;
    void* light_shader_params;
} RhSLLight;

/* ------------------------------------------------------------------ */
/*  Program lifecycle                                                  */
/* ------------------------------------------------------------------ */

RhSLProgram* rh_sl_program_create(void) {
    RhSLProgram* p = calloc(1, sizeof(RhSLProgram));
    return p;
}

void rh_sl_program_free(RhSLProgram* prog) {
    if (!prog) return;
    free(prog->code);
    free(prog->const_pool);
    for (int i = 0; i < prog->string_count; i++)
        free(prog->string_table[i]);
    free(prog->string_table);
    free(prog);
}

/* ------------------------------------------------------------------ */
/*  Shader instance lifecycle                                          */
/* ------------------------------------------------------------------ */

RhSLShader* rh_sl_shader_create(RhSLProgram* program) {
    if (!program) return NULL;
    RhSLShader* s = calloc(1, sizeof(RhSLShader));
    s->program = program;
    s->num_params = program->num_params;
    /* Allocate param_values and fill with defaults */
    if (program->num_params > 0) {
        /* Count total floats needed for all params */
        int total = 0;
        for (int i = 0; i < program->num_params; i++)
            total += program->params[i].num_components;
        s->param_values = calloc((size_t)total, sizeof(float));
        /* Copy defaults from const pool */
        int offset = 0;
        for (int i = 0; i < program->num_params; i++) {
            int nc = program->params[i].num_components;
            int di = program->params[i].default_idx;
            for (int c = 0; c < nc; c++) {
                if (di + c < program->const_count)
                    s->param_values[offset + c] = program->const_pool[di + c];
            }
            offset += nc;
        }
    }
    return s;
}

void rh_sl_shader_free(RhSLShader* shader) {
    if (!shader) return;
    free(shader->param_values);
    /* Free cached textures */
    if (shader->textures) {
        for (int i = 0; i < shader->num_textures; i++) {
            if (shader->textures[i])
                rh_texture_destroy((RhTexture*)shader->textures[i]);
        }
        free(shader->textures);
    }
    /* Free string overrides */
    if (shader->string_overrides) {
        for (int i = 0; i < shader->num_string_overrides; i++)
            free(shader->string_overrides[i]);
        free(shader->string_overrides);
    }
    free(shader);
}

int rh_sl_shader_set_param(RhSLShader* shader, const char* name,
                           const float* value, int count) {
    if (!shader || !shader->program || !name || !value) return -1;
    RhSLProgram* prog = shader->program;
    /* Find the parameter by name */
    int offset = 0;
    for (int i = 0; i < prog->num_params; i++) {
        int nc = prog->params[i].num_components;
        if (strcmp(prog->params[i].name, name) == 0) {
            if (prog->params[i].type == RH_SL_TYPE_STRING)
                return -1; /* string params handled by set_string_param */
            int to_copy = (count < nc) ? count : nc;
            for (int c = 0; c < to_copy; c++)
                shader->param_values[offset + c] = value[c];
            return 0;
        }
        offset += nc;
    }
    return -1; /* param not found */
}

int rh_sl_shader_set_string_param(RhSLShader* shader, const char* name,
                                  const char* value) {
    if (!shader || !shader->program || !name) return -1;
    RhSLProgram* prog = shader->program;

    /* Find the param by name to get its default_idx (string table slot) */
    for (int i = 0; i < prog->num_params; i++) {
        if (prog->params[i].type != RH_SL_TYPE_STRING) continue;
        if (strcmp(prog->params[i].name, name) != 0) continue;

        int str_idx = prog->params[i].default_idx;

        /* Allocate string_overrides array if needed */
        if (!shader->string_overrides && prog->string_count > 0) {
            shader->num_string_overrides = prog->string_count;
            shader->string_overrides = calloc((size_t)prog->string_count,
                                              sizeof(char*));
        }
        if (str_idx < 0 || str_idx >= shader->num_string_overrides) return -1;

        /* Replace any existing override */
        free(shader->string_overrides[str_idx]);
        if (value && value[0]) {
            size_t len = strlen(value) + 1;
            shader->string_overrides[str_idx] = malloc(len);
            memcpy(shader->string_overrides[str_idx], value, len);
        } else {
            shader->string_overrides[str_idx] = NULL;
        }
        return 0;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/*  Helper: evaluate a single light for illuminance                    */
/* ------------------------------------------------------------------ */

/*
 * Evaluate light i and fill L_out, Cl_out.
 * Returns 1 if the light contributes (passes cone test), 0 otherwise.
 */
static int evaluate_light(const RhSLLight* light, const float* regs,
                          float* L_out, float* Cl_out,
                          RhShaderContext* ctx) {
    float lc_r = light->color.r * light->intensity;
    float lc_g = light->color.g * light->intensity;
    float lc_b = light->color.b * light->intensity;

    /* Ambient lights don't contribute via illuminance */
    if (light->type[0] == 'a')
        return 0;

    float Lx, Ly, Lz;
    float attenuation = 1.0f;

    float Px = regs[R_P + 0];
    float Py = regs[R_P + 1];
    float Pz = regs[R_P + 2];

    if (light->type[0] == 'd') {
        /* Distant light: L = direction (already normalized) */
        Lx = light->direction.x;
        Ly = light->direction.y;
        Lz = light->direction.z;
    } else {
        /* Point or spot light: L = normalize(light_pos - P) */
        float dx = light->position.x - Px;
        float dy = light->position.y - Py;
        float dz = light->position.z - Pz;
        float len = sqrtf(dx * dx + dy * dy + dz * dz);
        if (len < 1e-12f) return 0;
        float inv = 1.0f / len;
        Lx = dx * inv;
        Ly = dy * inv;
        Lz = dz * inv;

        /* Spotlight cone attenuation */
        if (light->type[0] == 's') {
            float cos_angle = light->direction.x * Lx
                            + light->direction.y * Ly
                            + light->direction.z * Lz;
            float clamped = cos_angle;
            if (clamped < -1.0f) clamped = -1.0f;
            if (clamped >  1.0f) clamped =  1.0f;
            float angle = acosf(clamped);

            float inner = light->coneangle;
            float outer = light->coneangle + light->conedeltaangle;

            if (angle > outer) {
                attenuation = 0.0f;
            } else if (angle > inner) {
                float t = (angle - inner) / (outer - inner);
                attenuation = 1.0f - t;
                attenuation *= attenuation;
            }

            if (attenuation > 0.0f && light->beamdistribution > 0.0f) {
                float cg = cos_angle > 0.0f ? cos_angle : 0.0f;
                attenuation *= powf(cg, light->beamdistribution);
            }
        }
    }

    if (attenuation <= 0.0f) return 0;

    /* Shadow map lookup */
    if (light->shadowmap) {
        if (rh_shadow_in_frustum(light->shadowmap, ctx->P_world)) {
            float sf = rh_shadow_pcf_lookup(
                light->shadowmap, ctx->P_world,
                light->shadow_samples, light->shadow_bias, light->shadow_blur);
            attenuation *= (1.0f - sf);
        }
    }

    if (attenuation <= 0.0f) return 0;

    /* Illuminance cone test: dot(L, N) > 0 (hemisphere) */
    float Nx = regs[R_N + 0];
    float Ny = regs[R_N + 1];
    float Nz = regs[R_N + 2];
    float n_dot_l = Nx * Lx + Ny * Ly + Nz * Lz;

    if (n_dot_l <= 0.0f) return 0;

    L_out[0] = Lx;
    L_out[1] = Ly;
    L_out[2] = Lz;
    Cl_out[0] = lc_r * attenuation;
    Cl_out[1] = lc_g * attenuation;
    Cl_out[2] = lc_b * attenuation;
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Built-in lighting helpers: ambient(), diffuse(), specular()        */
/* ------------------------------------------------------------------ */

static void builtin_ambient(const float* regs, RhSLExecState* state,
                            float* out) {
    (void)regs;
    out[0] = out[1] = out[2] = 0.0f;
    if (!state->light_list) {
        /* Default ambient fallback */
        out[0] = out[1] = out[2] = 0.2f;
        return;
    }

    RhSLLight* lights = (RhSLLight*)state->light_list;
    int found_ambient = 0;
    for (int i = 0; i < state->num_lights; i++) {
        if (lights[i].type[0] == 'a') {
            out[0] += lights[i].color.r * lights[i].intensity;
            out[1] += lights[i].color.g * lights[i].intensity;
            out[2] += lights[i].color.b * lights[i].intensity;
            found_ambient = 1;
        }
    }
    if (!found_ambient) {
        out[0] = out[1] = out[2] = 0.2f;
    }
}

/*
 * Evaluate a single light (VM shader or C fallback).
 * Returns 1 if the light contributes, 0 otherwise.
 * L_out is normalized direction, Cl_out is light color.
 */
static int eval_light_any(const RhSLLight* light, float* regs,
                          float* L_out, float* Cl_out,
                          RhShaderContext* ctx) {
    if (light->light_shader != NULL) {
        /* Execute VM light shader */
        RhShaderContext ctx_light;
        memset(&ctx_light, 0, sizeof(ctx_light));
        /* Use camera-space P since light params (from/to) are in camera space */
        ctx_light.Ps.x = regs[R_P+0];
        ctx_light.Ps.y = regs[R_P+1];
        ctx_light.Ps.z = regs[R_P+2];
        ctx_light.N.x = regs[R_N+0];
        ctx_light.N.y = regs[R_N+1];
        ctx_light.N.z = regs[R_N+2];
        if (ctx)
            ctx_light.P_world = ctx->P_world;
        ctx_light.light_list = NULL;
        ctx_light.num_lights = 0;

        light->light_shader(&ctx_light, light->light_shader_params);

        float Lx = ctx_light.L_out.x;
        float Ly = ctx_light.L_out.y;
        float Lz = ctx_light.L_out.z;

        /* Skip if L == 0 (ambient or outside cone) */
        float Llen2 = Lx*Lx + Ly*Ly + Lz*Lz;
        if (Llen2 < 1e-24f) return 0;

        /* Negate and normalize L: illuminate gives light-to-surface,
         * illuminance expects surface-to-light */
        float inv_len = 1.0f / sqrtf(Llen2);
        L_out[0] = -Lx * inv_len;
        L_out[1] = -Ly * inv_len;
        L_out[2] = -Lz * inv_len;
        Cl_out[0] = ctx_light.Cl_out.r;
        Cl_out[1] = ctx_light.Cl_out.g;
        Cl_out[2] = ctx_light.Cl_out.b;
        return 1;
    }

    return evaluate_light(light, regs, L_out, Cl_out, ctx);
}

static void builtin_diffuse(float* regs, RhSLExecState* state,
                            uint16_t nf_reg, float* out) {
    out[0] = out[1] = out[2] = 0.0f;
    if (!state->light_list) return;

    float Nf_x = regs[nf_reg + 0];
    float Nf_y = regs[nf_reg + 1];
    float Nf_z = regs[nf_reg + 2];

    RhSLLight* lights = (RhSLLight*)state->light_list;
    for (int i = 0; i < state->num_lights; i++) {
        float L[3], Cl[3];
        if (!eval_light_any(&lights[i], regs, L, Cl, state->shader_ctx))
            continue;
        float n_dot_l = Nf_x * L[0] + Nf_y * L[1] + Nf_z * L[2];
        if (n_dot_l > 0.0f) {
            out[0] += Cl[0] * n_dot_l;
            out[1] += Cl[1] * n_dot_l;
            out[2] += Cl[2] * n_dot_l;
        }
    }
}

static void builtin_specular(float* regs, RhSLExecState* state,
                             uint16_t nf_reg, uint16_t rough_reg,
                             float* out) {
    out[0] = out[1] = out[2] = 0.0f;
    if (!state->light_list) return;

    float Nf_x = regs[nf_reg + 0];
    float Nf_y = regs[nf_reg + 1];
    float Nf_z = regs[nf_reg + 2];

    float roughness = regs[rough_reg];
    if (roughness <= 0.0f) return;
    float spec_power = 1.0f / roughness;

    /* View vector V = normalize(-I) */
    float Vx = -regs[R_I + 0];
    float Vy = -regs[R_I + 1];
    float Vz = -regs[R_I + 2];
    float vlen = sqrtf(Vx * Vx + Vy * Vy + Vz * Vz);
    if (vlen > 1e-12f) {
        float vi = 1.0f / vlen;
        Vx *= vi; Vy *= vi; Vz *= vi;
    }

    RhSLLight* lights = (RhSLLight*)state->light_list;
    for (int i = 0; i < state->num_lights; i++) {
        float L[3], Cl[3];
        if (!eval_light_any(&lights[i], regs, L, Cl, state->shader_ctx))
            continue;
        float n_dot_l = Nf_x * L[0] + Nf_y * L[1] + Nf_z * L[2];
        if (n_dot_l > 0.0f) {
            /* Phong reflection: R = 2*N*(N.L) - L */
            float Rx = 2.0f * Nf_x * n_dot_l - L[0];
            float Ry = 2.0f * Nf_y * n_dot_l - L[1];
            float Rz = 2.0f * Nf_z * n_dot_l - L[2];
            float r_dot_v = Rx * Vx + Ry * Vy + Rz * Vz;
            if (r_dot_v > 0.0f) {
                float s = powf(r_dot_v, spec_power);
                out[0] += Cl[0] * s;
                out[1] += Cl[1] * s;
                out[2] += Cl[2] * s;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Helper: advance illuminance loop to next qualifying light          */
/* ------------------------------------------------------------------ */

/*
 * Scan from state->current_light forward to find the next light that
 * passes the illuminance cone test.  If found, load L and Cl into
 * registers, advance current_light past it, and return 1.
 * If no more qualifying lights, return 0.
 */
static int rh_sl_vm_advance_light(RhSLExecState* state, float* r) {
    if (!state->light_list) return 0;
    RhSLLight* lights = (RhSLLight*)state->light_list;
    while (state->current_light < state->num_lights) {
        RhSLLight* light = &lights[state->current_light];

        float L[3], Cl[3];
        if (eval_light_any(light, r, L, Cl, state->shader_ctx)) {
            /* Hemisphere test: dot(L_normalized, N) > 0 */
            float n_dot_l = r[R_N+0]*L[0] + r[R_N+1]*L[1] + r[R_N+2]*L[2];
            if (n_dot_l <= 0.0f) {
                state->current_light++;
                continue;
            }
            r[R_L+0] = L[0]; r[R_L+1] = L[1]; r[R_L+2] = L[2];
            r[R_CL+0] = Cl[0]; r[R_CL+1] = Cl[1]; r[R_CL+2] = Cl[2];
            state->current_light++;
            return 1;
        }
        state->current_light++;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Texture/string helpers                                             */
/* ------------------------------------------------------------------ */

static const char* get_string(RhSLShader* shader, const RhSLProgram* prog,
                              int str_idx) {
    if (shader && shader->string_overrides &&
        str_idx >= 0 && str_idx < shader->num_string_overrides &&
        shader->string_overrides[str_idx]) {
        return shader->string_overrides[str_idx];
    }
    if (str_idx >= 0 && str_idx < prog->string_count)
        return prog->string_table[str_idx];
    return "";
}

/* Sentinel for "tried to load, got NULL" to avoid repeated load attempts */
#define LOAD_FAILED_SENTINEL ((void*)(uintptr_t)1)

static RhTexture* get_texture(RhSLExecState* state,
                              RhSLShader* shader,
                              const RhSLProgram* prog,
                              int str_idx, const char* name) {
    if (!shader || !name || !name[0]) return NULL;

    /* Lazy-allocate texture cache */
    if (!shader->textures && prog->string_count > 0) {
        shader->num_textures = prog->string_count;
        shader->textures = calloc((size_t)prog->string_count, sizeof(void*));
    }
    if (str_idx < 0 || str_idx >= shader->num_textures) return NULL;

    /* Return cached result (either loaded texture or failed sentinel) */
    if (shader->textures[str_idx]) {
        if (shader->textures[str_idx] == LOAD_FAILED_SENTINEL) return NULL;
        return (RhTexture*)shader->textures[str_idx];
    }

    /* Load on first use (using search-path-aware callback if available) */
    RhTexture* tex = NULL;
    if (state->texture_load_cb) {
        tex = state->texture_load_cb(name, RH_TEX_RGB);
    } else {
        tex = rh_texture_load(name, RH_TEX_RGB);
    }
    shader->textures[str_idx] = tex ? tex : LOAD_FAILED_SENTINEL;
    return tex;
}

static RhShadowMap* get_shadowmap(RhSLExecState* state,
                                  RhSLShader* shader,
                                  const RhSLProgram* prog,
                                  int str_idx, const char* name) {
    if (!shader || !name || !name[0]) return NULL;

    /* Lazy-allocate texture cache (shares the same array) */
    if (!shader->textures && prog->string_count > 0) {
        shader->num_textures = prog->string_count;
        shader->textures = calloc((size_t)prog->string_count, sizeof(void*));
    }
    if (str_idx < 0 || str_idx >= shader->num_textures) return NULL;

    if (shader->textures[str_idx]) {
        if (shader->textures[str_idx] == LOAD_FAILED_SENTINEL) return NULL;
        return (RhShadowMap*)shader->textures[str_idx];
    }

    RhShadowMap* sm = NULL;
    if (state->shadow_read_cb) {
        sm = state->shadow_read_cb(name);
    } else {
        sm = rh_shadowmap_read(name);
    }
    shader->textures[str_idx] = sm ? sm : LOAD_FAILED_SENTINEL;
    return sm;
}

/* ------------------------------------------------------------------ */
/*  VM execution loop                                                  */
/* ------------------------------------------------------------------ */

void rh_sl_vm_execute(const RhSLProgram* program, RhSLExecState* state) {
    float* r = state->regs;
    const float* cp = program->const_pool;

    while (state->pc < program->code_len) {
        uint64_t instr = program->code[state->pc++];
        uint8_t  op    = RH_SL_DECODE_OP(instr);
        uint8_t  flags = RH_SL_DECODE_FLAGS(instr);
        uint16_t dst   = RH_SL_DECODE_DST(instr);
        uint16_t src1  = RH_SL_DECODE_SRC1(instr);
        uint16_t src2  = RH_SL_DECODE_SRC2(instr);

        switch (op) {

        /* ---- Float arithmetic ---- */
        case OP_FADD:   r[dst] = r[src1] + r[src2]; break;
        case OP_FSUB:   r[dst] = r[src1] - r[src2]; break;
        case OP_FMUL:   r[dst] = r[src1] * r[src2]; break;
        case OP_FDIV:   r[dst] = (r[src2] != 0.0f) ? r[src1] / r[src2] : 0.0f; break;
        case OP_FNEG:   r[dst] = -r[src1]; break;
        case OP_FABS:   r[dst] = fabsf(r[src1]); break;
        case OP_FMOD:   r[dst] = (r[src2] != 0.0f) ? fmodf(r[src1], r[src2]) : 0.0f; break;
        case OP_FPOW:   r[dst] = powf(r[src1], r[src2]); break;
        case OP_FSQRT:  r[dst] = sqrtf(r[src1]); break;
        case OP_FMIN:   r[dst] = (r[src1] < r[src2]) ? r[src1] : r[src2]; break;
        case OP_FMAX:   r[dst] = (r[src1] > r[src2]) ? r[src1] : r[src2]; break;
        case OP_FFLOOR: r[dst] = floorf(r[src1]); break;
        case OP_FCEIL:  r[dst] = ceilf(r[src1]); break;
        case OP_FSIGN:  r[dst] = (r[src1] > 0.0f) ? 1.0f : (r[src1] < 0.0f) ? -1.0f : 0.0f; break;
        case OP_FROUND: r[dst] = roundf(r[src1]); break;

        /* ---- Trigonometric / transcendental ---- */
        case OP_FSIN:   r[dst] = sinf(r[src1]); break;
        case OP_FCOS:   r[dst] = cosf(r[src1]); break;
        case OP_FTAN:   r[dst] = tanf(r[src1]); break;
        case OP_FASIN:  r[dst] = asinf(r[src1]); break;
        case OP_FACOS:  r[dst] = acosf(r[src1]); break;
        case OP_FATAN:  r[dst] = atanf(r[src1]); break;
        case OP_FATAN2: r[dst] = atan2f(r[src1], r[src2]); break;
        case OP_FEXP:   r[dst] = expf(r[src1]); break;
        case OP_FLOG:   r[dst] = logf(r[src1]); break;

        /* ---- Tuple operations ---- */
        case OP_VADD:
            r[dst+0] = r[src1+0] + r[src2+0];
            r[dst+1] = r[src1+1] + r[src2+1];
            r[dst+2] = r[src1+2] + r[src2+2];
            break;
        case OP_VSUB:
            r[dst+0] = r[src1+0] - r[src2+0];
            r[dst+1] = r[src1+1] - r[src2+1];
            r[dst+2] = r[src1+2] - r[src2+2];
            break;
        case OP_VMUL:
            r[dst+0] = r[src1+0] * r[src2+0];
            r[dst+1] = r[src1+1] * r[src2+1];
            r[dst+2] = r[src1+2] * r[src2+2];
            break;
        case OP_VDIV: {
            r[dst+0] = (r[src2+0] != 0.0f) ? r[src1+0] / r[src2+0] : 0.0f;
            r[dst+1] = (r[src2+1] != 0.0f) ? r[src1+1] / r[src2+1] : 0.0f;
            r[dst+2] = (r[src2+2] != 0.0f) ? r[src1+2] / r[src2+2] : 0.0f;
            break;
        }
        case OP_VNEG:
            r[dst+0] = -r[src1+0];
            r[dst+1] = -r[src1+1];
            r[dst+2] = -r[src1+2];
            break;
        case OP_VSMUL:
            r[dst+0] = r[src1+0] * r[src2];
            r[dst+1] = r[src1+1] * r[src2];
            r[dst+2] = r[src1+2] * r[src2];
            break;
        case OP_VSDIV: {
            float d = (r[src2] != 0.0f) ? 1.0f / r[src2] : 0.0f;
            r[dst+0] = r[src1+0] * d;
            r[dst+1] = r[src1+1] * d;
            r[dst+2] = r[src1+2] * d;
            break;
        }
        case OP_DOT:
            r[dst] = r[src1+0]*r[src2+0] + r[src1+1]*r[src2+1] + r[src1+2]*r[src2+2];
            break;
        case OP_CROSS:
            r[dst+0] = r[src1+1]*r[src2+2] - r[src1+2]*r[src2+1];
            r[dst+1] = r[src1+2]*r[src2+0] - r[src1+0]*r[src2+2];
            r[dst+2] = r[src1+0]*r[src2+1] - r[src1+1]*r[src2+0];
            break;
        case OP_LENGTH: {
            float lsq = r[src1+0]*r[src1+0] + r[src1+1]*r[src1+1] + r[src1+2]*r[src1+2];
            r[dst] = sqrtf(lsq);
            break;
        }
        case OP_NORMALIZE: {
            float lsq = r[src1+0]*r[src1+0] + r[src1+1]*r[src1+1] + r[src1+2]*r[src1+2];
            float inv = (lsq > 1e-24f) ? 1.0f / sqrtf(lsq) : 0.0f;
            r[dst+0] = r[src1+0] * inv;
            r[dst+1] = r[src1+1] * inv;
            r[dst+2] = r[src1+2] * inv;
            break;
        }
        case OP_DISTANCE: {
            float dx = r[src1+0] - r[src2+0];
            float dy = r[src1+1] - r[src2+1];
            float dz = r[src1+2] - r[src2+2];
            r[dst] = sqrtf(dx*dx + dy*dy + dz*dz);
            break;
        }
        case OP_FACEFORWARD: {
            /* faceforward(N, I): return (dot(N, I) < 0) ? N : -N */
            float d = r[src1+0]*r[src2+0] + r[src1+1]*r[src2+1] + r[src1+2]*r[src2+2];
            float sign = (d < 0.0f) ? 1.0f : -1.0f;
            r[dst+0] = r[src1+0] * sign;
            r[dst+1] = r[src1+1] * sign;
            r[dst+2] = r[src1+2] * sign;
            break;
        }
        case OP_REFLECT: {
            /* reflect(I, N): I - 2*dot(I,N)*N */
            float d = r[src1+0]*r[src2+0] + r[src1+1]*r[src2+1] + r[src1+2]*r[src2+2];
            float t = 2.0f * d;
            r[dst+0] = r[src1+0] - t * r[src2+0];
            r[dst+1] = r[src1+1] - t * r[src2+1];
            r[dst+2] = r[src1+2] - t * r[src2+2];
            break;
        }
        case OP_VCOMP: {
            int comp = flags & 0x3;
            r[dst] = r[src1 + comp];
            break;
        }
        case OP_VSETCOMP: {
            int comp = flags & 0x3;
            r[dst + comp] = r[src1];
            break;
        }

        /* ---- Comparison and logic ---- */
        case OP_FEQ: r[dst] = (r[src1] == r[src2]) ? 1.0f : 0.0f; break;
        case OP_FNE: r[dst] = (r[src1] != r[src2]) ? 1.0f : 0.0f; break;
        case OP_FLT: r[dst] = (r[src1] <  r[src2]) ? 1.0f : 0.0f; break;
        case OP_FLE: r[dst] = (r[src1] <= r[src2]) ? 1.0f : 0.0f; break;
        case OP_FGT: r[dst] = (r[src1] >  r[src2]) ? 1.0f : 0.0f; break;
        case OP_FGE: r[dst] = (r[src1] >= r[src2]) ? 1.0f : 0.0f; break;
        case OP_AND: r[dst] = (r[src1] != 0.0f && r[src2] != 0.0f) ? 1.0f : 0.0f; break;
        case OP_OR:  r[dst] = (r[src1] != 0.0f || r[src2] != 0.0f) ? 1.0f : 0.0f; break;
        case OP_NOT: r[dst] = (r[src1] == 0.0f) ? 1.0f : 0.0f; break;

        /* ---- Data movement ---- */
        case OP_FMOV:  r[dst] = r[src1]; break;
        case OP_VMOV:
            r[dst+0] = r[src1+0];
            r[dst+1] = r[src1+1];
            r[dst+2] = r[src1+2];
            break;
        case OP_FCONST: r[dst] = cp[src1]; break;
        case OP_VCONST:
            r[dst+0] = cp[src1+0];
            r[dst+1] = cp[src1+1];
            r[dst+2] = cp[src1+2];
            break;
        case OP_FTOV:
            r[dst+0] = r[src1];
            r[dst+1] = r[src1];
            r[dst+2] = r[src1];
            break;

        /* ---- Control flow ---- */
        case OP_JUMP:
            state->pc = dst;
            break;
        case OP_JUMP_IF:
            if (r[src1] != 0.0f) state->pc = dst;
            break;
        case OP_JUMP_IFNOT:
            if (r[src1] == 0.0f) state->pc = dst;
            break;

        /* ---- Function calls ---- */
        case OP_CALL:
            if (state->call_sp < RH_SL_MAX_CALL_DEPTH) {
                state->call_stack[state->call_sp++] = state->pc;
                state->pc = dst;
            }
            break;
        case OP_RET:
            if (state->call_sp > 0) {
                state->pc = state->call_stack[--state->call_sp];
            }
            break;

        /* ---- Lighting ---- */
        case OP_ILLUMINANCE_BEGIN: {
            /*
             * dst = instruction to jump to when loop is done (past END)
             * src1 = register of P (surface point)
             * src2 = register of N (surface normal)
             *
             * We initialize the light iterator and fall through to
             * evaluate the first qualifying light.
             */
            state->current_light = 0;
            if (!state->light_list || state->num_lights == 0) {
                state->pc = dst; /* skip loop body entirely */
                break;
            }
            /* Find first contributing light */
            if (!rh_sl_vm_advance_light(state, r)) {
                state->pc = dst; /* no qualifying lights */
            }
            break;
        }

        case OP_ILLUMINANCE_END: {
            /*
             * dst = instruction after ILLUMINANCE_BEGIN (loop body start)
             * Advance to next qualifying light. If found, set L/Cl and
             * jump back to the body. Otherwise fall through.
             */
            if (rh_sl_vm_advance_light(state, r)) {
                state->pc = dst;
            }
            /* else: no more lights -- fall through past END */
            break;
        }

        case OP_AMBIENT:
            builtin_ambient(r, state, &r[dst]);
            break;

        case OP_DIFFUSE:
            builtin_diffuse(r, state, src1, &r[dst]);
            break;

        case OP_SPECULAR:
            builtin_specular(r, state, src1, src2, &r[dst]);
            break;

        /* ---- Texture / shadow ---- */
        case OP_TEXTURE: {
            RhSLShader* sh = (RhSLShader*)state->shader;
            const char* tname = get_string(sh, program, src2);
            RhTexture* tex = get_texture(state, sh, program, src2, tname);
            if (tex) {
                RhColor c = rh_texture_sample(tex, r[src1], r[src1+1],
                                              r[R_DU], r[R_DV]);
                r[dst] = c.r; r[dst+1] = c.g; r[dst+2] = c.b;
            } else {
                r[dst] = 1.0f; r[dst+1] = 0.0f; r[dst+2] = 1.0f;
            }
            break;
        }
        case OP_SHADOW: {
            RhSLShader* sh = (RhSLShader*)state->shader;
            const char* sname = get_string(sh, program, src2);
            RhShadowMap* sm = get_shadowmap(state, sh, program, src2, sname);
            if (sm) {
                RhVec3 pw = {r[src1], r[src1+1], r[src1+2]};
                r[dst] = rh_shadow_pcf_lookup(sm, pw, 16, 0.05f, 1.0f);
            } else {
                r[dst] = 0.0f;
            }
            break;
        }

        /* ---- Light shader blocks (illuminate / solar) ---- */
        case OP_ILLUMINATE_BEGIN: {
            /*
             * dst = instruction after block end (skip target)
             * src1 = register of light position (from)
             * Compute L = Ps - from, store in R_L. Fall through to body.
             */
            float Lx = r[R_PS+0] - r[src1+0];
            float Ly = r[R_PS+1] - r[src1+1];
            float Lz = r[R_PS+2] - r[src1+2];
            r[R_L+0] = Lx;
            r[R_L+1] = Ly;
            r[R_L+2] = Lz;
            /* Fall through to body */
            break;
        }

        case OP_ILLUMINATE_END:
            /* Single-execution block (not a loop). Just fall through. */
            break;

        case OP_SOLAR_BEGIN: {
            /*
             * dst = instruction after block end (skip target)
             * src1 = register of axis direction (or 0 if no axis)
             * Set L = -axis, fall through to body.
             */
            if (src1 != 0) {
                r[R_L+0] = -r[src1+0];
                r[R_L+1] = -r[src1+1];
                r[R_L+2] = -r[src1+2];
            }
            /* Fall through to body */
            break;
        }

        case OP_SOLAR_END:
            /* Single-execution block. Just fall through. */
            break;

        case OP_ENVMAP:
        case OP_NOISE1:    r[dst] = rh_noise1(r[src1]); break;
        case OP_NOISE2:    r[dst] = rh_noise2(r[src1], r[src2]); break;
        case OP_NOISE3:    r[dst] = rh_noise3(r[src1], r[src1+1], r[src1+2]); break;
        case OP_PNOISE:    r[dst] = rh_pnoise1(r[src1], r[src2]); break;
        case OP_CELLNOISE: r[dst] = rh_cellnoise1(r[src1]); break;
        case OP_TRANSFORM:
        case OP_NTRANSFORM:
        case OP_VTRANSFORM:
        case OP_DU:
        case OP_DV:
        case OP_AREA:
        case OP_CALCNORMAL:
            /* Stub: leave dst unchanged */
            break;

        case OP_PRINTF:
            if (src1 < (uint16_t)program->string_count) {
                fprintf(stderr, "[SL] %s\n", program->string_table[src1]);
            }
            break;

        case OP_HALT:
            return;

        default:
            /* Unknown opcode -- halt */
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Shader executor (RhShaderFunc signature)                           */
/* ------------------------------------------------------------------ */

void rh_sl_vm_shader_exec(RhShaderContext* ctx, void* params) {
    RhSLShader* shader = (RhSLShader*)params;
    if (!shader || !shader->program) return;

    RhSLProgram* prog = shader->program;

    /* Allocate register file on stack for small shaders, heap for large */
    float stack_regs[256];
    float* regs;
    if (prog->num_regs <= 256) {
        regs = stack_regs;
    } else {
        regs = malloc(sizeof(float) * (size_t)prog->num_regs);
        if (!regs) return;
    }
    memset(regs, 0, sizeof(float) * (size_t)prog->num_regs);

    /* Load built-in globals from RhShaderContext */
    regs[R_P+0] = ctx->P.x;   regs[R_P+1] = ctx->P.y;   regs[R_P+2] = ctx->P.z;
    regs[R_N+0] = ctx->N.x;   regs[R_N+1] = ctx->N.y;   regs[R_N+2] = ctx->N.z;
    regs[R_NG+0] = ctx->N.x;  regs[R_NG+1] = ctx->N.y;  regs[R_NG+2] = ctx->N.z;
    regs[R_I+0] = ctx->I.x;   regs[R_I+1] = ctx->I.y;   regs[R_I+2] = ctx->I.z;
    regs[R_E+0] = 0.0f;       regs[R_E+1] = 0.0f;       regs[R_E+2] = 0.0f;
    regs[R_CS+0] = ctx->Cs.r; regs[R_CS+1] = ctx->Cs.g; regs[R_CS+2] = ctx->Cs.b;
    regs[R_OS+0] = ctx->Os.r; regs[R_OS+1] = ctx->Os.g; regs[R_OS+2] = ctx->Os.b;
    regs[R_CI+0] = 0.0f;      regs[R_CI+1] = 0.0f;      regs[R_CI+2] = 0.0f;
    regs[R_OI+0] = 1.0f;      regs[R_OI+1] = 1.0f;      regs[R_OI+2] = 1.0f;
    regs[R_S] = ctx->u;       /* s defaults to u if not overridden */
    regs[R_T] = ctx->v;       /* t defaults to v */
    regs[R_U] = ctx->u;
    regs[R_V] = ctx->v;
    regs[R_DU] = ctx->du;
    regs[R_DV] = ctx->dv;
    regs[R_PW+0] = ctx->P_world.x; regs[R_PW+1] = ctx->P_world.y; regs[R_PW+2] = ctx->P_world.z;
    regs[R_PS+0] = ctx->Ps.x; regs[R_PS+1] = ctx->Ps.y; regs[R_PS+2] = ctx->Ps.z;

    /* Load shader instance parameters */
    if (shader->param_values) {
        int offset = 0;
        for (int i = 0; i < prog->num_params; i++) {
            int reg = prog->params[i].reg;
            int nc = prog->params[i].num_components;
            for (int c = 0; c < nc; c++)
                regs[reg + c] = shader->param_values[offset + c];
            offset += nc;
        }
    } else {
        /* Load defaults from const pool */
        for (int i = 0; i < prog->num_params; i++) {
            int reg = prog->params[i].reg;
            int nc = prog->params[i].num_components;
            int di = prog->params[i].default_idx;
            for (int c = 0; c < nc; c++) {
                if (di + c < prog->const_count)
                    regs[reg + c] = prog->const_pool[di + c];
            }
        }
    }

    /* Initialize execution state */
    RhSLExecState state;
    state.regs = regs;
    state.pc = 0;
    state.call_sp = 0;
    state.current_light = 0;
    state.num_lights = ctx->num_lights;
    state.light_list = ctx->light_list;
    state.shader_ctx = ctx;
    state.shader = shader;

    /* Execute */
    rh_sl_vm_execute(prog, &state);

    /* Read back outputs */
    ctx->Ci.r = regs[R_CI+0]; ctx->Ci.g = regs[R_CI+1]; ctx->Ci.b = regs[R_CI+2];
    ctx->Oi.r = regs[R_OI+0]; ctx->Oi.g = regs[R_OI+1]; ctx->Oi.b = regs[R_OI+2];

    /* For displacement shaders, also write back P and N */
    if (prog->shader_type == RH_SL_SHADER_DISPLACEMENT) {
        ctx->P.x = regs[R_P+0]; ctx->P.y = regs[R_P+1]; ctx->P.z = regs[R_P+2];
        ctx->N.x = regs[R_N+0]; ctx->N.y = regs[R_N+1]; ctx->N.z = regs[R_N+2];
    }

    /* For light shaders, write back L and Cl */
    if (prog->shader_type == RH_SL_SHADER_LIGHT) {
        ctx->L_out.x = regs[R_L+0]; ctx->L_out.y = regs[R_L+1]; ctx->L_out.z = regs[R_L+2];
        ctx->Cl_out.r = regs[R_CL+0]; ctx->Cl_out.g = regs[R_CL+1]; ctx->Cl_out.b = regs[R_CL+2];
    }

    if (regs != stack_regs)
        free(regs);
}
