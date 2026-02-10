/**
 * ri_state.c - Graphics state management
 *
 * Handles transform/attribute stacks, motion blur, and transformations.
 */

#include "ri_internal.h"

// --- State Stack ---

void RiTransformBegin(void) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx || ctx->stack_ptr >= MAX_STACK_DEPTH - 1) return;
    int p = ctx->stack_ptr;
    ctx->stack_ptr++;
    // Copy entire state (including shaders) for simplicity
    // In strict RenderMan, TransformBegin only saves CTM, but we need
    // shaders to be available for geometry created in this scope
    ctx->stack[ctx->stack_ptr] = ctx->stack[p];
}

void RiTransformEnd(void) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx || ctx->stack_ptr <= 0) return;
    // If strict compliance: only restore transformation.
    // RhMat4 t = ctx->stack[ctx->stack_ptr - 1].transform; // Previous transform

    // Restore transform (pop logic usually handles this just by decr pointer)
    // But we need to ensure we didn't accidentally pop attributes if this was TransformEnd.
    // Standard says TransformBegin/End saves "Current Transformation".
    // AttributeBegin/End saves everything.
    // To do this right with one stack, we just use AttributeBegin/End logic for now.
    ctx->stack_ptr--;
}

void RiAttributeBegin(void) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx || ctx->stack_ptr >= MAX_STACK_DEPTH - 1) return;
    int p = ctx->stack_ptr;
    ctx->stack_ptr++;
    ctx->stack[ctx->stack_ptr] = ctx->stack[p];
}

void RiAttributeEnd(void) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx || ctx->stack_ptr <= 0) return;
    ctx->stack_ptr--;
}

// --- Motion Blur ---

void RiMotionBeginV(RtInt n, RtFloat* times) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    if (n != 2) {
        fprintf(stderr, "Warning: RiMotionBegin only supports n=2 (two-sample motion)\n");
        return;
    }
    if (ctx->motion_active) {
        fprintf(stderr, "Warning: Nested MotionBegin blocks not supported\n");
        return;
    }

    ctx->motion_active = true;
    ctx->motion_sample_index = 0;

    // Store times directly - remapping to shutter interval happens in rasterization
    ctx->motion_times[0] = times[0];
    ctx->motion_times[1] = times[1];

    // Save the current transform as the starting point for both
    // The first transform call will update transform (t0)
    // The second will update transform_t1
    ri_curr()->transform_t1 = ri_curr()->transform;
}

void RiMotionBegin(RtInt n, ...) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    if (n != 2) {
        fprintf(stderr, "Warning: RiMotionBegin only supports n=2 (two-sample motion)\n");
        return;
    }

    RtFloat times[2];
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) {
        times[i] = (RtFloat)va_arg(ap, double);
    }
    va_end(ap);

    RiMotionBeginV(n, times);
}

void RiMotionEnd(void) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    if (!ctx->motion_active) {
        fprintf(stderr, "Warning: RiMotionEnd without RiMotionBegin\n");
        return;
    }

    ctx->motion_active = false;
    ctx->motion_sample_index = 0;

    // Mark that this attribute state has motion if transforms differ
    if (!rh_mat4_equal(ri_curr()->transform, ri_curr()->transform_t1)) {
        ri_curr()->has_motion = true;
    }
}

// --- Transformations ---

void RiIdentity(void) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    if (ctx->motion_active) {
        if (ctx->motion_sample_index == 0) {
            ri_curr()->transform = rh_mat4_identity();
        } else {
            ri_curr()->transform_t1 = rh_mat4_identity();
        }
        ctx->motion_sample_index++;
    } else {
        ri_curr()->transform = rh_mat4_identity();
        ri_curr()->transform_t1 = rh_mat4_identity();
    }
}

void RiTransform(RtMatrix transform) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    // Copy RtMatrix to RhMat4
    RhMat4 m;
    memcpy(m.m, transform, sizeof(RtMatrix));
    if (ctx->motion_active) {
        if (ctx->motion_sample_index == 0) {
            ri_curr()->transform = m;
        } else {
            ri_curr()->transform_t1 = m;
        }
        ctx->motion_sample_index++;
    } else {
        ri_curr()->transform = m;
        ri_curr()->transform_t1 = m;
    }
}

