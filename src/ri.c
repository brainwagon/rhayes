#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include "ri.h"
#include "rh_math.h"
#include "rh_image.h"
#include "rh_geometry.h"
#include "rh_raster.h"
#include "rh_shader.h"
#include "rh_texture.h"
#include "teapot_data.h"

// --- Internal Types ---

#define MAX_STACK_DEPTH 64

typedef struct {
    RhMat4 transform; // Current Transformation Matrix (Object -> World/Camera)
    RhColor color;

    // Basis
    RhMat4 u_basis;
    int u_step;
    RhMat4 v_basis;
    int v_step;

    // Shading
    RhShaderFunc current_surface_shader;
    void* current_shader_params;
    float shading_rate;  // Controls splitting granularity (default 1.0)
} RiAttributeState;

// Standard Basis Matrices
RtMatrix RiBezierBasis = {
    {-1,  3, -3,  1},
    { 3, -6,  3,  0},
    {-3,  3,  0,  0},
    { 1,  0,  0,  0}
};

RtMatrix RiBSplineBasis = {
    {-1.0/6,  3.0/6, -3.0/6,  1.0/6},
    { 3.0/6, -6.0/6,  3.0/6,  0.0/6},
    {-3.0/6,  0.0/6,  3.0/6,  0.0/6},
    { 1.0/6,  4.0/6,  1.0/6,  0.0/6}
};

RtMatrix RiCatmullRomBasis = {
    {-0.5,  1.5, -1.5,  0.5},
    { 1.0, -2.5,  2.0, -0.5},
    {-0.5,  0.0,  0.5,  0.0},
    { 0.0,  1.0,  0.0,  0.0}
};

RtMatrix RiHermiteBasis = {
    { 2, -2,  1,  1},
    {-3,  3, -2, -1},
    { 0,  0,  1,  0},
    { 1,  0,  0,  0}
};

RtMatrix RiPowerBasis = {
    { 1,  0,  0,  0},
    { 0,  1,  0,  0},
    { 0,  0,  1,  0},
    { 0,  0,  0,  1}
};

typedef struct {
    RhPrimitive prim;
    RhMat4 transform; // CTM at creation
    RhColor color;
    RhShaderFunc shader;
    void* shader_params;
    float shading_rate;  // Captured from attribute state
    bool processed;      // Has this primitive been split/diced/shaded?
} RhRenderItem;

// --- Micropolygon Types for Efficient Bucket Rendering ---

typedef enum {
    RH_SHADE_VERTEX,   // Per-vertex shading, interpolate 4 colors
    RH_SHADE_CENTER    // Single flat color per micropolygon
} RhShadingMode;

typedef struct {
    RhVec3 v[4];       // Screen-space vertices (x=pixel, y=pixel, z=depth)
    RhColor c[4];      // Per-vertex colors (for interpolation mode)
    RhColor o[4];      // Per-vertex opacities (for interpolation mode)
    RhColor center;    // Center color (for flat shading mode)
    RhColor center_opacity; // Center opacity (for flat shading mode)
    int min_x, min_y;  // Screen-space bounding box
    int max_x, max_y;
} RhMicropolygon;

typedef struct {
    RhMicropolygon* data;
    int count;
    int capacity;
} RhMicropolygonList;

typedef struct {
    RhRenderItem** items;
    int item_count;
    int item_capacity;
    RhMicropolygonList queued;  // Micropolygons forwarded from earlier buckets
} RhBucket;

// Internal Light Structure
typedef struct {
    char type[32]; // "distantlight", "pointlight", "ambientlight", "spotlight"
    RhVec3 position; // "from"
    RhVec3 direction; // Normalized (to - from)
    RhColor color;
    float intensity;
    RhMat4 transform; // Transform at creation time
    // Spotlight parameters
    float coneangle;        // Half-angle of full illumination cone (degrees)
    float conedeltaangle;   // Penumbra width (degrees)
    float beamdistribution; // Cosine power for beam shape
} RhLight;

#define MAX_LIGHTS 8

// Grid size constants for adaptive splitting/dicing
#define MAX_GRID_SIZE 16
#define MAX_GRID_AREA (MAX_GRID_SIZE * MAX_GRID_SIZE)  // 256 pixels
#define MIN_GRID_SIZE 2
#define MAX_SPLIT_DEPTH 12  // Safety limit to prevent infinite recursion
#define MIN_SPLIT_DEPTH 3   // Minimum splits before considering area

typedef struct {
    RhPrimitive prim;
    RhMat4 transform;
} RhObjectItem;

typedef struct {
    RhObjectItem* items;
    int count;
    int capacity;
    RhMat4 inv_transform;
} RhObject;

typedef struct {
    // Options
    int xres, yres;
    char display_name[256];
    RhMat4 projection;

    // Supersampling
    int pixel_samples_x, pixel_samples_y;
    RtFilterFunc pixel_filter;
    float filter_width_x, filter_width_y;
    int ss_xres, ss_yres;  // Supersampled resolution (set in RiWorldBegin)

    // Depth of Field
    float dof_fstop;
    float dof_focallength;
    float dof_focaldistance;
    
    // State Stack
    RiAttributeState stack[MAX_STACK_DEPTH];
    int stack_ptr;

    // Rendering Context
    RhRasterizer* raster;
    RhMat4 view_matrix; // Current View Matrix
    bool world_active;

    // Objects
    RhObject* current_obj;
    RhObject** objects;
    int objects_count;
    int objects_capacity;

    // Buckets
    RhBucket* buckets;
    int num_buckets_x;
    int num_buckets_y;
    int bucket_size;
    
    // Global list of all items for cleanup
    RhRenderItem** all_items;
    int all_items_count;
    int all_items_capacity;

    // Lights
    RhLight lights[MAX_LIGHTS];
    int num_lights;

    // Grid counter for diagnostic shaders (monotonically increasing ID)
    int grid_counter;

    // Shading mode for micropolygon rendering
    RhShadingMode shading_mode;

    // Rendering statistics
    struct {
        int primitives_by_type[9];  // Indexed by RhPrimitiveType
        int grids_by_size[17];      // Index 0 unused, 1-16 for grid sizes
        int total_grids;
        int total_micropolygons;
        int primitives_processed;   // Unique primitives (not per-bucket)
    } stats;

    // Statistics output options (set via RiOption "statistics")
    struct {
        int endofframe;             // 0=off, non-zero=on (default 0)
        char filename[256];         // Text output file (empty = stderr)
        char jsonfilename[256];     // JSON output file (empty = none)
    } stats_options;
} RiContextData;

static RiContextData* g_ctx = NULL;

// Forward declarations of internal helpers
static void ri_process_item_recursive(RhRenderItem* item, int depth, RhMicropolygonList* out_mpolys, RhShadingMode mode);
static void ri_add_to_buckets(const RhPrimitive* p, const RhMat4* transform, const RhColor* color);
static void ri_add_geometry(RhPrimitive* p);

static RiAttributeState* curr() {
    return &g_ctx->stack[g_ctx->stack_ptr];
}

static RhRenderItem* ri_render_item_create(const RhPrimitive* p, const RhMat4* transform, const RhColor* color) {
    RhRenderItem* item = (RhRenderItem*)malloc(sizeof(RhRenderItem));
    item->prim = *p;
    // Deep copy if polygon
    if (p->type == RH_PRIM_POLYGON) {
        item->prim.data.polygon.vertices = (RhVec3*)malloc(p->data.polygon.count * sizeof(RhVec3));
        memcpy(item->prim.data.polygon.vertices, p->data.polygon.vertices, p->data.polygon.count * sizeof(RhVec3));
    }
    item->transform = *transform;
    item->color = *color;
    item->shader = curr()->current_surface_shader;
    item->shader_params = curr()->current_shader_params;
    item->shading_rate = curr()->shading_rate;
    item->processed = false;

    // Add to global list for cleanup
    if (g_ctx->all_items_count >= g_ctx->all_items_capacity) {
        g_ctx->all_items_capacity = g_ctx->all_items_capacity == 0 ? 16 : g_ctx->all_items_capacity * 2;
        g_ctx->all_items = (RhRenderItem**)realloc(g_ctx->all_items, g_ctx->all_items_capacity * sizeof(RhRenderItem*));
    }
    g_ctx->all_items[g_ctx->all_items_count++] = item;
    
    return item;
}

static void ri_render_item_destroy(RhRenderItem* item) {
    if (item) {
        rh_prim_free_data(&item->prim);
        free(item);
    }
}

// --- Micropolygon List Helpers ---

