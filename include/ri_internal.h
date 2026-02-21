#ifndef RI_INTERNAL_H
#define RI_INTERNAL_H

/**
 * ri_internal.h - Internal types and helpers for the RenderMan API implementation
 *
 * This header is shared between all ri_*.c modules. External code should only
 * include ri.h, not this header.
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include "ri.h"
#include "rh_math.h"
#include "rh_image.h"
#include "rh_geometry.h"
#include "rh_raster.h"
#include "rh_shader.h"
#include "rh_texture.h"
#include "rh_shadow.h"

// --- Constants ---

#define MAX_STACK_DEPTH 64
#define MAX_DECLARATIONS 256
#define MAX_LIGHTS 8
#define MAX_IMPL_ATTRIBUTES 32
#define MAX_IMPL_ATTR_NAME 64
#define MAX_NAMED_COORD_SYS 32

// --- Transform Frame (for separate transform stack) ---

typedef struct {
    RhMat4 transform;
    RhMat4 transform_t1;
    bool has_motion;
} RhTransformFrame;

// --- Named Coordinate System ---

typedef struct RhNamedCoordSys {
    char name[64];
    RhMat4 matrix;  /* Transform from named space to world space */
} RhNamedCoordSys;

// --- Display Mode ---

typedef enum {
    RH_DISPLAY_RGB,     // Normal color rendering (3 channels)
    RH_DISPLAY_RGBA,    // Color + alpha (4 channels)
    RH_DISPLAY_Z        // Depth-only for shadow maps (1 channel, float)
} RhDisplayMode;

// Grid size constants for adaptive splitting/dicing
#define MAX_GRID_SIZE 16
#define MAX_GRID_AREA (MAX_GRID_SIZE * MAX_GRID_SIZE)  // 256 pixels
#define MIN_GRID_SIZE 2
#define MAX_SPLIT_DEPTH 12  // Safety limit to prevent infinite recursion
#define MIN_SPLIT_DEPTH 3   // Minimum splits before considering area

// Motion blur time samples
#define MAX_MOTION_TIME_SAMPLES 64  // Supports up to 8x8 supersampling

// --- Implementation-Specific Attribute Entry ---

typedef struct {
    char category[MAX_IMPL_ATTR_NAME];  // Attribute category (e.g., "bound", "dice", "user")
    char name[MAX_IMPL_ATTR_NAME];      // Parameter name (e.g., "displacement", "rasterorient")
    float values[16];                    // Float values (up to 16 for matrix)
    int num_values;                      // Number of float values
    char string_value[256];              // String value (if applicable)
    bool is_string;                      // True if this is a string attribute
} RhImplAttribute;

// --- Attribute State ---

typedef struct {
    RhMat4 transform;    // Current Transformation Matrix (Object -> World/Camera)
    RhMat4 transform_t1; // Transform at t1 for motion blur
    bool has_motion;     // True if transform differs from transform_t1
    RhColor color;
    RhColor opacity;     // Surface opacity (default: {1,1,1} = fully opaque)

    // Basis
    RhMat4 u_basis;
    int u_step;
    RhMat4 v_basis;
    int v_step;

    // Shading
    RhShaderFunc current_surface_shader;
    void* current_shader_params;
    RhShaderFunc current_atmosphere_shader;
    void* current_atmosphere_params;
    float shading_rate;  // Controls splitting granularity (default 1.0)
    float displace_bound;         /* sphere radius in displace_coordsys (default 0) */
    char  displace_coordsys[64];  /* coordinate system name (default "object") */
    RhMat4 current_shader_to_world;  /* CTM captured when current surface shader was bound */

    // Orientation and sides
    bool orientation_lh;         // false = right-handed (default), true = left-handed
    int reverse_orientation;     // Count of ReverseOrientation calls (odd = flipped)
    int sides;                   // 1 = front only (default), 2 = both sides
    bool is_matte;               // True if this is a hold-out matte object

    // Implementation-specific attributes (RiAttribute)
    RhImplAttribute impl_attrs[MAX_IMPL_ATTRIBUTES];
    int num_impl_attrs;
} RiAttributeState;

// --- Variable Declaration ---