void RiConcatTransform(RtMatrix transform) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    RhMat4 m;
    memcpy(m.m, transform, sizeof(RtMatrix));
    // Standard RenderMan: CTM = CTM * transform
    if (ctx->motion_active) {
        if (ctx->motion_sample_index == 0) {
            ri_curr()->transform = rh_mat4_mul(ri_curr()->transform, m);
        } else {
            ri_curr()->transform_t1 = rh_mat4_mul(ri_curr()->transform_t1, m);
        }
        ctx->motion_sample_index++;
    } else {
        ri_curr()->transform = rh_mat4_mul(ri_curr()->transform, m);
        ri_curr()->transform_t1 = ri_curr()->transform;
    }
}

void RiTranslate(RtFloat dx, RtFloat dy, RtFloat dz) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    RhMat4 m = rh_mat4_translate(dx, dy, dz);
    if (ctx->motion_active) {
        if (ctx->motion_sample_index == 0) {
            ri_curr()->transform = rh_mat4_mul(ri_curr()->transform, m);
        } else {
            ri_curr()->transform_t1 = rh_mat4_mul(ri_curr()->transform_t1, m);
        }
        ctx->motion_sample_index++;
    } else {
        ri_curr()->transform = rh_mat4_mul(ri_curr()->transform, m);
        ri_curr()->transform_t1 = ri_curr()->transform;
    }
}

void RiRotate(RtFloat angle, RtFloat dx, RtFloat dy, RtFloat dz) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    float rad = angle * (RH_PI / 180.0f);
    float c = cosf(rad);
    float s = sinf(rad);
    float inv_c = 1.0f - c;

    // Normalize axis
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len > 0) { dx/=len; dy/=len; dz/=len; }

    RhMat4 m = rh_mat4_identity();
    m.m[0][0] = dx*dx*inv_c + c;    m.m[0][1] = dx*dy*inv_c - dz*s; m.m[0][2] = dx*dz*inv_c + dy*s;
    m.m[1][0] = dy*dx*inv_c + dz*s; m.m[1][1] = dy*dy*inv_c + c;    m.m[1][2] = dy*dz*inv_c - dx*s;
    m.m[2][0] = dz*dx*inv_c - dy*s; m.m[2][1] = dz*dy*inv_c + dx*s; m.m[2][2] = dz*dz*inv_c + c;

    if (ctx->motion_active) {
        if (ctx->motion_sample_index == 0) {
            ri_curr()->transform = rh_mat4_mul(ri_curr()->transform, m);
        } else {
            ri_curr()->transform_t1 = rh_mat4_mul(ri_curr()->transform_t1, m);
        }
        ctx->motion_sample_index++;
    } else {
        ri_curr()->transform = rh_mat4_mul(ri_curr()->transform, m);
        ri_curr()->transform_t1 = ri_curr()->transform;
    }
}

void RiScale(RtFloat sx, RtFloat sy, RtFloat sz) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    RhMat4 m = rh_mat4_scale(sx, sy, sz);
    if (ctx->motion_active) {
        if (ctx->motion_sample_index == 0) {
            ri_curr()->transform = rh_mat4_mul(ri_curr()->transform, m);
        } else {
            ri_curr()->transform_t1 = rh_mat4_mul(ri_curr()->transform_t1, m);
        }
        ctx->motion_sample_index++;
    } else {
        ri_curr()->transform = rh_mat4_mul(ri_curr()->transform, m);
        ri_curr()->transform_t1 = ri_curr()->transform;
    }
}

void RiBasis(RtMatrix ubasis, RtInt ustep, RtMatrix vbasis, RtInt vstep) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    // Copy RtMatrix to RhMat4
    RhMat4 um, vm;
    memcpy(um.m, ubasis, sizeof(RtMatrix));
    memcpy(vm.m, vbasis, sizeof(RtMatrix));

    ri_curr()->u_basis = um;
    ri_curr()->u_step = ustep;
    ri_curr()->v_basis = vm;
    ri_curr()->v_step = vstep;
}