static void ri_mpoly_list_init(RhMicropolygonList* list) {
    list->data = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void ri_mpoly_list_push(RhMicropolygonList* list, const RhMicropolygon* mpoly) {
    if (list->count >= list->capacity) {
        list->capacity = list->capacity == 0 ? 64 : list->capacity * 2;
        list->data = (RhMicropolygon*)realloc(list->data, list->capacity * sizeof(RhMicropolygon));
    }
    list->data[list->count++] = *mpoly;
}

static void ri_mpoly_list_clear(RhMicropolygonList* list) {
    list->count = 0;
    // Keep capacity and data allocated for reuse
}

static void ri_mpoly_list_free(RhMicropolygonList* list) {
    free(list->data);
    list->data = NULL;
    list->count = 0;
    list->capacity = 0;
}

// Convert a shaded grid to micropolygons
// Grid has width*height vertices = (width-1)*(height-1) micropolygon quads
static void ri_grid_to_mpolys(const RhMicroGrid* grid, RhMicropolygonList* out_list, RhShadingMode mode) {
    int w = grid->width;
    int h = grid->height;

    // Track micropolygon statistics
    int mpoly_count = (w - 1) * (h - 1);
    g_ctx->stats.total_micropolygons += mpoly_count;

    for (int j = 0; j < h - 1; j++) {
        for (int i = 0; i < w - 1; i++) {
            // Vertex indices for quad corners
            int i00 = j * w + i;           // top-left
            int i10 = j * w + (i + 1);     // top-right
            int i01 = (j + 1) * w + i;     // bottom-left
            int i11 = (j + 1) * w + (i + 1); // bottom-right

            RhMicropolygon mpoly;

            // Copy screen-space positions (v[0]=TL, v[1]=TR, v[2]=BR, v[3]=BL)
            mpoly.v[0] = grid->positions[i00];
            mpoly.v[1] = grid->positions[i10];
            mpoly.v[2] = grid->positions[i11];
            mpoly.v[3] = grid->positions[i01];

            // Copy per-vertex colors
            mpoly.c[0] = grid->colors[i00];
            mpoly.c[1] = grid->colors[i10];
            mpoly.c[2] = grid->colors[i11];
            mpoly.c[3] = grid->colors[i01];

            // Copy per-vertex opacities
            mpoly.o[0] = grid->opacities[i00];
            mpoly.o[1] = grid->opacities[i10];
            mpoly.o[2] = grid->opacities[i11];
            mpoly.o[3] = grid->opacities[i01];

            // Compute center color (average of 4 corners)
            mpoly.center.r = (mpoly.c[0].r + mpoly.c[1].r + mpoly.c[2].r + mpoly.c[3].r) * 0.25f;
            mpoly.center.g = (mpoly.c[0].g + mpoly.c[1].g + mpoly.c[2].g + mpoly.c[3].g) * 0.25f;
            mpoly.center.b = (mpoly.c[0].b + mpoly.c[1].b + mpoly.c[2].b + mpoly.c[3].b) * 0.25f;

            // Compute center opacity (average of 4 corners)
            mpoly.center_opacity.r = (mpoly.o[0].r + mpoly.o[1].r + mpoly.o[2].r + mpoly.o[3].r) * 0.25f;
            mpoly.center_opacity.g = (mpoly.o[0].g + mpoly.o[1].g + mpoly.o[2].g + mpoly.o[3].g) * 0.25f;
            mpoly.center_opacity.b = (mpoly.o[0].b + mpoly.o[1].b + mpoly.o[2].b + mpoly.o[3].b) * 0.25f;

            // Compute screen-space bounding box
            float min_x = mpoly.v[0].x;
            float max_x = mpoly.v[0].x;
            float min_y = mpoly.v[0].y;
            float max_y = mpoly.v[0].y;

            for (int k = 1; k < 4; k++) {
                if (mpoly.v[k].x < min_x) min_x = mpoly.v[k].x;
                if (mpoly.v[k].x > max_x) max_x = mpoly.v[k].x;
                if (mpoly.v[k].y < min_y) min_y = mpoly.v[k].y;
                if (mpoly.v[k].y > max_y) max_y = mpoly.v[k].y;
            }

            // Store as integer bounds (floor min, ceil max for conservative coverage)
            mpoly.min_x = (int)floorf(min_x);
            mpoly.min_y = (int)floorf(min_y);
            mpoly.max_x = (int)ceilf(max_x);
            mpoly.max_y = (int)ceilf(max_y);

            ri_mpoly_list_push(out_list, &mpoly);

            (void)mode; // Mode affects sampling, not conversion
        }
    }
}

// Edge function for rasterization - returns positive if (px,py) is to the right of edge (v0->v1)
static inline float ri_edge_function(RhVec3 v0, RhVec3 v1, float px, float py) {
    return (px - v0.x) * (v1.y - v0.y) - (py - v0.y) * (v1.x - v0.x);
}

// Rasterize a single micropolygon within the given clip bounds
// mpoly vertex layout: v[0]=TL, v[1]=TR, v[2]=BR, v[3]=BL
// Triangles: T1(v0, v1, v3) and T2(v1, v2, v3)
static void ri_sample_mpoly(
    RhRasterizer* r,
    const RhMicropolygon* mpoly,
    int clip_min_x, int clip_min_y,
    int clip_max_x, int clip_max_y,
    RhShadingMode mode
) {
    // Compute clipped bounding box
    int x0 = rh_max(mpoly->min_x, clip_min_x);
    int y0 = rh_max(mpoly->min_y, clip_min_y);
    int x1 = rh_min(mpoly->max_x, clip_max_x);
    int y1 = rh_min(mpoly->max_y, clip_max_y);

    // Early out if completely clipped
    if (x0 > x1 || y0 > y1) return;

    // Get vertex references
    RhVec3 v0 = mpoly->v[0]; // TL
    RhVec3 v1 = mpoly->v[1]; // TR
    RhVec3 v2 = mpoly->v[2]; // BR
    RhVec3 v3 = mpoly->v[3]; // BL

    // Compute triangle areas
    float area1 = ri_edge_function(v0, v1, v3.x, v3.y); // Triangle 1: v0, v1, v3
    float area2 = ri_edge_function(v1, v2, v3.x, v3.y); // Triangle 2: v1, v2, v3

    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float px = x + 0.5f;
            float py = y + 0.5f;

            // --- Triangle 1 (v0, v1, v3) ---
            bool drawn = false;
            if (fabsf(area1) > 1e-6f) {
                float w0 = ri_edge_function(v1, v3, px, py);
                float w1 = ri_edge_function(v3, v0, px, py);
                float w2 = ri_edge_function(v0, v1, px, py);

                bool inside = (area1 > 0) ? (w0 >= 0 && w1 >= 0 && w2 >= 0)
                                          : (w0 <= 0 && w1 <= 0 && w2 <= 0);
                if (inside) {
                    float invArea = 1.0f / area1;
                    w0 *= invArea;
                    w1 *= invArea;
                    w2 *= invArea;

                    float z = w0 * v0.z + w1 * v1.z + w2 * v3.z;

                    int idx = y * r->width + x;
                    if (z < r->zbuffer[idx]) {
                        r->zbuffer[idx] = z;

                        RhColor final_color;
                        RhColor final_opacity;
                        if (mode == RH_SHADE_CENTER) {
                            final_color = mpoly->center;
                            final_opacity = mpoly->center_opacity;
                        } else {
                            // Per-vertex interpolation
                            final_color.r = w0 * mpoly->c[0].r + w1 * mpoly->c[1].r + w2 * mpoly->c[3].r;
                            final_color.g = w0 * mpoly->c[0].g + w1 * mpoly->c[1].g + w2 * mpoly->c[3].g;
                            final_color.b = w0 * mpoly->c[0].b + w1 * mpoly->c[1].b + w2 * mpoly->c[3].b;

                            final_opacity.r = w0 * mpoly->o[0].r + w1 * mpoly->o[1].r + w2 * mpoly->o[3].r;
                            final_opacity.g = w0 * mpoly->o[0].g + w1 * mpoly->o[1].g + w2 * mpoly->o[3].g;
                            final_opacity.b = w0 * mpoly->o[0].b + w1 * mpoly->o[1].b + w2 * mpoly->o[3].b;
                        }

                        rh_image_set_pixel_with_opacity(r->image, x, y, final_color, final_opacity);
                        drawn = true;
                    }
                }
            }

            // --- Triangle 2 (v1, v2, v3) ---
            if (!drawn && fabsf(area2) > 1e-6f) {
                float u0 = ri_edge_function(v2, v3, px, py);
                float u1 = ri_edge_function(v3, v1, px, py);
                float u2 = ri_edge_function(v1, v2, px, py);

                bool inside2 = (area2 > 0) ? (u0 >= 0 && u1 >= 0 && u2 >= 0)
                                           : (u0 <= 0 && u1 <= 0 && u2 <= 0);
                if (inside2) {
                    float invArea = 1.0f / area2;
                    u0 *= invArea;
                    u1 *= invArea;
                    u2 *= invArea;

                    float z = u0 * v1.z + u1 * v2.z + u2 * v3.z;

                    int idx = y * r->width + x;
                    if (z < r->zbuffer[idx]) {
                        r->zbuffer[idx] = z;

                        RhColor final_color;
                        RhColor final_opacity;
                        if (mode == RH_SHADE_CENTER) {
                            final_color = mpoly->center;
                            final_opacity = mpoly->center_opacity;
                        } else {
                            // Per-vertex interpolation
                            final_color.r = u0 * mpoly->c[1].r + u1 * mpoly->c[2].r + u2 * mpoly->c[3].r;
                            final_color.g = u0 * mpoly->c[1].g + u1 * mpoly->c[2].g + u2 * mpoly->c[3].g;
                            final_color.b = u0 * mpoly->c[1].b + u1 * mpoly->c[2].b + u2 * mpoly->c[3].b;

                            final_opacity.r = u0 * mpoly->o[1].r + u1 * mpoly->o[2].r + u2 * mpoly->o[3].r;
                            final_opacity.g = u0 * mpoly->o[1].g + u1 * mpoly->o[2].g + u2 * mpoly->o[3].g;
                            final_opacity.b = u0 * mpoly->o[1].b + u1 * mpoly->o[2].b + u2 * mpoly->o[3].b;
                        }

                        rh_image_set_pixel_with_opacity(r->image, x, y, final_color, final_opacity);
                    }
                }
            }
        }
    }
}

