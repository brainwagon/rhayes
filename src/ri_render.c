/**
 * ri_render.c - Rendering pipeline
 *
 * Handles RiWorldBegin, RiWorldEnd, micropolygon management, and sampling/rasterization.
 */

#include "ri_internal.h"

// --- Forward declarations ---

static void ri_process_item_recursive(RhRenderItem* item, int depth, RhMicropolygonList* out_mpolys, RhShadingMode mode);

// --- Progress Bar Display ---

static void ri_print_progress(int current, int total, double start_time) {
    if (total <= 0) return;

    // Calculate progress percentage
    double pct = (double)current / total * 100.0;

    // Get elapsed time
    double elapsed = ri_get_time() - start_time;

    // Estimate remaining time
    double eta = (current > 0) ? (elapsed / current) * (total - current) : 0;

    // Build progress bar (40 chars wide)
    int bar_width = 40;
    int filled = (int)(bar_width * current / total);

    // Format: [####....] 25/100 (25.0%) 00:05 ETA 00:15
    fprintf(stderr, "\r[");
    for (int i = 0; i < bar_width; i++) {
        fprintf(stderr, "%c", i < filled ? '#' : '.');
    }
    fprintf(stderr, "] %d/%d (%.1f%%) %02d:%02d ETA %02d:%02d",
            current, total, pct,
            (int)(elapsed / 60), (int)elapsed % 60,
            (int)(eta / 60), (int)eta % 60);
    fflush(stderr);
}

// --- Render Item Memory Management ---

// Estimate memory size for a primitive (for tracking)
static size_t ri_primitive_memory_size(const RhPrimitive* p) {
    RiContextData* ctx = ri_get_ctx();
    size_t size = sizeof(RhRenderItem);
    if (p->type == RH_PRIM_POLYGON) {
        size += p->data.polygon.count * sizeof(RhVec3);
    }
    if (p->num_primvars > 0 && p->primvars) {
        for (int i = 0; i < p->num_primvars; i++) {
            size += sizeof(RhPrimVar) + p->primvars[i].count * sizeof(float) *
                    rh_type_component_count(p->primvars[i].type);
        }
    }
    (void)ctx;
    return size;
}

RhRenderItem* ri_render_item_create(const RhPrimitive* p, const RhMat4* transform, const RhColor* color) {
    RiContextData* ctx = ri_get_ctx();

    // Check memory budget before allocating
    size_t item_size = ri_primitive_memory_size(p);
    if (ctx->memory.max_memory_bytes > 0 &&
        ctx->memory.current_bytes + item_size > ctx->memory.max_memory_bytes) {
        ctx->memory.primitives_dropped++;
        return NULL;
    }

    // Try to reuse from pool first
    RhRenderItem* item;
    if (ctx->item_pool.free_count > 0) {
        item = ctx->item_pool.free_list[--ctx->item_pool.free_count];
        ctx->item_pool.pool_hits++;
    } else {
        item = (RhRenderItem*)malloc(sizeof(RhRenderItem));
        if (!item) return NULL;
        ctx->item_pool.pool_misses++;
    }

    item->prim = *p;
    // Deep copy if polygon
    if (p->type == RH_PRIM_POLYGON) {
        item->prim.data.polygon.vertices = (RhVec3*)malloc(p->data.polygon.count * sizeof(RhVec3));
        memcpy(item->prim.data.polygon.vertices, p->data.polygon.vertices, p->data.polygon.count * sizeof(RhVec3));
    }
    // Deep copy primvars
    if (p->num_primvars > 0 && p->primvars) {
        item->prim.primvars = (RhPrimVar*)malloc(p->num_primvars * sizeof(RhPrimVar));
        item->prim.num_primvars = p->num_primvars;
        for (int i = 0; i < p->num_primvars; i++) {
            rh_primvar_copy(&item->prim.primvars[i], &p->primvars[i]);
        }
    } else {
        item->prim.primvars = NULL;
        item->prim.num_primvars = 0;
    }
    item->transform = *transform;
    item->transform_t1 = ri_curr()->transform_t1;
    item->has_motion = ri_curr()->has_motion;
    item->motion_t0 = ctx->motion_times[0];
    item->motion_t1 = ctx->motion_times[1];
    item->color = *color;
    item->shader = ri_curr()->current_surface_shader;
    item->shader_params = ri_curr()->current_shader_params;
    item->shading_rate = ri_curr()->shading_rate;
    item->processed = false;
    item->last_bucket_idx = -1;
    item->min_depth = 1e30f;   // Will be set in ri_add_to_buckets
    item->max_depth = -1e30f;

    // Add to global list for cleanup
    if (ctx->all_items_count >= ctx->all_items_capacity) {
        ctx->all_items_capacity = ctx->all_items_capacity == 0 ? 16 : ctx->all_items_capacity * 2;
        ctx->all_items = (RhRenderItem**)realloc(ctx->all_items, ctx->all_items_capacity * sizeof(RhRenderItem*));
    }
    item->all_items_idx = ctx->all_items_count;
    ctx->all_items[ctx->all_items_count++] = item;

    // Track memory usage
    ctx->memory.current_bytes += item_size;
    ctx->memory.primitive_bytes += item_size;
    if (ctx->memory.current_bytes > ctx->memory.peak_bytes) {
        ctx->memory.peak_bytes = ctx->memory.current_bytes;
    }

    return item;
}

void ri_render_item_destroy(RhRenderItem* item) {
    if (!item) return;
    RiContextData* ctx = ri_get_ctx();

    // Track memory freed
    size_t item_size = ri_primitive_memory_size(&item->prim);
    ctx->memory.current_bytes -= item_size;
    ctx->memory.primitive_bytes -= item_size;

    // Free primitive data (polygon vertices, primvars)
    rh_prim_free_data(&item->prim);

    // Return item struct to pool for reuse instead of freeing
    if (ctx->item_pool.free_count >= ctx->item_pool.free_capacity) {
        int new_cap = ctx->item_pool.free_capacity == 0 ? 256 : ctx->item_pool.free_capacity * 2;
        // Limit pool size to avoid unbounded growth
        if (new_cap > 16384) new_cap = 16384;
        if (ctx->item_pool.free_count < new_cap) {
            ctx->item_pool.free_capacity = new_cap;
            ctx->item_pool.free_list = (RhRenderItem**)realloc(
                ctx->item_pool.free_list, new_cap * sizeof(RhRenderItem*));
        }
    }

    // Add to pool if there's room, otherwise free
    if (ctx->item_pool.free_count < ctx->item_pool.free_capacity) {
        ctx->item_pool.free_list[ctx->item_pool.free_count++] = item;
    } else {
        free(item);
    }
}

// --- Micropolygon List Helpers ---

void ri_mpoly_list_init(RhMicropolygonList* list) {
    list->data = NULL;
    list->count = 0;
    list->capacity = 0;
}

void ri_mpoly_list_push(RhMicropolygonList* list, const RhMicropolygon* mpoly) {
    RiContextData* ctx = ri_get_ctx();
    if (list->count >= list->capacity) {
        int old_capacity = list->capacity;
        list->capacity = list->capacity == 0 ? 64 : list->capacity * 2;
        list->data = (RhMicropolygon*)realloc(list->data, list->capacity * sizeof(RhMicropolygon));
        // Track memory growth
        size_t growth = (list->capacity - old_capacity) * sizeof(RhMicropolygon);
        ctx->memory.mpoly_queue_bytes += growth;
        ctx->memory.current_bytes += growth;
        if (ctx->memory.current_bytes > ctx->memory.peak_bytes) {
            ctx->memory.peak_bytes = ctx->memory.current_bytes;
        }
    }
    list->data[list->count++] = *mpoly;
}

void ri_mpoly_list_clear(RhMicropolygonList* list) {
    RiContextData* ctx = ri_get_ctx();
    // Update memory tracking before clearing
    ctx->memory.mpoly_queue_bytes -= list->count * sizeof(RhMicropolygon);
    list->count = 0;
    // Keep capacity and data allocated for reuse
}

void ri_mpoly_list_free(RhMicropolygonList* list) {
    RiContextData* ctx = ri_get_ctx();
    ctx->stats.mpolys_freed += list->count;
    // Update memory tracking
    ctx->memory.mpoly_queue_bytes -= list->capacity * sizeof(RhMicropolygon);
    ctx->memory.current_bytes -= list->capacity * sizeof(RhMicropolygon);
    free(list->data);
    list->data = NULL;
    list->count = 0;
    list->capacity = 0;
}