// --- Attributes ---

void RiColor(RtColor color) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    ri_curr()->color.r = color[0];
    ri_curr()->color.g = color[1];
    ri_curr()->color.b = color[2];
}

void RiOpacity(RtColor color) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    ri_curr()->opacity.r = color[0];
    ri_curr()->opacity.g = color[1];
    ri_curr()->opacity.b = color[2];
}

void RiShadingRate(RtFloat size) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    ri_curr()->shading_rate = size;
}

void RiOrientation(RtToken orientation) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    if (orientation && strcmp(orientation, "lh") == 0) {
        ri_curr()->orientation_lh = true;
    } else {
        ri_curr()->orientation_lh = false;  // default "rh"
    }
}

void RiReverseOrientation(void) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    ri_curr()->reverse_orientation++;
}

void RiSides(RtInt nsides) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    ri_curr()->sides = (nsides == 2) ? 2 : 1;
}

void RiMatte(RtBoolean onoff) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    ri_curr()->is_matte = (onoff != 0);
}

// --- Implementation-Specific Attributes ---

// Helper: find or create attribute slot for given category/name
static RhImplAttribute* ri_find_or_create_attr(const char* category, const char* name) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return NULL;

    RiAttributeState* state = ri_curr();

    // Look for existing attribute with same category and name
    for (int i = 0; i < state->num_impl_attrs; i++) {
        if (strcmp(state->impl_attrs[i].category, category) == 0 &&
            strcmp(state->impl_attrs[i].name, name) == 0) {
            return &state->impl_attrs[i];
        }
    }

    // Create new entry if space available
    if (state->num_impl_attrs >= MAX_IMPL_ATTRIBUTES) {
        fprintf(stderr, "Warning: Maximum implementation attributes exceeded\n");
        return NULL;
    }

    RhImplAttribute* attr = &state->impl_attrs[state->num_impl_attrs];
    state->num_impl_attrs++;

    // Initialize
    strncpy(attr->category, category, MAX_IMPL_ATTR_NAME - 1);
    attr->category[MAX_IMPL_ATTR_NAME - 1] = '\0';
    strncpy(attr->name, name, MAX_IMPL_ATTR_NAME - 1);
    attr->name[MAX_IMPL_ATTR_NAME - 1] = '\0';
    attr->num_values = 0;
    attr->string_value[0] = '\0';
    attr->is_string = false;

    return attr;
}

void RiAttributeV(RtToken name, RtToken* tokens, RtPointer* values, int count) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx || !name) return;

    // Process each token/value pair
    for (int i = 0; i < count; i++) {
        if (!tokens[i] || !values[i]) continue;

        RhImplAttribute* attr = ri_find_or_create_attr(name, tokens[i]);
        if (!attr) continue;

        // Look up declaration to determine type
        const RiDeclaration* decl = ri_lookup_declaration(tokens[i]);

        if (decl) {
            if (decl->type == RI_TYPE_STRING) {
                // String value
                const char* str = *(const char**)values[i];
                if (str) {
                    strncpy(attr->string_value, str, sizeof(attr->string_value) - 1);
                    attr->string_value[sizeof(attr->string_value) - 1] = '\0';
                }
                attr->is_string = true;
                attr->num_values = 0;
            } else {
                // Numeric value - get float count from declaration
                int num_floats = ri_declaration_float_count(decl);
                if (num_floats > 16) num_floats = 16;

                const float* fvals = (const float*)values[i];
                for (int j = 0; j < num_floats; j++) {
                    attr->values[j] = fvals[j];
                }
                attr->num_values = num_floats;
                attr->is_string = false;
            }
        } else {
            // No declaration found - assume single float
            const float* fval = (const float*)values[i];
            attr->values[0] = *fval;
            attr->num_values = 1;
            attr->is_string = false;
        }
    }
}

void RiAttribute(RtToken name, ...) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx || !name) return;

    // Build token/value arrays from varargs
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

    RiAttributeV(name, tokens, values, count);
}
