# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
make              # Build the project (outputs ./rhayes binary)
make clean        # Remove object files and binary
./rhayes          # Run renderer, generates output.ppm
```

The project uses strict C99 with `-Wall -Wextra -Werror -pedantic` flags. Only dependency is libm (math library).

## Architecture Overview

Rhayes implements a **REYES (Render Everything You Ever Saw) pipeline** with a partial RenderMan Interface implementation.

### Pipeline Stages

```
RenderMan API calls → Graphics State Stack → Scene Collection (WorldBegin/End)
    → Splitting (recursive subdivision) → Dicing (micropolygon grids)
    → Shading (per-vertex) → Z-buffer hiding → Rasterization → PPM output
```

### Module Responsibilities

| File | Purpose |
|------|---------|
| `ri.c` | RenderMan API implementation, graphics state management, rendering orchestration |
| `rh_geometry.c` | Primitive evaluation (sphere, cylinder, cone, patches), splitting, dicing to micropolygon grids |
| `rh_shader.c` | Surface shaders (constant, matte, plastic) and lighting calculations |
| `rh_raster.c` | Triangle rasterization with Z-buffer, micropolygon grid rendering |
| `rh_math.c` | Vector/matrix operations, transformations |
| `rh_image.c` | PPM image output with gamma correction |
| `main.c` | Test scene setup |

### Key Data Flow

1. **Primitives** (`RhPrimitive`) store parametric geometry with u/v domain [0,1]
2. **Splitting** recursively subdivides primitives until screen-space small enough
3. **Dicing** evaluates parametric surface into `RhMicroGrid` (positions, normals, colors)
4. **Shading** transforms vertices to camera space, executes shader function via `RhShaderContext`
5. **Rasterization** converts micropolygon quads to triangles, Z-tests, writes to framebuffer

### Graphics State

- `RiTransformBegin/End` and `RiAttributeBegin/End` manage a hierarchical state stack
- Current transform, color, and shader are inherited when pushing stack
- Lights stored in context, passed to shaders via `light_list` in `RhShaderContext`

## Naming Conventions

- `Ri*` - RenderMan API functions (public interface)
- `Rh*` - Internal types and functions
- `RhFloat` - Portable float typedef (currently `float`)

## Implementation Notes

- **Light struct synchronization**: `RhLight_ShaderView` in `rh_shader.c` must exactly match `RhLight` in `ri.c` (including the `transform` field) or array indexing will be corrupted
- **Polygon normals**: Computed via finite differences; vertex winding order affects normal direction
- **Sphere pole normals**: Finite difference method degenerates at poles; fallback to analytic normal `N = normalize(P)`

## Coordinate System

RenderMan uses a **left-handed** coordinate system:
- **+X** points right
- **+Y** points up
- **+Z** points INTO the screen (toward objects, away from viewer)
- Camera at origin looks down the **+Z axis**
- Objects in front of the camera have **positive Z** in camera space
- Default orientation is left-handed (`RiOrientation "lh"`)

When setting up camera transforms before `WorldBegin`:
- `RiTranslate(0, y, -distance)` positions camera at negative Z looking toward positive Z
- Objects at/near the origin will be visible

## Key Reference Documents

- `PARTI.md` - RenderMan Interface Specification v3.1 (reference for API behavior)
- `FSD.md` - Functional System Description with design details
- `PLAN.md` - Development roadmap for RISpec compliance