// --- A-Buffer Sample List Functions ---

__attribute__((unused))
static void ri_subpixel_list_init(RhSubpixelList* list) {
    list->samples = NULL;
    list->count = 0;
    list->capacity = 0;
    list->accum_opacity_r = 0.0f;
    list->accum_opacity_g = 0.0f;
    list->accum_opacity_b = 0.0f;
}

__attribute__((unused))
static bool ri_subpixel_list_insert(RhSubpixelList* list, float z, RhColor color, RhColor opacity) {
    RiContextData* ctx = ri_get_ctx();
    float othresh = ctx->memory.opacity_threshold;

    // Early visibility culling: if this subpixel is already fully opaque, skip
    if (list->accum_opacity_r >= othresh &&
        list->accum_opacity_g >= othresh &&
        list->accum_opacity_b >= othresh) {
        return false;
    }

    // Find insertion point (depth-sorted, front to back)
    int insert_pos = list->count;
    for (int i = 0; i < list->count; i++) {
        if (z < list->samples[i].z) {
            insert_pos = i;
            break;
        }
    }

    // Calculate visibility at this depth (opacity accumulated from samples in front)
    float vis_r = 1.0f, vis_g = 1.0f, vis_b = 1.0f;
    for (int i = 0; i < insert_pos; i++) {
        vis_r *= (1.0f - list->samples[i].opacity.r);
        vis_g *= (1.0f - list->samples[i].opacity.g);
        vis_b *= (1.0f - list->samples[i].opacity.b);
    }

    // If this sample would be invisible, don't insert it
    float min_vis = fminf(vis_r, fminf(vis_g, vis_b));
    if (min_vis < (1.0f - othresh)) {
        return false;
    }

    // Grow array if needed
    if (list->count >= list->capacity) {
        list->capacity = list->capacity == 0 ? 4 : list->capacity * 2;
        list->samples = (RhSample*)realloc(list->samples, list->capacity * sizeof(RhSample));
    }

    // Shift samples to make room for insertion
    for (int i = list->count; i > insert_pos; i--) {
        list->samples[i] = list->samples[i - 1];
    }

    // Insert the new sample
    list->samples[insert_pos].z = z;
    list->samples[insert_pos].color = color;
    list->samples[insert_pos].opacity = opacity;
    list->count++;

    // Update accumulated opacity
    list->accum_opacity_r = 0.0f;
    list->accum_opacity_g = 0.0f;
    list->accum_opacity_b = 0.0f;
    float trans_r = 1.0f, trans_g = 1.0f, trans_b = 1.0f;
    for (int i = 0; i < list->count; i++) {
        list->accum_opacity_r += trans_r * list->samples[i].opacity.r;
        list->accum_opacity_g += trans_g * list->samples[i].opacity.g;
        list->accum_opacity_b += trans_b * list->samples[i].opacity.b;
        trans_r *= (1.0f - list->samples[i].opacity.r);
        trans_g *= (1.0f - list->samples[i].opacity.g);
        trans_b *= (1.0f - list->samples[i].opacity.b);
    }

    return true;
}

__attribute__((unused))
static void ri_subpixel_list_composite(const RhSubpixelList* list, RhColor* out_color, RhColor* out_opacity) {
    RhColor color = {0.0f, 0.0f, 0.0f};
    float trans_r = 1.0f, trans_g = 1.0f, trans_b = 1.0f;

    for (int i = 0; i < list->count; i++) {
        color.r += trans_r * list->samples[i].color.r;
        color.g += trans_g * list->samples[i].color.g;
        color.b += trans_b * list->samples[i].color.b;

        trans_r *= (1.0f - list->samples[i].opacity.r);
        trans_g *= (1.0f - list->samples[i].opacity.g);
        trans_b *= (1.0f - list->samples[i].opacity.b);
    }

    *out_color = color;
    out_opacity->r = 1.0f - trans_r;
    out_opacity->g = 1.0f - trans_g;
    out_opacity->b = 1.0f - trans_b;
}

static void ri_subpixel_list_clear(RhSubpixelList* list) {
    list->count = 0;
    list->accum_opacity_r = 0.0f;
    list->accum_opacity_g = 0.0f;
    list->accum_opacity_b = 0.0f;
}

static void ri_subpixel_list_free(RhSubpixelList* list) {
    free(list->samples);
    list->samples = NULL;
    list->count = 0;
    list->capacity = 0;
    list->accum_opacity_r = 0.0f;
    list->accum_opacity_g = 0.0f;
    list->accum_opacity_b = 0.0f;
}

__attribute__((unused))
static RhBucketSamples* ri_bucket_samples_create(int bucket_width, int bucket_height, int samples_x, int samples_y) {
    RhBucketSamples* bs = (RhBucketSamples*)malloc(sizeof(RhBucketSamples));
    bs->bucket_width = bucket_width;
    bs->bucket_height = bucket_height;
    bs->samples_x = samples_x;
    bs->samples_y = samples_y;
    bs->num_lists = bucket_width * bucket_height * samples_x * samples_y;
    bs->lists = (RhSubpixelList*)calloc(bs->num_lists, sizeof(RhSubpixelList));
    for (int i = 0; i < bs->num_lists; i++) {
        ri_subpixel_list_init(&bs->lists[i]);
    }
    return bs;
}

__attribute__((unused))
static void ri_bucket_samples_clear(RhBucketSamples* bs) {
    for (int i = 0; i < bs->num_lists; i++) {
        ri_subpixel_list_clear(&bs->lists[i]);
    }
}

__attribute__((unused))
static void ri_bucket_samples_destroy(RhBucketSamples* bs) {
    if (bs) {
        for (int i = 0; i < bs->num_lists; i++) {
            ri_subpixel_list_free(&bs->lists[i]);
        }
        free(bs->lists);
        free(bs);
    }
}

__attribute__((unused))
static RhSubpixelList* ri_bucket_samples_get(RhBucketSamples* bs, int bucket_x, int bucket_y, int subpixel_x, int subpixel_y) {
    int px = subpixel_x / bs->samples_x;
    int py = subpixel_y / bs->samples_y;
    int sx = subpixel_x % bs->samples_x;
    int sy = subpixel_y % bs->samples_y;
    (void)bucket_x; (void)bucket_y;

    if (px < 0 || px >= bs->bucket_width || py < 0 || py >= bs->bucket_height) {
        return NULL;
    }

    int idx = ((py * bs->bucket_width + px) * bs->samples_y + sy) * bs->samples_x + sx;
    return &bs->lists[idx];
}

// --- Hierarchical Z-Buffer Functions ---

static RhHiZBuffer* ri_hiz_create(int width, int height, int offset_x, int offset_y) {
    RhHiZBuffer* hiz = (RhHiZBuffer*)malloc(sizeof(RhHiZBuffer));
    if (!hiz) return NULL;

    hiz->base_offset_x = offset_x;
    hiz->base_offset_y = offset_y;

    // Build pyramid levels until size reaches 1x1 or we hit max levels
    int w = width;
    int h = height;
    hiz->num_levels = 0;

    for (int level = 0; level < MAX_HIZ_LEVELS && (w >= 1 || h >= 1); level++) {
        hiz->width[level] = w > 0 ? w : 1;
        hiz->height[level] = h > 0 ? h : 1;
        hiz->levels[level] = (float*)malloc(hiz->width[level] * hiz->height[level] * sizeof(float));

        // Initialize to max depth (far plane)
        for (int i = 0; i < hiz->width[level] * hiz->height[level]; i++) {
            hiz->levels[level][i] = 1e30f;
        }

        hiz->num_levels++;
        if (w <= 1 && h <= 1) break;
        w = (w + 1) / 2;
        h = (h + 1) / 2;
    }

    return hiz;
}

static void ri_hiz_destroy(RhHiZBuffer* hiz) {
    if (!hiz) return;
    for (int i = 0; i < hiz->num_levels; i++) {
        free(hiz->levels[i]);
    }
    free(hiz);
}