typedef struct {
    char name[64];           // Token name (e.g., "temperature")
    RiStorageClass sclass;   // Storage class
    RiVarType type;          // Data type
    int array_size;          // Array count (1 for non-arrays)
} RiDeclaration;

// --- Render Item ---

typedef struct {
    RhPrimitive prim;
    RhMat4 transform;    // CTM at creation (t0)
    RhMat4 transform_t1; // Transform at t1 for motion blur
    bool has_motion;     // True if motion blur active
    float motion_t0;     // Time value for transform (from MotionBegin)
    float motion_t1;     // Time value for transform_t1 (from MotionBegin)
    RhColor color;
    RhColor opacity;     // Surface opacity captured at primitive creation
    RhShaderFunc shader;
    void* shader_params;
    RhShaderFunc atmosphere_shader;
    void* atmosphere_params;
    float shading_rate;  // Captured from attribute state
    float displace_bound;
    char  displace_coordsys[64];
    bool processed;      // Has this primitive been split/diced/shaded?
    int last_bucket_idx; // Linear index of last bucket containing this item
    int all_items_idx;   // Index in g_ctx->all_items for cleanup
    float min_depth;     // Minimum depth for front-to-back sorting
    float max_depth;     // Maximum depth for Hi-Z culling
    bool orientation_flipped;    // Combined orientation state at primitive creation
    int sides;                   // Sides value captured at primitive creation
    bool is_matte;               // True if this is a hold-out matte object
    RhMat4 shader_to_world;      // CTM at shader-bind time (for "shader" space transforms)
} RhRenderItem;

// --- Micropolygon Types ---

typedef enum {
    RH_SHADE_VERTEX,   // Per-vertex shading, interpolate 4 colors
    RH_SHADE_CENTER    // Single flat color per micropolygon
} RhShadingMode;

typedef struct {
    RhVec3 v[4];       // Screen-space vertices (x=pixel, y=pixel, z=depth) at t0
    RhVec3 v_t1[4];    // Screen-space vertices at t1 for motion blur
    bool has_motion;   // True if motion blur active
    float motion_t0;   // Time value for v[] positions (from MotionBegin)
    float motion_t1;   // Time value for v_t1[] positions (from MotionBegin)
    RhColor c[4];      // Per-vertex colors (for interpolation mode)
    RhColor o[4];      // Per-vertex opacities (for interpolation mode)
    RhColor center;    // Center color (for flat shading mode)
    RhColor center_opacity; // Center opacity (for flat shading mode)
    int min_x, min_y;  // Screen-space bounding box (union of t0 and t1)
    int max_x, max_y;
    bool orientation_flipped;  // Orientation state for backface culling
    int sides;                 // 1 or 2
    bool is_matte;             // True if this is a hold-out matte object
} RhMicropolygon;

typedef struct {
    RhMicropolygon* data;
    int count;
    int capacity;
} RhMicropolygonList;

// --- Motion Blur Time-Indexed Sampling ---

typedef struct {
    RhVec3 v[4];           // Interpolated vertices at this time
    float area1, area2;    // Precomputed triangle areas
    int min_x, min_y;      // Per-time-slice bounding box
    int max_x, max_y;
} RhTimeSlice;

typedef struct {
    RhTimeSlice slices[MAX_MOTION_TIME_SAMPLES];
    int num_slices;
    int ss_x, ss_y;
} RhMotionCache;

// --- A-Buffer Sample List Structures ---

typedef struct {
    float z;            // Depth value
    RhColor color;      // Premultiplied color (color * opacity)
    RhColor opacity;    // Per-channel opacity
    bool is_matte;      // True if this is a hold-out matte sample
} RhSample;

typedef struct {
    RhSample* samples;  // Depth-sorted array (front to back)
    int count;
    int capacity;
    float accum_opacity_r;  // Accumulated opacity for early culling
    float accum_opacity_g;
    float accum_opacity_b;
    float opaque_z;         // Z where accumulated opacity >= othresh (for Hi-Z culling)
} RhSubpixelList;

typedef struct {
    RhSubpixelList* lists;  // Array of subpixel lists (bucket_size^2 * pixel_samples^2)
    int num_lists;          // Total number of subpixel lists
    int bucket_width;       // Bucket width in pixels
    int bucket_height;      // Bucket height in pixels
    int samples_x;          // Pixel samples in X
    int samples_y;          // Pixel samples in Y
} RhBucketSamples;

