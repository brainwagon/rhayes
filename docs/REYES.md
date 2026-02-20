# Reyes Rendering Architecture — Implementation-Focused Summary 🧱🎬

A practical breakdown of the 1987 Reyes system (Cook, Carpenter, Catmull) geared toward implementers and extenders of a Reyes-style renderer.

---

## 1) Core Idea and Goals 🎯

- Core idea: Reduce all geometry to a common representation—subpixel-sized, flat-shaded micropolygons—then perform shading and visibility on these micropolygons.
- Primary goals:
  - Handle visually rich scenes with extreme geometric and textural complexity.
  - Prioritize speed for animation (minutes per frame).
  - High image quality with robust antialiasing and minimal artifacts.
  - Flexibility to incorporate diverse primitives and future techniques (via a “back door”).

---

## 2) Design Principles 🧩

- Natural coordinates:
  - Do each task where it’s natural: texture access in local object coordinates (uv), visibility in screen space.
- Vectorization:
  - Process similar work in batches (e.g., shade whole grids at once).
- Common representation:
  - Convert everything to micropolygons (flat-shaded quads ~0.5 px).
- Locality:
  - Geometric locality: process a primitive without touching others.
  - Texture locality: access textures sequentially; avoid thrash.
- Linearity and scale:
  - Time grows ~linearly with model size; support unbounded primitives via streaming and z-buffer compositing.
- Texture maps:
  - Optimize for heavy texture use; rely on prefiltered pyramids.

---

## 3) Core Data and Definitions 📦

- Micropolygon: Flat-shaded quadrilateral ~0.5 px per side (Nyquist scale).
- Grid: 2D array of micropolygons from dicing a primitive in its native parameterization (e.g., uv).
- z-buffer with stochastic subpixel samples:
  - Per-pixel: N jittered samples (e.g., 16).
  - Each sample stores visibility (and possibly multiple hits for transparency/CSG).
- CAT vs RAT textures:
  - CAT (Coherent Access Texture): s = a u + b, t = c v + d (linear in uv). Enables sequential access and zero runtime filtering with proper dicing and prefiltered pyramids.
  - RAT (Random Access Texture): arbitrary access (e.g., environment maps, decals) — more general, slower; cache carefully.
- Space conventions:
  - World space → Eye space → Screen space.
  - Hither/yon planes bound valid z range.
  - ε-plane (epsilon): small positive z in front of the eye to avoid perspective singularities for geometry spanning behind the eye.

---

## 4) End-to-End Pipeline 🛠️

High-level flow:
1. Initialize z-buffer with jittered samples per pixel.
2. Stream primitives (one at a time).
3. Bound in eye space; cull if out of z or frustum (with margin if filter > 1 px).
4. Dice or split:
   - If “diceable” → dice to a grid at ~0.5 px scale (target).
   - Else → split into smaller primitives; loop.
5. Shade the whole grid (object/uv space). Compute normals/tangents; resolve texture lookups.
6. Transform micropolygons to screen space.
7. Sample micropolygons against jittered sample points; z-test and store hits.
8. After all geometry: filter samples to pixels (reconstruction filter).
9. Output image.

Pseudo-code (abridged):

```text
init_zbuffer(samples_per_pixel, jitter_pattern)
for primitive in model_stream:
  bound_eye = primitive.bound_eye()
  if outside_hither_yon(bound_eye): continue
  bound_screen = project_to_screen(bound_eye)

  if outside_frustum(bound_screen): continue

  queue = [primitive]
  while queue:
    p = queue.pop()
    if spans_epsilon_and_hither(p): split_and_push(p, queue); continue
    if diceable(p, target_px=0.5, max_grid=limit): 
      grid = dice_to_grid(p, spacing_uv=pow2_aligned_for_CAT)
      prep_normals_tangents(grid)
      shade_grid(grid)               # in object/uv space
      for micro in grid:
        if outside_hither_yon(micro): continue
        micro_screen = to_screen(micro)
        for sample in covered_samples(micro_screen):
          z = interpolate_z(micro_screen, sample.xy)
          if z < zbuffer[sample].z: zbuffer[sample] = Hit(z, color, attrs)
    else:
      queue.extend(split(p))
filter_samples_to_pixels(zbuffer, reconstruction_filter)
save_image()
```

