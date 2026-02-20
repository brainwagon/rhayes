# RenderMan 3.1 Transforms: A Practical Overview 📐✨

Below is a concise, practical guide to how transforms work in the RenderMan Interface Specification (RISpec) 3.1, including:
- The transformation pipeline and matrices
- All coordinate spaces you can target with the `transform` shadeop
- How those spaces relate to geometry and shaders
- How to move the camera before WorldBegin
- How view/projection/screen/NDC/raster spaces are defined
- A sample RIB file demonstrating the essentials

---

## 1) The Transformation Pipeline 🚀

Conceptually, geometry and shading flow through these spaces:

- object → world → camera → screen → NDC → raster

With matrices and mappings:
- p_world = M_objectToWorld · p_object
- p_camera = V · p_world, where V = inverse(C2W)
- p_screen = Projection(p_camera)  // perspective or orthographic
- p_ndc = MapScreenToNDC(p_screen) // screen window → [0,1]^2
- p_raster = MapNDCToRaster(p_ndc) // scale to pixel resolution, pixel centers at (i+0.5, j+0.5)

Notes:
- Right-handed system; the default camera looks down the −Z axis; +X right, +Y up.
- Inside WorldBegin: modeling transforms build M_objectToWorld.
- Before WorldBegin: transforms build C2W (camera-to-world). The view matrix is V = inverse(C2W).

---

## 2) Modeling vs. Camera Transforms 🔁

- Inside WorldBegin/WorldEnd
  - Transform stack = modeling
  - Transforms (Translate/Rotate/Scale/Transform/ConcatTransform) position geometry in world space
  - TransformBegin/TransformEnd: push/pop only transforms
  - AttributeBegin/AttributeEnd: push/pop attributes (and also save/restore the current transform)

- Before WorldBegin
  - The same Transform/Translate/Rotate operations define the camera-to-world transform (C2W)
  - Combined with Projection/ScreenWindow/Clipping/Format/PixelSamples etc., this defines the view

---

## 3) Coordinate Spaces for the `transform` Shadeop 🧭

RSL `transform` converts points/vectors/normals between named spaces. Common forms:
- x' = transform("tospace", x)                  // from current/shader space to tospace
- x' = transform("fromspace", "tospace", x)

Type-aware:
- If x is point, vector, or normal, the correct math is used automatically (normals use inverse-transpose, etc.)
- For explicit control you can also use vtransform/ntransform (if available), but `transform` typically suffices

Built-in targetable spaces:
- "object" — Local space of the current primitive (before modeling transforms)
- "world" — After modeling transforms; global scene space for lights and geometry
- "camera" — Camera space; camera at origin looking along −Z
- "screen" — Post-projection space in units of the ScreenWindow rectangle
- "NDC" — Normalized device coordinates; screen window mapped to [0,1]×[0,1]
- "raster" — Pixel space; NDC scaled to image resolution; pixel centers are at half-integers
- "current" — The space in which the shader’s globals (e.g., P, N) are currently provided
- "shader" — Synonym/alias for the shader’s current space in many RenderMan implementations
- any user-named system created by CoordinateSystem "name" in RIB

How spaces are defined relative to geometry/shaders:
- Geometry is authored in "object"; modeling transforms move it into "world"
- Shaders receive P, N, I, L, etc. in the current/shader space, which can be set via:
  - Attribute "shading" "space" ["object"|"world"|"camera"|<named>]
- The shadeop can reference:
  - Any built-in space above
  - Any space named by CoordinateSystem "myspace"
  - "current"/"shader" (the space of the shader inputs)

---

## 4) Named Coordinate Systems (CoordinateSystem) 🏷️

- RIB command: CoordinateSystem "name"
  - Captures the current transform at that point in the RIB stream
  - That name becomes a first-class space you can use in RIB (e.g., ConcatTransform "name") and in shaders: transform("name", P)
- Useful for:
  - Per-object local frames
  - Rigging reference frames
  - Light or camera-relative computations inside shaders

---

## 5) Camera Setup Before WorldBegin 🎥

Place camera with the same transform commands you use for modeling—but issue them before WorldBegin:

- Projection "perspective" "fov" [fovy_degrees] or "orthographic"
- Format xres yres pixelaspect
- ScreenWindow left right bottom top
- Clipping hither yon
- DepthOfField fstop focallength focaldistance
- Shutter open close
- PixelSamples sx sy, PixelFilter, etc.
- Then: Translate/Rotate/Scale/Transform/ConcatTransform to set C2W
  - Move camera “back” by translating along +Z to see objects near the origin (camera looks down −Z)
  - The final CTM at WorldBegin time is C2W; the renderer uses V = inverse(C2W) internally

Animation:
- Put setup inside FrameBegin/FrameEnd, vary transforms per frame

---

## 6) Screen, NDC, and Raster Definitions 🧮

- screen space:
  - After projection from camera space
  - X/Y measured in ScreenWindow units (user-configurable rectangle)
  - Z is the post-projection depth (nonlinear in perspective)