static inline int ri_clamp_int(int val, int min_val, int max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

// Compute screen-space bounding box area for a primitive
// Samples actual surface points within the current parametric domain
static float ri_compute_screen_area(const RhPrimitive* p, const RhMat4* mvp) {
    float min_x = 1e30f, max_x = -1e30f;
    float min_y = 1e30f, max_y = -1e30f;

    // Sample 9 points on the surface (3x3 grid within parametric domain)
    for (int j = 0; j <= 2; j++) {
        float v = p->v_min + (p->v_max - p->v_min) * (j / 2.0f);
        for (int i = 0; i <= 2; i++) {
            float u = p->u_min + (p->u_max - p->u_min) * (i / 2.0f);

            // Evaluate surface point at (u,v)
            RhVec3 pos_obj = rh_prim_eval_point(p, u, v);

            // Project to screen space
            RhVec3 p_ndc = rh_mat4_mul_point(*mvp, pos_obj);
            float rx = (p_ndc.x + 1.0f) * 0.5f * g_ctx->ss_xres;
            float ry = (1.0f - (p_ndc.y + 1.0f) * 0.5f) * g_ctx->ss_yres;

            if (rx < min_x) min_x = rx;
            if (rx > max_x) max_x = rx;
            if (ry < min_y) min_y = ry;
            if (ry > max_y) max_y = ry;
        }
    }

    float width = max_x - min_x;
    float height = max_y - min_y;

    // Handle degenerate cases (behind camera, etc.)
    if (width < 0.0f) width = 0.0f;
    if (height < 0.0f) height = 0.0f;

    return width * height;
}

static void ri_add_to_buckets(const RhPrimitive* p, const RhMat4* transform, const RhColor* color) {
    if (!g_ctx || !g_ctx->world_active) return;

    RhRenderItem* item = ri_render_item_create(p, transform, color);

    // Track primitive statistics
    if (p->type >= 0 && p->type < 9) {
        g_ctx->stats.primitives_by_type[p->type]++;
    }

    // Calculate Screen Bounds
    RhBounds3 obj_bounds = rh_prim_bound(p);
    RhMat4 mvp = rh_mat4_mul(g_ctx->projection, rh_mat4_mul(g_ctx->view_matrix, item->transform));

    RhVec3 corners[8];
    corners[0] = rh_vec3_create(obj_bounds.min.x, obj_bounds.min.y, obj_bounds.min.z);
    corners[1] = rh_vec3_create(obj_bounds.max.x, obj_bounds.min.y, obj_bounds.min.z);
    corners[2] = rh_vec3_create(obj_bounds.min.x, obj_bounds.max.y, obj_bounds.min.z);
    corners[3] = rh_vec3_create(obj_bounds.max.x, obj_bounds.max.y, obj_bounds.min.z);
    corners[4] = rh_vec3_create(obj_bounds.min.x, obj_bounds.min.y, obj_bounds.max.z);
    corners[5] = rh_vec3_create(obj_bounds.max.x, obj_bounds.min.y, obj_bounds.max.z);
    corners[6] = rh_vec3_create(obj_bounds.min.x, obj_bounds.max.y, obj_bounds.max.z);
    corners[7] = rh_vec3_create(obj_bounds.max.x, obj_bounds.max.y, obj_bounds.max.z);

    float min_x = (float)g_ctx->ss_xres, max_x = 0;
    float min_y = (float)g_ctx->ss_yres, max_y = 0;

    for (int i = 0; i < 8; i++) {
        RhVec3 p_ndc = rh_mat4_mul_point(mvp, corners[i]);
        float rx = (p_ndc.x + 1.0f) * 0.5f * g_ctx->ss_xres;
        float ry = (1.0f - (p_ndc.y + 1.0f) * 0.5f) * g_ctx->ss_yres;
        if (rx < min_x) min_x = rx; 
        if (rx > max_x) max_x = rx;
        if (ry < min_y) min_y = ry; 
        if (ry > max_y) max_y = ry;
    }

    int b_min_x = (int)floorf(min_x / g_ctx->bucket_size);
    int b_max_x = (int)floorf(max_x / g_ctx->bucket_size);
    int b_min_y = (int)floorf(min_y / g_ctx->bucket_size);
    int b_max_y = (int)floorf(max_y / g_ctx->bucket_size);

    // Clamp to bucket grid
    if (b_min_x < 0) b_min_x = 0;
    if (b_max_x >= g_ctx->num_buckets_x) b_max_x = g_ctx->num_buckets_x - 1;
    if (b_min_y < 0) b_min_y = 0;
    if (b_max_y >= g_ctx->num_buckets_y) b_max_y = g_ctx->num_buckets_y - 1;

    for (int y = b_min_y; y <= b_max_y; y++) {
        for (int x = b_min_x; x <= b_max_x; x++) {
            RhBucket* b = &g_ctx->buckets[y * g_ctx->num_buckets_x + x];
            if (b->item_count >= b->item_capacity) {
                b->item_capacity = b->item_capacity == 0 ? 4 : b->item_capacity * 2;
                b->items = (RhRenderItem**)realloc(b->items, b->item_capacity * sizeof(RhRenderItem*));
            }
            b->items[b->item_count++] = item;
        }
    }
}

static void ri_add_geometry(RhPrimitive* p) {
    if (g_ctx->current_obj) {
        RhObject* obj = g_ctx->current_obj;
        if (obj->count >= obj->capacity) {
            obj->capacity = obj->capacity == 0 ? 4 : obj->capacity * 2;
            obj->items = (RhObjectItem*)realloc(obj->items, obj->capacity * sizeof(RhObjectItem));
        }
        RhObjectItem* item = &obj->items[obj->count++];
        item->prim = *p;
        if (p->type == RH_PRIM_POLYGON) {
            item->prim.data.polygon.vertices = (RhVec3*)malloc(p->data.polygon.count * sizeof(RhVec3));
            memcpy(item->prim.data.polygon.vertices, p->data.polygon.vertices, p->data.polygon.count * sizeof(RhVec3));
        }
        item->transform = curr()->transform;
    } else {
        ri_add_to_buckets(p, &curr()->transform, &curr()->color);
    }
}

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

// --- 1. Relationship to external world ---

void RiBegin(RtToken name) {
    if (g_ctx) return;
    g_ctx = (RiContextData*)calloc(1, sizeof(RiContextData));
    
    // Defaults
    g_ctx->xres = 800;
    g_ctx->yres = 600;
    strcpy(g_ctx->display_name, "ri_output.ppm");
    
    // Default State
    g_ctx->stack_ptr = 0;
    g_ctx->stack[0].transform = rh_mat4_identity();
    g_ctx->stack[0].color = (RhColor){1.0f, 1.0f, 1.0f};
    
    // Default Basis (Bezier)
    RhMat4 bezier_m;
    memcpy(bezier_m.m, RiBezierBasis, sizeof(RtMatrix));
    g_ctx->stack[0].u_basis = bezier_m;
    g_ctx->stack[0].u_step = 3;
    g_ctx->stack[0].v_basis = bezier_m;
    g_ctx->stack[0].v_step = 3;
    
    // Default Shader (Plastic-like default or constant?)
    // RISpec says implementation dependent default.
    g_ctx->stack[0].current_surface_shader = rh_shader_surface_plastic; // Let's use plastic
    g_ctx->stack[0].current_shader_params = NULL; // Defaults
    g_ctx->stack[0].shading_rate = 1.0f; // Default ShadingRate

    // Default Projection (Perspective)
    g_ctx->projection = rh_mat4_identity();

    g_ctx->num_lights = 0;
    g_ctx->bucket_size = 32;
    g_ctx->shading_mode = RH_SHADE_VERTEX;  // Default: per-vertex shading with interpolation

    // Default Supersampling (1x1 = no supersampling)
    g_ctx->pixel_samples_x = 1;
    g_ctx->pixel_samples_y = 1;
    g_ctx->pixel_filter = RiBoxFilter;
    g_ctx->filter_width_x = 1.0f;
    g_ctx->filter_width_y = 1.0f;

    // Default DoF (pinhole camera - no depth of field)
    g_ctx->dof_fstop = RI_INFINITY;
    g_ctx->dof_focallength = 1.0f;
    g_ctx->dof_focaldistance = 1.0f;

    (void)name;
}

void RiEnd(void) {
    if (g_ctx) {
        if (g_ctx->raster) rh_raster_destroy(g_ctx->raster);
        if (g_ctx->all_items) {
            for(int i=0; i<g_ctx->all_items_count; i++) ri_render_item_destroy(g_ctx->all_items[i]);
            free(g_ctx->all_items);
        }
        if (g_ctx->objects) {
            for(int i=0; i<g_ctx->objects_count; i++) {
                RhObject* obj = g_ctx->objects[i];
                for(int j=0; j<obj->count; j++) rh_prim_free_data(&obj->items[j].prim);
                free(obj->items);
                free(obj);
            }
            free(g_ctx->objects);
        }
        free(g_ctx);
        g_ctx = NULL;
    }
}

RtPointer RiGetContext(void) {
    return (RtPointer)g_ctx;
}

void RiContext(RtPointer ctx) {
    g_ctx = (RiContextData*)ctx;
}

// --- 2. Options ---

void RiOption(RtToken name, ...) {
    if (!g_ctx) return;

    va_list ap;
    va_start(ap, name);

    if (strcmp(name, "statistics") == 0) {
        // Parse statistics options
        RtToken param;
        while ((param = va_arg(ap, RtToken)) != RI_NULL) {
            if (strcmp(param, "endofframe") == 0) {
                RtInt* val = va_arg(ap, RtInt*);
                g_ctx->stats_options.endofframe = *val;
            } else if (strcmp(param, "filename") == 0) {
                RtToken filename = va_arg(ap, RtToken);
                if (filename) {
                    strncpy(g_ctx->stats_options.filename, filename, 255);
                    g_ctx->stats_options.filename[255] = '\0';
                } else {
                    g_ctx->stats_options.filename[0] = '\0';
                }
            } else if (strcmp(param, "jsonfilename") == 0) {
                RtToken filename = va_arg(ap, RtToken);
                if (filename) {
                    strncpy(g_ctx->stats_options.jsonfilename, filename, 255);
                    g_ctx->stats_options.jsonfilename[255] = '\0';
                } else {
                    g_ctx->stats_options.jsonfilename[0] = '\0';
                }
            }
        }
    }

    va_end(ap);
}

void RiFormat(RtInt xresolution, RtInt yresolution, RtFloat pixelaspectratio) {
    if (!g_ctx) return;
    g_ctx->xres = xresolution;
    g_ctx->yres = yresolution;
    (void)pixelaspectratio;
}

void RiDisplay(RtToken name, RtToken type, RtToken mode, ...) {
    if (!g_ctx) return;
    if (name) strncpy(g_ctx->display_name, name, 255);
    (void)type; (void)mode;
}

void RiPixelSamples(RtFloat xsamples, RtFloat ysamples) {
    if (!g_ctx) return;
    g_ctx->pixel_samples_x = (int)rh_max(1.0f, xsamples);
    g_ctx->pixel_samples_y = (int)rh_max(1.0f, ysamples);
}

void RiPixelFilter(RtFilterFunc filterfunc, RtFloat xwidth, RtFloat ywidth) {
    if (!g_ctx) return;
    g_ctx->pixel_filter = filterfunc;
    g_ctx->filter_width_x = xwidth;
    g_ctx->filter_width_y = ywidth;
}

void RiDepthOfField(RtFloat fstop, RtFloat focallength, RtFloat focaldistance) {
    if (!g_ctx) return;
    g_ctx->dof_fstop = fstop;
    g_ctx->dof_focallength = focallength;
    g_ctx->dof_focaldistance = focaldistance;
}

void RiProjection(RtToken name, ...) {
    if (!g_ctx) return;
    // For MVP, we assume "perspective" and hardcode/parse FOV.
    // Let's assume va_list contains "fov" if name is perspective.
    // For now, hardcode a standard perspective matrix for simplicity or parse simple args.
    
    va_list ap;
    va_start(ap, name);
    
    if (strcmp(name, "perspective") == 0) {
        // RenderMan standard: RiProjection("perspective", "fov", &fov, RI_NULL);
        // We'll cheat and make a standard one.
        // float fov = 45.0f; // Default
        
        // Construct Projection Matrix
        float fov_rad = 45.0f * (RH_PI / 180.0f);
        float aspect = (float)g_ctx->xres / (float)g_ctx->yres;
        float f = 1.0f / tanf(fov_rad / 2.0f);
        float zNear = 0.1f;
        float zFar = 100.0f;
        
        g_ctx->projection = rh_mat4_identity();
        g_ctx->projection.m[0][0] = f / aspect;
        g_ctx->projection.m[1][1] = f;
        g_ctx->projection.m[2][2] = (zFar + zNear) / (zNear - zFar);
        g_ctx->projection.m[2][3] = (2 * zFar * zNear) / (zNear - zFar);
        g_ctx->projection.m[3][2] = -1.0f;
        g_ctx->projection.m[3][3] = 0.0f;
    } else if (strcmp(name, "orthographic") == 0) {
        // Identity / Scale
        g_ctx->projection = rh_mat4_identity();
    }
    
    va_end(ap);
}

// --- 3. Graphics State ---

void RiTransformBegin(void) {
    if (!g_ctx || g_ctx->stack_ptr >= MAX_STACK_DEPTH - 1) return;
    int p = g_ctx->stack_ptr;
    g_ctx->stack_ptr++;
    // Copy entire state (including shaders) for simplicity
    // In strict RenderMan, TransformBegin only saves CTM, but we need
    // shaders to be available for geometry created in this scope
    g_ctx->stack[g_ctx->stack_ptr] = g_ctx->stack[p];
}

void RiTransformEnd(void) {
    if (!g_ctx || g_ctx->stack_ptr <= 0) return;
    // If strict compliance: only restore transformation.
    // RhMat4 t = g_ctx->stack[g_ctx->stack_ptr - 1].transform; // Previous transform
    
    // Restore transform (pop logic usually handles this just by decr pointer)
    // But we need to ensure we didn't accidentally pop attributes if this was TransformEnd.
    // Standard says TransformBegin/End saves "Current Transformation".
    // AttributeBegin/End saves everything.
    // To do this right with one stack, we just use AttributeBegin/End logic for now.
    g_ctx->stack_ptr--;
}

void RiAttributeBegin(void) {
    if (!g_ctx || g_ctx->stack_ptr >= MAX_STACK_DEPTH - 1) return;
    int p = g_ctx->stack_ptr;
    g_ctx->stack_ptr++;
    g_ctx->stack[g_ctx->stack_ptr] = g_ctx->stack[p];
}

void RiAttributeEnd(void) {
    if (!g_ctx || g_ctx->stack_ptr <= 0) return;
    g_ctx->stack_ptr--;
}

// --- 4. Transformations ---

void RiIdentity(void) {
    if (!g_ctx) return;
    curr()->transform = rh_mat4_identity();
}

void RiTransform(RtMatrix transform) {
    if (!g_ctx) return;
    // Copy RtMatrix to RhMat4
    RhMat4 m;
    memcpy(m.m, transform, sizeof(RtMatrix));
    curr()->transform = m;
}

void RiConcatTransform(RtMatrix transform) {
    if (!g_ctx) return;
    RhMat4 m;
    memcpy(m.m, transform, sizeof(RtMatrix));
    // Standard RenderMan: CTM = CTM * transform
    curr()->transform = rh_mat4_mul(curr()->transform, m);
}

void RiTranslate(RtFloat dx, RtFloat dy, RtFloat dz) {
    if (!g_ctx) return;
    RhMat4 m = rh_mat4_translate(dx, dy, dz);
    curr()->transform = rh_mat4_mul(curr()->transform, m);
}

void RiRotate(RtFloat angle, RtFloat dx, RtFloat dy, RtFloat dz) {
    if (!g_ctx) return;
    
    float rad = angle * (RH_PI / 180.0f);
    float c = cosf(rad);
    float s = sinf(rad);
    float inv_c = 1.0f - c;
    
    // Normalize axis
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len > 0) { dx/=len; dy/=len; dz/=len; }
    
    RhMat4 m = rh_mat4_identity();
    m.m[0][0] = dx*dx*inv_c + c;    m.m[0][1] = dx*dy*inv_c - dz*s; m.m[0][2] = dx*dz*inv_c + dy*s;
    m.m[1][0] = dy*dx*inv_c + dz*s; m.m[1][1] = dy*dy*inv_c + c;    m.m[1][2] = dy*dz*inv_c - dx*s;
    m.m[2][0] = dz*dx*inv_c - dy*s; m.m[2][1] = dz*dy*inv_c + dx*s; m.m[2][2] = dz*dz*inv_c + c;
    
    curr()->transform = rh_mat4_mul(curr()->transform, m);
}

