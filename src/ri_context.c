/**
 * ri_context.c - RenderMan context management
 *
 * Handles RiBegin, RiEnd, RiGetContext, RiContext, and the global context.
 */

#include "ri_internal.h"

// --- Global Context ---

static RiContextData* g_ctx = NULL;

// --- Accessor Functions ---

RiContextData* ri_get_ctx(void) {
    return g_ctx;
}

RiAttributeState* ri_curr(void) {
    return &g_ctx->stack[g_ctx->stack_ptr];
}

// --- Standard Tokens ---
// These are declared extern in ri.h and defined here

RtToken RI_P = "P";
RtToken RI_CZ = "Cz";
RtToken RI_INTENSITY = "intensity";
RtToken RI_LIGHTCOLOR = "lightcolor";
RtToken RI_FROM = "from";
RtToken RI_TO = "to";

// --- Context API ---

void RiBegin(RtToken name) {
    if (g_ctx) return;
    g_ctx = (RiContextData*)calloc(1, sizeof(RiContextData));

    // Defaults
    g_ctx->xres = 800;
    g_ctx->yres = 600;
    strcpy(g_ctx->display_name, "ri_output.png");
    g_ctx->display_channels = 4;  // RGBA by default

    // Default State
    g_ctx->stack_ptr = 0;
    g_ctx->stack[0].transform = rh_mat4_identity();
    g_ctx->stack[0].transform_t1 = rh_mat4_identity();
    g_ctx->stack[0].has_motion = false;
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
    g_ctx->stack[0].current_surface_shader = rh_shader_surface_plastic;
    g_ctx->stack[0].current_shader_params = NULL;
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

    // Default motion blur (no blur)
    g_ctx->shutter_open = 0.0f;
    g_ctx->shutter_close = 0.0f;
    g_ctx->motion_active = false;
    g_ctx->motion_sample_index = 0;
    g_ctx->motion_times[0] = 0.0f;
    g_ctx->motion_times[1] = 0.0f;

    // Initialize variable declaration table with standard declarations
    g_ctx->num_declarations = 0;
    ri_install_standard_declarations();

    // Default hider options (jitter enabled)
    g_ctx->hider_options.jitter = 1;

    // Initialize memory tracking
    g_ctx->memory.current_bytes = 0;
    g_ctx->memory.peak_bytes = 0;
    g_ctx->memory.primitive_bytes = 0;
    g_ctx->memory.mpoly_queue_bytes = 0;
    g_ctx->memory.max_memory_bytes = 0;  // 0 = unlimited
    g_ctx->memory.primitives_dropped = 0;
    g_ctx->memory.opacity_threshold = 0.999f;  // Default: cull when 99.9% opaque

    // A-buffer sample storage (allocated per-bucket during rendering)
    g_ctx->bucket_samples = NULL;

    // Initialize render item pool
    g_ctx->item_pool.free_list = NULL;
    g_ctx->item_pool.free_count = 0;
    g_ctx->item_pool.free_capacity = 0;
    g_ctx->item_pool.pool_hits = 0;
    g_ctx->item_pool.pool_misses = 0;

    (void)name;
}

void RiEnd(void) {
    if (g_ctx) {
        if (g_ctx->raster) rh_raster_destroy(g_ctx->raster);
        if (g_ctx->all_items) {
            // Items may have been freed during bucket processing; skip NULLs
            for (int i = 0; i < g_ctx->all_items_count; i++) {
                if (g_ctx->all_items[i]) ri_render_item_destroy(g_ctx->all_items[i]);
            }
            free(g_ctx->all_items);
        }
        // Free the item pool
        if (g_ctx->item_pool.free_list) {
            for (int i = 0; i < g_ctx->item_pool.free_count; i++) {
                free(g_ctx->item_pool.free_list[i]);
            }
            free(g_ctx->item_pool.free_list);
        }
        if (g_ctx->objects) {
            for (int i = 0; i < g_ctx->objects_count; i++) {
                RhObject* obj = g_ctx->objects[i];
                for (int j = 0; j < obj->count; j++) rh_prim_free_data(&obj->items[j].prim);
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
