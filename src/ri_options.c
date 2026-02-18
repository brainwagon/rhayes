/**
 * ri_options.c - RenderMan options and display settings
 *
 * Handles RiOption, RiHider, RiFormat, RiDisplay, RiPixelSamples,
 * RiPixelFilter, RiDepthOfField, RiShutter, RiProjection, and filter functions.
 */

#include "ri_internal.h"

// --- Standard Pixel Filter Functions ---

RtFloat RiBoxFilter(RtFloat x, RtFloat y, RtFloat xwidth, RtFloat ywidth) {
    (void)x; (void)y; (void)xwidth; (void)ywidth;
    return 1.0f;
}

RtFloat RiTriangleFilter(RtFloat x, RtFloat y, RtFloat xwidth, RtFloat ywidth) {
    float fx = 1.0f - fabsf(x) / (xwidth / 2.0f);
    float fy = 1.0f - fabsf(y) / (ywidth / 2.0f);
    return rh_max(0.0f, fx) * rh_max(0.0f, fy);
}

RtFloat RiGaussianFilter(RtFloat x, RtFloat y, RtFloat xwidth, RtFloat ywidth) {
    float sigma_x = xwidth / 4.0f;
    float sigma_y = ywidth / 4.0f;
    float gx = expf(-(x * x) / (2.0f * sigma_x * sigma_x));
    float gy = expf(-(y * y) / (2.0f * sigma_y * sigma_y));
    return gx * gy;
}

// --- Options ---

void RiOptionV(RtToken name, RtToken* tokens, RtPointer* values, int count) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    if (strcmp(name, "progress") == 0) {
        for (int i = 0; i < count; i++) {
            if (!tokens[i]) continue;
            if (strcmp(tokens[i], "show") == 0) {
                RtInt* val = (RtInt*)values[i];
                ctx->show_progress = (*val != 0);
            }
        }
    } else if (strcmp(name, "statistics") == 0) {
        for (int i = 0; i < count; i++) {
            if (!tokens[i]) continue;
            if (strcmp(tokens[i], "endofframe") == 0) {
                RtInt* val = (RtInt*)values[i];
                ctx->stats_options.endofframe = *val;
            } else if (strcmp(tokens[i], "filename") == 0) {
                RtToken filename = (RtToken)values[i];
                if (filename) {
                    strncpy(ctx->stats_options.filename, filename, 255);
                    ctx->stats_options.filename[255] = '\0';
                } else {
                    ctx->stats_options.filename[0] = '\0';
                }
            } else if (strcmp(tokens[i], "jsonfilename") == 0) {
                RtToken filename = (RtToken)values[i];
                if (filename) {
                    strncpy(ctx->stats_options.jsonfilename, filename, 255);
                    ctx->stats_options.jsonfilename[255] = '\0';
                } else {
                    ctx->stats_options.jsonfilename[0] = '\0';
                }
            }
        }
    } else if (strcmp(name, "searchpath") == 0) {
        for (int i = 0; i < count; i++) {
            if (!tokens[i]) continue;
            if (strcmp(tokens[i], "shader") == 0) {
                const char* path = (const char*)values[i];
                if (path) {
                    size_t len = strlen(path);
                    if (len >= sizeof(ctx->shader_searchpath))
                        len = sizeof(ctx->shader_searchpath) - 1;
                    memcpy(ctx->shader_searchpath, path, len);
                    ctx->shader_searchpath[len] = '\0';
                }
            } else if (strcmp(tokens[i], "texture") == 0) {
                const char* path = (const char*)values[i];
                if (path) {
                    size_t len = strlen(path);
                    if (len >= sizeof(ctx->texture_searchpath))
                        len = sizeof(ctx->texture_searchpath) - 1;
                    memcpy(ctx->texture_searchpath, path, len);
                    ctx->texture_searchpath[len] = '\0';
                }
            }
        }
    } else if (strcmp(name, "limits") == 0) {
        // Memory limit options for large scenes
        for (int i = 0; i < count; i++) {
            if (!tokens[i]) continue;
            if (strcmp(tokens[i], "memory") == 0) {
                // Memory limit in MB (RIB parser passes as float*)
                float* val = (float*)values[i];
                ctx->memory.max_memory_bytes = (size_t)(*val) * 1024 * 1024;
            } else if (strcmp(tokens[i], "bucketsize") == 0) {
                // Bucket size in pixels (RIB parser passes as float*)
                float* val = (float*)values[i];
                ctx->bucket_size = (int)(*val);
                if (ctx->bucket_size < 8) ctx->bucket_size = 8;
                if (ctx->bucket_size > 256) ctx->bucket_size = 256;
            } else if (strcmp(tokens[i], "othresh") == 0) {
                // Opacity threshold for A-buffer visibility culling
                // When accumulated opacity exceeds this, samples are culled
                float* val = (float*)values[i];
                ctx->memory.opacity_threshold = *val;
                if (ctx->memory.opacity_threshold < 0.0f) ctx->memory.opacity_threshold = 0.0f;
                if (ctx->memory.opacity_threshold > 1.0f) ctx->memory.opacity_threshold = 1.0f;
            }
        }
    }
}

void RiOption(RtToken name, ...) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

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

    RiOptionV(name, tokens, values, count);
}