void RiScale(RtFloat sx, RtFloat sy, RtFloat sz) {
    if (!g_ctx) return;
    RhMat4 m = rh_mat4_scale(sx, sy, sz);
    curr()->transform = rh_mat4_mul(curr()->transform, m);
}

void RiBasis(RtMatrix ubasis, RtInt ustep, RtMatrix vbasis, RtInt vstep) {
    if (!g_ctx) return;
    
    // Copy RtMatrix to RhMat4
    RhMat4 um, vm;
    memcpy(um.m, ubasis, sizeof(RtMatrix));
    memcpy(vm.m, vbasis, sizeof(RtMatrix));
    
    curr()->u_basis = um;
    curr()->u_step = ustep;
    curr()->v_basis = vm;
    curr()->v_step = vstep;
}

// --- 5. Attributes ---

void RiColor(RtColor color) {
    if (!g_ctx) return;
    curr()->color.r = color[0];
    curr()->color.g = color[1];
    curr()->color.b = color[2];
}

void RiOpacity(RtColor color) {
    // Not implemented in rasterizer yet
    (void)color;
}

void RiShadingRate(RtFloat size) {
    if (!g_ctx) return;
    curr()->shading_rate = size;
}

// --- 6. Scene Structure ---

void RiWorldBegin(void) {
    if (!g_ctx) return;

    // Compute and store supersampled resolution
    g_ctx->ss_xres = g_ctx->xres * g_ctx->pixel_samples_x;
    g_ctx->ss_yres = g_ctx->yres * g_ctx->pixel_samples_y;

    // 1. Initialize Rasterizer at supersampled resolution
    if (g_ctx->raster) rh_raster_destroy(g_ctx->raster);
    g_ctx->raster = rh_raster_create(g_ctx->ss_xres, g_ctx->ss_yres);
    rh_raster_clear(g_ctx->raster);

    // 2. Set World State
    g_ctx->world_active = true;

    // 3. Current Transform becomes Identity (World Space)
    g_ctx->view_matrix = rh_mat4_inverse(curr()->transform);

    // 4. Initialize Buckets (using supersampled resolution)
    g_ctx->num_buckets_x = (g_ctx->ss_xres + g_ctx->bucket_size - 1) / g_ctx->bucket_size;
    g_ctx->num_buckets_y = (g_ctx->ss_yres + g_ctx->bucket_size - 1) / g_ctx->bucket_size;
    int count = g_ctx->num_buckets_x * g_ctx->num_buckets_y;
    g_ctx->buckets = (RhBucket*)calloc(count, sizeof(RhBucket));

    g_ctx->all_items_count = 0;
    g_ctx->grid_counter = 0;  // Reset grid counter for diagnostic shaders

    // Reset CTM to Identity for World block
    RiAttributeBegin(); // Push a new state for World
    RiIdentity();
}

