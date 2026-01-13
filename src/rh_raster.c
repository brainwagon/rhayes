#include "rh_raster.h"
#include <stdlib.h>
#include <float.h>
#include <math.h>

// Helper: 2D bounding box for a micropolygon (quad)
typedef struct {
    int min_x, min_y;
    int max_x, max_y;
} Bounds2i;

RhRasterizer* rh_raster_create(int width, int height) {
    RhRasterizer* r = (RhRasterizer*)malloc(sizeof(RhRasterizer));
    if (!r) return NULL;

    r->width = width;
    r->height = height;
    r->image = rh_image_create(width, height);
    r->zbuffer = (float*)malloc(width * height * sizeof(float));

    // Default clip bounds
    r->clip_min_x = 0;
    r->clip_max_x = width - 1;
    r->clip_min_y = 0;
    r->clip_max_y = height - 1;

    if (!r->image || !r->zbuffer) {
        if (r->image) rh_image_destroy(r->image);
        if (r->zbuffer) free(r->zbuffer);
        free(r);
        return NULL;
    }

    return r;
}

void rh_raster_destroy(RhRasterizer* r) {
    if (r) {
        rh_image_destroy(r->image);
        if (r->zbuffer) free(r->zbuffer);
        free(r);
    }
}

void rh_raster_clear(RhRasterizer* r) {
    // Clear image (handled by a loop or similar, rh_image doesn't have clear yet, so do manual)
    int count = r->width * r->height;
    for (int i = 0; i < count; i++) {
        r->image->pixels[i] = (RhColor){0.0f, 0.0f, 0.0f};
        r->zbuffer[i] = FLT_MAX; // Far plane
    }
}

// Simple edge function for rasterization
// Returns positive if point (px, py) is to the right of edge (v0->v1)
static inline float edge_function(RhVec3 v0, RhVec3 v1, float px, float py) {
    return (px - v0.x) * (v1.y - v0.y) - (py - v0.y) * (v1.x - v0.x);
}

