#include "rh_texture.h"
#include "stb_image.h"
#include "xpt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

/* Forward declarations for format-specific loaders */
struct RhExrResult;
struct RhPfmResult;

/* Helper: get channel count from format */
static unsigned int format_to_channels(RhTextureFormat format) {
    switch (format) {
        case RH_TEX_GREY: return 1;
        case RH_TEX_RGB:  return 3;
        case RH_TEX_RGBA: return 4;
        default:          return 4;
    }
}

/* Helper: max of two unsigned ints */
static unsigned int max_u(unsigned int a, unsigned int b) {
    return (a > b) ? a : b;
}

/* Helper: case-insensitive string comparison for file extensions */
static int str_ends_with_ci(const char* str, const char* suffix) {
    size_t str_len = strlen(str);
    size_t suf_len = strlen(suffix);
    if (suf_len > str_len) return 0;
    const char* end = str + str_len - suf_len;
    for (size_t i = 0; i < suf_len; i++) {
        if (tolower((unsigned char)end[i]) != tolower((unsigned char)suffix[i]))
            return 0;
    }
    return 1;
}

/* Box filter downsample: decimate by 2x in each dimension (float data) */
static void downsample_level(
    const float* src, unsigned int src_w, unsigned int src_h,
    float* dst, unsigned int dst_w, unsigned int dst_h,
    unsigned int channels)
{
    for (unsigned int dy = 0; dy < dst_h; dy++) {
        for (unsigned int dx = 0; dx < dst_w; dx++) {
            unsigned int sx = dx * 2;
            unsigned int sy = dy * 2;

            for (unsigned int c = 0; c < channels; c++) {
                float sum = 0.0f;
                unsigned int count = 0;

                for (unsigned int oy = 0; oy < 2 && (sy + oy) < src_h; oy++) {
                    for (unsigned int ox = 0; ox < 2 && (sx + ox) < src_w; ox++) {
                        unsigned int src_idx =
                            ((sy + oy) * src_w + (sx + ox)) * channels + c;
                        sum += src[src_idx];
                        count++;
                    }
                }

                unsigned int dst_idx = (dy * dst_w + dx) * channels + c;
                dst[dst_idx] = sum / (float)count;
            }
        }
    }
}

unsigned int rh_texture_mip_count(unsigned int width, unsigned int height) {
    unsigned int max_dim = max_u(width, height);
    unsigned int levels = 1;
    while (max_dim > 1) {
        max_dim >>= 1;
        levels++;
    }
    return levels;
}

/* Build mipmap pyramid from level 0 float data already in tex */
static int build_mipmaps(RhTexture* tex) {
    for (unsigned int i = 1; i < tex->num_levels; i++) {
        unsigned int lw = max_u(1, tex->levels[i - 1].width / 2);
        unsigned int lh = max_u(1, tex->levels[i - 1].height / 2);

        tex->levels[i].width = lw;
        tex->levels[i].height = lh;
        tex->levels[i].data = (float*)malloc(
            (size_t)lw * (size_t)lh * (size_t)tex->channels * sizeof(float));

        if (!tex->levels[i].data) {
            xpt_error("rh.texture", "Could not allocate mipmap level %u", i);
            return 0;
        }

        downsample_level(
            tex->levels[i - 1].data, tex->levels[i - 1].width, tex->levels[i - 1].height,
            tex->levels[i].data, lw, lh,
            tex->channels
        );
    }
    return 1;
}

/* Load via stb_image (handles PNG, JPG, BMP, TGA, GIF, PSD, HDR, PIC, PNM) */
static float* load_stbi(const char* filename, int* out_w, int* out_h,
                        int* out_channels, int desired_channels, int* out_is_hdr) {
    *out_is_hdr = 0;

    /* Check if this is an HDR format that stb_image handles natively as float */
    if (stbi_is_hdr(filename)) {
        float* fdata = stbi_loadf(filename, out_w, out_h, out_channels, desired_channels);
        if (fdata) {
            *out_is_hdr = 1;
            if (desired_channels > 0) *out_channels = desired_channels;
        }
        return fdata;
    }

    /* LDR path: load as bytes, convert to float */
    unsigned char* raw = stbi_load(filename, out_w, out_h, out_channels, desired_channels);
    if (!raw) return NULL;

    int ch = (desired_channels > 0) ? desired_channels : *out_channels;
    if (desired_channels > 0) *out_channels = desired_channels;

    size_t pixel_count = (size_t)(*out_w) * (size_t)(*out_h) * (size_t)ch;
    float* fdata = (float*)malloc(pixel_count * sizeof(float));
    if (!fdata) {
        stbi_image_free(raw);
        return NULL;
    }

    for (size_t i = 0; i < pixel_count; i++) {
        fdata[i] = raw[i] / 255.0f;
    }
    stbi_image_free(raw);
    return fdata;
}