void RiWorldEnd(void) {
    if (!g_ctx) return;

    int ss_xres = g_ctx->xres * g_ctx->pixel_samples_x;
    int ss_yres = g_ctx->yres * g_ctx->pixel_samples_y;

    // Render Buckets - Efficient algorithm: process each primitive only once
    RhShadingMode mode = g_ctx->shading_mode;

    for (int y = 0; y < g_ctx->num_buckets_y; y++) {
        for (int x = 0; x < g_ctx->num_buckets_x; x++) {
            RhBucket* b = &g_ctx->buckets[y * g_ctx->num_buckets_x + x];

            // Compute clip bounds for this bucket
            int clip_min_x = x * g_ctx->bucket_size;
            int clip_max_x = rh_min((x + 1) * g_ctx->bucket_size - 1, ss_xres - 1);
            int clip_min_y = y * g_ctx->bucket_size;
            int clip_max_y = rh_min((y + 1) * g_ctx->bucket_size - 1, ss_yres - 1);

            // Phase 1: Sample queued micropolygons from earlier buckets
            for (int qi = 0; qi < b->queued.count; qi++) {
                ri_sample_mpoly(g_ctx->raster, &b->queued.data[qi],
                               clip_min_x, clip_min_y, clip_max_x, clip_max_y, mode);
            }
            ri_mpoly_list_clear(&b->queued);

            // Phase 2: Process unprocessed primitives
            for (int i = 0; i < b->item_count; i++) {
                RhRenderItem* item = b->items[i];
                if (item->processed) continue;  // Already processed by earlier bucket
                item->processed = true;
                g_ctx->stats.primitives_processed++;

                // Generate all micropolygons for this primitive (done once)
                RhMicropolygonList mpolys;
                ri_mpoly_list_init(&mpolys);
                ri_process_item_recursive(item, 0, &mpolys, mode);

                // Distribute micropolygons to buckets
                for (int mi = 0; mi < mpolys.count; mi++) {
                    RhMicropolygon* mpoly = &mpolys.data[mi];

                    // Compute which buckets this micropolygon overlaps
                    int bx_min = mpoly->min_x / g_ctx->bucket_size;
                    int bx_max = mpoly->max_x / g_ctx->bucket_size;
                    int by_min = mpoly->min_y / g_ctx->bucket_size;
                    int by_max = mpoly->max_y / g_ctx->bucket_size;

                    // Clamp to valid bucket range
                    bx_min = ri_clamp_int(bx_min, 0, g_ctx->num_buckets_x - 1);
                    bx_max = ri_clamp_int(bx_max, 0, g_ctx->num_buckets_x - 1);
                    by_min = ri_clamp_int(by_min, 0, g_ctx->num_buckets_y - 1);
                    by_max = ri_clamp_int(by_max, 0, g_ctx->num_buckets_y - 1);

                    for (int by = by_min; by <= by_max; by++) {
                        for (int bx = bx_min; bx <= bx_max; bx++) {
                            if (by == y && bx == x) {
                                // Current bucket: sample immediately
                                ri_sample_mpoly(g_ctx->raster, mpoly,
                                               clip_min_x, clip_min_y, clip_max_x, clip_max_y, mode);
                            } else if (by > y || (by == y && bx > x)) {
                                // Later bucket: queue for later
                                RhBucket* later = &g_ctx->buckets[by * g_ctx->num_buckets_x + bx];
                                ri_mpoly_list_push(&later->queued, mpoly);
                            }
                            // Earlier bucket: already rendered, skip
                        }
                    }
                }

                ri_mpoly_list_free(&mpolys);
            }

            // Cleanup bucket item list
            free(b->items);
            b->items = NULL;
            ri_mpoly_list_free(&b->queued);
        }
    }
    free(g_ctx->buckets);
    g_ctx->buckets = NULL;

    // Downsample if supersampling is enabled
    if (g_ctx->raster && g_ctx->raster->image &&
        (g_ctx->pixel_samples_x > 1 || g_ctx->pixel_samples_y > 1)) {

        RhImage* ss_image = g_ctx->raster->image;
        RhImage* final_image = rh_image_create(g_ctx->xres, g_ctx->yres);

        int sx = g_ctx->pixel_samples_x;
        int sy = g_ctx->pixel_samples_y;

        for (int py = 0; py < g_ctx->yres; py++) {
            for (int px = 0; px < g_ctx->xres; px++) {
                float r = 0, g = 0, b = 0;
                float or_ = 0, og = 0, ob = 0;  // Opacity accumulator
                float weight_sum = 0;

                // Sample all subpixels for this pixel
                for (int ssy = 0; ssy < sy; ssy++) {
                    for (int ssx = 0; ssx < sx; ssx++) {
                        int src_x = px * sx + ssx;
                        int src_y = py * sy + ssy;

                        // Position relative to pixel center (in pixel units)
                        float rel_x = ((float)ssx + 0.5f) / sx - 0.5f;
                        float rel_y = ((float)ssy + 0.5f) / sy - 0.5f;

                        // Scale to filter coordinates
                        rel_x *= g_ctx->filter_width_x;
                        rel_y *= g_ctx->filter_width_y;

                        float w = g_ctx->pixel_filter(rel_x, rel_y,
                                                       g_ctx->filter_width_x,
                                                       g_ctx->filter_width_y);

                        RhColor c = rh_image_get_pixel(ss_image, src_x, src_y);
                        r += c.r * w;
                        g += c.g * w;
                        b += c.b * w;

                        // Also filter opacity
                        int idx = src_y * ss_image->width + src_x;
                        RhColor o = ss_image->opacities[idx];
                        or_ += o.r * w;
                        og += o.g * w;
                        ob += o.b * w;

                        weight_sum += w;
                    }
                }

                if (weight_sum > 0) {
                    r /= weight_sum;
                    g /= weight_sum;
                    b /= weight_sum;
                    or_ /= weight_sum;
                    og /= weight_sum;
                    ob /= weight_sum;
                }

                rh_image_set_pixel_with_opacity(final_image, px, py,
                    (RhColor){r, g, b}, (RhColor){or_, og, ob});
            }
        }

        // Replace supersampled image with final image
        rh_image_destroy(ss_image);
        g_ctx->raster->image = final_image;
    }

    // Save the image
    if (g_ctx->raster && g_ctx->raster->image) {
        rh_image_save_png(g_ctx->raster->image, g_ctx->display_name);
    }

    // Output rendering statistics if enabled via Option "statistics" "endofframe"
    if (g_ctx->stats_options.endofframe) {
        const char* prim_names[] = {
            "Sphere", "Cylinder", "Cone", "Paraboloid", "Polygon",
            "Patch (bicubic)", "Disk", "Torus", "Hyperboloid"
        };
        int total_prims = 0;
        for (int i = 0; i < 9; i++) {
            total_prims += g_ctx->stats.primitives_by_type[i];
        }

        // Text output (to file or stderr)
        FILE* text_out = stderr;
        if (g_ctx->stats_options.filename[0] != '\0') {
            text_out = fopen(g_ctx->stats_options.filename, "w");
            if (!text_out) text_out = stderr;
        }

        fprintf(text_out, "\n=== Rendering Statistics ===\n");
        fprintf(text_out, "Primitives by type:\n");
        for (int i = 0; i < 9; i++) {
            if (g_ctx->stats.primitives_by_type[i] > 0) {
                fprintf(text_out, "  %-16s: %d\n", prim_names[i], g_ctx->stats.primitives_by_type[i]);
            }
        }
        fprintf(text_out, "  %-16s: %d\n", "Total", total_prims);
        fprintf(text_out, "Primitives processed (unique): %d\n", g_ctx->stats.primitives_processed);
        fprintf(text_out, "\nGrids by size:\n");
        for (int i = 1; i <= 16; i++) {
            if (g_ctx->stats.grids_by_size[i] > 0) {
                fprintf(text_out, "  %2dx%-2d: %d\n", i, i, g_ctx->stats.grids_by_size[i]);
            }
        }
        fprintf(text_out, "Total grids: %d\n", g_ctx->stats.total_grids);
        fprintf(text_out, "Total micropolygons: %d\n", g_ctx->stats.total_micropolygons);
        fprintf(text_out, "============================\n\n");

        if (text_out != stderr) fclose(text_out);

        // JSON output if jsonfilename specified
        if (g_ctx->stats_options.jsonfilename[0] != '\0') {
            FILE* json_out = fopen(g_ctx->stats_options.jsonfilename, "w");
            if (json_out) {
                fprintf(json_out, "{\n");
                fprintf(json_out, "  \"primitives\": {\n");
                int first = 1;
                for (int i = 0; i < 9; i++) {
                    if (g_ctx->stats.primitives_by_type[i] > 0) {
                        if (!first) fprintf(json_out, ",\n");
                        fprintf(json_out, "    \"%s\": %d", prim_names[i], g_ctx->stats.primitives_by_type[i]);
                        first = 0;
                    }
                }
                fprintf(json_out, "\n  },\n");
                fprintf(json_out, "  \"primitives_total\": %d,\n", total_prims);
                fprintf(json_out, "  \"primitives_processed\": %d,\n", g_ctx->stats.primitives_processed);
                fprintf(json_out, "  \"grids\": {\n");
                first = 1;
                for (int i = 1; i <= 16; i++) {
                    if (g_ctx->stats.grids_by_size[i] > 0) {
                        if (!first) fprintf(json_out, ",\n");
                        fprintf(json_out, "    \"%dx%d\": %d", i, i, g_ctx->stats.grids_by_size[i]);
                        first = 0;
                    }
                }
                fprintf(json_out, "\n  },\n");
                fprintf(json_out, "  \"grids_total\": %d,\n", g_ctx->stats.total_grids);
                fprintf(json_out, "  \"micropolygons_total\": %d\n", g_ctx->stats.total_micropolygons);
                fprintf(json_out, "}\n");
                fclose(json_out);
            }
        }
    }

    // Cleanup items
    for (int i = 0; i < g_ctx->all_items_count; i++) {
        ri_render_item_destroy(g_ctx->all_items[i]);
    }
    g_ctx->all_items_count = 0;

    RiAttributeEnd(); // Pop World state
    g_ctx->world_active = false;
}

