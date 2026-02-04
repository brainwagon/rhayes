# Fix: Buggy Hi-Z Occlusion Culling

## Problem Statement
The Hi-Z occlusion culling feature was previously disabled because it caused "mysterious holes" in opaque geometry (specifically the cat model). The root cause was a combination of intra-mesh precision issues and a timing mismatch between grid processing and Hi-Z updates.

### Original Bug Mechanics
1.  **Immediate Updates:** Hi-Z was updated immediately after each micropolygon was rasterized.
2.  **Intra-Mesh Culling:** Grid A (front) would update the Hi-Z buffer. Grid B (adjacent, same depth) would then be tested against the Hi-Z. Due to floating-point precision or interpolation differences at shared edges, Grid B's `min_depth` might be slightly greater than the value Grid A just wrote, causing Grid B to be culled incorrectly.
3.  **Inadequate Epsilon:** To avoid holes, a huge 5% epsilon was used, which effectively disabled most useful culling. Reducing the epsilon to a "normal" value (e.g., 0.1%) caused the holes to reappear.

## Implemented Solution

I have implemented a robust, hierarchical, and persistent Hi-Z system that fixes these issues and provides safe, efficient culling.

### 1. Global Persistent Hi-Z Buffer
Moved the Hi-Z buffer from a per-bucket allocation to a global allocation in the `RiContextData`. This allows occlusion information to persist across buckets, enabling inter-bucket culling which was previously impossible.

### 2. Hierarchical Hi-Z Pyramid
Implemented a full 8-level Hi-Z pyramid with **MAX-pooling**. 
- Level 0: Full supersampled resolution.
- Level i+1: Each pixel stores the **MAX** (furthest) depth of the 2x2 area in Level i.
- This is mathematically conservative for occlusion: if a grid is behind the MAX depth of a tile, it is guaranteed to be behind every pixel in that tile.

### 3. Conservative Hierarchical Test
Updated `ri_hiz_test_occluded` to use the pyramid. It now automatically selects the optimal pyramid level based on the grid's screen-space size.
- **Natural Edge Protection:** Because higher pyramid levels use MAX-pooling, any tile that touches an "empty" pixel (depth = far_clip) will have a MAX depth of far_clip. This automatically prevents culling of any grid that sits on the edge of an opaque region, solving the "holes at edges" problem without requiring a massive epsilon.

### 4. Deferred Updates (per RenderItem)
Moved the Hi-Z update call from the per-micropolygon level to the per-item level. 
- Grids within the same `RhRenderItem` no longer cull each other based on immediate updates.
- Subsequent items in the same bucket still benefit from culling.

### 5. A-Buffer Integration
The system now correctly uses the `opaque_z` value from the A-buffer subpixel lists. `opaque_z` represents the depth at which a pixel reaches the `othresh` (opacity threshold), ensuring that transparent objects are never culled incorrectly and never cause incorrect culling of things behind them until they are actually opaque.

### 6. Diagnostic Shader Stability
Fixed a bug where culled grids would skip the `grid_counter` increment. This caused diagnostic shaders like `random` and `randomgrid` to shift colors when culling was enabled. Culled grids now correctly increment the counter to maintain deterministic IDs.

## Results

### Verification (Cat Scene)
- **Baseline (Hi-Z Disabled):** Correct image.
- **New System (0.1% Epsilon):** **Identical to Baseline** (verified via `cmp`).
- **Efficiency:** Achieved **14.1% grid culling** on the cat model (culling the back-side of the mesh).
- **Stability:** All test suite cases pass (with the exception of known baseline failures).

## Modified Files
- `include/ri_internal.h`: Added `global_hiz` to context; updated `RhHiZBuffer` definition.
- `src/ri_context.c`: Handled initialization and cleanup of global Hi-Z.
- `src/ri_render.c`: 
    - Implemented `ri_hiz_update` with pyramid propagation.
    - Implemented hierarchical `ri_hiz_test_occluded`.
    - Implemented region-based pyramid propagation in `ri_hiz_update_from_abuffer`.
    - Reorganized `RiWorldBegin`/`RiWorldEnd` to handle global persistence and deferred updates.