---

## 5) Geometry Processing Details 🧭

- Diceable test:
  - Estimate screen-space size with parametric derivatives; aim for ~0.5 px micropolygons.
  - Reject dicing if grid would be too large or have large size variation → split instead.
- Dicing:
  - In object/uv space; grid edges align with uv isolines.
  - For CAT textures, choose uv steps so s,t land on integer multiples of 1/2^k for pyramid alignment.
  - Use forward differencing for efficient subdivision on patches.
- Splitting:
  - Must monotonically shrink bounds; guarantees termination (important for ε-plane handling).
  - Example: large sphere → split into ~32 bicubic patches → dice patches.
- No clipping:
  - Use split-until-cullable around ε-plane and hither plane; cull outside ranges.
- Displacement maps:
  - Apply before visibility (they move geometry).
  - Must inflate bounds conservatively to include displacements.

---

## 6) Shading Strategy 🎨

- Shade entire grids at once (vectorizable and cache-friendly).
- Shade in object/uv space — no inverse perspective needed.
- Shade trees:
  - Programmable shading graphs; expect many texture channels (color, bump, displacement, shadows, env maps, etc.).
- Normals/tangents per grid:
  - Compute coherently for shading and bump mapping.
- Trade-off: shading-before-visibility can compute unused shading where there’s depth complexity; pays off via coherence and texture locality.

---

## 7) Texture System 🧵

- CAT optimization:
  - If s = a u + b, t = c v + d and grid aligns to power-of-two boundaries, and textures use prefiltered pyramids:
    - No runtime filtering for CATs (texture pixels align with micropolygons).
    - Access in scanline order for terrific locality.
- RATs:
  - Used for env maps, decals, any texture with s,t dependent on world-space vectors or normals.
  - Requires general filtering; use robust caches to avoid thrash.
- Filtering:
  - Prefiltered, pre-scaled resolution pyramids (MIP-like) recommended.
  - For RATs, trilinear or better can be used; beware quality vs cost.
- Texture locality:
  - Shade in uv order; batch requests per texture.
  - Avoid thrash by processing entire grids and keeping textures hot.

---

## 8) Visibility and Sampling 👁️

- Stochastic sampling (jittering):
  - Per pixel, subdivide into N subpixels (e.g., 4×4).
  - Place one sample per subpixel with random jitter to convert aliasing into noise.
- z-buffer:
  - Compare depth per sample (not per pixel).
  - Allows 3D compositing and a “back door” to merge samples from other rendering algorithms (e.g., ray tracing/radiosity).
- Filtering to pixels:
  - After storing all samples, filter to final pixels with a chosen reconstruction filter.
  - Expand frustum by filter radius so off-screen content influences edge pixels.

---

## 9) Memory and Scheduling 🧮

- Bucket rendering:
  - Divide screen into rectangular buckets.
  - First pass: assign primitives to buckets via screen bounds (e.g., upper-left bucket).
  - Process buckets in raster order:
    - Split/dice primitives in bucket.
    - Shade during dicing; dispatch micropolygons to all overlapped buckets.
    - Sample micropolygons held in the bucket; then free it.
  - Keeps z-buffer limited to bucket size and bounds memory.
- Grid size limit:
  - Enforce a max micropolygons per grid to cap memory; otherwise force split.

---

## 10) Extensions and “Back Door” 🚪

- Motion blur:
  - Jitter sample time per subpixel; move micropolygons to time.
  - Note: shading-before-sampling limits correct temporal shading; geometry motion is correct, shading temporal effects approximate.
- Depth of field:
  - Jitter lens position; project to sample positions accordingly.
- Transparency and CSG:
  - Per-sample A-buffer-like hit lists; sort by z; evaluate compositing or boolean ops.
- Shadow maps (depth map shadows):
  - Texture-based approach; coherent access possible by aligning with surface uvs when appropriate.
- Environment mapping:
  - Use RAT access; reduces need for full ray tracing.
- Back door:
  - Merge samples from ray tracing, radiosity, or other methods via z-buffer compositing at sample granularity (3D compositing before filtering).

---

## 11) Performance and Quality Notes ⚙️

- Complexity:
  - Rendering time ∝ number of micropolygons (and shading cost).
  - Micropolygons ∝ screen area coverage and scene complexity.
