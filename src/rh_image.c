#include "rh_image.h"
#include "xpt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "stb_image_write.h"

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

RhImage* rh_image_create(int width, int height) {
    RhImage* img = (RhImage*)malloc(sizeof(RhImage));
    if (!img) return NULL;

    img->width = width;
    img->height = height;
    // Calloc to initialize pixels to black (0,0,0) and opacities to transparent (0,0,0)
    img->pixels = (RhColor*)calloc(width * height, sizeof(RhColor));
    img->opacities = (RhColor*)calloc(width * height, sizeof(RhColor));

    if (!img->pixels || !img->opacities) {
        if (img->pixels) free(img->pixels);
        if (img->opacities) free(img->opacities);
        free(img);
        return NULL;
    }

    return img;
}

void rh_image_set_pixel(RhImage* img, int x, int y, RhColor color) {
    if (x < 0 || x >= img->width || y < 0 || y >= img->height) return;
    img->pixels[y * img->width + x] = color;
}

void rh_image_set_pixel_with_opacity(RhImage* img, int x, int y, RhColor color, RhColor opacity) {
    if (x < 0 || x >= img->width || y < 0 || y >= img->height) return;
    int idx = y * img->width + x;
    img->pixels[idx] = color;
    img->opacities[idx] = opacity;
}

RhColor rh_image_get_pixel(const RhImage* img, int x, int y) {
    if (x < 0 || x >= img->width || y < 0 || y >= img->height) {
        RhColor black = {0.0f, 0.0f, 0.0f};
        return black;
    }
    return img->pixels[y * img->width + x];
}

static unsigned char to_byte(float v) {
    // Clamp to [0, 1] then map to [0, 255]
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return (unsigned char)(powf(v, 1.0f/2.2f) * 255.0f); // Simple Gamma 2.2 correction
}

void rh_image_save_ppm(const RhImage* img, const char* filename) {
    FILE* fp = fopen(filename, "w");
    if (!fp) {
        xpt_error("rh.image", "Could not open file %s for writing", filename);
        return;
    }

    // P3 Header: P3 <width> <height> <max_val>
    fprintf(fp, "P3\n%d %d\n255\n", img->width, img->height);

    for (int y = 0; y < img->height; y++) {
        for (int x = 0; x < img->width; x++) {
            RhColor c = img->pixels[y * img->width + x];
            fprintf(fp, "%d %d %d ", to_byte(c.r), to_byte(c.g), to_byte(c.b));
        }
        fprintf(fp, "\n");
    }

    fclose(fp);
    printf("Saved image to %s\n", filename);
}

// Compute single-channel alpha from 3-channel opacity using weighted luminance
static float luminance_alpha(RhColor opacity) {
    return 0.2126f * opacity.r + 0.7152f * opacity.g + 0.0722f * opacity.b;
}

void rh_image_save_png_channels(const RhImage* img, const char* filename, int channels) {
    if (channels != 3 && channels != 4) {
        xpt_error("rh.image", "PNG channels must be 3 (RGB) or 4 (RGBA)");
        return;
    }

    unsigned char* data = (unsigned char*)malloc((size_t)img->width * (size_t)img->height * (size_t)channels);
    if (!data) {
        xpt_error("rh.image", "Could not allocate memory for PNG output");
        return;
    }

    for (int i = 0; i < img->width * img->height; i++) {
        RhColor c = img->pixels[i];      // Premultiplied color (Ci)
        RhColor o = img->opacities[i];   // Opacity (Oi)
        float alpha = luminance_alpha(o);

        // Unpremultiply for PNG (straight alpha format)
        float r, g, b;
        if (alpha > 1e-6f) {
            r = c.r / alpha;
            g = c.g / alpha;
            b = c.b / alpha;
        } else {
            r = g = b = 0.0f;
        }

        if (channels == 4) {
            data[i * 4 + 0] = to_byte(r);
            data[i * 4 + 1] = to_byte(g);
            data[i * 4 + 2] = to_byte(b);
            data[i * 4 + 3] = to_byte(alpha);
        } else {
            // RGB: composite against black background
            data[i * 3 + 0] = to_byte(r * alpha);
            data[i * 3 + 1] = to_byte(g * alpha);
            data[i * 3 + 2] = to_byte(b * alpha);
        }
    }

    int stride = img->width * channels;
    int result = stbi_write_png(filename, img->width, img->height, channels, data, stride);
    free(data);

    if (!result) {
        xpt_error("rh.image", "PNG encoding failed for '%s'", filename);
    } else {
        printf("Saved image to %s\n", filename);
    }
}