// --- 8. Lighting ---

RtToken RiLightSource(RtToken name, ...) {
    if (!g_ctx || g_ctx->num_lights >= MAX_LIGHTS) return RI_NULL;

    RhLight* l = &g_ctx->lights[g_ctx->num_lights];
    g_ctx->num_lights++;

    strncpy(l->type, name, 31);

    // Defaults
    l->intensity = 1.0f;
    l->color = (RhColor){1.0f, 1.0f, 1.0f};
    l->position = rh_vec3_create(0.0f, 0.0f, 0.0f); // "From"
    RhVec3 to = rh_vec3_create(0.0f, 0.0f, 1.0f);   // "To"
    // Spotlight defaults
    l->coneangle = 30.0f;
    l->conedeltaangle = 5.0f;
    l->beamdistribution = 2.0f;

    // Parse arguments
    va_list ap;
    va_start(ap, name);

    RtToken token;
    while ((token = va_arg(ap, RtToken)) != RI_NULL) {
        if (strcmp(token, "intensity") == 0) {
            RtFloat* val = va_arg(ap, RtFloat*);
            l->intensity = *val;
        } else if (strcmp(token, "lightcolor") == 0) {
            RtColor* col = va_arg(ap, RtColor*);
            l->color.r = (*col)[0];
            l->color.g = (*col)[1];
            l->color.b = (*col)[2];
        } else if (strcmp(token, "from") == 0) {
            RtPoint* p = va_arg(ap, RtPoint*);
            l->position = rh_vec3_create((*p)[0], (*p)[1], (*p)[2]);
        } else if (strcmp(token, "to") == 0) {
            RtPoint* p = va_arg(ap, RtPoint*);
            to = rh_vec3_create((*p)[0], (*p)[1], (*p)[2]);
        } else if (strcmp(token, "coneangle") == 0) {
            RtFloat* val = va_arg(ap, RtFloat*);
            l->coneangle = *val;
        } else if (strcmp(token, "conedeltaangle") == 0) {
            RtFloat* val = va_arg(ap, RtFloat*);
            l->conedeltaangle = *val;
        } else if (strcmp(token, "beamdistribution") == 0) {
            RtFloat* val = va_arg(ap, RtFloat*);
            l->beamdistribution = *val;
        }
    }
    va_end(ap);

    // Calculate direction for distant/spotlight
    l->direction = rh_vec3_normalize(rh_vec3_sub(l->position, to));

    // Transform to world space
    l->position = rh_mat4_mul_point(curr()->transform, l->position);
    RhVec3 to_world = rh_mat4_mul_point(curr()->transform, to);
    l->direction = rh_vec3_normalize(rh_vec3_sub(l->position, to_world));

    return RI_NULL;
}