- Depth complexity:
  - Shading cost grows with average number of surfaces per sample; typical scenes are manageable if models avoid unneeded interior geometry.
- Advantages:
  - No clipping or inverse projections.
  - Strong vectorization opportunities.
  - Superb texture locality; CATs can be near-zero-cost per lookup.
- Disadvantages:
  - Shading before visibility can waste shading work behind occluders.
  - Some primitives hard to bound/dice (blobs, some particle systems).
  - Polygons lack a natural dicing parameterization compared to patches.

---

## 12) Implementation Checklist ✅

Data structures:
- Primitive interface:
  - bound_eye()
  - diceable(target_px, max_grid, variation_threshold)
  - dice() → Grid
  - split() → [Primitive]
- Grid:
  - uv topology, vertices, micropolygons, per-vertex attributes (P, N, tangents, uv, st, derivatives).
- Texture system:
  - CAT: uv → st linear mapping metadata; aligned grid generator.
  - RAT: filtered sampling API; cache and LRU.
  - Prefiltered pyramids on disk; streaming loader with per-texture window.
- Sample buffer (A-buffer variant):
  - Per sample: best hit or sorted hit list (for transparency/CSG).
- Buckets:
  - Z-buffer tiles, lists of pending primitives/micropolygons.

Key algorithms:
- Parametric derivative estimation to size dicing rate.
- Forward differencing for efficient grid generation.
- ε-plane split-until-cullable for near-eye robustness.
- Jitter pattern generation (stable/stochastic sequence per frame).
- Reconstruction filtering pass.

Parameters to expose:
- Target micropolygon size (default ~0.5 px).
- Max grid size; max variation across grid.
- Samples per pixel; filter kernel.
- ε-plane offset.
- Texture cache size and per-texture residency hints.
- Split thresholds (area, curvature/derivative bounds).

Robustness and edge cases:
- Displacement bounds inflation.
- Patch crack prevention:
  - Ensure shared edges dice compatibly (power-of-two uv alignment).
- Off-screen influence:
  - Expand frustum by filter radius.
- Numeric stability near eye (ε-plane) and huge perspective scales.
- RAT caching to avoid thrash; batch shading by texture locality.
- Motion blur and DoF require bound enlargement and time/lens jitter-aware splitting.

---

## 13) Practical Tips for Extensions 🧪

- Decoupled shading: cache shaded grids and reuse across nearby samples or frames when valid (careful with motion/deformation).
- Adaptive sampling: increase per-pixel samples where variance is high; preserves Reyes core while improving image quality/spend.
- Hybrid path tracing back door:
  - Use z-buffer samples to seed secondary rays only where needed (e.g., glossy highlights), then composite in 3D.
- GPU mapping:
  - Dice on CPU (or compute), rasterize micropolygons in screen space on GPU; or implement full micropolygon rasterizer.
- Texture graph compilation:
  - Pre-schedule RAT/CAT groups; fuse nodes for fewer fetches.

---

## 14) Minimal “Sphere-to-Grid” Example Flow 🧪

- Primitive: Sphere
  - diceable?
    - If big: split → patches (e.g., 32).
    - If small: dice directly.
  - Patches dice:
    - Choose uv steps aligned to power-of-1/2 for CATs.
    - Build grid; compute P, N, tangents via forward differencing.
  - Shade grid in uv order:
    - Evaluate shade tree; fetch textures (CAT sequentially; RAT via cache).
  - Visibility:
    - Transform micropolygons to screen; sample with jitter; z-test; store hits.
  - Filter:
    - Reconstruct pixel colors from subpixel samples.

---

## 15) When to Deviate 🔧

- If scenes are dominated by large, simple, untextured surfaces:
  - Consider non-Reyes rasterization to avoid over-dicing.
- If heavy global illumination is required:
  - Use the back door to integrate path tracing results (and possibly reduce shading-before-visibility cost by shading-on-demand per visible sample).
- If RAT usage dominates:
  - Invest more in RAT caches, request coalescing, and shading schedule that reduces random access.

---

This summary captures the Reyes architecture’s key constraints, data flows, and implementation hooks so you can build a robust micropolygon-based renderer and extend it with modern features while preserving its strengths in coherence, locality, and antialiasing. 🚀
