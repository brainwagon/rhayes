#include "rh_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "lodepng.h"

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
        fprintf(stderr, "Error: Could not open file %s for writing.\n", filename);
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

void rh_image_save_png(const RhImage* img, const char* filename) {
    unsigned char* data = (unsigned char*)malloc((size_t)img->width * (size_t)img->height * 4);
    if (!data) {
        fprintf(stderr, "Error: Could not allocate memory for PNG output.\n");
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

        data[i * 4 + 0] = to_byte(r);
        data[i * 4 + 1] = to_byte(g);
        data[i * 4 + 2] = to_byte(b);
        data[i * 4 + 3] = to_byte(alpha);
    }

    // Use LodePNGState to set bKGD chunk
    LodePNGState state;
    lodepng_state_init(&state);

    // Input is RGBA 8-bit
    state.info_raw.colortype = LCT_RGBA;
    state.info_raw.bitdepth = 8;

    // Output as RGBA 8-bit
    state.info_png.color.colortype = LCT_RGBA;
    state.info_png.color.bitdepth = 8;

    // Set bKGD chunk to black
    state.info_png.background_defined = 1;
    state.info_png.background_r = 0;
    state.info_png.background_g = 0;
    state.info_png.background_b = 0;

    unsigned char* png_data = NULL;
    size_t png_size = 0;
    unsigned error = lodepng_encode(&png_data, &png_size, data, (unsigned)img->width, (unsigned)img->height, &state);
    free(data);

    if (!error) {
        error = lodepng_save_file(png_data, png_size, filename);
    }
    free(png_data);
    lodepng_state_cleanup(&state);

    if (error) {
        fprintf(stderr, "Error: PNG encoding failed: %s\n", lodepng_error_text(error));
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