RhTexture* rh_texture_load(const char* filename, RhTextureFormat format) {
    if (!filename) {
        xpt_error("rh.texture", "NULL filename passed to rh_texture_load");
        return NULL;
    }

    int desired_channels = 0;
    if (format != RH_TEX_AUTO) {
        desired_channels = (int)format_to_channels(format);
    }

    int w, h, actual_channels;
    int is_hdr = 0;
    float* fdata = NULL;

    /* Route by file extension */
    if (str_ends_with_ci(filename, ".exr")) {
        /* EXR loader (rh_exr.c) — loaded externally, plugged in via rh_texture_load */
        extern float* rh_exr_load(const char* filename, int* w, int* h, int* channels);
        fdata = rh_exr_load(filename, &w, &h, &actual_channels);
        is_hdr = 1;
        if (fdata && desired_channels > 0 && desired_channels != actual_channels) {
            /* Channel conversion for EXR: expand or contract */
            size_t npixels = (size_t)w * (size_t)h;
            float* converted = (float*)malloc(npixels * (size_t)desired_channels * sizeof(float));
            if (!converted) { free(fdata); return NULL; }
            for (size_t i = 0; i < npixels; i++) {
                for (int c = 0; c < desired_channels; c++) {
                    if (c < actual_channels) {
                        converted[i * desired_channels + c] = fdata[i * actual_channels + c];
                    } else {
                        converted[i * desired_channels + c] = (c == 3) ? 1.0f : 0.0f;
                    }
                }
            }
            free(fdata);
            fdata = converted;
            actual_channels = desired_channels;
        }
    } else if (str_ends_with_ci(filename, ".pfm")) {
        /* PFM loader (rh_pfm.c) */
        extern float* rh_pfm_load(const char* filename, int* w, int* h, int* channels);
        fdata = rh_pfm_load(filename, &w, &h, &actual_channels);
        is_hdr = 1;
        if (fdata && desired_channels > 0 && desired_channels != actual_channels) {
            size_t npixels = (size_t)w * (size_t)h;
            float* converted = (float*)malloc(npixels * (size_t)desired_channels * sizeof(float));
            if (!converted) { free(fdata); return NULL; }
            for (size_t i = 0; i < npixels; i++) {
                for (int c = 0; c < desired_channels; c++) {
                    if (c < actual_channels) {
                        converted[i * desired_channels + c] = fdata[i * actual_channels + c];
                    } else {
                        converted[i * desired_channels + c] = (c == 3) ? 1.0f : 0.0f;
                    }
                }
            }
            free(fdata);
            fdata = converted;
            actual_channels = desired_channels;
        }
    } else {
        /* All other formats: stb_image */
        fdata = load_stbi(filename, &w, &h, &actual_channels, desired_channels, &is_hdr);
    }

    if (!fdata) {
        xpt_error("rh.texture", "Could not load texture '%s'", filename);
        return NULL;
    }

    unsigned int channels = (desired_channels > 0)
        ? (unsigned int)desired_channels
        : (unsigned int)actual_channels;

    /* Allocate texture structure */
    RhTexture* tex = (RhTexture*)malloc(sizeof(RhTexture));
    if (!tex) {
        free(fdata);
        xpt_error("rh.texture", "Could not allocate texture structure");
        return NULL;
    }

    tex->base_width = (unsigned int)w;
    tex->base_height = (unsigned int)h;
    tex->channels = channels;
    tex->is_hdr = is_hdr;
    tex->num_levels = rh_texture_mip_count((unsigned int)w, (unsigned int)h);

    tex->levels = (RhMipLevel*)calloc(tex->num_levels, sizeof(RhMipLevel));
    if (!tex->levels) {
        free(fdata);
        free(tex);
        xpt_error("rh.texture", "Could not allocate mipmap level array");
        return NULL;
    }

    /* Level 0: take ownership of fdata */
    tex->levels[0].width = (unsigned int)w;
    tex->levels[0].height = (unsigned int)h;
    tex->levels[0].data = fdata;

    /* Build mipmap pyramid */
    if (!build_mipmaps(tex)) {
        rh_texture_destroy(tex);
        return NULL;
    }

    return tex;
}

void rh_texture_destroy(RhTexture* tex) {
    if (!tex) return;

    if (tex->levels) {
        for (unsigned int i = 0; i < tex->num_levels; i++) {
            free(tex->levels[i].data);
        }
        free(tex->levels);
    }
    free(tex);
}

