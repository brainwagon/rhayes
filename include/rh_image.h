#ifndef RH_IMAGE_H
#define RH_IMAGE_H

#include "rh_math.h"

// Moving RhColor here if it was not in rh_math.h?
// Wait, typically RhColor is in rh_image.h or rh_math.h.
// Let's check rh_math.h first.
// If it is in rh_image.h, then rh_shader.h must include rh_image.h.

typedef struct {
    float r, g, b;
} RhColor;

typedef struct {
    int width;
    int height;
    RhColor* pixels;
} RhImage;

// Create a new image with given dimensions
RhImage* rh_image_create(int width, int height);

// Set the color of a specific pixel
void rh_image_set_pixel(RhImage* img, int x, int y, RhColor color);

// Get the color of a specific pixel (optional, helpful for debugging)
RhColor rh_image_get_pixel(const RhImage* img, int x, int y);

// Save the image to a file in PPM (P3) format
void rh_image_save_ppm(const RhImage* img, const char* filename);

// Free the image memory
void rh_image_destroy(RhImage* img);

#endif // RH_IMAGE_H