// Update Hi-Z after writing a pixel at (x, y) with depth z
// x, y are in screen coordinates (not bucket-local)
static void ri_hiz_update(RhHiZBuffer* hiz, int x, int y, float z) {
    if (!hiz) return;

    // Convert to bucket-local coordinates
    int lx = x - hiz->base_offset_x;
    int ly = y - hiz->base_offset_y;

    // Update level 0 (full resolution)
    if (lx < 0 || lx >= hiz->width[0] || ly < 0 || ly >= hiz->height[0]) return;

    int idx = ly * hiz->width[0] + lx;
    if (z < hiz->levels[0][idx]) {
        hiz->levels[0][idx] = z;

        // Propagate minimum to coarser levels
        for (int level = 1; level < hiz->num_levels; level++) {
            lx /= 2;
            ly /= 2;
            if (lx >= hiz->width[level]) lx = hiz->width[level] - 1;
            if (ly >= hiz->height[level]) ly = hiz->height[level] - 1;

            idx = ly * hiz->width[level] + lx;
            if (z < hiz->levels[level][idx]) {
                hiz->levels[level][idx] = z;
            } else {
                break;  // No need to propagate further if not smaller
            }
        }
    }
}

// Test if a rectangle is fully occluded (all pixels behind existing geometry)
// Returns true if the region is FULLY occluded and can be skipped
// min_x, min_y, max_x, max_y are in screen coordinates
// min_depth is the closest depth value of the region to test
static bool ri_hiz_test_occluded(RhHiZBuffer* hiz, int min_x, int min_y,
                                  int max_x, int max_y, float min_depth) {
    if (!hiz) return false;

    // Convert to bucket-local coordinates
    int lmin_x = min_x - hiz->base_offset_x;
    int lmin_y = min_y - hiz->base_offset_y;
    int lmax_x = max_x - hiz->base_offset_x;
    int lmax_y = max_y - hiz->base_offset_y;

    // Clamp to bucket bounds
    if (lmin_x < 0) lmin_x = 0;
    if (lmin_y < 0) lmin_y = 0;
    if (lmax_x >= hiz->width[0]) lmax_x = hiz->width[0] - 1;
    if (lmax_y >= hiz->height[0]) lmax_y = hiz->height[0] - 1;

    // If region is entirely outside bucket, it's not occluded by THIS bucket
    if (lmin_x > lmax_x || lmin_y > lmax_y) return false;

    // Find the coarsest level where we can do a single lookup
    // that covers the entire region
    int level = 0;

    while (level < hiz->num_levels - 1) {
        // Check if region fits in a single cell at next level
        int scale = 1 << (level + 1);
        int cell_min_x = lmin_x / scale;
        int cell_max_x = lmax_x / scale;
        int cell_min_y = lmin_y / scale;
        int cell_max_y = lmax_y / scale;

        if (cell_min_x == cell_max_x && cell_min_y == cell_max_y) {
            level++;
        } else {
            break;
        }
    }

    // At this level, check all cells that cover our region
    int scale = 1 << level;
    int cell_min_x = lmin_x / scale;
    int cell_max_x = lmax_x / scale;
    int cell_min_y = lmin_y / scale;
    int cell_max_y = lmax_y / scale;

    // Clamp to level bounds
    if (cell_min_x < 0) cell_min_x = 0;
    if (cell_min_y < 0) cell_min_y = 0;
    if (cell_max_x >= hiz->width[level]) cell_max_x = hiz->width[level] - 1;
    if (cell_max_y >= hiz->height[level]) cell_max_y = hiz->height[level] - 1;

    // Hi-Z stores the minimum (closest) z at each cell.
    // For a grid to be fully occluded, ALL pixels must be behind existing geometry.
    // So we need: grid_min_z > hiz_z for ALL cells in the coverage.
    // Find the maximum Hi-Z value in the region (the farthest of the closest points).
    float max_hiz_depth = -1e30f;
    for (int cy = cell_min_y; cy <= cell_max_y; cy++) {
        for (int cx = cell_min_x; cx <= cell_max_x; cx++) {
            int idx = cy * hiz->width[level] + cx;
            float val = hiz->levels[level][idx];
            if (val > max_hiz_depth) {
                max_hiz_depth = val;
            }
        }
    }

    // Grid is occluded if its closest point is farther than ALL Hi-Z values
    return min_depth > max_hiz_depth;
}

// --- Front-to-Back Sorting Comparison ---

static int ri_compare_item_depth(const void* a, const void* b) {
    RhRenderItem* ia = *(RhRenderItem**)a;
    RhRenderItem* ib = *(RhRenderItem**)b;
    if (ia->min_depth < ib->min_depth) return -1;
    if (ia->min_depth > ib->min_depth) return 1;
    return 0;
}

// --- Rasterization Helpers ---

static inline float ri_edge_function(RhVec3 v0, RhVec3 v1, float px, float py) {
    return (px - v0.x) * (v1.y - v0.y) - (py - v0.y) * (v1.x - v0.x);
}

static inline void ri_spatial_jitter(int x, int y, float* jitter_x, float* jitter_y) {
    unsigned int hash = (unsigned int)(x * 127717) ^ (unsigned int)(y * 94531);
    *jitter_x = ((float)(hash & 0xFFFF) / 65536.0f) - 0.5f;
    *jitter_y = ((float)((hash >> 16) & 0xFFFF) / 65536.0f) - 0.5f;
}

static inline float ri_temporal_hash(int x, int y, int ss_x, int ss_y, int jitter_enabled) {
    int sub_x = x % ss_x;
    int sub_y = y % ss_y;
    int total_samples = ss_x * ss_y;

    int tile_x = x / ss_x;
    int tile_y = y / ss_y;
    unsigned int tile_hash = (unsigned int)(tile_x * 73856093) ^ (unsigned int)(tile_y * 19349663);

    int base_idx = sub_y * ss_x + sub_x;
    int sample_idx = (base_idx + (int)(tile_hash % (unsigned int)total_samples)) % total_samples;

    float jitter;
    if (jitter_enabled) {
        unsigned int pixel_hash = (unsigned int)(x * 127717) ^ (unsigned int)(y * 94531);
        jitter = (float)(pixel_hash & 0xFFFF) / 65536.0f;
    } else {
        jitter = 0.5f;
    }

    return ((float)sample_idx + jitter) / (float)total_samples;
}

static inline RhVec3 ri_vec3_lerp(RhVec3 a, RhVec3 b, float t) {
    return rh_vec3_create(
        a.x + t * (b.x - a.x),
        a.y + t * (b.y - a.y),
        a.z + t * (b.z - a.z)
    );
}

static inline int ri_get_time_slice_idx(int x, int y, int ss_x, int ss_y) {
    int sub_x = x % ss_x;
    int sub_y = y % ss_y;
    int total_samples = ss_x * ss_y;

    int tile_x = x / ss_x;
    int tile_y = y / ss_y;
    unsigned int tile_hash = (unsigned int)(tile_x * 73856093) ^ (unsigned int)(tile_y * 19349663);

    int base_idx = sub_y * ss_x + sub_x;
    return (base_idx + (int)(tile_hash % (unsigned int)total_samples)) % total_samples;
}

static void ri_precompute_motion_cache(
    const RhMicropolygon* mpoly,
    RhMotionCache* cache,
    int ss_x, int ss_y,
    float shutter_open, float shutter_close,
    float motion_t0, float motion_t1
) {
    int total_samples = ss_x * ss_y;
    cache->num_slices = total_samples;
    cache->ss_x = ss_x;
    cache->ss_y = ss_y;

    float motion_range = motion_t1 - motion_t0;
    bool degenerate_motion = fabsf(motion_range) < 1e-6f;

    for (int slice_idx = 0; slice_idx < total_samples; slice_idx++) {
        RhTimeSlice* slice = &cache->slices[slice_idx];

        float pixel_t = ((float)slice_idx + 0.5f) / (float)total_samples;
        float world_time = shutter_open + pixel_t * (shutter_close - shutter_open);

        float t;
        if (degenerate_motion) {
            t = 0.0f;
        } else {
            t = (world_time - motion_t0) / motion_range;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
        }

        slice->v[0] = ri_vec3_lerp(mpoly->v[0], mpoly->v_t1[0], t);
        slice->v[1] = ri_vec3_lerp(mpoly->v[1], mpoly->v_t1[1], t);
        slice->v[2] = ri_vec3_lerp(mpoly->v[2], mpoly->v_t1[2], t);
        slice->v[3] = ri_vec3_lerp(mpoly->v[3], mpoly->v_t1[3], t);

        slice->area1 = ri_edge_function(slice->v[0], slice->v[1], slice->v[3].x, slice->v[3].y);
        slice->area2 = ri_edge_function(slice->v[1], slice->v[2], slice->v[3].x, slice->v[3].y);

        float min_x = slice->v[0].x;
        float max_x = slice->v[0].x;
        float min_y = slice->v[0].y;
        float max_y = slice->v[0].y;

        for (int i = 1; i < 4; i++) {
            if (slice->v[i].x < min_x) min_x = slice->v[i].x;
            if (slice->v[i].x > max_x) max_x = slice->v[i].x;
            if (slice->v[i].y < min_y) min_y = slice->v[i].y;
            if (slice->v[i].y > max_y) max_y = slice->v[i].y;
        }

        slice->min_x = (int)floorf(min_x);
        slice->max_x = (int)ceilf(max_x);
        slice->min_y = (int)floorf(min_y);
        slice->max_y = (int)ceilf(max_y);
    }
}