// --- Bucket ---

typedef struct {
    RhRenderItem** items;
    int item_count;
    int item_capacity;
    RhMicropolygonList queued;  // Micropolygons forwarded from earlier buckets
} RhBucket;

// --- Hierarchical Z-Buffer (Hi-Z) for Occlusion Culling ---

#define MAX_HIZ_LEVELS 8

typedef struct {
    float* levels[MAX_HIZ_LEVELS];  // Pyramid (level 0 = full res)
    int width[MAX_HIZ_LEVELS];
    int height[MAX_HIZ_LEVELS];
    int num_levels;
    int base_offset_x;              // Bucket offset in screen coords
    int base_offset_y;
} RhHiZBuffer;

// --- Light Structure ---

typedef struct {
    char type[32]; // "distantlight", "pointlight", "ambientlight", "spotlight"
    RhVec3 position; // "from"
    RhVec3 direction; // Normalized (to - from)
    RhColor color;
    float intensity;
    RhMat4 transform; // Transform at creation time
    // Spotlight parameters
    float coneangle;        // Half-angle of full illumination cone (radians)
    float conedeltaangle;   // Penumbra width (radians)
    float beamdistribution; // Cosine power for beam shape
    // Shadow map parameters
    RhShadowMap* shadowmap;     // Shadow map data (NULL if no shadows)
    int shadow_samples;         // Number of PCF samples (default 16)
    float shadow_bias;          // Depth bias (default 0.005)
    float shadow_blur;          // Filter size multiplier (default 1.0)
    // VM light shader (NULL for C fallback via evaluate_light)
    RhShaderFunc light_shader;
    void* light_shader_params;
} RhLight;

// --- Grid Scratch Buffers ---

typedef struct {
    RhVec3 screen_pos[MAX_GRID_SIZE * MAX_GRID_SIZE];
    RhVec3 screen_pos_t1[MAX_GRID_SIZE * MAX_GRID_SIZE];
    RhVec3 cam_positions[MAX_GRID_SIZE * MAX_GRID_SIZE];
    RhVec3 cam_normals[MAX_GRID_SIZE * MAX_GRID_SIZE];
    RhVec3 world_positions[MAX_GRID_SIZE * MAX_GRID_SIZE];
    /* Fully-displaced positions from displacement pre-pass (see ri_render.c).
     * Used by OP_CALCNORMAL so all neighbours are displaced before normal
     * computation, eliminating grid-boundary normal discontinuities. */
    RhVec3 predisplace_positions[MAX_GRID_SIZE * MAX_GRID_SIZE];
} RhGridScratch;

// --- Object (Retained Geometry) ---

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

// --- Options State (for RiFrameBegin/End save/restore) ---

typedef struct {
    // Display options
    int xres, yres;
    char display_name[256];
    int display_channels;

    // Camera options
    RhMat4 projection;

    // Sampling options
    int pixel_samples_x, pixel_samples_y;
    RtFilterFunc pixel_filter;
    float filter_width_x, filter_width_y;

    // Depth of field
    float dof_fstop, dof_focallength, dof_focaldistance;

    // Motion blur
    float shutter_open, shutter_close;

    // Hider options
    struct {
        int jitter;
    } hider_options;

    // Statistics options
    struct {
        int endofframe;
        char filename[256];
        char jsonfilename[256];
    } stats_options;

    // Progress
    bool show_progress;

    // Shader search path
    char shader_searchpath[512];
    char texture_searchpath[512];
} RiOptionsState;

// --- Context Data ---