void RiIlluminate(RtToken light, RtBoolean onoff) {
    (void)light; (void)onoff;
}

// --- 9. Retained Geometry ---

RtObjectHandle RiObjectBegin(void) {
    if (!g_ctx) return RI_NULL;
    RhObject* obj = (RhObject*)calloc(1, sizeof(RhObject));
    obj->inv_transform = rh_mat4_inverse(curr()->transform);
    g_ctx->current_obj = obj;
    return (RtObjectHandle)obj;
}

void RiObjectEnd(void) {
    if (!g_ctx || !g_ctx->current_obj) return;
    
    RhObject* obj = g_ctx->current_obj;
    if (g_ctx->objects_count >= g_ctx->objects_capacity) {
        g_ctx->objects_capacity = g_ctx->objects_capacity == 0 ? 4 : g_ctx->objects_capacity * 2;
        g_ctx->objects = (RhObject**)realloc(g_ctx->objects, g_ctx->objects_capacity * sizeof(RhObject*));
    }
    g_ctx->objects[g_ctx->objects_count++] = obj;
    g_ctx->current_obj = NULL;
}

void RiObjectInstance(RtObjectHandle handle) {
    if (!g_ctx || !handle || !g_ctx->world_active) return;
    
    RhObject* obj = (RhObject*)handle;
    RhMat4 instance_transform = curr()->transform;
    RhColor instance_color = curr()->color;
    
    // Instance CTM * inv(BeginCTM) * item_local_CTM
    RhMat4 base_transform = rh_mat4_mul(instance_transform, obj->inv_transform);
    
    for (int i = 0; i < obj->count; i++) {
        RhObjectItem* item = &obj->items[i];
        RhMat4 final_transform = rh_mat4_mul(base_transform, item->transform);
        ri_add_to_buckets(&item->prim, &final_transform, &instance_color);
    }
}

// --- 7. Primitives ---

// Process primitive and output micropolygons (efficient bucket rendering)
// Used by efficient bucket rendering to process each primitive only once
static void ri_process_item_recursive(RhRenderItem* item, int depth, RhMicropolygonList* out_mpolys, RhShadingMode mode) {
    RhPrimitive* p = &item->prim;

    // Compute MVP matrix for screen-space calculations
    RhMat4 mvp = rh_mat4_mul(g_ctx->projection,
                  rh_mat4_mul(g_ctx->view_matrix, item->transform));

    // Compute screen-space bounding box area
    float screen_area = ri_compute_screen_area(p, &mvp);

    // Adjust area threshold for supersampling and shading rate
    float ss_factor = (float)(g_ctx->pixel_samples_x * g_ctx->pixel_samples_y);
    float shading_rate_sq = item->shading_rate * item->shading_rate;
    float area_threshold = MAX_GRID_AREA * ss_factor * shading_rate_sq;

    // Must split N-gons with > 4 vertices regardless of area
    bool must_split_polygon = (p->type == RH_PRIM_POLYGON &&
                               p->data.polygon.count > 4);

    // Split conditions
    bool need_more_splits = (depth < MIN_SPLIT_DEPTH) ||
                            (screen_area >= area_threshold && depth < MAX_SPLIT_DEPTH);

    if (need_more_splits || must_split_polygon) {
        RhPrimitive children[2];
        int count = rh_prim_split(p, children);
        for (int i = 0; i < count; i++) {
            RhRenderItem child_item = *item;
            child_item.prim = children[i];
            ri_process_item_recursive(&child_item, depth + 1, out_mpolys, mode);
            rh_prim_free_data(&children[i]);
        }
    } else {
        // Grid size is fixed at MAX_GRID_SIZE
        int gridSize = MAX_GRID_SIZE;

        RhMicroGrid* grid = rh_grid_create(gridSize, gridSize);
        if (!grid) return;

        // Track grid statistics
        g_ctx->stats.total_grids++;
        if (gridSize >= 1 && gridSize <= 16) {
            g_ctx->stats.grids_by_size[gridSize]++;
        }

        rh_prim_dice(p, gridSize, gridSize, grid);

        // Matrices
        RhMat4 model = item->transform;
        RhMat4 view = g_ctx->view_matrix;
        RhMat4 proj = g_ctx->projection;

        RhMat4 model_inv = rh_mat4_inverse(model);
        RhMat4 model_inv_tr = rh_mat4_transpose(model_inv);

        RhColor cur_col = item->color;

        // Prepare Lights in Camera Space
        RhLight cam_lights[MAX_LIGHTS];
        for (int k = 0; k < g_ctx->num_lights; k++) {
            cam_lights[k] = g_ctx->lights[k];
            cam_lights[k].position = rh_mat4_mul_point(view, g_ctx->lights[k].position);
            cam_lights[k].direction = rh_mat4_mul_dir(view, g_ctx->lights[k].direction);
            cam_lights[k].direction = rh_vec3_normalize(cam_lights[k].direction);
        }

        for (int i = 0; i < gridSize * gridSize; i++) {
            RhVec3 pos_obj = grid->positions[i];
            RhVec3 norm_obj = grid->normals[i];

            // Obj -> World
            RhVec3 pos_world = rh_mat4_mul_point(model, pos_obj);
            RhVec3 norm_world = rh_mat4_mul_dir(model_inv_tr, norm_obj);
            norm_world = rh_vec3_normalize(norm_world);

            // World -> Camera
            RhVec3 pos_cam = rh_mat4_mul_point(view, pos_world);
            RhVec3 norm_cam = rh_mat4_mul_dir(view, norm_world);
            norm_cam = rh_vec3_normalize(norm_cam);

            // Camera -> NDC
            RhVec3 pos_ndc = rh_mat4_mul_point(proj, pos_cam);

            // --- Execute Shader ---
            RhShaderContext ctx;
            ctx.P = pos_cam;
            ctx.N = norm_cam;
            ctx.I = pos_cam;
            ctx.Cs = cur_col;
            ctx.Os = (RhColor){1,1,1};
            ctx.light_list = cam_lights;
            ctx.num_lights = g_ctx->num_lights;
            ctx.grid_ptr = (void*)(intptr_t)(g_ctx->grid_counter);
            ctx.vertex_index = i;

            // Texture coordinates from grid
            ctx.u = grid->u_coords[i];
            ctx.v = grid->v_coords[i];

            // Texture coordinate derivatives (parametric spacing per grid cell)
            ctx.du = (p->u_max - p->u_min) / (RhFloat)(gridSize - 1);
            ctx.dv = (p->v_max - p->v_min) / (RhFloat)(gridSize - 1);

            if (item->shader) {
                item->shader(&ctx, item->shader_params);
            } else {
                ctx.Ci = cur_col;
                ctx.Oi = ctx.Os;  // Default: pass through opacity
            }

            // Raster coords (use supersampled resolution)
            float rx = (pos_ndc.x + 1.0f) * 0.5f * g_ctx->ss_xres;
            float ry = (1.0f - (pos_ndc.y + 1.0f) * 0.5f) * g_ctx->ss_yres;

            grid->colors[i] = ctx.Ci;
            grid->opacities[i] = ctx.Oi;
            grid->positions[i] = rh_vec3_create(rx, ry, pos_ndc.z);
        }

        // Convert grid to micropolygons instead of direct rasterization
        ri_grid_to_mpolys(grid, out_mpolys, mode);

        rh_grid_destroy(grid);
        g_ctx->grid_counter++;
    }
}


void RiSphere(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax, ...) {
    if (!g_ctx) return;
    RhPrimitive p = rh_prim_create_sphere(radius, zmin, zmax, 0.0f, tmax);
    ri_add_geometry(&p);
    rh_prim_free_data(&p);
}

void RiCylinder(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax, ...) {
    if (!g_ctx) return;
    RhPrimitive p = rh_prim_create_cylinder(radius, zmin, zmax, tmax);
    ri_add_geometry(&p);
    rh_prim_free_data(&p);
}

