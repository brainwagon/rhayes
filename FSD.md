# Functional System Description: REYES-style Software Renderer

## 1. Introduction
This project aims to implement a fast, portable, software-based 3D rendering library based on the REYES (Render Everything You Ever Saw) architecture, originally developed by Lucasfilm/Pixar. The renderer will be written in vanilla C (C99 standard) with minimal external dependencies to ensure maximum portability across different hardware and operating systems.

## 2. Design Goals
*   **Portability:** Pure C implementation with no reliance on OS-specific graphics APIs (like OpenGL, DirectX, or Vulkan) or heavy third-party libraries.
*   **Performance:** optimized for software execution, utilizing efficient memory management and algorithms suitable for general-purpose CPUs.
*   **Quality:** High-quality image synthesis supporting motion blur, depth of field, and curved surfaces via micropolygon rendering.
*   **Simplicity:** A clean, modular codebase that is easy to understand, maintain, and port.

## 3. System Architecture
The renderer follows the classic REYES pipeline, processing geometric primitives through a series of stages to produce the final image.

### 3.1. The Pipeline
1.  **Scene Ingestion**: Reading geometry, lights, camera settings, and attributes from an input format (custom or standard).
2.  **Bounding & Culling**: Computing screen-space bounding boxes for primitives. Objects completely off-screen are discarded immediately.
3.  **Splitting**: Large primitives are recursively split into smaller sub-primitives until they are small enough to be diced.
4.  **Dicing**: Primitives are subdivided into a grid of "micropolygons" (quadrilaterals approx. 1/2 pixel in size).
5.  **Shading**: Shaders are executed on the micropolygon vertices. This separates shading from hidden surface removal, a key REYES characteristic.
6.  **Busting/Sampling**: Micropolygons are diced into screen-space samples.
7.  **Hiding**: Hidden surface removal is performed. A generic "bucket" scheduler will likely be used to manage memory, rendering the image in tiles.
8.  **Filtering**: Samples are filtered to produce final pixel colors (Anti-Aliasing).

## 4. Core Modules

### 4.1. Core / Math
*   Basic types (Vectors, Matrices, Colors, Quaternions).
*   Math utilities (interpolation, noise functions).
*   Memory Arena/Pool allocators for high-performance dynamic allocation during the frame.

### 4.2. Geometry Engine
*   **Primitive Types**: Support for Quadrics (Sphere, Cylinder, Disk, Cone) and Parametric Patches (Bezier, B-Spline).
*   **Transformations**: Hierarchical transformation stack.
*   **Subdivision**: Logic to split primitives based on screen-space criteria.

### 4.3. Shading System
*   **Surface Shaders**: Functions to determine the color of a surface (e.g., Plastic, Metal, Matte).
*   **Light Shaders**: Definitions for light sources (Ambient, Point, Distant, Spot).
*   **Imager Shaders**: Post-processing on pixel data.
*   This will be implemented via C function pointers or a simple interpreted bytecode to maintain the "Vanilla C" constraint.

### 4.4. Rasterizer (Hiding)
*   **Bucket Scheduler**: Divides the screen into buckets (tiles) to process geometry locally, minimizing memory footprint.
*   **Sample Buffer**: Storage for sub-pixel samples within a bucket (A-Buffer or Z-Buffer approach).
*   **Stochastic Sampling**: Jittered sample positions for anti-aliasing, motion blur, and depth of field.

### 4.5. I/O Interface
*   **Scene Input**: A parser for a simplified RIB-like (RenderMan Interface Bytestream) structure or a custom API.
*   **Image Output**: A writer for standard image formats (PPM for absolute minimal dependency, or simple TGA/BMP).

## 5. Implementation Constraints
*   **Language**: ISO C99.
*   **Dependencies**: Standard C Library (`libc`). No other external libraries for the core renderer.
*   **Build System**: Makefile or a simple shell script.

## 6. Future Expansion
*   Displacement mapping.
*   Texture mapping support (requires a lightweight image reader).
*   Multi-threading support (pthreads or platform-agnostic worker queues).

## 7. The RenderMan specification
*   Available from https://groups.csail.mit.edu/graphics/classes/6.838/S97/rispec31_4.pdf
