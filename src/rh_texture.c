#include "rh_texture.h"
#include "lodepng.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Helper: convert RhTextureFormat to LodePNGColorType */
static LodePNGColorType format_to_lodepng(RhTextureFormat format) {
    switch (format) {
        case RH_TEX_GREY: return LCT_GREY;
        case RH_TEX_RGB:  return LCT_RGB;
        case RH_TEX_RGBA: return LCT_RGBA;
        default:          return LCT_RGBA;
    }
}

/* Helper: get channel count from format */
static unsigned int format_to_channels(RhTextureFormat format) {
    switch (format) {
        case RH_TEX_GREY: return 1;
        case RH_TEX_RGB:  return 3;
        case RH_TEX_RGBA: return 4;
        default:          return 4;
    }
}

/* Helper: detect PNG format from file */
static RhTextureFormat detect_png_format(const char* filename) {
    unsigned char* data = NULL;
    size_t datasize = 0;

    /* Load file into memory */
    if (lodepng_load_file(&data, &datasize, filename) != 0) {
        return RH_TEX_RGBA;  /* Default fallback */
    }

    /* Inspect PNG header */
    LodePNGState state;
    lodepng_state_init(&state);

    unsigned int w, h;
    unsigned int error = lodepng_inspect(&w, &h, &state, data, datasize);

    RhTextureFormat result = RH_TEX_RGBA;  /* Default */

    if (error == 0) {
        LodePNGColorType ct = state.info_png.color.colortype;
        switch (ct) {
            case LCT_GREY:
                result = RH_TEX_GREY;
                break;
            case LCT_GREY_ALPHA:
                result = RH_TEX_RGBA;  /* Promote to preserve alpha */
                break;
            case LCT_RGB:
                result = RH_TEX_RGB;
                break;
            case LCT_RGBA:
            case LCT_PALETTE:
            default:
                result = RH_TEX_RGBA;
                break;
        }
    }

    lodepng_state_cleanup(&state);
    free(data);
    return result;
}

/* Helper: max of two unsigned ints */
static unsigned int max_u(unsigned int a, unsigned int b) {
    return (a > b) ? a : b;
}

