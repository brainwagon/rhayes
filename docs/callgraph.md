# Call Graph for `bin/render`

This document outlines the function call hierarchy for the `bin/render` executable, starting from the entry point.

## Entry Point: `main` (src/render.c)

The `main` function initializes the RIB parser and drives the rendering process.

- `main`
    - `rib_parser_create` (src/rib_parse.c)
    - `rib_parser_parse_file` (src/rib_parse.c)
        - *Parses the RIB file and invokes callbacks in `ri_render_callbacks`*
    - `rib_parser_destroy` (src/rib_parse.c)

## Callback Interface (`ri_render_callbacks`)

The parser invokes these static wrapper functions in `src/render.c`, which translate the callback signature to the public RenderMan API (`Ri*` functions).

- `render_Begin` -> `RiBegin` (src/ri_context.c)
- `render_End` -> `RiEnd` (src/ri_context.c)
- `render_Option` -> `RiOptionV` (src/ri_options.c)
- `render_Format` -> `RiFormat` (src/ri_options.c)
- `render_Projection` -> `RiProjectionV` (src/ri_options.c)
- `render_Display` -> `RiDisplay` (src/ri_options.c)
- `render_WorldBegin` -> `RiWorldBegin` (src/ri_render.c)
- `render_WorldEnd` -> `RiWorldEnd` (src/ri_render.c)
- `render_Translate`/`Rotate`/`Scale` -> `RiTranslate`/`RiRotate`/`RiScale` (src/ri_state.c)
- `render_Surface` -> `RiSurfaceV` (src/ri_primitive.c)
- `render_Sphere` -> `RiSphereV` (src/ri_primitive.c)
- `render_Polygon` -> `RiPolygonV` (src/ri_primitive.c)
- *(And other geometric primitives)*

## Core Rendering Pipeline

The rendering logic is primarily driven by `RiWorldBegin` (setup) and `RiWorldEnd` (execution).

### Setup Phase: `RiWorldBegin` (src/ri_render.c)
- `RiWorldBegin`
    - `rh_raster_create` (src/rh_raster.c)
    - `rh_raster_clear` (src/rh_raster.c)
    - *Allocates buckets and initializes state*
    - `RiAttributeBegin` (src/ri_state.c)

### Geometry Processing Phase
Called when primitives are encountered between `WorldBegin` and `WorldEnd`.

- `RiSphereV` / `RiPolygonV` / etc. (src/ri_primitive.c)
    - `rh_prim_create_*` (src/rh_geometry.c)
    - `ri_parse_primvars` (src/ri_primitive.c)
    - `ri_add_geometry` (src/ri_primitive.c)
        - `ri_add_to_buckets` (src/ri_primitive.c)
            - `ri_render_item_create` (src/ri_render.c)
            - `rh_prim_bound` (src/rh_geometry.c)
            - *Adds item to relevant buckets based on screen-space bounds*

### Execution Phase: `RiWorldEnd` (src/ri_render.c)
This is the REYES algorithm implementation.

- `RiWorldEnd`
    - *Loops over all buckets (y, then x)*
    
    #### 1. Splitting & Dicing (Geometry -> Micropolygons)
    - `ri_process_item_recursive` (src/ri_render.c)
        - `rh_mat4_mul` (src/rh_math.c)
        - `ri_compute_screen_area` (src/ri_render.c)
        - **If splitting needed:**
            - `rh_prim_split` (src/rh_geometry.c)
            - `ri_process_item_recursive` (Recursive call)
        - **If diceable:**
            - `ri_compute_grid_size` (src/ri_render.c)
            - `rh_grid_create` (src/rh_geometry.c)
            - `rh_prim_dice` (src/rh_geometry.c)
            
            ##### Shading
            - `item->shader` (Function pointer)
                - `rh_shader_surface_plastic` / `matte` / etc. (src/rh_shader.c)
            
            ##### Grid Conversion
            - `ri_grid_to_mpolys_motion` (src/ri_render.c)
                - `ri_mpoly_list_push` (src/ri_render.c)
                - *Pushes micropolygons to current or future buckets*

    #### 2. Sampling & Rasterization (Micropolygons -> Pixels)
    - *Sorts bucket items front-to-back*
    - `ri_hiz_create` (src/ri_render.c) - *Hierarchical Z-buffer for occlusion*
    - *Loops over queued micropolygons in bucket*
        - `ri_sample_mpoly` (src/ri_render.c)
            - `ri_precompute_motion_cache` (src/ri_render.c) *If motion blur enabled*
            - *Loops over pixels in bounding box*
                - `ri_spatial_jitter` (src/ri_render.c)
                - `ri_temporal_hash` (src/ri_render.c)
                - `ri_edge_function` (src/ri_render.c) *Point-in-triangle test*
                - `ri_hiz_update` (src/ri_render.c)
                - `rh_image_set_pixel_with_opacity` (src/rh_image.c)

    #### 3. Filtering & Output
    - `ri_hiz_destroy` (src/ri_render.c)
    - *Filter/Downsample super-sampled buffer (if applicable)*
        - `rh_image_create` (src/rh_image.c)
        - `rh_image_get_pixel` (src/rh_image.c)
        - `rh_image_set_pixel_with_opacity` (src/rh_image.c)
    - `rh_image_save_png_channels` (src/rh_image.c)
        - `lodepng_encode32` (src/lodepng.c)
    - `RiAttributeEnd` (src/ri_state.c)

## Low-Level Math & Utility (Commonly Called)
- `src/rh_math.c`
    - `rh_mat4_mul`, `rh_mat4_inverse`, `rh_vec3_normalize`, `rh_vec3_dot`
- `src/rh_geometry.c`
    - `rh_prim_eval_point`, `rh_prim_eval_derivs`

## Memory Management
- `ri_render_item_destroy` (src/ri_render.c)
- `ri_mpoly_list_free` (src/ri_render.c)
- `rh_grid_destroy` (src/rh_geometry.c)
