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
    RhColor* pixels;     // RGB color (Ci - premultiplied)
    RhColor* opacities;  // RGB opacity (Oi)
} RhImage;

// Create a new image with given dimensions
RhImage* rh_image_create(int width, int height);

// Set the color of a specific pixel
void rh_image_set_pixel(RhImage* img, int x, int y, RhColor color);

// Set the color and opacity of a specific pixel
void rh_image_set_pixel_with_opacity(RhImage* img, int x, int y, RhColor color, RhColor opacity);

// Get the color of a specific pixel (optional, helpful for debugging)
RhColor rh_image_get_pixel(const RhImage* img, int x, int y);

// Save the image to a file in PPM (P3) format
void rh_image_save_ppm(const RhImage* img, const char* filename);

// Save the image to a file in PNG format with alpha channel (RGBA)
void rh_image_save_png(const RhImage* img, const char* filename);

// Save the image to a file in PNG format with specified channel count
// channels: 3 for RGB, 4 for RGBA
void rh_image_save_png_channels(const RhImage* img, const char* filename, int channels);

// Save the image to a file, choosing format by filename extension.
// Supported: .png, .jpg/.jpeg, .bmp, .tga, .ppm
// If channels is 4 (RGBA) but the format doesn't support alpha,
// a warning is printed and the image is saved as RGB.
void rh_image_save(const RhImage* img, const char* filename, int channels);

// Free the image memory
void rh_image_destroy(RhImage* img);

#endif // RH_IMAGE_H
