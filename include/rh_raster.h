#ifndef RH_RASTER_H
#define RH_RASTER_H

#include "rh_geometry.h"
#include "rh_image.h"

typedef struct {
    int width;
    int height;
    RhImage* image;
    float* zbuffer; // Full screen Z-buffer for simplicity for now

    // Clipping bounds (for bucketing)
    int clip_min_x, clip_max_x;
    int clip_min_y, clip_max_y;
} RhRasterizer;

RhRasterizer* rh_raster_create(int width, int height);
void rh_raster_destroy(RhRasterizer* r);

// Clears image to black and depth to infinity
void rh_raster_clear(RhRasterizer* r);

// Rasterizes a micropolygon grid into the buffer
// Assumes grid positions are in Screen Space (pixels)
void rh_raster_draw_grid(RhRasterizer* r, const RhMicroGrid* g);

#endif // RH_RASTER_H