void RiHiderV(RtToken type, RtToken* tokens, RtPointer* values, int count) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    // Type is typically "hidden" for z-buffer rendering, but we accept any
    (void)type;

    // Parse "jitter" parameter
    for (int i = 0; i < count; i++) {
        if (tokens[i] && strcmp(tokens[i], "jitter") == 0) {
            float* val = (float*)values[i];
            ctx->hider_options.jitter = (*val != 0.0f) ? 1 : 0;
        }
    }
}

void RiHider(RtToken type, ...) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    RtToken tokens[16];
    RtPointer values[16];
    int count = 0;

    va_list ap;
    va_start(ap, type);
    RtToken token;
    while ((token = va_arg(ap, RtToken)) != RI_NULL && count < 16) {
        tokens[count] = token;
        values[count] = va_arg(ap, RtPointer);
        count++;
    }
    va_end(ap);

    RiHiderV(type, tokens, values, count);
}

void RiFormat(RtInt xresolution, RtInt yresolution, RtFloat pixelaspectratio) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    ctx->xres = xresolution;
    ctx->yres = yresolution;
    (void)pixelaspectratio;
}

void RiDisplayV(RtToken name, RtToken type, RtToken mode, RtToken* tokens, RtPointer* values, int count) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    if (name) strncpy(ctx->display_name, name, 255);

    // Parse mode to determine channel count and display mode
    if (mode) {
        if (strcmp(mode, "rgb") == 0) {
            ctx->display_channels = 3;
            ctx->display_mode = RH_DISPLAY_RGB;
        } else if (strcmp(mode, "rgba") == 0) {
            ctx->display_channels = 4;
            ctx->display_mode = RH_DISPLAY_RGBA;
        } else if (strcmp(mode, "z") == 0) {
            ctx->display_channels = 1;
            ctx->display_mode = RH_DISPLAY_Z;
        }
    }

    (void)type;    // "file" is the only supported type
    (void)tokens;  // Currently unused
    (void)values;
    (void)count;
}

void RiDisplay(RtToken name, RtToken type, RtToken mode, ...) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    RtToken tokens[16];
    RtPointer values[16];
    int count = 0;

    va_list ap;
    va_start(ap, mode);
    RtToken token;
    while ((token = va_arg(ap, RtToken)) != RI_NULL && count < 16) {
        tokens[count] = token;
        values[count] = va_arg(ap, RtPointer);
        count++;
    }
    va_end(ap);

    RiDisplayV(name, type, mode, tokens, values, count);
}

void RiPixelSamples(RtFloat xsamples, RtFloat ysamples) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    ctx->pixel_samples_x = (int)rh_max(1.0f, xsamples);
    ctx->pixel_samples_y = (int)rh_max(1.0f, ysamples);
}

void RiPixelFilter(RtFilterFunc filterfunc, RtFloat xwidth, RtFloat ywidth) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    ctx->pixel_filter = filterfunc;
    ctx->filter_width_x = xwidth;
    ctx->filter_width_y = ywidth;
}

void RiDepthOfField(RtFloat fstop, RtFloat focallength, RtFloat focaldistance) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    ctx->dof_fstop = fstop;
    ctx->dof_focallength = focallength;
    ctx->dof_focaldistance = focaldistance;
}

void RiShutter(RtFloat open, RtFloat close) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    ctx->shutter_open = open;
    ctx->shutter_close = close;
}

void RiClipping(RtFloat nearclip, RtFloat farclip) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    ctx->near_clip = nearclip;
    ctx->far_clip = farclip;
}

void RiProjectionV(RtToken name, RtToken* tokens, RtPointer* values, int count) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    float fov = 45.0f;  // Default FOV

    // Look for "fov" parameter
    for (int i = 0; i < count; i++) {
        if (tokens[i] && strcmp(tokens[i], "fov") == 0) {
            RtFloat* fov_ptr = (RtFloat*)values[i];
            if (fov_ptr) fov = *fov_ptr;
            break;
        }
    }

    if (strcmp(name, "perspective") == 0) {
        // Construct Projection Matrix
        // Widen FOV by 10% for shadow maps to avoid edge artifacts
        float actual_fov = fov;
        if (ctx->display_mode == RH_DISPLAY_Z) {
            actual_fov *= 1.1f;
        }
        float fov_rad = actual_fov * (RH_PI / 180.0f);
        float aspect = (float)ctx->xres / (float)ctx->yres;
        float f = 1.0f / tanf(fov_rad / 2.0f);
        float zNear = 0.1f;
        float zFar = 1e30f;

        // Store clip distances for culling and shadow maps
        ctx->near_clip = zNear;
        ctx->far_clip = zFar;

        ctx->projection = rh_mat4_identity();
        ctx->projection.m[0][0] = f / aspect;
        ctx->projection.m[1][1] = f;
        // RenderMan left-handed: camera looks down +Z, objects in front have positive Z
        ctx->projection.m[2][2] = (zFar + zNear) / (zFar - zNear);
        ctx->projection.m[2][3] = -(2 * zFar * zNear) / (zFar - zNear);
        ctx->projection.m[3][2] = 1.0f;
        ctx->projection.m[3][3] = 0.0f;
    } else if (strcmp(name, "orthographic") == 0) {
        // Identity / Scale
        ctx->projection = rh_mat4_identity();
    }
}

void RiProjection(RtToken name, ...) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

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

    RiProjectionV(name, tokens, values, count);
}
