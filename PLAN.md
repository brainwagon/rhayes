# Plan for RenderMan Interface Specification 3.1 Compliance

## 1. Executive Summary
This document outlines the development roadmap to upgrade the current "Rhayes" software renderer to be compliant with the **RenderMan Interface Specification (RISpec) 3.1**. The implementation now includes a REYES architecture with bucketing, bicubic/bilinear patches, programmable shading, and sampling features.

## 2. Architecture & Core Systems (Phase 1) - COMPLETE

### 2.1 Bucket Scheduler - DONE
- Screen divided into tiles (buckets)
- Primitives sorted into buckets based on bounding box
- `RiHider` controls bucket size and jitter

### 2.2 Retained Geometry & Instancing - DONE
- `RiObjectBegin` / `RiObjectEnd` capture primitives
- `RiObjectInstance` re-injects retained primitives with new transform

### 2.3 Attributes & State Management - DONE
- Full attribute stack with `RiAttributeBegin`/`End`
- Supports `ShadingRate`, `Surface`, `Color`, `Opacity`, `LightList`

## 3. Advanced Geometry (Phase 2) - PARTIAL

### 3.1 Bicubic Patches - DONE
- `RiPatch "bicubic"` with 16 control points
- `RiBasis` supports Bezier, B-Spline, Catmull-Rom, Hermite, Power matrices
- Parametric splitting and dicing

### 3.2 Bilinear Patches - DONE
- `RiPatch "bilinear"` with 4 control points
- Proper bilinear interpolation
- Test cases: basic, non-planar (saddle), textured

### 3.3 NURBS (`RiNuPatch`) - NOT STARTED
- Non-Uniform Rational B-Splines
- Knot vector handling
- Homogeneous coordinate splitting
- `RiTrimCurve` support

### 3.4 Procedurals (`RiProcedural`) - NOT STARTED
- User-defined geometry generation
- Callback mechanism for lazy evaluation

## 4. Shading System (Phase 3) - COMPLETE

### 4.1 Shader Interface - DONE
- C-style function pointers for shaders
- `RhShaderContext` with P, N, Cs, Os, u, v, du, dv, etc.
- `RiSurface(name, params)` binds shaders by name
- Parameter binding via token/value pairs

### 4.2 Surface Shaders - DONE
- `constant` - unlit flat color
- `matte` - diffuse Lambert shading
- `plastic` - diffuse + specular
- `metal` - specular only with Fresnel
- `paintedplastic` - texture-mapped plastic
- `shinymetal` - reflective metal
- `randomgrid` / `random` - procedural patterns

### 4.3 Light Sources - DONE
- `ambientlight` - uniform ambient
- `distantlight` - directional (sun-like)
- `pointlight` - omnidirectional point
- `spotlight` - cone with falloff

### 4.4 Displacement Mapping - NOT STARTED
- Modify geometry before shading
- Displacement bounds for correct culling

## 5. Sampling & Anti-Aliasing (Phase 4) - COMPLETE

### 5.1 Stochastic Sampling - DONE
- `RiPixelSamples` for subpixel sampling
- `RiPixelFilter` for reconstruction filters
- Jittered sample positions

### 5.2 Motion Blur - DONE
- `RiMotionBegin`/`End` stores moving keys
- `RiShutter` controls shutter open/close
- Geometry/transform interpolation at sample time

### 5.3 Depth of Field - DONE
- `RiDepthOfField` (fstop, focal length, focal distance)
- Lens sampling for bokeh effects

## 6. Primitives Support

| Primitive | Status |
|-----------|--------|
| Sphere | DONE |
| Cylinder | DONE |
| Cone | DONE |
| Paraboloid | DONE |
| Hyperboloid | DONE |
| Torus | DONE |
| Disk | DONE |
| Polygon | DONE |
| Patch (bilinear) | DONE |
| Patch (bicubic) | DONE |
| NuPatch | NOT STARTED |
| Curves | NOT STARTED |
| Points | NOT STARTED |
| Blobby | NOT STARTED |
| Procedural | NOT STARTED |

## 7. Remaining Work

### High Priority
1. **NURBS** - `RiNuPatch` with trim curves
2. **Curves** - `RiCurves` for hair/fur
3. **Displacement** - Pre-shade geometry modification

### Medium Priority
4. **Procedurals** - `RiProcedural` for lazy geometry
5. **Points** - `RiPoints` for particles
6. **Area Lights** - Extended light primitives

### Lower Priority
7. **Blobby** - Implicit surfaces
8. **Subdivision Surfaces** - `RiSubdivisionMesh`
9. **Volume Rendering** - Atmosphere/interior shaders

## 8. Verification
- Test suite with 94 passing tests covering:
  - All primitive types
  - All shader types
  - All light types
  - Motion blur
  - Pixel sampling
  - Transform hierarchies
- Teapot scene renders correctly (28 Bezier patches)