- NDC:
  - X/Y: affine mapping of screen window to [0,1]×[0,1]
  - Z: mapped to [0,1] according to projection/clipping (near→0, far→1)
- raster:
  - X/Y: NDC scaled to image resolution; pixel centers at (i+0.5, j+0.5)
  - Handy for screen-aligned effects in shaders

Defaults and handedness:
- If ScreenWindow is not explicitly set, it’s chosen to match image aspect ratio (so circles don’t appear stretched)
- Raster (0,0) is typically the lower-left of the image

---

## 7) View/Projection Matrices (Conceptual) 🧊

- Camera-to-world (C2W): Built from transforms before WorldBegin
- View matrix (world→camera): V = inverse(C2W)
- Modeling (object→world): M_objectToWorld from transforms inside WorldBegin
- Perspective projection (conceptually):
  - x_screen = (x_cam / −z_cam) * s_x
  - y_screen = (y_cam / −z_cam) * s_y
  - where s_x, s_y depend on fov and ScreenWindow
- Orthographic projection:
  - x_screen = x_cam * s_x
  - y_screen = y_cam * s_y
- Then screen → NDC → raster via linear window/scale mappings

You normally don’t need to build these matrices by hand in RIB/RSL; this is how the renderer conceptualizes the pipeline.

---

## 8) Sample RIB File 🧾

This example sets up a perspective camera, moves it before WorldBegin, defines a named coordinate system, and renders a simple scene. It also shows how to place geometry with modeling transforms and how to control shading space.

```
# frame setup
FrameBegin 1
    Format 640 480 1
    PixelSamples 4 4
    Projection "perspective" "fov" [45]
    # Match screen window to aspect ratio (optional explicit setup)
    # ScreenWindow -1.333333 1.333333 -1 1

    # Camera placement (before WorldBegin): build C2W
    Translate 0 0 6         # move camera back along +Z
    Rotate -15 1 0 0        # tilt camera down 15 degrees
    # You can add more transforms here to animate the camera

    # Define a named camera-relative space for shaders to use
    CoordinateSystem "cam_space"

    WorldBegin
        # Lighting
        LightSource "distantlight" 1 "from" [0 5 5] "to" [0 0 0]
        LightSource "ambientlight" 2 "intensity" [0.05]

        # Define a world-space reference frame for shading
        CoordinateSystem "world_ref"

        # Optionally set the space in which shaders receive P, N, etc.
        Attribute "shading" "space" ["world"]

        # Ground plane
        AttributeBegin
            Surface "matte" "Kd" [0.8]
            TransformBegin
                Rotate 90 1 0 0
                Translate 0 0 0
                Scale 10 10 1
                Polygon "P" [-1 -1 0   1 -1 0   1 1 0   -1 1 0]
            TransformEnd
        AttributeEnd

        # A named object space
        AttributeBegin
            TransformBegin
                Translate -1 0 0
                CoordinateSystem "left_obj"
                Surface "plastic" "Kd" [0.6] "Ks" [0.3]
                Sphere 1 -1 1 360
            TransformEnd
        AttributeEnd

        # Another object with different modeling transform
        AttributeBegin
            TransformBegin
                Translate 1 0 0
                Rotate 30 0 1 0
                CoordinateSystem "right_obj"
                Surface "plastic" "Kd" [0.6] "Ks" [0.5]
                Cone 1 2 360
            TransformEnd
        AttributeEnd

    WorldEnd
FrameEnd
```

What this demonstrates:
- Camera is placed via Translate/Rotate before WorldBegin
- Named spaces: "cam_space", "world_ref", "left_obj", "right_obj"
- Shaders will receive P, N in world space due to Attribute "shading" "space" ["world"]

---

## 9) Using `transform` in Shaders (At-a-Glance) 🧩

Typical RSL idioms:
- Convert current P to world:
  - point Pw = transform("world", P);
- Convert a normal from world to object:
  - normal Nobj = transform("world", "object", N);
- Use a named space captured in RIB:
  - point Plocal = transform("left_obj", P);
- Convert to raster for screen-aligned effects:
  - point Pr = transform("raster", P);

Tip:
- "current" and "shader" are useful when you want to be explicit about the shader’s working space:
  - point Pc = transform("current", "world", P); // same as transform("world", P) if "current"==shader space

---

## 10) Practical Tips & Gotchas 🧠

- Always think in two phases:
  - Before WorldBegin: camera transform and viewing parameters
  - Inside WorldBegin: modeling transforms
- Use CoordinateSystem to create stable references for shading (e.g., texture projections, rig frames)
- AttributeBegin/End can scope transforms too (it saves/restores the current CTM), but use TransformBegin/End if you only want to isolate transforms
- For perspective: depth in NDC/raster is nonlinear; don’t expect linearity with camera-space Z
- If your object disappears, check Clipping hither/yon and camera placement (translate camera back along +Z)

---

If you want, I can add a minimal RSL shader snippet demonstrating `transform` against the named spaces created in the sample RIB. 🎯