// Rasterize a single Micropolygon (Quad)
// Splitting into two triangles for simplicity in this basic implementation
static void draw_micropolygon(RhRasterizer* r, RhVec3 v0, RhVec3 v1, RhVec3 v2, RhVec3 v3, RhColor c0, RhColor c1, RhColor c2, RhColor c3) {
    // We will approximate the micropolygon as a single flat quad or two triangles.
    // REYES often rasterizes the bounding box of the micropolygon if it's small enough (< 1 pixel).
    // For this "functional system", let's implement a standard triangle rasterizer for the quad (v0-v1-v3) and (v1-v2-v3).
    // NOTE: Vertex order assumed:
    // v0 - v1
    // |    |
    // v3 - v2
    
    // To keep it strictly vanilla C and simple, we'll do a bounding box iteration over the quad.
    
    // Compute Bounding Box
    float min_x = rh_min(rh_min(v0.x, v1.x), rh_min(v2.x, v3.x));
    float min_y = rh_min(rh_min(v0.y, v1.y), rh_min(v2.y, v3.y));
    float max_x = rh_max(rh_max(v0.x, v1.x), rh_max(v2.x, v3.x));
    float max_y = rh_max(rh_max(v0.y, v1.y), rh_max(v2.y, v3.y));

    // Clip to screen and bucket
    int x0 = (int)floorf(min_x);
    int y0 = (int)floorf(min_y);
    int x1 = (int)ceilf(max_x);
    int y1 = (int)ceilf(max_y);

    x0 = rh_max(x0, r->clip_min_x);
    y0 = rh_max(y0, r->clip_min_y);
    x1 = rh_min(x1, r->clip_max_x);
    y1 = rh_min(y1, r->clip_max_y);

    // If the micropolygon is roughly 1 pixel or less (classic REYES optimization),
    // we could just shade the pixel. But for robustness, we'll test coverage.
    // We will treat it as two triangles: T1(v0, v1, v3) and T2(v1, v2, v3).
    // Note: Vertex layout assumed: v0(TL), v1(TR), v3(BL), v2(BR) - careful with v2/v3 swap in args
    // The arguments passed are: v0, v1, v2(BR), v3(BL). 
    // Wait, in draw_grid: v2 is i11 (BR), v3 is i01 (BL).
    // So Quad is:
    // v0 - v1
    // |    |
    // v3 - v2
    // Triangles: (v0, v1, v3) and (v1, v2, v3).

    // Precompute edge functions constants or areas could optimize, but we do per pixel for simplicity.
    
    // Triangle 1: v0, v1, v3
    // Area (doubled) of T1. If 0, degenerate.
    float area1 = edge_function(v0, v1, v3.x, v3.y);
    // Triangle 2: v1, v2, v3
    float area2 = edge_function(v1, v2, v3.x, v3.y);

    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            // Pixel center
            float px = x + 0.5f;
            float py = y + 0.5f;

            // --- Triangle 1 (v0, v1, v3) ---
            bool drawn = false;
            if (fabs(area1) > RH_EPSILON) {
                float w0 = edge_function(v1, v3, px, py);
                float w1 = edge_function(v3, v0, px, py);
                float w2 = edge_function(v0, v1, px, py);

                // Check if inside - handle both CCW (area > 0) and CW (area < 0) winding
                // For CCW: all w >= 0 when inside
                // For CW: all w <= 0 when inside
                bool inside = (area1 > 0) ? (w0 >= 0 && w1 >= 0 && w2 >= 0)
                                          : (w0 <= 0 && w1 <= 0 && w2 <= 0);
                if (inside) {
                    float invArea = 1.0f / area1;
                    w0 *= invArea;
                    w1 *= invArea;
                    w2 *= invArea;

                    // Interpolate Z
                    float z = w0 * v0.z + w1 * v1.z + w2 * v3.z;

                    // Z-Test
                    int idx = y * r->width + x;
                    if (z < r->zbuffer[idx]) {
                        r->zbuffer[idx] = z;
                        
                        // Interpolate Color
                        RhColor final_color;
                        final_color.r = w0 * c0.r + w1 * c1.r + w2 * c3.r;
                        final_color.g = w0 * c0.g + w1 * c1.g + w2 * c3.g;
                        final_color.b = w0 * c0.b + w1 * c1.b + w2 * c3.b;
                        
                        rh_image_set_pixel(r->image, x, y, final_color);
                        drawn = true;
                    }
                }
            }

            // --- Triangle 2 (v1, v2, v3) ---
            if (!drawn && fabs(area2) > RH_EPSILON) {
                float u0 = edge_function(v2, v3, px, py); // Weight for v1
                float u1 = edge_function(v3, v1, px, py); // Weight for v2
                float u2 = edge_function(v1, v2, px, py); // Weight for v3

                // Handle both CCW and CW winding
                bool inside2 = (area2 > 0) ? (u0 >= 0 && u1 >= 0 && u2 >= 0)
                                           : (u0 <= 0 && u1 <= 0 && u2 <= 0);
                if (inside2) {
                     float invArea = 1.0f / area2;
                     u0 *= invArea;
                     u1 *= invArea;
                     u2 *= invArea;

                     // Interpolate Z
                     float z = u0 * v1.z + u1 * v2.z + u2 * v3.z;

                     int idx = y * r->width + x;
                     if (z < r->zbuffer[idx]) {
                         r->zbuffer[idx] = z;

                         // Interpolate Color
                         RhColor final_color;
                         final_color.r = u0 * c1.r + u1 * c2.r + u2 * c3.r;
                         final_color.g = u0 * c1.g + u1 * c2.g + u2 * c3.g;
                         final_color.b = u0 * c1.b + u1 * c2.b + u2 * c3.b;
                         
                         rh_image_set_pixel(r->image, x, y, final_color);
                     }
                }
            }
        }
    }
}

void rh_raster_draw_grid(RhRasterizer* r, const RhMicroGrid* g) {
    if (!g || !r) return;

    // Iterate over micropolygons (quads) defined by the grid
    for (int j = 0; j < g->height - 1; j++) {
        for (int i = 0; i < g->width - 1; i++) {
            // Vertex indices
            int i00 = j * g->width + i;
            int i10 = j * g->width + (i + 1);
            int i01 = (j + 1) * g->width + i;
            int i11 = (j + 1) * g->width + (i + 1);

            RhVec3 v0 = g->positions[i00];
            RhVec3 v1 = g->positions[i10]; // top-right
            RhVec3 v2 = g->positions[i11]; // bottom-right
            RhVec3 v3 = g->positions[i01]; // bottom-left
            
            RhColor c0 = g->colors[i00];
            RhColor c1 = g->colors[i10];
            RhColor c2 = g->colors[i11];
            RhColor c3 = g->colors[i01];

            draw_micropolygon(r, v0, v1, v2, v3, c0, c1, c2, c3);
        }
    }
}
