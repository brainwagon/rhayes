#include "rh_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

RhImage* rh_image_create(int width, int height) {
    RhImage* img = (RhImage*)malloc(sizeof(RhImage));
    if (!img) return NULL;

    img->width = width;
    img->height = height;
    // Calloc to initialize to black (0,0,0)
    img->pixels = (RhColor*)calloc(width * height, sizeof(RhColor));
    
    if (!img->pixels) {
        free(img);
        return NULL;
    }

    return img;
}

void rh_image_set_pixel(RhImage* img, int x, int y, RhColor color) {
    if (x < 0 || x >= img->width || y < 0 || y >= img->height) return;
    img->pixels[y * img->width + x] = color;
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

void rh_image_destroy(RhImage* img) {
    if (img) {
        if (img->pixels) free(img->pixels);
        free(img);
    }
}