typedef struct {
    // Options
    int xres, yres;
    char display_name[256];
    int display_channels;  // 3 for RGB, 4 for RGBA, 1 for Z
    RhDisplayMode display_mode;  // RGB, RGBA, or Z (shadow map)
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

    // Motion blur / Shutter
    float shutter_open;      // Default: 0.0
    float shutter_close;     // Default: 0.0 (no blur when equal)
    bool motion_active;      // Inside MotionBegin/End block
    int motion_sample_index; // 0 or 1 (which sample we're capturing)
    float motion_times[2];   // Time values for each sample

    // State Stack
    RiAttributeState stack[MAX_STACK_DEPTH];
    int stack_ptr;

    // Separate transform-only stack (for RiTransformBegin/End)
    RhTransformFrame transform_stack[MAX_STACK_DEPTH];
    int transform_stack_ptr;  /* -1 = empty */

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
        int primitives_by_type[10];  // Indexed by RhPrimitiveType
        int grids_by_size[17];      // Index 0 unused, 1-16 for grid sizes
        int total_grids;
        int total_micropolygons;
        int mpolys_freed;           // Track freed mpolys
        int primitives_processed;   // Unique primitives (not per-bucket)
        // Per-bucket tracking for peak/average
        int buckets_processed;      // Count of buckets
        int grids_this_bucket;      // Grids in current bucket
        int peak_grids_per_bucket;  // Max grids in any bucket
        int mpolys_this_bucket;     // Mpolys in current bucket
        int peak_mpolys_per_bucket; // Max mpolys in any bucket
        // Occlusion culling statistics
        int grids_culled;           // Grids skipped by Hi-Z
        int grids_shaded;           // Grids actually shaded
    } stats;

    // Performance timing statistics
    struct {
        double bucket_time;         // Total time in bucket processing
        double rasterize_time;      // Time spent in ri_sample_mpoly
        double transform_time;      // Time spent transforming grids
        int motion_mpolys;          // Micropolygons with motion blur
        int static_mpolys;          // Micropolygons without motion blur
    } timing;

    // Pre-allocated scratch buffers (avoids malloc in hot path)
    RhGridScratch* grid_scratch;

    // Reusable micropolygon list for bucket processing
    RhMicropolygonList bucket_mpolys;

    // Statistics output options (set via RiOption "statistics")
    struct {
        int endofframe;             // 0=off, non-zero=on (default 0)
        char filename[256];         // Text output file (empty = stderr)
        char jsonfilename[256];     // JSON output file (empty = none)
    } stats_options;

    // Hider options (set via RiHider)
    struct {
        int jitter;                 // 1 = enabled (default), 0 = disabled
    } hider_options;

    // Variable declarations (RiDeclare)
    RiDeclaration declarations[MAX_DECLARATIONS];
    int num_declarations;

    // Memory management for large scenes
    struct {
        size_t current_bytes;           // Current allocated memory estimate
        size_t peak_bytes;              // Peak memory usage
        size_t primitive_bytes;         // Memory used by primitives
        size_t mpoly_queue_bytes;       // Memory used by micropolygon queues
        size_t max_memory_bytes;        // Memory budget (0 = unlimited)
        int primitives_dropped;         // Primitives skipped due to memory limit
        int max_bucket_queue;           // Max mpolys queued per bucket (0 = unlimited)
        int mpolys_dropped;             // Mpolys dropped due to queue limit
        float opacity_threshold;        // Threshold for visibility culling (default 0.999)
    } memory;

    // A-buffer sample storage (allocated per-bucket during rendering)
    RhBucketSamples* bucket_samples;

    // Hierarchical Z-buffer for occlusion culling
    RhHiZBuffer* bucket_hiz;    // Points to global_hiz for testing
    RhHiZBuffer* global_hiz;    // Global persistent buffer

    // Progress bar settings
    bool show_progress;         // Enable progress bar output
    double render_start_time;   // Start time for ETA calculation

    // Near plane clipping
    float near_clip;            // Near clipping plane distance (default 0.1)
    float far_clip;             // Far clipping plane distance (default 1e30)

    // Shadow map output (when display_mode == RH_DISPLAY_Z)
    float* depth_buffer;        // Depth buffer for shadow map output
    RhMat4 world_to_ndc;        // Combined view * projection for shadow map

    // Render item pool for memory reuse
    struct {
        RhRenderItem** free_list;       // Pool of freed items for reuse
        int free_count;
        int free_capacity;
        int pool_hits;                  // Stats: items reused from pool
        int pool_misses;                // Stats: new allocations
    } item_pool;

    // Shader search path (colon-separated, with BUILTIN token)
    char shader_searchpath[512];
    char texture_searchpath[512];

    // Named coordinate systems (from CoordinateSystem RIB command)
    RhNamedCoordSys named_coord_sys[MAX_NAMED_COORD_SYS];
    int num_named_coord_sys;

    // Frame state (RiFrameBegin/End)
    bool frame_active;
    int frame_number;
    RiOptionsState saved_options;
    int lights_at_frame_begin;    // Track lights to clean up
    int objects_at_frame_begin;   // Track objects to clean up
} RiContextData;