/* Get a texel from a specific mip level with clamped coordinates (float data) */
static void get_texel(const RhTexture* tex, unsigned int level,
                      int x, int y, float* out) {
    if (level >= tex->num_levels) {
        level = tex->num_levels - 1;
    }

    const RhMipLevel* mip = &tex->levels[level];

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if ((unsigned int)x >= mip->width) x = (int)mip->width - 1;
    if ((unsigned int)y >= mip->height) y = (int)mip->height - 1;

    unsigned int idx = ((unsigned int)y * mip->width + (unsigned int)x) * tex->channels;
    memcpy(out, &mip->data[idx], tex->channels * sizeof(float));
}

/* Bilinear interpolation with optional alpha output */
static RhColor sample_bilinear_alpha(const RhTexture* tex, unsigned int level,
                                     float u, float v, float* out_alpha) {
    RhColor result = {0, 0, 0};
    if (!tex || !tex->levels) {
        if (out_alpha) *out_alpha = 1.0f;
        return result;
    }

    if (level >= tex->num_levels) level = tex->num_levels - 1;

    const RhMipLevel* mip = &tex->levels[level];

    /* Wrap coordinates to [0,1] (repeat mode) */
    u = u - floorf(u);
    v = v - floorf(v);

    /* Flip v: image files store top-left origin,
       texture coords assume bottom-left origin */
    v = 1.0f - v;

    /* Convert to texel coordinates */
    float fx = u * (float)(mip->width - 1);
    float fy = v * (float)(mip->height - 1);

    int x0 = (int)floorf(fx);
    int y0 = (int)floorf(fy);
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    if (x1 >= (int)mip->width) x1 = (int)mip->width - 1;
    if (y1 >= (int)mip->height) y1 = (int)mip->height - 1;

    float tx = fx - (float)x0;
    float ty = fy - (float)y0;

    /* Fetch four texels (up to 4 channels each) */
    float c00[4], c10[4], c01[4], c11[4];
    get_texel(tex, level, x0, y0, c00);
    get_texel(tex, level, x1, y0, c10);
    get_texel(tex, level, x0, y1, c01);
    get_texel(tex, level, x1, y1, c11);

    /* Bilinear interpolation — data is already [0,1] float */
    float r_top = c00[0] * (1.0f - tx) + c10[0] * tx;
    float r_bot = c01[0] * (1.0f - tx) + c11[0] * tx;
    result.r = r_top * (1.0f - ty) + r_bot * ty;

    if (tex->channels >= 3) {
        float g_top = c00[1] * (1.0f - tx) + c10[1] * tx;
        float g_bot = c01[1] * (1.0f - tx) + c11[1] * tx;
        result.g = g_top * (1.0f - ty) + g_bot * ty;

        float b_top = c00[2] * (1.0f - tx) + c10[2] * tx;
        float b_bot = c01[2] * (1.0f - tx) + c11[2] * tx;
        result.b = b_top * (1.0f - ty) + b_bot * ty;
    } else {
        result.g = result.r;
        result.b = result.r;
    }

    if (out_alpha) {
        if (tex->channels == 4) {
            float a_top = c00[3] * (1.0f - tx) + c10[3] * tx;
            float a_bot = c01[3] * (1.0f - tx) + c11[3] * tx;
            *out_alpha = a_top * (1.0f - ty) + a_bot * ty;
        } else {
            *out_alpha = 1.0f;
        }
    }

    return result;
}

RhColor rh_texture_sample_bilinear(const RhTexture* tex, unsigned int level,
                                   float u, float v) {
    return sample_bilinear_alpha(tex, level, u, v, NULL);
}

static int compute_mip_level(const RhTexture* tex, float du, float dv) {
    float filter_width = fmaxf(fabsf(du), fabsf(dv));
    float max_dim = fmaxf((float)tex->base_width, (float)tex->base_height);
    float texel_coverage = filter_width * max_dim;

    float mip_float = log2f(fmaxf(1.0f, texel_coverage));
    int level = (int)floorf(mip_float);

    if (level < 0) level = 0;
    if (level >= (int)tex->num_levels) level = (int)tex->num_levels - 1;

    return level;
}

RhColor rh_texture_sample(const RhTexture* tex, float u, float v,
                          float du, float dv) {
    if (!tex) return (RhColor){1.0f, 0.0f, 1.0f};

    int level = compute_mip_level(tex, du, dv);
    return rh_texture_sample_bilinear(tex, (unsigned int)level, u, v);
}

RhColor rh_texture_sample_with_alpha(const RhTexture* tex, float u, float v,
                                     float du, float dv, float* out_alpha) {
    if (!tex) {
        if (out_alpha) *out_alpha = 1.0f;
        return (RhColor){1.0f, 0.0f, 1.0f};
    }

    int level = compute_mip_level(tex, du, dv);
    return sample_bilinear_alpha(tex, (unsigned int)level, u, v, out_alpha);
}
