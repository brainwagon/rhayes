# Coordinate Spaces in RenderMan 3.1 Specification (Updated)

Below is the updated summary of coordinate spaces as defined in the RenderMan 3.1 specification (from the provided document at [https://groups.csail.mit.edu/graphics/classes/6.838/S97/rispec31_4.pdf](https://groups.csail.mit.edu/graphics/classes/6.838/S97/rispec31_4.pdf)). This markdown now includes information on "Current Space" alongside the previously covered spaces to assist a coding agent in implementing these in a Reyes-compliant renderer.

---

## Overview of Coordinate Spaces 📍
Coordinate spaces in RenderMan define the context in which geometry, transformations, and rendering operations occur. Each space serves a specific purpose in the rendering pipeline, from defining object geometry to final screen output. The specification outlines the following key spaces:

- **Object Space**: The local coordinate system of an individual object.
- **World Space**: The global coordinate system where all objects are placed.
- **Camera Space**: The coordinate system relative to the camera's position and orientation.
- **NDC (Normalized Device Coordinates)**: A normalized 3D space used for clipping and projection.
- **Screen Space**: The 2D space of the final rendered image.
- **Current Space**: A dynamic coordinate system reflecting the active transformation state at a given point in the rendering process.
- **Shader Space**: the coordinate system that was active when the shader (either a LightSource or other shaders like 
Surface) appears in the RIB file.

Understanding and implementing transformations between these spaces is critical for a Reyes-compliant renderer.

---

## Detailed Breakdown of Coordinate Spaces 🛠️

### 1. Object Space
- **Definition**: The local coordinate system of a geometric object, defined at the time of object creation (via `RiGeometry` or other primitives).
- **Key Characteristics**:
  - Origin and axes are specific to the object.
  - Transformations (e.g., `RiTranslate`, `RiRotate`, `RiScale`) applied to the object modify its position/orientation relative to world space.
- **Coding Notes**:
  - Store object geometry in its local space before applying transformations.
  - Maintain a transformation matrix (e.g., 4x4 matrix) to convert from object to world space.
- **Testing Notes**:
  - Test object transformations (translate, rotate, scale) to ensure correct mapping to world space.
  - Verify that multiple transformations concatenate correctly.

### 2. World Space
- **Definition**: The global coordinate system where all objects are positioned after transformations.
- **Key Characteristics**:
  - Serves as the reference frame for lighting and camera placement.
  - Objects are transformed from object space to world space using the current transformation matrix (CTM) at the time of definition.
- **Coding Notes**:
  - Maintain a stack of transformation matrices (via `RiPushMatrix`/`RiPopMatrix`) to handle hierarchical transformations.
  - Apply the CTM to object vertices to map them to world space.
- **Testing Notes**:
  - Test hierarchical transformations (e.g., nested objects) to ensure correct world space positioning.
  - Validate that world space coordinates remain consistent across different transformation stacks.

### 3. Camera Space
- **Definition**: The coordinate system relative to the camera, defined by `RiFrameBegin`/`RiFrameEnd` and camera parameters (e.g., `RiProjection`, `RiClipping`).
- **Key Characteristics**:
  - Origin is at the camera position, with axes aligned to the camera's view (typically z-axis along the view direction).
  - World-to-camera transformation is applied to position objects relative to the camera.
- **Coding Notes**:
  - Implement camera transformation matrix based on `RiProjection` (e.g., perspective or orthographic).
  - Transform world space points to camera space using the inverse of the camera’s transformation matrix.
- **Testing Notes**:
  - Test perspective and orthographic projections to ensure correct depth and field of view.
  - Verify that objects outside clipping planes (`RiClipping`) are correctly handled.

### 4. NDC (Normalized Device Coordinates)
- **Definition**: A normalized 3D coordinate system post-projection, where coordinates are mapped to a unit cube (typically [-1, 1] in x, y, z).
- **Key Characteristics**:
  - Used for clipping and preparing geometry for final rasterization.
  - Defined after applying the projection matrix (from `RiProjection`).
- **Coding Notes**:
  - Apply the projection matrix to camera space points to obtain NDC coordinates.
  - Handle clipping in NDC space to discard geometry outside the view frustum.
- **Testing Notes**:
  - Test clipping behavior for objects partially or fully outside the NDC cube.
  - Validate that NDC coordinates map correctly for different projection types.

### 5. Screen Space
- **Definition**: The 2D coordinate system of the final image, typically with (0,0) at the top-left corner and (width, height) at the bottom-right.
- **Key Characteristics**:
  - Derived from NDC by mapping x and y coordinates to pixel coordinates (via `RiFormat` for resolution).
  - Depth (z) information may be retained for hidden surface removal (e.g., z-buffering in Reyes rendering).
- **Coding Notes**:
  - Map NDC x and y to screen space using viewport transformation (based on `RiFormat` resolution).
  - Implement Reyes-specific micropolygon rasterization in screen space for sampling and shading.
- **Testing Notes**:
  - Test resolution changes (`RiFormat`) to ensure correct mapping to screen pixels.
  - Verify that depth values are correctly used for occlusion in screen space.

### 6. Current Space
- **Definition**: A conceptual coordinate system that represents the active transformation state at any given point in the rendering process, as defined by the current transformation matrix (CTM).
- **Key Characteristics**:
  - Not a fixed space like world or camera space, but rather a dynamic state reflecting the cumulative effect of all transformations applied up to the current point in the scene description.
  - Typically corresponds to world space or a modified version of it based on transformation operations (e.g., after `RiTranslate`, `RiRotate`, etc.).
  - Used implicitly when defining geometry or applying transformations; objects are placed relative to the "current" transformation state.
- **Coding Notes**:
  - Maintain the CTM as the representation of the "current space" in the renderer.
  - Update the CTM with each transformation call (e.g., `RiTranslate`, `RiScale`) to reflect the current coordinate system.
  - When geometry is defined, transform it from object space to the current space using the CTM before further processing to world or camera space.
- **Testing Notes**:
  - Test the CTM updates with sequences of transformation calls to ensure the "current space" reflects the expected state.
  - Verify that geometry defined after transformations is correctly positioned relative to the current space.
  - Test nested transformation blocks (using `RiTransformBegin`/`RiTransformEnd`) to ensure the current space is properly managed on the transformation stack.

### 6. Shader Space
- **Definition**: When a shader is created in the RIB file, it creates a coordinate system which represents
the current space at that point in the RIB file.
- It is commonly used inside light shaders to transform points into a meaningful coordinate system for the light.

---

## Transformation Pipeline 🔄
The RenderMan pipeline involves a series of transformations between these spaces. Below is the typical flow for a point in the rendering process:

1. **Object Space → Current Space**:
   - Apply the current transformation matrix (CTM) at the time of object definition to position the object relative to the active transformation state.
2. **Current Space → World Space**:
   - If not already in world space, the current space may resolve to world space as the base global coordinate system (depending on transformation hierarchy).
3. **World Space → Camera Space**:
   - Apply the inverse camera transformation matrix (based on camera position/orientation).
4. **Camera Space → NDC**:
   - Apply the projection matrix (perspective or orthographic) to map to normalized coordinates.
5. **NDC → Screen Space**:
   - Map NDC x and y to pixel coordinates using viewport transformation.

**Coding Notes**:
- Use 4x4 homogeneous matrices for all transformations to handle translation, rotation, scaling, and projection.
- Store intermediate results for debugging and testing purposes, especially for the CTM representing the current space.

**Testing Notes**:
- Create test cases for each transformation step to ensure accuracy (e.g., known points transformed through the pipeline).
- Test edge cases, such as points at clipping boundaries or extreme transformations, and verify current space behavior with complex transformation sequences.

---

## Reyes-Specific Considerations 🎨
Since the target is a Reyes-compliant renderer, additional considerations apply:
- **Micropolygon Generation**: Reyes rendering dices geometry into micropolygons in world or camera space before projection to screen space.
- **Sampling and Shading**: Shading occurs in world space or object space (depending on shader bindings), while final visibility is determined in screen space.
- **Current Space in Reyes**: The current space affects how geometry is initially positioned before dicing; ensure the CTM is correctly applied during this step.
- **Coding Notes**:
  - Implement dicing algorithms to split primitives into micropolygons before projection.
  - Ensure shaders can access coordinates in the appropriate space (e.g., via `transform()` in shaders).
- **Testing Notes**:
  - Test micropolygon generation for various primitive types (e.g., spheres, patches) in different spaces.
  - Validate shader outputs when transforming coordinates between spaces, including from current space.

---

## Key RenderMan Interface Calls to Implement 🖥️
The following RenderMan Interface (RI) calls directly affect coordinate spaces, including current space, and must be implemented:

- **RiTransformBegin/RiTransformEnd**: Manage transformation stack for object-to-world transformations and define the scope of the current space.
- **RiTranslate/RiRotate/RiScale**: Define object transformations in the current space, updating the CTM.
- **RiProjection**: Set the projection type (perspective/orthographic) for camera-to-NDC transformation.
- **RiClipping**: Define near/far clipping planes in camera space.
- **RiFormat**: Set resolution for NDC-to-screen mapping.
- **RiFrameBegin/RiFrameEnd**: Establish camera context and transformations.
- **RiPushMatrix/RiPopMatrix**: Manage the transformation stack to save and restore the current space state.

**Coding Notes**:
- Maintain a transformation stack to handle nested transformations and preserve the current space state.
- Ensure RI calls update the appropriate matrices (e.g., CTM, projection matrix) and reflect changes in the current space.

**Testing Notes**:
- Test each RI call with known inputs to verify matrix updates, especially for current space modifications.
- Create scenes with complex transformation hierarchies to stress-test the stack and current space behavior.

---

## Practical Implementation Tips 💡
- **Matrix Library**: Use or implement a robust 4x4 matrix library for transformations (e.g., multiplication, inversion).
- **Debugging**: Log transformation matrices and intermediate coordinates for each space, including the CTM for current space, during development.
- **Performance**: Optimize matrix operations and avoid redundant transformations in the pipeline.
- **Shader Support**: Provide built-in functions (like `transform()` in RSL) to convert between spaces, including access to current space if needed for shading.

---

## Summary of Test Cases to Develop 🧪
To ensure a correct implementation, develop the following test cases:
- **Object-to-Current**: Test transformation of geometry using the CTM to ensure correct positioning in the current space.
- **Current-to-World**: Test resolution of current space to world space in various transformation contexts.
- **World-to-Camera**: Test camera positioning and orientation with different projections.
- **Camera-to-NDC**: Test clipping and projection accuracy for perspective and orthographic views.
- **NDC-to-Screen**: Test mapping to different resolutions and aspect ratios.
- **Reyes Pipeline**: Test micropolygon dicing and rasterization for visibility and shading accuracy.
- **Edge Cases**: Test extreme transformations, near/far clipping, objects at NDC boundaries, and complex current space transformation sequences.

---

This updated summary includes the concept of "Current Space" as a dynamic state represented by the current transformation matrix (CTM) in the RenderMan 3.1 specification. It provides a foundation for implementing coordinate spaces in a Reyes-compliant renderer. Use the coding and testing notes to guide development, and refer to the original specification for detailed parameter descriptions and edge-case behaviors. If further clarification is needed on specific RI calls or Reyes algorithms, let me know! 😊