// Convert a shaded grid to micropolygons with motion blur support
static void ri_grid_to_mpolys_motion(const RhMicroGrid* grid,
                                      const RhVec3* screen_pos,
                                      const RhVec3* screen_pos_t1,
                                      const RhVec3* cam_normals,
                                      bool has_motion,
                                      float motion_t0,
                                      float motion_t1,
                                      RhMicropolygonList* out_list,
                                      RhShadingMode mode) {
    RiContextData* ctx = ri_get_ctx();
    int w = grid->width;
    int h = grid->height;

    for (int j = 0; j < h - 1; j++) {
        for (int i = 0; i < w - 1; i++) {
            int i00 = j * w + i;
            int i10 = j * w + (i + 1);
            int i01 = (j + 1) * w + i;
            int i11 = (j + 1) * w + (i + 1);

            RhVec3 v0 = screen_pos[i00];
            RhVec3 v1 = screen_pos[i10];
            RhVec3 v2 = screen_pos[i11];
            RhVec3 v3 = screen_pos[i01];

            (void)cam_normals;

            RhMicropolygon mpoly;

            mpoly.v[0] = v0;
            mpoly.v[1] = v1;
            mpoly.v[2] = v2;
            mpoly.v[3] = v3;

            mpoly.has_motion = has_motion;
            mpoly.motion_t0 = motion_t0;
            mpoly.motion_t1 = motion_t1;
            if (has_motion && screen_pos_t1) {
                mpoly.v_t1[0] = screen_pos_t1[i00];
                mpoly.v_t1[1] = screen_pos_t1[i10];
                mpoly.v_t1[2] = screen_pos_t1[i11];
                mpoly.v_t1[3] = screen_pos_t1[i01];
            } else {
                mpoly.v_t1[0] = mpoly.v[0];
                mpoly.v_t1[1] = mpoly.v[1];
                mpoly.v_t1[2] = mpoly.v[2];
                mpoly.v_t1[3] = mpoly.v[3];
            }

            mpoly.c[0] = grid->colors[i00];
            mpoly.c[1] = grid->colors[i10];
            mpoly.c[2] = grid->colors[i11];
            mpoly.c[3] = grid->colors[i01];

            mpoly.o[0] = grid->opacities[i00];
            mpoly.o[1] = grid->opacities[i10];
            mpoly.o[2] = grid->opacities[i11];
            mpoly.o[3] = grid->opacities[i01];

            mpoly.center.r = (mpoly.c[0].r + mpoly.c[1].r + mpoly.c[2].r + mpoly.c[3].r) * 0.25f;
            mpoly.center.g = (mpoly.c[0].g + mpoly.c[1].g + mpoly.c[2].g + mpoly.c[3].g) * 0.25f;
            mpoly.center.b = (mpoly.c[0].b + mpoly.c[1].b + mpoly.c[2].b + mpoly.c[3].b) * 0.25f;

            mpoly.center_opacity.r = (mpoly.o[0].r + mpoly.o[1].r + mpoly.o[2].r + mpoly.o[3].r) * 0.25f;
            mpoly.center_opacity.g = (mpoly.o[0].g + mpoly.o[1].g + mpoly.o[2].g + mpoly.o[3].g) * 0.25f;
            mpoly.center_opacity.b = (mpoly.o[0].b + mpoly.o[1].b + mpoly.o[2].b + mpoly.o[3].b) * 0.25f;

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

            if (has_motion) {
                for (int k = 0; k < 4; k++) {
                    if (mpoly.v_t1[k].x < min_x) min_x = mpoly.v_t1[k].x;
                    if (mpoly.v_t1[k].x > max_x) max_x = mpoly.v_t1[k].x;
                    if (mpoly.v_t1[k].y < min_y) min_y = mpoly.v_t1[k].y;
                    if (mpoly.v_t1[k].y > max_y) max_y = mpoly.v_t1[k].y;
                }
            }

            mpoly.min_x = (int)floorf(min_x);
            mpoly.min_y = (int)floorf(min_y);
            mpoly.max_x = (int)ceilf(max_x);
            mpoly.max_y = (int)ceilf(max_y);

            ctx->stats.total_micropolygons++;
            ctx->stats.mpolys_this_bucket++;

            ri_mpoly_list_push(out_list, &mpoly);

            (void)mode;
        }
    }
}

