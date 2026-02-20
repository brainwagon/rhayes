Okay, here's a summary of the shadow algorithm described in "Rendering Antialiased Shadows with Depth Maps" by Reeves, Salesin, and Cook, focusing on the key elements for implementation:

**Core Idea: Percentage Closer Filtering (PCF)**

Instead of directly filtering depth values, PCF reverses the order of filtering and comparison:

1.  **Compare depth map values to the surface depth:** For each sample point within a region of the depth map, compare the depth value stored in the depth map to the depth of the surface being rendered at that point (in light space).  This results in a binary value (1 if in shadow, 0 if not).
2.  **Filter the binary results:**  Filter the resulting array of binary shadow values to determine the proportion of the region in shadow. This proportion is then used to attenuate the light's intensity.

**Algorithm Steps:**

1.  **First Pass: Creating the Depth Map (from the Light's Perspective)**
    *   Render the scene from the light source's point of view.
    *   For each pixel in the depth map, store the *z* value (depth) of the closest object to the light source. Use floating-point format.
    *   *Optional*: Store a bounding box encompassing all pixels with finite depths to reduce storage.

2.  **Second Pass: Rendering the Scene (from the Camera's Perspective)**
    *   For each surface point (or region) to be shaded:
        *   Transform the surface point (or region) into light space (coordinates relative to the light source).
        *   Determine the corresponding region in the depth map.
        *   Apply Percentage Closer Filtering (PCF):
            *   **Sampling:** Select multiple sample points within the depth map region.  The paper suggests several sampling methods:
                *   (a) Random sampling within a bounding box.
                *   (b) Gaussian distribution within a bounding box.
                *   (c) Jittered sampling: Partition the bounding box into subregions and sample each one with jitter (preferred method).
                *   (d) Sampling only within the geometric boundary of the region (more complex).
            *   **Comparison:** For each sample point:
                *   Compare the *z* value from the depth map at that sample point to the transformed *z* value of the surface point (in light space).  If the surface point's *z* is *behind* the depth map *z* (plus a bias), consider the sample point to be in shadow.  `if (surfaceZ > depthMapZ + bias) inShadow++;`
            *   **Filtering:** Calculate the proportion of sample points that are in shadow: `shadowFactor = (float)inShadow / numSamples;`
        *   Attenuate the light's intensity based on the `shadowFactor`.  Repeat this process for each light source that casts shadows.

**Key Parameters:**

*   `NumSamples`: Number of samples used for PCF.  Higher values reduce noise but increase computation. A value of 16 is often used.
*   `ResFactor`:  Artificially enlarges the sampling region, creating softer shadows. Values between 2 and 4 are often used.  Must be at least 1.
*   `Bias`:  Offsets the surface depth slightly to prevent incorrect self-shadowing (Moiré patterns). A small range of random bias values for each sample is suggested. Bias values depend on the scale of the scene.
*   Depth Map Resolution: Higher resolution depth maps capture more detail but require more storage. The paper suggests starting with a depth map the same size or twice as large as the final image resolution.
*   `MinSamples`, `MinSize`: Control the minimum number of samples and minimum size of the sampling region, especially for oddly shaped regions in the depth map.

**Implementation Considerations:**

*   **Data Structures:** Store depth maps as floating-point values.
*   **Light Trees:** The authors used light trees to manage different light source characteristics (shadow softness, whether a light casts shadows, etc.).
*   **Optimization:**
    *   Ignore objects that never cast shadows when creating depth maps.
    *   Use a bounding box to limit the storage required for depth maps.
    *   Consider a tile-based caching scheme for depth maps to improve locality.

**Limitations (and potential Claude tasks):**

*   **No Motion Blur:**  The algorithm doesn't handle motion blur on shadows.
*   **Approximate Penumbrae:** The shadow edges are softened, but not true penumbrae that depend on the distances between light, object, and receiver.
*   **Transparency/Translucency:** The algorithm doesn't address shadows cast by transparent or translucent objects.

This summary should provide Claude with a solid foundation for implementing the percentage closer filtering algorithm for rendering soft shadows.

