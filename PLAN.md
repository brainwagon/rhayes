# Plan for RenderMan Interface Specification 3.1 Compliance

## 1. Executive Summary
This document outlines the development roadmap to upgrade the current "Rhayes" software renderer to be compliant with the **RenderMan Interface Specification (RISpec) 3.1**. The current implementation supports basic quadrics, polygons, and a simple immediate-mode rasterizer. The goal is to implement a full REYES architecture with bucketing, advanced geometry (Patches/NURBS), programmable shading support, and sampling features (Motion Blur/DOF).

## 2. Architecture & Core Systems (Phase 1)
**Goal:** Move from immediate-mode recursion to a robust Split/Dice/Bucket pipeline.

### 2.1 Bucket Scheduler
- **Concept:** Divide screen into tiles (buckets). Sort primitives into buckets based on bounding box. Render one bucket at a time to minimize memory usage.
- **Tasks:**
    - Implement `RhBucket` struct and primitive lists.
    - Replace `ri_render_recursive` with a `WorldEnd` loop: `Split -> Bucket -> Dice -> Rasterize`.
    - Implement `RiHider` to control bucket size/order.

### 2.2 Retained Geometry & Instancing
- **Concept:** Store geometry definitions for later reuse.
- **Tasks:**
    - Implement `RiObjectBegin` / `RiObjectEnd` to capture primitives into a list.
    - Implement `RiObjectInstance` to re-inject retained primitives with a new transform.

### 2.3 Attributes & State Management
- **Concept:** Full stack for all RISpec attributes.
- **Tasks:**
    - Expand `RiAttributeState` to include `ShadingRate`, `Surface`, `Displacement`, `LightList`.
    - Implement `RiAttributeBegin`/`End` properly (push/pop full state).

## 3. Advanced Geometry (Phase 2)
**Goal:** Support curved surfaces and procedural geometry.

### 3.1 Bicubic Patches
- **Concept:** `RiPatch "bicubic"`.
- **Tasks:**
    - Implement Bezier, B-Spline, Catmull-Rom basis matrices (`RiBasis`).
    - Implement splitting logic for Bicubic patches (De Casteljau or matrix subdivision).
    - Implement dicing for Bicubic patches.

### 3.2 NURBS (`RiNuPatch`)
- **Concept:** Non-Uniform Rational B-Splines.
- **Tasks:**
    - Implement Knot Vector handling.
    - Implement Homogeneous coordinate splitting (4D).
    - Support `RiTrimCurve` (optional, but core for production NURBS).

### 3.3 Procedurals (`RiProcedural`)
- **Concept:** User-defined geometry generation.
- **Tasks:**
    - Implement the callback mechanism.
    - Integrate with the splitting loop (call subdivide func when bound is reached).

## 4. Shading System (Phase 3)
**Goal:** Replace hardcoded shading with a flexible shader binding system.

### 4.1 Shader Interface
- **Concept:** C-style function pointers for shaders (simulating RSL).
- **Tasks:**
    - Define `RhShaderContext` (P, N, Cs, Os, du, dv, etc.).
    - Implement `RiSurface(name, params)` to bind C functions by name.
    - Implement Parameter Binding: Parse variable argument lists (Tokens) and map them to shader variables.

### 4.2 Standard Shaders
- **Tasks:**
    - Implement `constant`, `matte`, `metal`, `plastic`, `paintedplastic`.
    - Implement lights: `ambientlight`, `pointlight`, `distantlight`, `spotlight`.

### 4.3 Displacement Mapping
- **Concept:** Modify geometry before shading.
- **Tasks:**
    - Integrate `RiDisplacement` shader execution into the Dicing stage.
    - Update Bounds logic to account for displacement (displacement bounds).

## 5. Sampling & Anti-Aliasing (Phase 4)
**Goal:** High-quality output.

### 5.1 Stochastic Sampling
- **Concept:** Jittered sample positions within pixels.
- **Tasks:**
    - Update `RhRasterizer` to store sub-pixel samples (A-Buffer or Sample List).
    - Implement `RiPixelSamples` and `RiPixelFilter`.

### 5.2 Motion Blur
- **Concept:** Temporal sampling.
- **Tasks:**
    - Implement `RiMotionBegin`/`End` to store moving keys.
    - Interpolate geometry/transforms at sample time.

### 5.3 Depth of Field
- **Concept:** Lens sampling.
- **Tasks:**
    - Implement `RiDepthOfField` (f-stop, focal length).
    - Jitter eye position on the lens disk during sampling.

## 6. Implementation Roadmap
1.  **Bucketing**: Refactor `main.c`/`ri.c` to use a bucket list.
2.  **Shader Infra**: Create the shader parameter system.
3.  **Patches**: Add `RiPatch`.
4.  **Instancing**: Add Object hooks.
5.  **Sampling**: Upgrade Rasterizer.

## 7. Verification
- Use the "Cornell Box" and "Teapot" scenes (standard REYES tests).
- Validate against RISpec 3.1 C headers.