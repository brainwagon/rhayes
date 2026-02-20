✦ Based on a static analysis of the codebase, here are the functions in the src directory that appear to be defined but
  not used (called) by any other code in src, include, or tests.

  Unused Functions by File

  `src/rh_geometry.c`
   * rh_prim_diceable

  `src/rh_image.c`
   * rh_image_set_pixel (Likely rh_image_set_pixel_with_opacity is used instead)
   * rh_image_save_ppm
   * rh_image_save_png (Wrapper; rh_image_save_png_channels is used instead)

  `src/rh_math.c`
   * rh_clamp

  `src/rh_raster.c`
   * rh_raster_draw_grid

  `src/rh_shader.c`
   * rh_shader_get_primvar

  `src/rh_texture.c`
   * rh_texture_destroy
   * rh_texture_sample_bilinear

  `src/ri_declare.c`
   * ri_declaration_float_count

  `src/ri_primitive.c`
   * RiCylinder (Varargs wrapper; RiCylinderV is used internally)
   * RiParaboloid (Varargs wrapper)
   * RiDisk (Varargs wrapper)
   * RiTorus (Varargs wrapper)
   * RiHyperboloid (Varargs wrapper)
   * RiPatch (Varargs wrapper)

  `src/ri_render.c`
   * ri_subpixel_list_insert
   * ri_subpixel_list_composite
   * ri_bucket_samples_create
   * ri_bucket_samples_clear
   * ri_bucket_samples_destroy
   * ri_bucket_samples_get
   * ri_compute_screen_area_motion

  `src/rib_output.c`
   * rib_output_begin_stream

  `src/rib_parse.c`
   * rib_parser_get_line
