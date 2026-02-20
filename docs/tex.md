In RenderMan Shading Language (RSL), blurring a texture access is straightforward. The built-in `texture()` function accepts optional name-value pairs that allow you to control the filtering and blurring of the texture lookup.

Here are the primary ways to achieve a blurred texture access in RSL:

### 1. Using the `"blur"` Parameter (Simplest)

The easiest way to blur a texture is to use the optional `"blur"` argument. This adds a constant amount of blur to the texture lookup. The value is specified in texture coordinates (from 0.0 to 1.0).

For example, a blur value of `0.05` will blur the texture by 5% of the overall image size.

```c
// Blurs the texture by 5%
color texColor = texture("myImage.tex", s, t, "blur", 0.05);

```

You can also control the blur independently in the *s* and *t* directions:

```c
// Blurs 5% in the 's' direction, and 10% in the 't' direction
color texColor = texture("myImage.tex", s, t, "sblur", 0.05, "tblur", 0.1);

```

### 2. Using the `"width"` Parameter

While `"blur"` adds a fixed amount of blur, the `"width"` parameter acts as a **multiplier** on the automatic anti-aliasing filter size that RenderMan calculates based on screen-space derivatives.

A width of `1.0` is the default (perfect anti-aliasing). A width of `2.0` doubles the size of the sample area, creating a blur.

```c
// Multiplies the filter size by 3, creating a noticeable blur
color texColor = texture("myImage.tex", s, t, "width", 3.0);

```

Like blur, this can be split into `"swidth"` and `"twidth"`:

```c
color texColor = texture("myImage.tex", s, t, "swidth", 2.0, "twidth", 4.0);

```

### 3. Explicit 4-Point Lookup (Advanced)

If you want complete control over the exact area of the texture being sampled, you can use the 4-point version of the `texture()` function. Instead of passing just `s` and `t`, you pass the four corners of the quadrilateral you want to sample.

By pushing these coordinates further apart, you artificially increase the sampled area, resulting in a blur.

```c
// Calculate derivatives
float ds = Du(s) * blurMultiplier;
float dt = Dv(t) * blurMultiplier;

// Define the 4 corners of the sample area
color texColor = texture("myImage.tex", 
                         s, t,             // Corner 1
                         s + ds, t,        // Corner 2
                         s, t + dt,        // Corner 3
                         s + ds, t + dt);  // Corner 4

```

### Summary of Best Practices

* Use **`"blur"`** if you want a specific, fixed amount of softness (like depth of field or a frosted glass effect) regardless of the camera's distance to the object.
* Use **`"width"`** if you just want to soften the texture relative to its actual anti-aliased screen size (good for taking the "crispness" off of a texture without turning it to mush when the camera gets close).

*(Note: RSL is considered legacy in modern versions of RenderMan (RIS), which now rely on OSL (Open Shading Language) and C++ for shading. If you are migrating to OSL, the syntax is quite similar, using the `blur` or `width` parameters inside the OSL `texture()` call!)*

---