// --- Accessor Functions ---
// Defined in ri_context.c

RiContextData* ri_get_ctx(void);
RiAttributeState* ri_curr(void);

// --- Declaration Functions ---
// Defined in ri_declare.c

const RiDeclaration* ri_lookup_declaration(const char* name);
int ri_declaration_float_count(const RiDeclaration* decl);
void ri_install_standard_declarations(void);
int ri_parse_inline_token(const char* token,
                           char* bare_name_out, int bare_name_size,
                           char* decl_string_out, int decl_size);

// --- Render Functions ---
// Defined in ri_render.c

void ri_mpoly_list_init(RhMicropolygonList* list);
void ri_mpoly_list_push(RhMicropolygonList* list, const RhMicropolygon* mpoly);
void ri_mpoly_list_clear(RhMicropolygonList* list);
void ri_mpoly_list_free(RhMicropolygonList* list);

RhRenderItem* ri_render_item_create(const RhPrimitive* p, const RhMat4* transform, const RhColor* color);
void ri_render_item_destroy(RhRenderItem* item);

// --- Primitive Functions ---
// Defined in ri_primitive.c

void ri_add_geometry(RhPrimitive* p);

// --- Shader Loading ---
// Defined in ri_primitive.c

#include "rh_sl_vm.h"
RhSLProgram* sl_load_program(const char* name);
void ri_sl_cache_clear(void);

RhTexture* ri_texture_load(const char* filename, RhTextureFormat format);
RhShadowMap* ri_shadowmap_read(const char* filename);

/* Try to load a shader from a single directory prefix.
   Returns cached or newly loaded program, or NULL. */
RhSLProgram* sl_try_load_from_dir(const char* name, const char* dir);

/* Iterate search path elements. Calls cb for each element.
   is_builtin=true for BUILTIN token, dir is prefix for file elements.
   Callback returns true to stop. */
typedef bool (*RiSearchPathCallback)(bool is_builtin, const char* dir, void* user);
void ri_iterate_searchpath(RiSearchPathCallback cb, void* user);

// --- Utility Functions ---

/* Convert displacement sphere radius from named coordsys to object space.
 * "object" returns d directly. Other spaces: divide by min column-scale of
 * obj_to_world (conservative over-estimate for non-uniform scale). */
static inline float ri_displace_bound_in_obj(float d, const char* coordsys,
                                              const RhMat4* obj_to_world) {
    if (strcmp(coordsys, "object") == 0) return d;
    float s0 = sqrtf(obj_to_world->m[0][0]*obj_to_world->m[0][0] +
                     obj_to_world->m[1][0]*obj_to_world->m[1][0] +
                     obj_to_world->m[2][0]*obj_to_world->m[2][0]);
    float s1 = sqrtf(obj_to_world->m[0][1]*obj_to_world->m[0][1] +
                     obj_to_world->m[1][1]*obj_to_world->m[1][1] +
                     obj_to_world->m[2][1]*obj_to_world->m[2][1]);
    float s2 = sqrtf(obj_to_world->m[0][2]*obj_to_world->m[0][2] +
                     obj_to_world->m[1][2]*obj_to_world->m[1][2] +
                     obj_to_world->m[2][2]*obj_to_world->m[2][2]);
    float ms = s0; if (s1 < ms) ms = s1; if (s2 < ms) ms = s2;
    return d / (ms > 1e-6f ? ms : 1.0f);
}

// Check if two matrices are approximately equal (used in ri_state.c)
static inline bool rh_mat4_equal(RhMat4 a, RhMat4 b) {
    const float eps = 1e-6f;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (fabsf(a.m[i][j] - b.m[i][j]) > eps) {
                return false;
            }
        }
    }
    return true;
}

// High-resolution timing helper for profiling
static inline double ri_get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// Integer clamp
static inline int ri_clamp_int(int val, int min_val, int max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

#endif // RI_INTERNAL_H