// Rasterize a single micropolygon within the given clip bounds
static void ri_sample_mpoly(
    RhRasterizer* r,
    const RhMicropolygon* mpoly,
    int clip_min_x, int clip_min_y,
    int clip_max_x, int clip_max_y,
    RhShadingMode mode
) {
    RiContextData* ctx = ri_get_ctx();

    int x0 = rh_max(mpoly->min_x, clip_min_x);
    int y0 = rh_max(mpoly->min_y, clip_min_y);
    int x1 = rh_min(mpoly->max_x, clip_max_x);
    int y1 = rh_min(mpoly->max_y, clip_max_y);

    if (x0 > x1 || y0 > y1) return;

    bool has_motion = mpoly->has_motion &&
                      (ctx->shutter_close > ctx->shutter_open);

    int ss_x = ctx->pixel_samples_x;
    int ss_y = ctx->pixel_samples_y;
    bool use_jitter = ctx->hider_options.jitter != 0;

    RhMotionCache cache;
    bool use_cache = has_motion && !use_jitter;
    if (use_cache) {
        ri_precompute_motion_cache(
            mpoly, &cache, ss_x, ss_y,
            ctx->shutter_open, ctx->shutter_close,
            mpoly->motion_t0, mpoly->motion_t1
        );
    }

    RhVec3 v0_base = mpoly->v[0];
    RhVec3 v1_base = mpoly->v[1];
    RhVec3 v2_base = mpoly->v[2];
    RhVec3 v3_base = mpoly->v[3];

    float area1_static = 0, area2_static = 0;
    if (!has_motion) {
        area1_static = ri_edge_function(v0_base, v1_base, v3_base.x, v3_base.y);
        area2_static = ri_edge_function(v1_base, v2_base, v3_base.x, v3_base.y);
    }

    float motion_range = mpoly->motion_t1 - mpoly->motion_t0;
    float inv_motion_range = (fabsf(motion_range) < 1e-6f) ? 0.0f : 1.0f / motion_range;
    float shutter_range = ctx->shutter_close - ctx->shutter_open;

    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float px = x + 0.5f;
            float py = y + 0.5f;

            if (use_jitter) {
                float jx, jy;
                ri_spatial_jitter(x, y, &jx, &jy);
                px += jx;
                py += jy;
            }

            RhVec3 v0, v1, v2, v3;
            float area1, area2;

            if (has_motion) {
                if (use_jitter) {
                    float pixel_t = ri_temporal_hash(x, y, ss_x, ss_y, 1);
                    float world_time = ctx->shutter_open + pixel_t * shutter_range;

                    float t;
                    if (inv_motion_range == 0.0f) {
                        t = 0.0f;
                    } else {
                        t = (world_time - mpoly->motion_t0) * inv_motion_range;
                        if (t < 0.0f) t = 0.0f;
                        if (t > 1.0f) t = 1.0f;
                    }

                    float x0_t = mpoly->v[0].x + t * (mpoly->v_t1[0].x - mpoly->v[0].x);
                    float x1_t = mpoly->v[1].x + t * (mpoly->v_t1[1].x - mpoly->v[1].x);
                    float x2_t = mpoly->v[2].x + t * (mpoly->v_t1[2].x - mpoly->v[2].x);
                    float x3_t = mpoly->v[3].x + t * (mpoly->v_t1[3].x - mpoly->v[3].x);
                    float y0_t = mpoly->v[0].y + t * (mpoly->v_t1[0].y - mpoly->v[0].y);
                    float y1_t = mpoly->v[1].y + t * (mpoly->v_t1[1].y - mpoly->v[1].y);
                    float y2_t = mpoly->v[2].y + t * (mpoly->v_t1[2].y - mpoly->v[2].y);
                    float y3_t = mpoly->v[3].y + t * (mpoly->v_t1[3].y - mpoly->v[3].y);

                    float bbox_min_x = fminf(fminf(x0_t, x1_t), fminf(x2_t, x3_t));
                    float bbox_max_x = fmaxf(fmaxf(x0_t, x1_t), fmaxf(x2_t, x3_t));
                    float bbox_min_y = fminf(fminf(y0_t, y1_t), fminf(y2_t, y3_t));
                    float bbox_max_y = fmaxf(fmaxf(y0_t, y1_t), fmaxf(y2_t, y3_t));

                    if (px < bbox_min_x || px > bbox_max_x ||
                        py < bbox_min_y || py > bbox_max_y) {
                        continue;
                    }

                    v0 = rh_vec3_create(x0_t, y0_t, mpoly->v[0].z + t * (mpoly->v_t1[0].z - mpoly->v[0].z));
                    v1 = rh_vec3_create(x1_t, y1_t, mpoly->v[1].z + t * (mpoly->v_t1[1].z - mpoly->v[1].z));
                    v2 = rh_vec3_create(x2_t, y2_t, mpoly->v[2].z + t * (mpoly->v_t1[2].z - mpoly->v[2].z));
                    v3 = rh_vec3_create(x3_t, y3_t, mpoly->v[3].z + t * (mpoly->v_t1[3].z - mpoly->v[3].z));

                    area1 = ri_edge_function(v0, v1, v3.x, v3.y);
                    area2 = ri_edge_function(v1, v2, v3.x, v3.y);
                } else {
                    int slice_idx = ri_get_time_slice_idx(x, y, ss_x, ss_y);
                    const RhTimeSlice* slice = &cache.slices[slice_idx];

                    if (x < slice->min_x || x > slice->max_x ||
                        y < slice->min_y || y > slice->max_y) {
                        continue;
                    }

                    v0 = slice->v[0];
                    v1 = slice->v[1];
                    v2 = slice->v[2];
                    v3 = slice->v[3];
                    area1 = slice->area1;
                    area2 = slice->area2;
                }
            } else {
                v0 = v0_base;
                v1 = v1_base;
                v2 = v2_base;
                v3 = v3_base;
                area1 = area1_static;
                area2 = area2_static;
            }

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

                        // Update Hi-Z buffer
                        ri_hiz_update(ctx->bucket_hiz, x, y, z);

                        RhColor final_color;
                        RhColor final_opacity;
                        if (mode == RH_SHADE_CENTER) {
                            final_color = mpoly->center;
                            final_opacity = mpoly->center_opacity;
                        } else {
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

                        // Update Hi-Z buffer
                        ri_hiz_update(ctx->bucket_hiz, x, y, z);

                        RhColor final_color;
                        RhColor final_opacity;
                        if (mode == RH_SHADE_CENTER) {
                            final_color = mpoly->center;
                            final_opacity = mpoly->center_opacity;
                        } else {
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

// Compute screen-space bounding box area for a primitive
static float ri_compute_screen_area_motion(const RhPrimitive* p, const RhMat4* mvp, const RhMat4* mvp_t1) {
    RiContextData* ctx = ri_get_ctx();

    float min_x = 1e30f, max_x = -1e30f;
    float min_y = 1e30f, max_y = -1e30f;

    for (int j = 0; j <= 2; j++) {
        float v = p->v_min + (p->v_max - p->v_min) * (j / 2.0f);
        for (int i = 0; i <= 2; i++) {
            float u = p->u_min + (p->u_max - p->u_min) * (i / 2.0f);

            RhVec3 pos_obj = rh_prim_eval_point(p, u, v);

            RhVec3 p_ndc = rh_mat4_mul_point(*mvp, pos_obj);
            float rx = (p_ndc.x + 1.0f) * 0.5f * ctx->ss_xres;
            float ry = (1.0f - (p_ndc.y + 1.0f) * 0.5f) * ctx->ss_yres;

            if (rx < min_x) min_x = rx;
            if (rx > max_x) max_x = rx;
            if (ry < min_y) min_y = ry;
            if (ry > max_y) max_y = ry;

            if (mvp_t1) {
                RhVec3 p_ndc_t1 = rh_mat4_mul_point(*mvp_t1, pos_obj);
                float rx_t1 = (p_ndc_t1.x + 1.0f) * 0.5f * ctx->ss_xres;
                float ry_t1 = (1.0f - (p_ndc_t1.y + 1.0f) * 0.5f) * ctx->ss_yres;

                if (rx_t1 < min_x) min_x = rx_t1;
                if (rx_t1 > max_x) max_x = rx_t1;
                if (ry_t1 < min_y) min_y = ry_t1;
                if (ry_t1 > max_y) max_y = ry_t1;
            }
        }
    }

    float width = max_x - min_x;
    float height = max_y - min_y;

    if (width < 0.0f) width = 0.0f;
    if (height < 0.0f) height = 0.0f;

    return width * height;
}

static float ri_compute_screen_area(const RhPrimitive* p, const RhMat4* mvp) {
    return ri_compute_screen_area_motion(p, mvp, NULL);
}

// Compute adaptive grid size based on screen-space area and shading rate.
// Returns grid dimension (same for u and v) in range [MIN_GRID_SIZE, MAX_GRID_SIZE].
// Target: each micropolygon should be ~1 pixel × shading_rate.
static int ri_compute_grid_size(float screen_area, float shading_rate) {
    // Guard against division by zero
    if (shading_rate <= 0.0f) shading_rate = 1.0f;

    // Each micropolygon (grid cell) should cover approximately shading_rate pixels.
    // For a grid of size N×N, we have (N-1)×(N-1) quads covering screen_area pixels.
    // So: (N-1)² ≈ screen_area / shading_rate
    // Therefore: N ≈ sqrt(screen_area / shading_rate) + 1
    float target_cells = screen_area / shading_rate;
    int grid_size = (int)(sqrtf(target_cells) + 1.5f);  // +1 for vertices vs cells, +0.5 for rounding

    // Clamp to valid range
    if (grid_size < MIN_GRID_SIZE) grid_size = MIN_GRID_SIZE;
    if (grid_size > MAX_GRID_SIZE) grid_size = MAX_GRID_SIZE;

    return grid_size;
}

// Process primitive and output micropolygons
static void ri_process_item_recursive(RhRenderItem* item, int depth, RhMicropolygonList* out_mpolys, RhShadingMode mode) {
    RiContextData* ctx = ri_get_ctx();
    RhPrimitive* p = &item->prim;

    RhMat4 mvp = rh_mat4_mul(ctx->projection,
                  rh_mat4_mul(ctx->view_matrix, item->transform));

    float screen_area = ri_compute_screen_area(p, &mvp);

    float ss_factor = (float)(ctx->pixel_samples_x * ctx->pixel_samples_y);
    float shading_rate_sq = item->shading_rate * item->shading_rate;
    float area_threshold = MAX_GRID_AREA * ss_factor * shading_rate_sq;

    bool must_split_polygon = (p->type == RH_PRIM_POLYGON &&
                               p->data.polygon.count > 4);

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
        int gridSize = ri_compute_grid_size(screen_area, item->shading_rate);

        RhMicroGrid* grid = rh_grid_create(gridSize, gridSize);
        if (!grid) return;

        ctx->stats.total_grids++;
        ctx->stats.grids_this_bucket++;
        if (gridSize >= 1 && gridSize <= 16) {
            ctx->stats.grids_by_size[gridSize]++;
        }

        rh_prim_dice(p, gridSize, gridSize, grid);

        RhMat4 model = item->transform;
        RhMat4 model_t1 = item->transform_t1;
        bool has_motion = item->has_motion;
        RhMat4 view = ctx->view_matrix;
        RhMat4 proj = ctx->projection;

        RhMat4 model_inv = rh_mat4_inverse(model);
        RhMat4 model_inv_tr = rh_mat4_transpose(model_inv);

        RhColor cur_col = item->color;

        RhLight cam_lights[MAX_LIGHTS];
        for (int k = 0; k < ctx->num_lights; k++) {
            cam_lights[k] = ctx->lights[k];
            cam_lights[k].position = rh_mat4_mul_point(view, ctx->lights[k].position);
            cam_lights[k].direction = rh_mat4_mul_dir(view, ctx->lights[k].direction);
            cam_lights[k].direction = rh_vec3_normalize(cam_lights[k].direction);
        }

        RhGridScratch* scratch = ctx->grid_scratch;
        RhVec3* screen_pos = scratch->screen_pos;
        RhVec3* screen_pos_t1 = has_motion ? scratch->screen_pos_t1 : NULL;
        RhVec3* cam_positions = scratch->cam_positions;
        RhVec3* cam_normals = scratch->cam_normals;

        for (int i = 0; i < gridSize * gridSize; i++) {
            RhVec3 pos_obj = grid->positions[i];
            RhVec3 norm_obj = grid->normals[i];

            RhVec3 pos_world = rh_mat4_mul_point(model, pos_obj);
            RhVec3 norm_world = rh_mat4_mul_dir(model_inv_tr, norm_obj);
            norm_world = rh_vec3_normalize(norm_world);

            cam_positions[i] = rh_mat4_mul_point(view, pos_world);
            cam_normals[i] = rh_vec3_normalize(rh_mat4_mul_dir(view, norm_world));

            RhVec3 pos_ndc = rh_mat4_mul_point(proj, cam_positions[i]);
            float rx = (pos_ndc.x + 1.0f) * 0.5f * ctx->ss_xres;
            float ry = (1.0f - (pos_ndc.y + 1.0f) * 0.5f) * ctx->ss_yres;
            screen_pos[i] = rh_vec3_create(rx, ry, pos_ndc.z);

            if (has_motion) {
                RhVec3 pos_world_t1 = rh_mat4_mul_point(model_t1, pos_obj);
                RhVec3 pos_cam_t1 = rh_mat4_mul_point(view, pos_world_t1);
                RhVec3 pos_ndc_t1 = rh_mat4_mul_point(proj, pos_cam_t1);
                float rx_t1 = (pos_ndc_t1.x + 1.0f) * 0.5f * ctx->ss_xres;
                float ry_t1 = (1.0f - (pos_ndc_t1.y + 1.0f) * 0.5f) * ctx->ss_yres;
                screen_pos_t1[i] = rh_vec3_create(rx_t1, ry_t1, pos_ndc_t1.z);
            }
        }

        // Compute grid screen bounds and min depth for Hi-Z culling
        int grid_min_x = (int)1e9, grid_max_x = -1;
        int grid_min_y = (int)1e9, grid_max_y = -1;
        float grid_min_depth = 1e30f;

        for (int i = 0; i < gridSize * gridSize; i++) {
            int sx = (int)floorf(screen_pos[i].x);
            int sy = (int)floorf(screen_pos[i].y);
            if (sx < grid_min_x) grid_min_x = sx;
            if (sx > grid_max_x) grid_max_x = sx;
            if (sy < grid_min_y) grid_min_y = sy;
            if (sy > grid_max_y) grid_max_y = sy;
            if (screen_pos[i].z < grid_min_depth) grid_min_depth = screen_pos[i].z;

            // Also check t1 positions for motion blur
            if (has_motion) {
                int sx_t1 = (int)floorf(screen_pos_t1[i].x);
                int sy_t1 = (int)floorf(screen_pos_t1[i].y);
                if (sx_t1 < grid_min_x) grid_min_x = sx_t1;
                if (sx_t1 > grid_max_x) grid_max_x = sx_t1;
                if (sy_t1 < grid_min_y) grid_min_y = sy_t1;
                if (sy_t1 > grid_max_y) grid_max_y = sy_t1;
                if (screen_pos_t1[i].z < grid_min_depth) grid_min_depth = screen_pos_t1[i].z;
            }
        }

        // Hi-Z occlusion test - skip shading if grid is fully occluded
        if (ctx->bucket_hiz &&
            ri_hiz_test_occluded(ctx->bucket_hiz, grid_min_x, grid_min_y,
                                 grid_max_x, grid_max_y, grid_min_depth)) {
            ctx->stats.grids_culled++;
            rh_grid_destroy(grid);
            return;
        }

        ctx->stats.grids_shaded++;

        for (int i = 0; i < gridSize * gridSize; i++) {
            int gx = i % gridSize;
            int gy = i / gridSize;

            float screen_dx_u = 0.0f, screen_dy_u = 0.0f, tex_du = 0.0f, tex_dv_u = 0.0f;
            if (gx < gridSize - 1) {
                int i_right = i + 1;
                screen_dx_u = screen_pos[i_right].x - screen_pos[i].x;
                screen_dy_u = screen_pos[i_right].y - screen_pos[i].y;
                tex_du = grid->u_coords[i_right] - grid->u_coords[i];
                tex_dv_u = grid->v_coords[i_right] - grid->v_coords[i];
            } else if (gx > 0) {
                int i_left = i - 1;
                screen_dx_u = screen_pos[i].x - screen_pos[i_left].x;
                screen_dy_u = screen_pos[i].y - screen_pos[i_left].y;
                tex_du = grid->u_coords[i] - grid->u_coords[i_left];
                tex_dv_u = grid->v_coords[i] - grid->v_coords[i_left];
            }

            float screen_dx_v = 0.0f, screen_dy_v = 0.0f, tex_du_v = 0.0f, tex_dv = 0.0f;
            if (gy < gridSize - 1) {
                int i_down = i + gridSize;
                screen_dx_v = screen_pos[i_down].x - screen_pos[i].x;
                screen_dy_v = screen_pos[i_down].y - screen_pos[i].y;
                tex_du_v = grid->u_coords[i_down] - grid->u_coords[i];
                tex_dv = grid->v_coords[i_down] - grid->v_coords[i];
            } else if (gy > 0) {
                int i_up = i - gridSize;
                screen_dx_v = screen_pos[i].x - screen_pos[i_up].x;
                screen_dy_v = screen_pos[i].y - screen_pos[i_up].y;
                tex_du_v = grid->u_coords[i] - grid->u_coords[i_up];
                tex_dv = grid->v_coords[i] - grid->v_coords[i_up];
            }

            float screen_dist_u = sqrtf(screen_dx_u * screen_dx_u + screen_dy_u * screen_dy_u);
            float screen_dist_v = sqrtf(screen_dx_v * screen_dx_v + screen_dy_v * screen_dy_v);
            float tex_dist_u = sqrtf(tex_du * tex_du + tex_dv_u * tex_dv_u);
            float tex_dist_v = sqrtf(tex_du_v * tex_du_v + tex_dv * tex_dv);

            float filter_u = 0.0f, filter_v = 0.0f;

            const float min_screen_dist = 0.5f;
            const float max_filter = 1.0f;

            if (screen_dist_u > min_screen_dist) {
                filter_u = tex_dist_u / screen_dist_u;
            } else if (tex_dist_u > 0.001f) {
                filter_u = max_filter;
            }

            if (screen_dist_v > min_screen_dist) {
                filter_v = tex_dist_v / screen_dist_v;
            } else if (tex_dist_v > 0.001f) {
                filter_v = max_filter;
            }

            float filter_width = fmaxf(filter_u, filter_v);

            RhShaderContext shctx;
            shctx.P = cam_positions[i];
            shctx.N = cam_normals[i];
            shctx.I = cam_positions[i];
            shctx.Cs = cur_col;
            shctx.Os = (RhColor){1,1,1};
            shctx.light_list = cam_lights;
            shctx.num_lights = ctx->num_lights;
            shctx.grid_ptr = (void*)(intptr_t)(ctx->grid_counter);
            shctx.vertex_index = i;

            shctx.primvars = item->prim.primvars;
            shctx.num_primvars = item->prim.num_primvars;

            shctx.u = grid->u_coords[i];
            shctx.v = grid->v_coords[i];

            shctx.du = filter_width;
            shctx.dv = filter_width;

            if (item->shader) {
                item->shader(&shctx, item->shader_params);
            } else {
                shctx.Ci = cur_col;
                shctx.Oi = shctx.Os;
            }

            grid->colors[i] = shctx.Ci;
            grid->opacities[i] = shctx.Oi;
        }

        ri_grid_to_mpolys_motion(grid, screen_pos, screen_pos_t1, cam_normals, has_motion,
                                 item->motion_t0, item->motion_t1, out_mpolys, mode);

        rh_grid_destroy(grid);
        ctx->grid_counter++;
    }
}

// --- World Begin/End ---

void RiWorldBegin(void) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    ctx->ss_xres = ctx->xres * ctx->pixel_samples_x;
    ctx->ss_yres = ctx->yres * ctx->pixel_samples_y;

    if (ctx->raster) rh_raster_destroy(ctx->raster);
    ctx->raster = rh_raster_create(ctx->ss_xres, ctx->ss_yres);
    rh_raster_clear(ctx->raster);

    ctx->world_active = true;

    ctx->view_matrix = rh_mat4_inverse(ri_curr()->transform);

    ctx->num_buckets_x = (ctx->ss_xres + ctx->bucket_size - 1) / ctx->bucket_size;
    ctx->num_buckets_y = (ctx->ss_yres + ctx->bucket_size - 1) / ctx->bucket_size;
    int count = ctx->num_buckets_x * ctx->num_buckets_y;
    ctx->buckets = (RhBucket*)calloc(count, sizeof(RhBucket));

    ctx->grid_scratch = (RhGridScratch*)malloc(sizeof(RhGridScratch));

    ri_mpoly_list_init(&ctx->bucket_mpolys);

    ctx->timing.bucket_time = 0.0;
    ctx->timing.rasterize_time = 0.0;
    ctx->timing.transform_time = 0.0;
    ctx->timing.motion_mpolys = 0;
    ctx->timing.static_mpolys = 0;

    ctx->all_items_count = 0;
    ctx->grid_counter = 0;

    // Initialize occlusion culling state
    ctx->bucket_hiz = NULL;
    ctx->stats.grids_culled = 0;
    ctx->stats.grids_shaded = 0;

    RiAttributeBegin();
    RiIdentity();
}

void RiWorldEnd(void) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    double bucket_start = ri_get_time();

    // Progress bar initialization
    int total_buckets = ctx->num_buckets_x * ctx->num_buckets_y;
    if (ctx->show_progress) {
        ctx->render_start_time = bucket_start;
        ri_print_progress(0, total_buckets, ctx->render_start_time);
    }

    int ss_xres = ctx->xres * ctx->pixel_samples_x;
    int ss_yres = ctx->yres * ctx->pixel_samples_y;

    RhShadingMode mode = ctx->shading_mode;

    RhMicropolygonList* mpolys = &ctx->bucket_mpolys;

    for (int by = 0; by < ctx->num_buckets_y; by++) {
        for (int bx = 0; bx < ctx->num_buckets_x; bx++) {
            int current_bucket_idx = by * ctx->num_buckets_x + bx;
            RhBucket* b = &ctx->buckets[current_bucket_idx];

            ctx->stats.grids_this_bucket = 0;
            ctx->stats.mpolys_this_bucket = 0;

            int clip_min_x = bx * ctx->bucket_size;
            int clip_max_x = rh_min((bx + 1) * ctx->bucket_size - 1, ss_xres - 1);
            int clip_min_y = by * ctx->bucket_size;
            int clip_max_y = rh_min((by + 1) * ctx->bucket_size - 1, ss_yres - 1);

            // Sort bucket items front-to-back for early Z rejection
            if (b->item_count > 1) {
                qsort(b->items, b->item_count, sizeof(RhRenderItem*), ri_compare_item_depth);
            }

            // Create Hi-Z buffer for this bucket
            int bucket_width = clip_max_x - clip_min_x + 1;
            int bucket_height = clip_max_y - clip_min_y + 1;
            ctx->bucket_hiz = ri_hiz_create(bucket_width, bucket_height, clip_min_x, clip_min_y);

            for (int qi = 0; qi < b->queued.count; qi++) {
                ri_sample_mpoly(ctx->raster, &b->queued.data[qi],
                               clip_min_x, clip_min_y, clip_max_x, clip_max_y, mode);
            }
            ri_mpoly_list_clear(&b->queued);

            for (int i = 0; i < b->item_count; i++) {
                RhRenderItem* item = b->items[i];

                if (!item->processed) {
                    item->processed = true;
                    ctx->stats.primitives_processed++;

                    ri_mpoly_list_clear(mpolys);
                    ri_process_item_recursive(item, 0, mpolys, mode);

                    for (int mi = 0; mi < mpolys->count; mi++) {
                        RhMicropolygon* mpoly = &mpolys->data[mi];

                        if (mpoly->has_motion) {
                            ctx->timing.motion_mpolys++;
                        } else {
                            ctx->timing.static_mpolys++;
                        }

                        int mbx_min = mpoly->min_x / ctx->bucket_size;
                        int mbx_max = mpoly->max_x / ctx->bucket_size;
                        int mby_min = mpoly->min_y / ctx->bucket_size;
                        int mby_max = mpoly->max_y / ctx->bucket_size;

                        mbx_min = ri_clamp_int(mbx_min, 0, ctx->num_buckets_x - 1);
                        mbx_max = ri_clamp_int(mbx_max, 0, ctx->num_buckets_x - 1);
                        mby_min = ri_clamp_int(mby_min, 0, ctx->num_buckets_y - 1);
                        mby_max = ri_clamp_int(mby_max, 0, ctx->num_buckets_y - 1);

                        for (int mby = mby_min; mby <= mby_max; mby++) {
                            for (int mbx = mbx_min; mbx <= mbx_max; mbx++) {
                                if (mby == by && mbx == bx) {
                                    ri_sample_mpoly(ctx->raster, mpoly,
                                                   clip_min_x, clip_min_y, clip_max_x, clip_max_y, mode);
                                } else if (mby > by || (mby == by && mbx > bx)) {
                                    RhBucket* later = &ctx->buckets[mby * ctx->num_buckets_x + mbx];
                                    ri_mpoly_list_push(&later->queued, mpoly);
                                }
                            }
                        }
                    }
                }

                if (current_bucket_idx == item->last_bucket_idx) {
                    ctx->all_items[item->all_items_idx] = NULL;
                    ri_render_item_destroy(item);
                }
            }

            ctx->stats.buckets_processed++;
            if (ctx->stats.grids_this_bucket > ctx->stats.peak_grids_per_bucket) {
                ctx->stats.peak_grids_per_bucket = ctx->stats.grids_this_bucket;
            }
            if (ctx->stats.mpolys_this_bucket > ctx->stats.peak_mpolys_per_bucket) {
                ctx->stats.peak_mpolys_per_bucket = ctx->stats.mpolys_this_bucket;
            }

            // Update progress bar
            if (ctx->show_progress) {
                ri_print_progress(ctx->stats.buckets_processed, total_buckets, ctx->render_start_time);
            }

            // Destroy Hi-Z buffer for this bucket
            ri_hiz_destroy(ctx->bucket_hiz);
            ctx->bucket_hiz = NULL;

            free(b->items);
            b->items = NULL;
            ri_mpoly_list_free(&b->queued);
        }
    }
    free(ctx->buckets);
    ctx->buckets = NULL;

    // Finish progress bar
    if (ctx->show_progress) {
        fprintf(stderr, "\n");
    }

    ri_mpoly_list_free(&ctx->bucket_mpolys);

    ctx->timing.bucket_time = ri_get_time() - bucket_start;

    if (ctx->raster && ctx->raster->image &&
        (ctx->pixel_samples_x > 1 || ctx->pixel_samples_y > 1)) {

        RhImage* ss_image = ctx->raster->image;
        RhImage* final_image = rh_image_create(ctx->xres, ctx->yres);

        int sx = ctx->pixel_samples_x;
        int sy = ctx->pixel_samples_y;

        for (int py = 0; py < ctx->yres; py++) {
            for (int px = 0; px < ctx->xres; px++) {
                float r = 0, g = 0, b = 0;
                float or_ = 0, og = 0, ob = 0;
                float weight_sum = 0;

                for (int ssy = 0; ssy < sy; ssy++) {
                    for (int ssx = 0; ssx < sx; ssx++) {
                        int src_x = px * sx + ssx;
                        int src_y = py * sy + ssy;

                        float rel_x = ((float)ssx + 0.5f) / sx - 0.5f;
                        float rel_y = ((float)ssy + 0.5f) / sy - 0.5f;

                        rel_x *= ctx->filter_width_x;
                        rel_y *= ctx->filter_width_y;

                        float w = ctx->pixel_filter(rel_x, rel_y,
                                                       ctx->filter_width_x,
                                                       ctx->filter_width_y);

                        RhColor c = rh_image_get_pixel(ss_image, src_x, src_y);
                        r += c.r * w;
                        g += c.g * w;
                        b += c.b * w;

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

        rh_image_destroy(ss_image);
        ctx->raster->image = final_image;
    }

    if (ctx->raster && ctx->raster->image) {
        rh_image_save_png_channels(ctx->raster->image, ctx->display_name, ctx->display_channels);
    }

    if (ctx->stats_options.endofframe >= 1) {
        const char* prim_names[] = {
            "Sphere", "Cylinder", "Cone", "Paraboloid", "Polygon",
            "Patch (bicubic)", "Patch (bilinear)", "Disk", "Torus", "Hyperboloid"
        };
        int total_prims = 0;
        for (int i = 0; i < 10; i++) {
            total_prims += ctx->stats.primitives_by_type[i];
        }

        FILE* text_out = stderr;
        if (ctx->stats_options.filename[0] != '\0') {
            text_out = fopen(ctx->stats_options.filename, "w");
            if (!text_out) text_out = stderr;
        }

        fprintf(text_out, "\n=== Rendering Statistics ===\n");
        fprintf(text_out, "Primitives: %d total\n", total_prims);
        for (int i = 0; i < 10; i++) {
            if (ctx->stats.primitives_by_type[i] > 0) {
                fprintf(text_out, "  %-16s: %d\n", prim_names[i], ctx->stats.primitives_by_type[i]);
            }
        }

        int num_buckets = ctx->stats.buckets_processed;
        float avg_grids = num_buckets > 0 ?
            (float)ctx->stats.total_grids / num_buckets : 0;
        fprintf(text_out, "\nGrids:\n");
        fprintf(text_out, "  Allocated:   %d\n", ctx->stats.total_grids);
        fprintf(text_out, "  Peak/bucket: %d\n", ctx->stats.peak_grids_per_bucket);
        fprintf(text_out, "  Avg/bucket:  %.1f\n", avg_grids);

        // Occlusion culling statistics
        if (ctx->stats.grids_culled > 0 || ctx->stats.grids_shaded > 0) {
            int total_grids_tested = ctx->stats.grids_culled + ctx->stats.grids_shaded;
            float cull_pct = total_grids_tested > 0 ?
                (100.0f * ctx->stats.grids_culled / total_grids_tested) : 0;
            fprintf(text_out, "\nOcclusion Culling:\n");
            fprintf(text_out, "  Grids shaded: %d\n", ctx->stats.grids_shaded);
            fprintf(text_out, "  Grids culled: %d (%.1f%%)\n", ctx->stats.grids_culled, cull_pct);
        }

        float avg_mpolys = num_buckets > 0 ?
            (float)ctx->stats.total_micropolygons / num_buckets : 0;
        fprintf(text_out, "\nMicropolygons:\n");
        fprintf(text_out, "  Allocated:   %d\n", ctx->stats.total_micropolygons);
        fprintf(text_out, "  Freed:       %d\n", ctx->stats.mpolys_freed);
        fprintf(text_out, "  Peak/bucket: %d\n", ctx->stats.peak_mpolys_per_bucket);
        fprintf(text_out, "  Avg/bucket:  %.1f\n", avg_mpolys);

        if (ctx->stats_options.endofframe >= 2) {
            fprintf(text_out, "\nGrids by size:\n");
            for (int i = 1; i <= 16; i++) {
                if (ctx->stats.grids_by_size[i] > 0) {
                    fprintf(text_out, "  %2dx%-2d: %d\n", i, i, ctx->stats.grids_by_size[i]);
                }
            }

            fprintf(text_out, "\nMotion blur:\n");
            fprintf(text_out, "  Motion mpolys: %d\n", ctx->timing.motion_mpolys);
            fprintf(text_out, "  Static mpolys: %d\n", ctx->timing.static_mpolys);

            fprintf(text_out, "\nPerformance timing:\n");
            fprintf(text_out, "  Bucket processing: %.3f s\n", ctx->timing.bucket_time);

            fprintf(text_out, "\nMemory usage:\n");
            fprintf(text_out, "  Peak:          %.2f MB\n", ctx->memory.peak_bytes / (1024.0 * 1024.0));
            fprintf(text_out, "  Pool hits:     %d\n", ctx->item_pool.pool_hits);
            fprintf(text_out, "  Pool misses:   %d\n", ctx->item_pool.pool_misses);
            if (ctx->memory.primitives_dropped > 0) {
                fprintf(text_out, "  Dropped prims: %d (memory limit reached)\n", ctx->memory.primitives_dropped);
            }
        }

        fprintf(text_out, "============================\n\n");

        if (text_out != stderr) fclose(text_out);

        if (ctx->stats_options.jsonfilename[0] != '\0') {
            FILE* json_out = fopen(ctx->stats_options.jsonfilename, "w");
            if (json_out) {
                fprintf(json_out, "{\n");
                fprintf(json_out, "  \"primitives\": {\n");
                int first = 1;
                for (int i = 0; i < 10; i++) {
                    if (ctx->stats.primitives_by_type[i] > 0) {
                        if (!first) fprintf(json_out, ",\n");
                        fprintf(json_out, "    \"%s\": %d", prim_names[i], ctx->stats.primitives_by_type[i]);
                        first = 0;
                    }
                }
                fprintf(json_out, "\n  },\n");
                fprintf(json_out, "  \"primitives_total\": %d,\n", total_prims);
                fprintf(json_out, "  \"primitives_processed\": %d,\n", ctx->stats.primitives_processed);
                fprintf(json_out, "  \"grids\": {\n");
                first = 1;
                for (int i = 1; i <= 16; i++) {
                    if (ctx->stats.grids_by_size[i] > 0) {
                        if (!first) fprintf(json_out, ",\n");
                        fprintf(json_out, "    \"%dx%d\": %d", i, i, ctx->stats.grids_by_size[i]);
                        first = 0;
                    }
                }
                fprintf(json_out, "\n  },\n");
                fprintf(json_out, "  \"grids_total\": %d,\n", ctx->stats.total_grids);
                fprintf(json_out, "  \"grids_shaded\": %d,\n", ctx->stats.grids_shaded);
                fprintf(json_out, "  \"grids_culled\": %d,\n", ctx->stats.grids_culled);
                fprintf(json_out, "  \"grids_peak_per_bucket\": %d,\n", ctx->stats.peak_grids_per_bucket);
                fprintf(json_out, "  \"micropolygons_total\": %d,\n", ctx->stats.total_micropolygons);
                fprintf(json_out, "  \"micropolygons_freed\": %d,\n", ctx->stats.mpolys_freed);
                fprintf(json_out, "  \"micropolygons_peak_per_bucket\": %d,\n", ctx->stats.peak_mpolys_per_bucket);
                fprintf(json_out, "  \"buckets_processed\": %d,\n", ctx->stats.buckets_processed);
                fprintf(json_out, "  \"motion_mpolys\": %d,\n", ctx->timing.motion_mpolys);
                fprintf(json_out, "  \"static_mpolys\": %d,\n", ctx->timing.static_mpolys);
                fprintf(json_out, "  \"bucket_time_sec\": %.6f,\n", ctx->timing.bucket_time);
                fprintf(json_out, "  \"memory_peak_bytes\": %zu,\n", ctx->memory.peak_bytes);
                fprintf(json_out, "  \"memory_pool_hits\": %d,\n", ctx->item_pool.pool_hits);
                fprintf(json_out, "  \"memory_pool_misses\": %d,\n", ctx->item_pool.pool_misses);
                fprintf(json_out, "  \"primitives_dropped\": %d\n", ctx->memory.primitives_dropped);
                fprintf(json_out, "}\n");
                fclose(json_out);
            }
        }
    }

    free(ctx->grid_scratch);
    ctx->grid_scratch = NULL;

    for (int i = 0; i < ctx->all_items_count; i++) {
        ri_render_item_destroy(ctx->all_items[i]);
    }
    ctx->all_items_count = 0;

    RiAttributeEnd();
    ctx->world_active = false;
}
