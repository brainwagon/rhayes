# Debug Plan: Transparent Teapot Occlusion Bug

## Problem Description

When rendering a transparent teapot (Opacity [0.5 0.5 0.5]) over a checkerboard ground plane, there are black triangular holes at certain polygon corners under the teapot body. The ground plane should be fully visible through the transparent teapot, but some portions are being incorrectly culled.

The holes appear at consistent locations when the camera rotates, suggesting this is a geometric culling issue rather than a rendering order issue.

## Test Scene

Location: `teapot-test/checker_shadow_render.rib`

Key setup:
- Teapot with `Opacity [0.5 0.5 0.5]` (50% transparent)
- `Sides 2` enabled globally
- Checkerboard consists of individual Polygon primitives
- Each polygon is a 2x2 unit square

## Root Cause Analysis

### Primary Suspect: Hi-Z Occlusion Culling

Location: `src/ri_render.c:1213-1220`

```c
// Hi-Z occlusion test - skip shading if grid is fully occluded
if (ctx->bucket_hiz &&
    ri_hiz_test_occluded(ctx->bucket_hiz, grid_min_x, grid_min_y,
                         grid_max_x, grid_max_y, grid_min_depth)) {
    ctx->stats.grids_culled++;
    rh_grid_destroy(grid);
    return;
}
```

**The Bug**: When the transparent teapot is rendered:
1. Its micropolygons write to the zbuffer and Hi-Z buffer
2. When ground plane polygons are subsequently processed, their grids get Hi-Z tested
3. Grids that are geometrically "behind" the teapot's zbuffer values fail the Hi-Z test
4. These grids are culled entirely and never rasterized
5. But they SHOULD be rendered because the teapot is transparent

### Secondary Issue: Zbuffer Updates for Transparent Samples

Location: `src/ri_render.c:899-904`

```c
// Always update zbuffer for depth tracking (needed for shadow maps)
int idx = y * r->width + x;
if (z < r->zbuffer[idx]) {
    r->zbuffer[idx] = z;
    ri_hiz_update(ctx->bucket_hiz, x, y, z);
}
```

Transparent samples update both the zbuffer and Hi-Z buffer. This causes subsequent opaque geometry at greater depths to be culled, even though it should be visible through the transparent surface.

## Debugging Tasks

### Task 1: Verify Hi-Z Culling is the Cause

Add debug output to count Hi-Z culled grids:
```c
if (ctx->bucket_hiz &&
    ri_hiz_test_occluded(...)) {
    static int hiz_cull_debug = 0;
    if (hiz_cull_debug < 20) {
        fprintf(stderr, "DEBUG: Grid culled by Hi-Z at depth %.2f, bounds (%d,%d)-(%d,%d)\n",
                grid_min_depth, grid_min_x, grid_min_y, grid_max_x, grid_max_y);
        hiz_cull_debug++;
    }
    ctx->stats.grids_culled++;
    rh_grid_destroy(grid);
    return;
}
```

### Task 2: Test with Hi-Z Disabled

Temporarily disable Hi-Z culling to confirm it's the cause:
```c
// TEMPORARILY DISABLED FOR DEBUGGING
// if (ctx->bucket_hiz &&
//     ri_hiz_test_occluded(...)) {
//     ...
// }
```

If the holes disappear, Hi-Z culling is confirmed as the cause.

### Task 3: Investigate Rendering Order

Check if the issue is caused by render order:
- Teapot rendered before ground plane → teapot occludes ground
- Ground plane rendered before teapot → should work correctly

Add debug output to track primitive processing order.

## Potential Fixes

### Fix Option A: Disable Hi-Z for Transparent Primitives

Track whether any transparent geometry has been rendered to the Hi-Z buffer. If so, disable Hi-Z culling for subsequent primitives in that bucket.

**Pros**: Simple to implement
**Cons**: Loses Hi-Z optimization when transparency is present

### Fix Option B: Don't Update Hi-Z for Transparent Samples

Only update Hi-Z when rendering fully opaque samples:
```c
// Only update zbuffer/Hi-Z for opaque samples
bool is_opaque = (final_opacity.r >= 0.99f &&
                  final_opacity.g >= 0.99f &&
                  final_opacity.b >= 0.99f);
if (z < r->zbuffer[idx] && is_opaque) {
    r->zbuffer[idx] = z;
    ri_hiz_update(ctx->bucket_hiz, x, y, z);
}
```

**Pros**: Preserves Hi-Z optimization for opaque geometry
**Cons**: May break shadow map generation (needs separate depth tracking)

### Fix Option C: Track Accumulated Opacity in Hi-Z

Modify Hi-Z to track both minimum depth AND accumulated opacity. Only cull grids when both:
1. Grid is behind the Hi-Z depth
2. Accumulated opacity at that depth is >= threshold

**Pros**: Most correct solution
**Cons**: Complex to implement, increases Hi-Z memory/computation

### Fix Option D: Two-Pass Rendering

Render opaque geometry first, then transparent geometry:
1. Pass 1: Render all opaque primitives with normal Hi-Z culling
2. Pass 2: Render all transparent primitives without Hi-Z culling

**Pros**: Correct results, maintains Hi-Z benefits for opaque geometry
**Cons**: Requires sorting primitives by opacity, two passes

## Recommended Approach

Start with **Task 2** (disable Hi-Z) to confirm the diagnosis.

Then implement **Fix Option B** as the simplest correct solution, but with a modification:
- Keep a separate depth buffer for shadow maps (already exists: `ctx->depth_buffer`)
- Only update the main zbuffer/Hi-Z for opaque samples
- Always write to the shadow depth buffer

This preserves:
1. Correct transparency compositing via A-buffer
2. Hi-Z optimization for opaque geometry
3. Shadow map generation

## Files to Modify

| File | Changes |
|------|---------|
| `src/ri_render.c` | Hi-Z culling logic, zbuffer update conditions |
| `include/ri_internal.h` | May need flag to track transparency in bucket |

## Verification

After fix:
1. Render `teapot-test/checker_shadow_render.rib`
2. Entire checkerboard should be visible through transparent teapot
3. Run full test suite to ensure no regressions
4. Verify shadow maps still work correctly