void RiCone(RtFloat height, RtFloat radius, RtFloat tmax, ...) {
    if (!g_ctx) return;
    RhPrimitive p = rh_prim_create_cone(height, radius, tmax);
    ri_add_geometry(&p);
    rh_prim_free_data(&p);
}

void RiParaboloid(RtFloat rmax, RtFloat zmin, RtFloat zmax, RtFloat tmax, ...) {
    if (!g_ctx) return;
    RhPrimitive p = rh_prim_create_paraboloid(rmax, zmin, zmax, tmax);
    ri_add_geometry(&p);
    rh_prim_free_data(&p);
}

void RiDisk(RtFloat height, RtFloat radius, RtFloat tmax, ...) {
    if (!g_ctx) return;
    RhPrimitive p = rh_prim_create_disk(height, radius, tmax);
    ri_add_geometry(&p);
    rh_prim_free_data(&p);
}

void RiTorus(RtFloat majorradius, RtFloat minorradius, RtFloat phimin, RtFloat phimax, RtFloat tmax, ...) {
    if (!g_ctx) return;
    RhPrimitive p = rh_prim_create_torus(majorradius, minorradius, phimin, phimax, tmax);
    ri_add_geometry(&p);
    rh_prim_free_data(&p);
}

void RiHyperboloid(RtPoint point1, RtPoint point2, RtFloat tmax, ...) {
    if (!g_ctx) return;
    RhVec3 p1 = rh_vec3_create(point1[0], point1[1], point1[2]);
    RhVec3 p2 = rh_vec3_create(point2[0], point2[1], point2[2]);
    RhPrimitive p = rh_prim_create_hyperboloid(p1, p2, tmax);
    ri_add_geometry(&p);
    rh_prim_free_data(&p);
}

void RiPolygon(RtInt nvertices, ...) {
    if (!g_ctx) return;
    
    // Variadic args: expects RI_P, RtPoint array, RI_NULL
    va_list ap;
    va_start(ap, nvertices);
    
    // Simple parser: Assume first token is "P" and second is array
    RtToken token = va_arg(ap, RtToken);
    if (token && strcmp(token, "P") == 0) {
        RtPoint* points = va_arg(ap, RtPoint*);
        // Convert RtPoint (float[3]) to RhVec3
        RhVec3* verts = (RhVec3*)malloc(nvertices * sizeof(RhVec3));
        for(int i=0; i<nvertices; i++) {
            verts[i].x = points[i][0];
            verts[i].y = points[i][1];
            verts[i].z = points[i][2];
        }
        
        RhPrimitive p = rh_prim_create_polygon(nvertices, verts);
        ri_add_geometry(&p);
        rh_prim_free_data(&p);
        free(verts);
    }
    
    va_end(ap);
}

void RiPatch(RtToken type, ...) {
    if (!g_ctx) return;
    
    // Supports "bicubic" and "bilinear"
    // For MVP, implementing "bicubic"
    
    if (strcmp(type, "bicubic") == 0) {
        va_list ap;
        va_start(ap, type);
        
        RtToken token = va_arg(ap, RtToken);
        if (token && strcmp(token, "P") == 0) {
            RtPoint* points = va_arg(ap, RtPoint*);
            
            // 16 control points
            RhVec3 cps[16];
            for(int i=0; i<16; i++) {
                cps[i].x = points[i][0];
                cps[i].y = points[i][1];
                cps[i].z = points[i][2];
            }
            
            // Use current basis
            RhPrimitive p = rh_prim_create_patch_bicubic(cps, curr()->u_basis, curr()->v_basis);
            ri_add_geometry(&p);
        }
        va_end(ap);
    }
}

void RiGeometry(RtToken type, ...) {
    if (!g_ctx) return;
    
    if (strcmp(type, "teapot") == 0) {
        // Render 28 Bezier patches
        // Save current basis
        RhMat4 old_u = curr()->u_basis;
        RhMat4 old_v = curr()->v_basis;
        int old_us = curr()->u_step;
        int old_vs = curr()->v_step;
        
        // Set Bezier Basis
        RhMat4 bezier_m;
        memcpy(bezier_m.m, RiBezierBasis, sizeof(RtMatrix));
        curr()->u_basis = bezier_m;
        curr()->v_basis = bezier_m;
        
        for (int i = 0; i < TEAPOT_NUM_PATCHES; i++) {
            RhVec3 cps[16];
            for (int j = 0; j < 16; j++) {
                cps[j].x = teapot_patches[i][j][0];
                cps[j].y = teapot_patches[i][j][1];
                cps[j].z = teapot_patches[i][j][2];
            }
            RhPrimitive p = rh_prim_create_patch_bicubic(cps, curr()->u_basis, curr()->v_basis);
            ri_add_geometry(&p);
        }
        
        // Restore Basis
        curr()->u_basis = old_u;
        curr()->v_basis = old_v;
        curr()->u_step = old_us;
        curr()->v_step = old_vs;
    }
    
    va_list ap;
    va_start(ap, type);
    va_end(ap);
}

void RiSurface(RtToken name, ...) {
    if (!g_ctx) return;

    if (strcmp(name, "plastic") == 0) {
        curr()->current_surface_shader = rh_shader_surface_plastic;
        curr()->current_shader_params = NULL;
    } else if (strcmp(name, "matte") == 0) {
        curr()->current_surface_shader = rh_shader_surface_matte;
        curr()->current_shader_params = NULL;
    } else if (strcmp(name, "constant") == 0) {
        curr()->current_surface_shader = rh_shader_surface_constant;
        curr()->current_shader_params = NULL;
    } else if (strcmp(name, "metal") == 0) {
        curr()->current_surface_shader = rh_shader_surface_metal;
        curr()->current_shader_params = NULL;
    } else if (strcmp(name, "paintedplastic") == 0) {
        curr()->current_surface_shader = rh_shader_surface_paintedplastic;
        // Parse paintedplastic parameters
        RhPaintedPlasticParams* params = (RhPaintedPlasticParams*)malloc(sizeof(RhPaintedPlasticParams));
        if (params) {
            // Initialize defaults
            params->Ka = 1.0f;
            params->Kd = 0.5f;
            params->Ks = 0.5f;
            params->roughness = 0.1f;
            params->specular_color = (RhColor){1.0f, 1.0f, 1.0f};
            params->texturename[0] = '\0';
            params->texture = NULL;

            va_list ap;
            va_start(ap, name);
            RtToken token;
            while ((token = va_arg(ap, RtToken)) != RI_NULL) {
                if (strcmp(token, "texturename") == 0) {
                    RtToken texname = va_arg(ap, RtToken);
                    if (texname) {
                        strncpy(params->texturename, texname, sizeof(params->texturename) - 1);
                        params->texturename[sizeof(params->texturename) - 1] = '\0';
                        // Load the texture
                        params->texture = rh_texture_load(texname, RH_TEX_RGB);
                        if (!params->texture) {
                            fprintf(stderr, "Warning: Failed to load texture '%s'\n", texname);
                        }
                    }
                } else if (strcmp(token, "Ka") == 0) {
                    RtFloat* val = va_arg(ap, RtFloat*);
                    params->Ka = *val;
                } else if (strcmp(token, "Kd") == 0) {
                    RtFloat* val = va_arg(ap, RtFloat*);
                    params->Kd = *val;
                } else if (strcmp(token, "Ks") == 0) {
                    RtFloat* val = va_arg(ap, RtFloat*);
                    params->Ks = *val;
                } else if (strcmp(token, "roughness") == 0) {
                    RtFloat* val = va_arg(ap, RtFloat*);
                    params->roughness = *val;
                } else if (strcmp(token, "specularcolor") == 0) {
                    RtColor* col = va_arg(ap, RtColor*);
                    params->specular_color.r = (*col)[0];
                    params->specular_color.g = (*col)[1];
                    params->specular_color.b = (*col)[2];
                }
            }
            va_end(ap);
            curr()->current_shader_params = params;
        } else {
            curr()->current_shader_params = NULL;
        }
        return;  // Already consumed varargs
    } else if (strcmp(name, "shinymetal") == 0) {
        curr()->current_surface_shader = rh_shader_surface_shinymetal;
        curr()->current_shader_params = NULL;
    } else if (strcmp(name, "randomgrid") == 0) {
        curr()->current_surface_shader = rh_shader_surface_randomgrid;
        curr()->current_shader_params = NULL;
    } else if (strcmp(name, "random") == 0) {
        curr()->current_surface_shader = rh_shader_surface_random;
        curr()->current_shader_params = NULL;
    }

    va_list ap;
    va_start(ap, name);
    va_end(ap);
}

