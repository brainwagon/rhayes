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
    g_ctx->stack[0].opacity = (RhColor){1.0f, 1.0f, 1.0f};

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
    g_ctx->stack[0].current_atmosphere_shader = NULL;
    g_ctx->stack[0].current_atmosphere_params = NULL;
    g_ctx->stack[0].shading_rate = 1.0f; // Default ShadingRate

    // Default Orientation and Sides
    g_ctx->stack[0].orientation_lh = true;        // Left-handed by default (RenderMan convention)
    g_ctx->stack[0].reverse_orientation = 0;
    g_ctx->stack[0].sides = 1;                    // Front-only by default (backface culling)

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

    // Hi-Z buffer pointers
    g_ctx->bucket_hiz = NULL;
    g_ctx->global_hiz = NULL;

    // Initialize render item pool
    g_ctx->item_pool.free_list = NULL;
    g_ctx->item_pool.free_count = 0;
    g_ctx->item_pool.free_capacity = 0;
    g_ctx->item_pool.pool_hits = 0;
    g_ctx->item_pool.pool_misses = 0;

    // Default clip distances
    g_ctx->near_clip = 0.1f;
    g_ctx->far_clip = 1e30f;

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
        // Free global HiZ if it exists
        // Note: ri_hiz_destroy is static in ri_render.c, so we can't call it here easily
        // unless we move its declaration or use a helper. 
        // For now, we rely on RiWorldEnd to clean it up properly.
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

// --- Frame Structure ---

void RiFrameBegin(RtInt frame) {
    if (!g_ctx) return;

    g_ctx->frame_active = true;
    g_ctx->frame_number = frame;

    // Save all options
    g_ctx->saved_options.xres = g_ctx->xres;
    g_ctx->saved_options.yres = g_ctx->yres;
    strcpy(g_ctx->saved_options.display_name, g_ctx->display_name);
    g_ctx->saved_options.display_channels = g_ctx->display_channels;
    g_ctx->saved_options.projection = g_ctx->projection;
    g_ctx->saved_options.pixel_samples_x = g_ctx->pixel_samples_x;
    g_ctx->saved_options.pixel_samples_y = g_ctx->pixel_samples_y;
    g_ctx->saved_options.pixel_filter = g_ctx->pixel_filter;
    g_ctx->saved_options.filter_width_x = g_ctx->filter_width_x;
    g_ctx->saved_options.filter_width_y = g_ctx->filter_width_y;
    g_ctx->saved_options.dof_fstop = g_ctx->dof_fstop;
    g_ctx->saved_options.dof_focallength = g_ctx->dof_focallength;
    g_ctx->saved_options.dof_focaldistance = g_ctx->dof_focaldistance;
    g_ctx->saved_options.shutter_open = g_ctx->shutter_open;
    g_ctx->saved_options.shutter_close = g_ctx->shutter_close;
    g_ctx->saved_options.hider_options.jitter = g_ctx->hider_options.jitter;
    g_ctx->saved_options.stats_options.endofframe = g_ctx->stats_options.endofframe;
    strcpy(g_ctx->saved_options.stats_options.filename, g_ctx->stats_options.filename);
    strcpy(g_ctx->saved_options.stats_options.jsonfilename, g_ctx->stats_options.jsonfilename);
    g_ctx->saved_options.show_progress = g_ctx->show_progress;

    // Track resources for cleanup
    g_ctx->lights_at_frame_begin = g_ctx->num_lights;
    g_ctx->objects_at_frame_begin = g_ctx->objects_count;
}

void RiFrameEnd(void) {
    if (!g_ctx || !g_ctx->frame_active) return;

    // Restore all options
    g_ctx->xres = g_ctx->saved_options.xres;
    g_ctx->yres = g_ctx->saved_options.yres;
    strcpy(g_ctx->display_name, g_ctx->saved_options.display_name);
    g_ctx->display_channels = g_ctx->saved_options.display_channels;
    g_ctx->projection = g_ctx->saved_options.projection;
    g_ctx->pixel_samples_x = g_ctx->saved_options.pixel_samples_x;
    g_ctx->pixel_samples_y = g_ctx->saved_options.pixel_samples_y;
    g_ctx->pixel_filter = g_ctx->saved_options.pixel_filter;
    g_ctx->filter_width_x = g_ctx->saved_options.filter_width_x;
    g_ctx->filter_width_y = g_ctx->saved_options.filter_width_y;
    g_ctx->dof_fstop = g_ctx->saved_options.dof_fstop;
    g_ctx->dof_focallength = g_ctx->saved_options.dof_focallength;
    g_ctx->dof_focaldistance = g_ctx->saved_options.dof_focaldistance;
    g_ctx->shutter_open = g_ctx->saved_options.shutter_open;
    g_ctx->shutter_close = g_ctx->saved_options.shutter_close;
    g_ctx->hider_options.jitter = g_ctx->saved_options.hider_options.jitter;
    g_ctx->stats_options.endofframe = g_ctx->saved_options.stats_options.endofframe;
    strcpy(g_ctx->stats_options.filename, g_ctx->saved_options.stats_options.filename);
    strcpy(g_ctx->stats_options.jsonfilename, g_ctx->saved_options.stats_options.jsonfilename);
    g_ctx->show_progress = g_ctx->saved_options.show_progress;

    // Remove lights created in frame
    g_ctx->num_lights = g_ctx->lights_at_frame_begin;

    // Free objects created in frame
    for (int i = g_ctx->objects_at_frame_begin; i < g_ctx->objects_count; i++) {
        if (g_ctx->objects[i]) {
            for (int j = 0; j < g_ctx->objects[i]->count; j++) {
                rh_prim_free_data(&g_ctx->objects[i]->items[j].prim);
            }
            free(g_ctx->objects[i]->items);
            free(g_ctx->objects[i]);
            g_ctx->objects[i] = NULL;
        }
    }
    g_ctx->objects_count = g_ctx->objects_at_frame_begin;

    g_ctx->frame_active = false;
}