/* Box filter downsample: decimate by 2x in each dimension */
static void downsample_level(
    const unsigned char* src, unsigned int src_w, unsigned int src_h,
    unsigned char* dst, unsigned int dst_w, unsigned int dst_h,
    unsigned int channels)
{
    for (unsigned int dy = 0; dy < dst_h; dy++) {
        for (unsigned int dx = 0; dx < dst_w; dx++) {
            /* Source coordinates (2:1 mapping) */
            unsigned int sx = dx * 2;
            unsigned int sy = dy * 2;

            for (unsigned int c = 0; c < channels; c++) {
                unsigned int sum = 0;
                unsigned int count = 0;

                /* Sample up to 2x2 texels (handles odd dimensions) */
                for (unsigned int oy = 0; oy < 2 && (sy + oy) < src_h; oy++) {
                    for (unsigned int ox = 0; ox < 2 && (sx + ox) < src_w; ox++) {
                        unsigned int src_idx =
                            ((sy + oy) * src_w + (sx + ox)) * channels + c;
                        sum += src[src_idx];
                        count++;
                    }
                }

                unsigned int dst_idx = (dy * dst_w + dx) * channels + c;
                dst[dst_idx] = (unsigned char)((sum + count / 2) / count);  /* Rounded average */
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

RhTexture* rh_texture_load(const char* filename, RhTextureFormat format) {
    if (!filename) {
        fprintf(stderr, "Error: NULL filename passed to rh_texture_load\n");
        return NULL;
    }

    /* Detect format if AUTO */
    if (format == RH_TEX_AUTO) {
        format = detect_png_format(filename);
    }

    /* Decode PNG */
    unsigned char* raw_data = NULL;
    unsigned int w, h;
    LodePNGColorType color_type = format_to_lodepng(format);
    unsigned int error = lodepng_decode_file(&raw_data, &w, &h, filename, color_type, 8);

    if (error) {
        fprintf(stderr, "Error: Could not load texture '%s': %s\n",
                filename, lodepng_error_text(error));
        return NULL;
    }

    /* Allocate texture structure */
    RhTexture* tex = (RhTexture*)malloc(sizeof(RhTexture));
    if (!tex) {
        free(raw_data);
        fprintf(stderr, "Error: Could not allocate texture structure\n");
        return NULL;
    }

    /* Initialize texture metadata */
    tex->base_width = w;
    tex->base_height = h;
    tex->channels = format_to_channels(format);
    tex->num_levels = rh_texture_mip_count(w, h);

    /* Allocate levels array */
    tex->levels = (RhMipLevel*)calloc(tex->num_levels, sizeof(RhMipLevel));
    if (!tex->levels) {
        free(raw_data);
        free(tex);
        fprintf(stderr, "Error: Could not allocate mipmap level array\n");
        return NULL;
    }

    /* Level 0: transfer ownership of raw_data */
    tex->levels[0].width = w;
    tex->levels[0].height = h;
    tex->levels[0].data = raw_data;

    /* Generate subsequent mip levels */
    for (unsigned int i = 1; i < tex->num_levels; i++) {
        unsigned int lw = max_u(1, tex->levels[i - 1].width / 2);
        unsigned int lh = max_u(1, tex->levels[i - 1].height / 2);

        tex->levels[i].width = lw;
        tex->levels[i].height = lh;
        tex->levels[i].data = (unsigned char*)malloc(lw * lh * tex->channels);

        if (!tex->levels[i].data) {
            /* Cleanup all previously allocated levels */
            for (unsigned int j = 0; j < i; j++) {
                free(tex->levels[j].data);
            }
            free(tex->levels);
            free(tex);
            fprintf(stderr, "Error: Could not allocate mipmap level %u\n", i);
            return NULL;
        }

        downsample_level(
            tex->levels[i - 1].data, tex->levels[i - 1].width, tex->levels[i - 1].height,
            tex->levels[i].data, lw, lh,
            tex->channels
        );
    }

    return tex;
}

void rh_texture_destroy(RhTexture* tex) {
    if (!tex) return;

    if (tex->levels) {
        for (unsigned int i = 0; i < tex->num_levels; i++) {
            free(tex->levels[i].data);  /* free(NULL) is safe */
        }
        free(tex->levels);
    }
    free(tex);
}

void rh_texture_get_texel(const RhTexture* tex, unsigned int level,
                          int x, int y, unsigned char* out) {
    if (!tex || !out) return;

    /* Clamp level */
    if (level >= tex->num_levels) {
        level = tex->num_levels - 1;
    }

    const RhMipLevel* mip = &tex->levels[level];

    /* Clamp coordinates */
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if ((unsigned int)x >= mip->width) x = (int)mip->width - 1;
    if ((unsigned int)y >= mip->height) y = (int)mip->height - 1;

    unsigned int idx = ((unsigned int)y * mip->width + (unsigned int)x) * tex->channels;
    memcpy(out, &mip->data[idx], tex->channels);
}

RhColor rh_texture_sample_bilinear(const RhTexture* tex, unsigned int level,
                                   float u, float v) {
    RhColor result = {0, 0, 0};
    if (!tex || !tex->levels) return result;

    /* Clamp mip level */
    if (level >= tex->num_levels) level = tex->num_levels - 1;

    const RhMipLevel* mip = &tex->levels[level];

    /* Wrap coordinates to [0,1] (repeat mode) */
    u = u - floorf(u);
    v = v - floorf(v);

    /* Flip v for PNG origin convention (PNG has origin at top-left,
       but texture coords assume origin at bottom-left) */
    v = 1.0f - v;

    /* Convert to texel coordinates */
    float fx = u * (float)(mip->width - 1);
    float fy = v * (float)(mip->height - 1);

    int x0 = (int)floorf(fx);
    int y0 = (int)floorf(fy);
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    /* Clamp to texture bounds */
    if (x1 >= (int)mip->width) x1 = (int)mip->width - 1;
    if (y1 >= (int)mip->height) y1 = (int)mip->height - 1;

    /* Fractional parts for interpolation */
    float tx = fx - (float)x0;
    float ty = fy - (float)y0;

    /* Fetch four texels */
    unsigned char c00[4], c10[4], c01[4], c11[4];
    rh_texture_get_texel(tex, level, x0, y0, c00);
    rh_texture_get_texel(tex, level, x1, y0, c10);
    rh_texture_get_texel(tex, level, x0, y1, c01);
    rh_texture_get_texel(tex, level, x1, y1, c11);

    /* Bilinear interpolation for each channel */
    float r_top = (float)c00[0] * (1.0f - tx) + (float)c10[0] * tx;
    float r_bot = (float)c01[0] * (1.0f - tx) + (float)c11[0] * tx;
    float r_val = r_top * (1.0f - ty) + r_bot * ty;
    result.r = r_val / 255.0f;

    if (tex->channels >= 3) {
        float g_top = (float)c00[1] * (1.0f - tx) + (float)c10[1] * tx;
        float g_bot = (float)c01[1] * (1.0f - tx) + (float)c11[1] * tx;
        float g_val = g_top * (1.0f - ty) + g_bot * ty;
        result.g = g_val / 255.0f;

        float b_top = (float)c00[2] * (1.0f - tx) + (float)c10[2] * tx;
        float b_bot = (float)c01[2] * (1.0f - tx) + (float)c11[2] * tx;
        float b_val = b_top * (1.0f - ty) + b_bot * ty;
        result.b = b_val / 255.0f;
    } else {
        /* Grayscale: replicate to all channels */
        result.g = result.r;
        result.b = result.r;
    }

    return result;
}

RhColor rh_texture_sample(const RhTexture* tex, float u, float v,
                          float du, float dv) {
    if (!tex) return (RhColor){1.0f, 0.0f, 1.0f}; /* Magenta for missing texture */

    /* Calculate filter width and mip level */
    float filter_width = fmaxf(fabsf(du), fabsf(dv));
    float max_dim = fmaxf((float)tex->base_width, (float)tex->base_height);
    float texel_coverage = filter_width * max_dim;

    float mip_float = log2f(fmaxf(1.0f, texel_coverage));
    int level = (int)floorf(mip_float);

    /* Clamp to valid range */
    if (level < 0) level = 0;
    if (level >= (int)tex->num_levels) level = (int)tex->num_levels - 1;

    /* Sample at computed mip level (no trilinear for now) */
    return rh_texture_sample_bilinear(tex, (unsigned int)level, u, v);
}