void rh_image_save_png(const RhImage* img, const char* filename) {
    rh_image_save_png_channels(img, filename, 4);
}

// Prepare pixel data as RGB bytes (3 channels, composited against black)
static unsigned char* prepare_rgb_data(const RhImage* img) {
    unsigned char* data = (unsigned char*)malloc((size_t)img->width * (size_t)img->height * 3);
    if (!data) return NULL;

    for (int i = 0; i < img->width * img->height; i++) {
        RhColor c = img->pixels[i];
        RhColor o = img->opacities[i];
        float alpha = luminance_alpha(o);

        float r, g, b;
        if (alpha > 1e-6f) {
            r = c.r / alpha;
            g = c.g / alpha;
            b = c.b / alpha;
        } else {
            r = g = b = 0.0f;
        }

        data[i * 3 + 0] = to_byte(r * alpha);
        data[i * 3 + 1] = to_byte(g * alpha);
        data[i * 3 + 2] = to_byte(b * alpha);
    }
    return data;
}

void rh_image_save(const RhImage* img, const char* filename, int channels) {
    if (!img || !filename) return;

    // Formats that support alpha
    int format_supports_alpha = str_ends_with_ci(filename, ".png") ||
                                str_ends_with_ci(filename, ".tga") ||
                                str_ends_with_ci(filename, ".bmp");

    // Warn and downgrade if RGBA requested but format doesn't support it
    if (channels == 4 && !format_supports_alpha) {
        xpt_warn("rh.image", "Display mode \"rgba\" requested but output format "
                 "does not support alpha; saving as RGB: %s", filename);
        channels = 3;
    }

    if (str_ends_with_ci(filename, ".png")) {
        rh_image_save_png_channels(img, filename, channels);
        return;
    }

    if (str_ends_with_ci(filename, ".ppm")) {
        rh_image_save_ppm(img, filename);
        return;
    }

    // For JPG, BMP, TGA: prepare RGB data and write
    unsigned char* data = NULL;
    int result = 0;

    if (channels == 4) {
        // RGBA path (only for formats that support it: TGA, BMP)
        data = (unsigned char*)malloc((size_t)img->width * (size_t)img->height * 4);
        if (!data) {
            xpt_error("rh.image", "Could not allocate memory for image output");
            return;
        }
        for (int i = 0; i < img->width * img->height; i++) {
            RhColor c = img->pixels[i];
            RhColor o = img->opacities[i];
            float alpha = luminance_alpha(o);
            float r, g, b;
            if (alpha > 1e-6f) {
                r = c.r / alpha;
                g = c.g / alpha;
                b = c.b / alpha;
            } else {
                r = g = b = 0.0f;
            }
            data[i * 4 + 0] = to_byte(r);
            data[i * 4 + 1] = to_byte(g);
            data[i * 4 + 2] = to_byte(b);
            data[i * 4 + 3] = to_byte(alpha);
        }
    } else {
        data = prepare_rgb_data(img);
        if (!data) {
            xpt_error("rh.image", "Could not allocate memory for image output");
            return;
        }
    }

    if (str_ends_with_ci(filename, ".jpg") || str_ends_with_ci(filename, ".jpeg")) {
        result = stbi_write_jpg(filename, img->width, img->height, channels, data, 95);
    } else if (str_ends_with_ci(filename, ".bmp")) {
        result = stbi_write_bmp(filename, img->width, img->height, channels, data);
    } else if (str_ends_with_ci(filename, ".tga")) {
        result = stbi_write_tga(filename, img->width, img->height, channels, data);
    } else {
        // Unknown extension: fall back to PNG
        xpt_warn("rh.image", "Unknown image extension, saving as PNG: %s", filename);
        free(data);
        rh_image_save_png_channels(img, filename, channels);
        return;
    }

    free(data);
    if (!result) {
        xpt_error("rh.image", "Image encoding failed for '%s'", filename);
    } else {
        printf("Saved image to %s\n", filename);
    }
}

void rh_image_destroy(RhImage* img) {
    if (img) {
        if (img->pixels) free(img->pixels);
        if (img->opacities) free(img->opacities);
        free(img);
    }
}
