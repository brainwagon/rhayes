# Hi-Z Occlusion Culling - Investigation Notes

## Problem Statement

After implementing transparency support with A-buffer compositing, opaque geometry (specifically a cat model with 35,288 polygons) exhibited mysterious "holes" where grids were being incorrectly culled by the Hi-Z occlusion test.

## Root Cause Analysis

### Issue 1: Hi-Z Updated Per-Sample Instead of Per-Opacity-Threshold

**Original behavior:** Hi-Z was updated immediately when any opaque sample was rasterized, using that sample's z value.

**Problem:** This didn't account for transparency correctly. The Hi-Z should represent the depth where a pixel becomes fully opaque (accumulated opacity >= threshold), not just where any opaque sample exists.

**Attempted fix:** Track `opaque_z` per subpixel list in the A-buffer - the z value where accumulated opacity reaches `othresh` (default 0.999). Update Hi-Z from this value after micropolygon rasterization.

### Issue 2: Timing Mismatch Between Hi-Z Test and Hi-Z Updates

**The fundamental problem:** Hi-Z test happens during grid processing (in `ri_process_item_recursive`), but Hi-Z is updated after micropolygon rasterization. This creates a timing mismatch:

1. Items are sorted front-to-back within a bucket
2. Item A processed: grids tested against Hi-Z, mpolys collected
3. Item A's mpolys rasterized, Hi-Z updated
4. Item B processed: grids tested against Hi-Z (now reflects Item A)
5. **If Item B is adjacent to Item A at similar depth, Item B's grids may be incorrectly culled**

Similarly, queued micropolygons from previous buckets update Hi-Z before current bucket's items are tested, causing adjacent polygons spanning bucket boundaries to be culled.

### Issue 3: Epsilon Tolerance Insufficient

Even with proportional epsilon (tried 0.1%, 1%, 5% of depth), adjacent polygons on meshes sharing edges can have depth differences smaller than any reasonable epsilon due to:
- Floating point precision in vertex positions
- Interpolation across micropolygon quads
- Bucket boundary effects

## Current Solution (Hi-Z Disabled)

Hi-Z updates are disabled during bucket processing. The Hi-Z buffer is created and initialized to `far_clip`, but never updated. This means:
- All grids pass the Hi-Z test (min_depth < far_clip always)
- 0 grids culled
- Correct rendering, no holes
- **Lost optimization:** No early rejection of occluded geometry

## Potential Future Solutions

### Option 1: Larger Depth Separation Requirement
Only cull grids that are **significantly** behind the opaque depth (e.g., 10-20% of depth). This preserves culling for clearly occluded geometry while avoiding adjacent-polygon issues.

### Option 2: Deferred Hi-Z Updates
Don't update Hi-Z until ALL items in a bucket have been processed. This prevents intra-bucket culling issues but still allows inter-bucket culling.

### Option 3: Item-Aware Culling
Track which items have contributed to Hi-Z. Only apply Hi-Z culling to grids from items that weren't adjacent (in scene graph or spatially) to items that updated Hi-Z.

### Option 4: Conservative Hi-Z
Use a more conservative approach where Hi-Z stores the **maximum** opaque_z across a tile of pixels rather than per-pixel values. This provides coarser but safer culling.

### Option 5: Two-Pass Approach
1. First pass: Process all grids without Hi-Z culling, collect depth information
2. Build accurate Hi-Z from complete depth buffer
3. Second pass: Use Hi-Z for culling (only useful for multi-pass rendering)

## Code Locations

- Hi-Z test: `ri_render.c:1300-1307` (in `ri_process_item_recursive`)
- Hi-Z update function: `ri_render.c:524` (`ri_hiz_update_from_abuffer` - currently unused)
- A-buffer opaque_z tracking: `ri_internal.h:168` (field in `RhSubpixelList`)
- Bucket processing: `ri_render.c:1505-1615`

## Test Results

With Hi-Z disabled:
- Cat model (35,288 polygons): 0 grids culled, no holes
- All transparency tests pass
- Main scene renders correctly
