# Version 3.1

# SECTION 1 - INTRODUCTION

## Features and Capabilities

The RenderMan® Interface is a standard interface between modeling programs and rendering programs capable of producing photorealistic quality images. A rendering program implementing the RenderMan Interface differs from an implementation of earlier graphics standards in that:

*   A photorealistic rendering program must simulate a real camera and its many attributes besides just position and direction of view. High quality implies that the simulation does not introduce artifacts from the computational process. Expressed in the terminology of computer graphics, this means that a photorealistic rendering program must be capable of:
    *   hidden surface removal so that only visible objects appear in the computed image,
    *   spatial filtering so that aliasing artifacts are not present,
    *   dithering so that quantization artifacts are not noticeable,
    *   temporal filtering so that the opening and closing of the shutter causes moving objects to be blurred,
    *   and depth of field so that only objects at the current focal distance are sharply in focus.
*   A photorealistic rendering program must also accept curved geometric primitives so that not only can geometry be accurately displayed, but also so that the basic shapes are rich enough to include the diversity of man-made and natural objects. This requires patches, quadrics, and representations of solids, as well as the ability to deal with complicated scenes containing on the order of 10,000 to 1,000,000 geometric primitives.
*   A photorealistic rendering program must be capable of simulating the optical properties of different materials and light sources. This includes surface shading models that describe how light interacts with a surface made of a given material, volume shading models that describe how light is scattered as it traverses a region in space, and light source models that describe the color and intensity of light emitted in different directions. Achieving greater realism often requires that the surface properties of an object vary. These properties are often controlled by texture mapping an image onto a surface. Texture maps are used in many different ways: direct image mapping to change the surface's color, transparency mapping, bump mapping for changing its normal vector, displacement mapping for modifying position, environment or reflection mapping for efficiently calculating global illumination, and shadow maps for simulating the presence of shadows.

The RenderMan Interface is designed so that the information needed to specify a photorealistic image can be passed to different rendering programs compactly and efficiently. The interface itself is designed to drive different hardware devices, software implementations and rendering algorithms. Many types of rendering systems are accommodated by this interface, including z-buffer-based, scanline-based, ray tracing, terrain rendering, molecule or sphere rendering and the Reyes rendering architecture. In order to achieve this, the interface does not specify how a picture is rendered, but instead specifies what picture is desired. The interface is designed to be used by both batch-oriented and real-time interactive rendering systems. Real-time rendering is accommodated by ensuring that all the information needed to draw a particular geometric primitive is available when the primitive is defined. Both batch and real-time rendering is accommodated by making limited use of inquiry functions and call-backs.

The RenderMan Interface is meant to be complete, but minimal, in its transfer of scene descriptions from modeling programs to rendering programs. The interface usually provides only a single way to communicate a parameter; it is expected that the modeling front end will provide other convenient variations. An example is color coordinate systems -- the RenderMan Interface supports multiple-component color models because a rendering program intrinsically computes with an n-component color model. However, the RenderMan Interface does not support all color coordinate systems because there are so many and because they must normally be immediately converted to the color representation used by the rendering program. Another example is geometric primitives -- the primitives defined by the RenderMan Interface are considered to be rendering primitives, not modeling primitives. The primitives were chosen either because special graphics algorithms or hardware is available to draw those primitives, or because they allow for a compact representation of a large database. The task of converting higher-level modeling primitives to rendering primitives must be done by the modeling program.

The RenderMan Interface is not designed to be a complete three-dimensional interactive programming environment. Such an environment would include many capabilities not addressed in this interface. These include: 1) screen space or two-dimensional primitives such as annotation text, markers, and 2-D lines and curves, 2) non-surface primitives such as 3-D lines and curves, and 3) user-interface issues such as window systems, input devices, events, selecting, highlighting, and incremental redisplay.

The RenderMan Interface is a collection of procedures to transfer the description of a scene to the rendering program. These procedures are described in Part I. A rendering program takes this input and produces an image. This image can be immediately displayed on a given display device or saved in an image file. The output image may contain color as well as coverage and depth information for postprocessing. Image files are also used to input texture maps. This document does not specify a "standard format" for image files.

The RenderMan Shading Language is a programming language for extending the predefined functionality of the RenderMan Interface. New materials and light sources can be created using this language. This language is also used to specify deformations, special camera projections, and simple image processing functions. All required shading functionality is also expressed in this language. A shading language is an essential part of a high-quality rendering program. No single material lighting equation can ever hope to model the complexity of all possible material models. The RenderMan Shading Language is described in Part II of this document.

## FEATURES AND CAPABILITIES

The RenderMan Interface was designed in a top-down fashion by asking what information is needed to specify a scene in enough detail so that a photorealistic image can be created. Photorealistic image synthesis is quite challenging and many rendering programs cannot implement all of the features provided by the RenderMan Interface. This section describes which features are required and which are considered optional capabilities. The set of required features is extensive in order that application writers and end-users may reasonably expect basic compatibility between, and a high level of performance from, all implementations of the RenderMan Interface. Capabilities are optional only in situations where it is reasonable to expect that some rendering programs are algorithmically incapable of supporting that capability, or where the capability is so advanced that it is reasonable to expect that most rendering implementations will not be able to provide it.

### Required features

All rendering programs which implement the RenderMan Interface must implement the interface as specified in this document. Implementations which are provided as a linkable C library must provide entry points for all of the subroutines and functions, accepting the parameters as described in this specification. All of the predefined types, variables and constants (including the entire set of constant RtToken variables for the predefined string arguments to the various RenderMan Interface subroutines) must be provided. The C header file ri.h (see Appendix C, Language Binding Details) describes these data items.

Implementations which are provided as prelinked standalone applications must accept as input the complete RenderMan Interface Bytestream (RIB). Such implementations may also provide a complete RenderMan Interface library as above, which contains subroutine stubs whose only function is to generate RIB.

All rendering programs which implement the RenderMan Interface must:

*   provide the complete hierarchical graphics state, including the attribute and transformation stacks and the active light list.
*   perform orthographic and perspective viewing transformations.
*   perform depth-based and "Painter's algorithm" hidden-surface elimination.
*   perform pixel filtering and antialiasing.
*   perform gamma correction and dithering before quantization.
*   produce picture files containing any combination of RGB, A, and Z. The resolutions of these files must be as specified by the user.
*   provide all of the geometric primitives described in the specification, and provide all of the standard primitive variables applicable to each primitive.
*   provide the fourteen standard light source, surface, volume, and displacement shaders required by the specification. Any additional shaders, and any deviations from the standard shaders presented in this specification, must be documented by providing the equivalent shader expressed in the RenderMan Shading Language.

Rendering programs which implement the RenderMan Interface receive all of their data through the interface. There will be no additional subroutines required to control or provide data to the rendering program. Data items which are substantially similar to items already described in this specification will be supplied through the normal mechanisms, and not through any of the implementation-specific extension mechanisms (RiAttribute, RiGeometry or RiOption). Rendering programs will not provide nonstandard alternatives to the existing mechanisms, such as any alternate language for programmable shading.

### Optional Capabilities

Rendering programs may also provide one or more of the following optional capabilities. If a capability is not provided by an implementation, a specific default is required (as described in the individual sections). A subset of the full functionality of a capability may be provided by a rendering program. For example, a rendering program might implement Motion Blur, but only of simple transformations, or only using a limited range of shutter times. Rendering programs should describe their implementation of the following optional capabilities using the terminology in the following list.

> *   **Solid Modeling**
>     The ability to define solid models as collections of surfaces and combine them using the set operations intersection, union and difference. (See the section on Solids and Spatial Set Operations.)
> *   **Trim Curves**
>     The ability to specify a subset of a parametric surface by giving a region in parameter space. (See the section on Patches.)
> *   **Level of Detail**
>     The ability to specify several definitions of the same model and have one selected based on the estimated screen size of the model. (See the section on Detail.)
> *   **Motion Blur**
>     The ability to process moving primitives and antialias them in time. (See Section 6, Motion.)
> *   **Depth of Field**
>     The ability to simulate focusing at different depths. (See the section on Camera.)
> *   **Programmable Shading**
>     The ability to perform shading calculations using user-supplied RenderMan Shading Language programs. (See Part II, The RenderMan Shading Language.)
> *   **Special Camera Projections**
>     The ability to perform nonstandard camera projections such as spherical or Omnimax projections. (See the section on Camera.)
> *   **Deformations**
>     The ability to handle nonlinear transformations such as bends and twists. (See the section on Transformations.)
> *   **Displacements**
>     The ability to handle displacements. (See the section on Transformations.)
> *   **Spectral Colors**
>     The ability to calculate colors with an arbitrary number of spectral color samples. (See the section on Additional options.)
> *   **Texture Mapping**
>     The ability to index a texture map with the surface's texture coordinates. (See the section on Basic texture maps.)
> *   **Environment Mapping**
>     The ability to model the environmental illumination by indexing a texture map with a direction vector. (See the section on Environment maps.)
> *   **Bump Mapping**
>     The ability to perturb just surface normals by giving a displacement map. (See the section on Bump maps.)
> *   **Shadow Depth Mapping**
>     The ability to index a shadow map with a position. (See the section on Shadow depth maps.)
> *   **Volume Shading**
>     The ability to attach and evaluate volumetric shading procedures. (See the section on Volume shading.)
> *   **Ray Tracing**
>     The ability to evaluate global illumination models using ray tracing. (See the section on Shading and Lighting Functions.)
> *   **Radiosity**
>     The ability to evaluate global illumination models using radiosity. (See the section on Illuminance and Illuminate Statements.)
> *   **Area Light Sources**
>     The ability to illuminate surfaces with area light sources. (See the section on Light sources.)

# SECTION 2 - LANGUAGE BINDING SUMMARY

* [C Binding](#C.binding)
* [Bytestream Protocol](#Bytestream)
* [Additional Information](#Additional)

In this document, the RenderMan® Interface is described in the C language, as originally specified by Kernighan and Ritchie. Other language bindings will be proposed in the future.

## C Binding

All types, procedures, tokens, predefined variables and utility procedures mentioned in this document are required to be present in all C implementations that conform to this specification. The C header file which declares all of these required names, ri.h, is listed in Appendix C, Language Binding Details.

The RenderMan Interface requires the following types:

```c
typedef short	RtBoolean;
typedef long	RtInt;
typedef float	RtFloat;
typedef char	*RtToken;
typedef RtFloat	RtColor;
[1]typedef RtFloat	RtPoint;
[1]typedef RtFloat	RtMatrix;
[2][2]typedef RtFloat	RtBasis;
[2][2]typedef RtFloat	RtBound;
[3]typedef char	*RtString;
typedef void	*RtPointer;
typedef void	RtVoid;
typedef RtFloat	(RtFloatFunc)();
typedef RtVoid	(*RtFunc)();
typedef RtPointer	RtObjectHandle;
typedef RtPointer	RtLightHandle;
```

All procedures and values defined in the interface are prefixed with **Ri** (for RenderMan Interface). All types are prefixed with **Rt** (for RenderMan type). Boolean values are either RI_FALSE or RI_TRUE. Special floating point values RI_INFINITY and RI_EPSILON are defined. The expression -RI_INFINITY has the obvious meaning. The number of components in a color is initially three, but can be changed (See the section Additional options, p. 33). A bound is a bounding box and is specified by 6 floating point values in the order xmin, xmax, ymin, ymax, zmin, zmax. A matrix is an array of 16 numbers describing a 4 by 4 transformation matrix. All multidimensional arrays are specified in row-major order. For example, a 4 by 4 translation matrix to the location (2,3,4) is specified with

```c
{{ 1.0, 0.0, 0.0, 0.0}, { 0.0, 1.0, 0.0, 0.0}, { 0.0, 0.0, 1.0, 0.0}, { 2.0, 3.0, 4.0, 1.0} }
```

Tokens are strings that have a special meaning to procedures implementing the interface. These meanings are described with each procedure. The capabilities of the RenderMan Interface can be extended by defining new tokens and passing them to various procedures. The most important of these are the tokens identifying variables defined by procedures called shaders, written in the Shading Language. Variables passed through the RenderMan Interface are bound by name to shader variables. To make the standard predeclared tokens and user-defined tokens similar, RenderMan Interface tokens are represented by strings. Associated with each of the standard predefined tokens, however, is a predefined string constant that the RenderMan Interface procedures can use for efficient parsing. The names of these string constants are derived from the token names used in this document by prepending an RI_ to a capitalized version of the string. For example, the predefined constant token for "rgb" is RI_RGB. The special predefined token RI_NULL is used to specify a *null* token.

In the C binding presented in this document, parameters are passed by value or by reference. C implementations of the RenderMan Interface are expected to make copies of any parameters whose values are to be retained across procedure invocations. Many procedures in the RenderMan Interface have variable length parameter lists. These are indicated by the syntactical construct parameterlist in the procedure's argument list. In the C binding described, parameterlist is a sequence of pairs of arguments, the first being an **RtToken** and the second being an **RtPointer**, an untyped pointer to an array of either **RtFloat**, **RtString** or other values. The list is terminated by the special token RI_NULL. In addition, each such procedure has an alternate vector interface, which passes the *parameterlist* as three arguments: an **RtInt** indicating the length of the parameter list; an array of that length that contains the **RtToken**s; and another array of the same length that contains the **RtPointer**s. This alternate procedure is denoted by appending an uppercase V to the procedure name. For example, the procedure **RiFoo** declared as

```c
**RiFoo**( *parameterlist* )
```

could be called in the following ways:

```c
**RtColor** colors;
**RtPoint** points;
**RtFloat** one_float;
**RtToken** tokens;
[1]**RtPointer** values;
[1]**RiFoo**( RI_NULL );
**RiFoo**(**RtToken**)"P", (**RtPointer**)points, (**RtToken**)"Cs",
(**RtPointer**)colors,
(**RtToken**)"Kd", (**RtPointer**)&one_float, RI_NULL );
**RiFoo**(RI_P, (**RtPointer**)points, RI_CS, (**RtPointer**)colors,	RI_KD, (**RtPointer**)&one_float, RI_NULL );
tokens = RI_P; values = (**RtPointer**)points;
tokens =[4] RI_CS; values =[4] (**RtPointer**)colors;
tokens =[5] RI_KD; values =[5] (**RtPointer**)&one_float;
**RiFooV**( 3, tokens, values);
```

It is not the intent of this document to propose that other language bindings use an identical mechanism for passing parameter lists. For example, a Fortran or Pascal binding might pass parameters using four arguments: an integer indicating the length of the parameter list, an array of that length that contains the tokens, an array of the same length containing integer indices into the final array containing the real values. A Common Lisp binding would be particularly simple because it has intrinsic support for variable length argument lists. The ANSI Standard C binding of RenderMan Interface is different from the K&R C binding presented in the document only in the normally expected ways. The semantics of the types, procedures and predefined variables are identical, and the necessary function prototype modifications are presented in a version of *ri.h* also listed in [Appendix C, Language Binding Details](appendix.C.html).

## Bytestream Protocol

This document also describes a byte stream representation of the RenderMan Interface, known as the RenderMan Interface Bytestream, or RIB. This byte stream serves as both a network transport protocol for modeling system clients to communicate requests to a remote rendering service, and an archive file format to save requests for later submission to a renderer. The RIB protocol provides both an ASCII and binary encoding of each request, in order to satisfy needs for both an understandable (potentially) interactive interface to a rendering server and a compact encoded format which minimizes transmission time and file storage costs. Some requests have multiple versions, for efficiency or to denote special cases of the request. The semantics of each RIB request are identical to the corresponding C entry point, except as specifically noted in the text. In Part I of this document, each RIB request is presented in its ASCII encoding, using the following format:

```
RIB BINDING **Request** parameter1 parameter2... parameterN
```

Explanation of the special semantics of the RIB protocol for this request. At the top of the description, *parameter1* through *parameterN* are the parameters that the request requires. The notation `-' in the parameter position indicates that the request expects no parameters. Normally the parameter names suggest their purpose, e.g., *x*, *y*, or *angle*. In RIB, square brackets ([ and ]) delimit arrays. Integers will be automatically promoted if supplied for parameters which require floating point values. A parameter list is simply a sequence of string-array pairs. There is no explicit termination symbol as in the C binding. Example parameter lists are:

"P" [0 1 2 3 4 5 6 7 8 9 10 11]
"distance" [.5] "roughness" [1.2]

The details of the lexical syntax of both the ASCII and binary encodings of the RIB protocol are presented in [Appendix C, Language Binding Details](appendix.C.html).

## Additional Information

Finally, the description of each RenderMan Interface request provides an example and cross-reference in the following format:

```
EXAMPLE **Request** 7 22.9 SEE ALSO **RiOtherRequest**
```

Some examples are presented in C, others in RIB, and a few are presented in both bindings (for comparison). It should be obvious from the syntax which binding is which.
# SECTION 3 - RELATIONSHIP TO THE RENDERMAN SHADING LANGUAGE

The capabilities of the RenderMan® Interface can be extended by using the Shading Language. The Shading Language is described in Part II of this document. This section describes the interaction between the RenderMan Interface and the Shading Language. Special procedures, called *shaders*, are declared in this language. The argument list of a shader declares variables that can be passed through the RenderMan Interface to a shader. For example, in the shading language a shader called *weird* might be declared as follows:

```c
surface weird( float f = 1.0; point p = (0,0,0) ) {
    Cs = Ci * mod( length(P-p)*f - s + t, 1.0 );
}
```

The shader *weird* is referred to by name and so are its variables.

```c
**RtFloat**	foo;
**RtPoint**	bar;
**RiSurface**("weird","f", (**RtPointer**)&foo, "p",
(**RtPointer**)bar, RI_NULL);
```

passes the value of *foo* to the Shading Language variable *f* and the value *bar* to the variable *p*. Note that since all parameters are passed as arrays, the single float must be passed by reference. In order to pass shading language variables, the RenderMan Interface must know the type of each variable defined in a shader. All predefined shaders predeclare the types of the variables that they use. Certain other variables, such as position, are also predeclared. Additional variables are declared with:

```c
**RtToken** **RiDeclare**( name, declaration )
char	*name;
char	*declaration;
```

Declare the name and type of a variable. The declaration indicates the size and semantics of values associated with the variable, or may be RI_NULL if there are no associated values. This information is used by the renderer in processing the variable argument list semantics of the RenderMan Interface. The syntax of *declaration* is exactly as described for variables in the Shading Language. **RiDeclare** also installs *name* into the set of known tokens and returns a constant token which can be used to indicate that variable. This constant token will generally have the same efficient parsing properties as the `RI_' versions of the predefined tokens.

```
RIB BINDING **Declare** name declaration
```

**RiDeclare** only needs to be powerful enough to support the declarations which a user needs: declaring shader variables and his own geometric primitive variables. The RIB stream declaration facility needs to be more powerful because it must be able to handle both predefined symbols and implementation-specific predefined option and attribute values which might be impossible to declare using the limited facility described above. Therefore, the exact syntax for *declaration* in RIB is more general:

```
[*class*] [*type*] [`[' *n* `]']
```

where class may be *uniform*, *varying* (as in the shading language), or *vertex* (position data, such as bicubic control points), and type may be one of: *float*, *integer*, *string*, *point*, and *color*. The optional bracket notation indicates an array of *n type* items, where n is a positive integer. If no array is specified, one item is assumed. If a *class* is not specified, the identifier is assumed to be uniform.

```c
EXAMPLE **RiDeclare**( "Np", "uniform point" );
**RiDeclare**( "Cs", "varying color" );
**Declare** "st" "varying float"
```

The storage modifiers *varying* and *uniform* are discussed in the section on [Uniform and Varying Variables in Part II](section11.html#Uniform) and in [Section 5, Geometric Primitives](section5.html). All procedure parameter tokens and shader variable name tokens named in this document are standard and are predefined by all implementations of the RenderMan Interface. In addition, a particular implementation may predeclare other variables for use with implementation-specific options, geometry, etc. Whenever a point variable is passed through the RenderMan Interface to shaders, the points are assumed to be in the current coordinate system. This is sometimes referred to as *object* coordinates. Different coordinate systems are discussed in the [Camera](section4.html#Camera) section. Normals ("N" and "Np") are also assumed to be in object coordinates. Whenever colors are passed through the RenderMan Interface, they are expected to have a number of floats equal to the number of color samples being used by the interface. This defaults to 3, but can be changed by the user (see the section on [Additional options](section4.html#Additional.options)).
# SECTION 4 - GRAPHICS STATE

*   Options
*   Attributes
*   Transformations
*   Implementation-specific Attributes

The RenderMan® Interface is similar to other graphics packages in that it maintains a graphics state. The *graphics state* contains all the information needed to render a geometric primitive. RenderMan Interface commands either change the graphics state or render a geometric primitive. The graphics state is divided into two parts: a global state that remains constant while rendering a single image or frame of a sequence, and a current state that changes from geometric primitive to geometric primitive. Parameters in the global state are referred to as *options*, whereas parameters in the current state are referred to as *attributes*. Options include the camera and display parameters, and other parameters that affect the quality or type of rendering in general (e.g., global level of detail, number of color samples, etc.). Attributes include the parameters controlling appearance or shading (e.g., color, opacity, surface shading model, light sources, etc.), how geometry is interpreted (e.g., orientation, subdivision level, bounding box, etc.), and the current modeling matrix. To aid in specifying hierarchical models, the attributes in the graphics state may be pushed and popped on a graphics state stack. The graphics state also maintains the interface *mode*.

The different modes of the interface are entered and exited by matching Begin-End command sequences.

```c
**RiBegin**( name ) **RtToken**	name;
**RiEnd**()
```

The bracketed set of commands **RiBegin-RiEnd** initialize and terminate a rendering session. name is used to select between various implementations that may be available. RI_NULL indicates that the default implementation should be used. When the interface is initialized all graphics state variables are set to their default values. When the interface is terminated any cleanup operations that need to be done are performed. All other RenderMan Interface procedures must be called within a **RiBegin-RiEnd** block (the only exceptions are **RiErrorHandler** and **RiOption**).

```c
**RiFrameBegin**( frame ) **RtInt**	frame;
**RiFrameEnd**()
```

The bracketed set of commands **RiFrameBegin-RiFrameEnd** mark the beginning and end of a single frame of an animated sequence. frame is the number of this frame. The values of all of the rendering options are saved when **RiFrameBegin** is called, and these values are restored when **RiFrameEnd** is called. All lights and retained objects defined inside the **RiFrameBegin-RiFrameEnd** frame block are removed and their storage reclaimed when **RiFrameEnd** is called (thus invalidating their handles). All of the information that changes from frame to frame should be inside a frame block. In this way, all of the information that is necessary to produce a single frame of an animated sequence may be extracted from a command stream by retaining only those commands within the appropriate frame block and any commands outside all of the frame blocks. This command need not be used if the application is producing a single image.

```
RIB BINDING **FrameBegin** - **FrameBegin** int **FrameEnd** -
EXAMPLE **RiFrameBegin**(14);
SEE ALSO **[RiWorldBegin](#RiWorldBegin)**
```

```c
**RiWorldBegin**()
**RiWorldEnd**()
```

When **RiWorldBegin** is invoked, all rendering options are frozen and cannot be changed until the picture is finished. The *world-to-camera transformation* is set to the *current transformation* and the *current transformation* is reinitialized to the identity. Inside an **RiWorldBegin-RiWorldEnd** block, the *current transformation* is interpreted to be the *object-to-world transformation*. After an **RiWorldBegin**, the interface can accept geometric primitives that define the scene. (The only other mode in which geometric primitives may be defined is inside a **RiObjectBegin-RiObjectEnd** block.) Some rendering programs may immediately begin rendering geometric primitives as they are defined, whereas other rendering programs may wait until the entire scene has been defined. **RiWorldEnd** does not normally return until the rendering program has completed drawing the image. If the image is to be saved in a file, this is done automatically by **RiWorldEnd**. All lights and retained objects defined inside the **RiWorldBegin-RiWorldEnd** world block are removed and their storage reclaimed when **RiWorldEnd** is called (thus invalidating their handles).

```
RIB BINDING **WorldBegin** - **WorldEnd** -
EXAMPLE **RiWorldEnd**();
SEE ALSO **[RiFrameBegin](#RiFrameBegin)**
```

The following is an example of the use of these procedures, showing how an application constructing an animation might be structured. In the example, an object is defined once and instanced in subsequent frames at different positions.

```c
**RtObjectHandle** BigUglyObject;
RiBegin();
    BigUglyObject = **RiObjectBegin**();
    ...
    **RiObjectEnd**();
    /* Display commands */
    **RiDisplay**(...):
    **RiFormat**(...);
    **RiFrameAspectRatio**(1.0);
    **RiScreenWindow**(...);
    **RiFrameBegin**(0);
    /* Camera commands */
    **RiProjection**(RI_PERSPECTIVE,...);
    **RiRotate**(...);
    **RiWorldBegin**();
    ...
    **RiColor**(...);
    **RiTranslate**(...);
    **RiObjectInstance**( BigUglyObject );
    ...
    **RiWorldEnd**();
    **RiFrameEnd**();
    **RiFrameBegin**(1);
    /* Camera commands */
    **RiProjection**(RI_PERSPECTIVE,...);
    **RiRotate**(...);
    **RiWorldBegin**();
    ...
    **RiColor**(...);
    **RiTranslate**(...);
    **RiObjectInstance**( BigUglyObject );
    ...
    **RiWorldEnd**();
    **RiFrameEnd**();
    . . .
**RiEnd**();
```

The following begin-end pairs also place the interface into special modes.

```c
**RiSolidBegin**()
**RiSolidEnd**()
**RiMotionBegin**()
**RiMotionEnd**()
**RiObjectBegin**()
**RiObjectEnd**()
```

The properties of these modes are described in the appropriate sections (see the sections on [Solids and Spatial Set Operations](section5.html#Solids); [Motion](section6.html); and [Retained Geometry](section5.html#Retained.Geometry). Two other begin-end pairs:

```c
**RiAttributeBegin**()
**RiAttributeEnd**()
**RiTransformBegin**()
**RiTransformEnd**()
```

save and restore the attributes in the graphics state, and save and restore the current transformation, respectively. All begin-end pairs (except **RiTransformBegin-RiTransformEnd** and **RiMotionBegin-RiMotionEnd**), implicitly save and restore attributes. Begin-end blocks of the various types may be nested to any depth, subject to their individual restrictions, but it is never legal for the blocks to overlap.

## Options

The graphics state has various *options* that must be set before rendering a frame. The complete set of options includes: a description of the camera, which controls all aspects of the imaging process (including the camera position and the type of projection); a description of the display, which controls the output of pixels (including the types of images desired, how they are quantized and which device they are displayed on); as well as renderer run-time controls (such as the hidden surface algorithm to use).

### Camera

The graphics state contains a set of parameters that define the properties of the camera. The complete set of camera options is described in [Table 4.1, Camera Options](#Table.4.1). The viewing transformation specifies the coordinate transformations involved with imaging the scene onto an image plane and sampling that image at integer locations to form a raster of pixel values. A few of these procedures set display parameters such as *resolution* and *pixel aspect ratio*. If the rendering program is designed to output to a particular display device these parameters are initialized in advance. Explicitly setting these makes the specification of an image more device dependent and should only be used if necessary. The defaults given in the *Camera Options* table characterize a hypothetical framebuffer and are the defaults for picture files.

**Table 4.1 Camera Options**

| Camera Option | Type | Default | Description |
|---|---|---|---|
| Horizontal Resolution Vertical Resolution | *integer integer* | 640\* 480\* | The horizontal and vertical resolution in the output image. |
| Pixel Aspect Ratio | *float* | 1.0\* | The ratio of the width to the height of a single pixel. |
| Crop Window | 4 *floats* | (0,1,0,1) | The region of the raster that is actually rendered. |
| Frame Aspect Ratio | *float* | 4/3 \* | The aspect ratio of the desired image. |
| Screen Window | 4 *floats* | (-4/3,4/3,-1,1)\* | The screen coordinates (coordinates after the projection) of the area to be rendered. |
| Camera Projection | *token* | "orthographic" | The camera to screen projection. |
| World to Camera | *transform* | identity | The world to camera transformation. |
| Clipping Planes | 2 *floats* | (epsilon, infinity) | The positions of the near and far clipping planes. |
| f-Stop Focal Length Focal Distance | *float* *float* *float* | infinity - - | Parameters controlling depth of field. |
| Shutter Open Shutter Close | *float* *float* | 0 0 | The times when the shutter opens and closes. |

*\* Interrelated defaults*

The camera model also supports near and far clipping planes. Depth of field is specified by setting an f-stop, focal length, and focal distance just as in a real camera. Objects located at the focal distance will be sharp and in focus while other objects will be out of focus. The shutter is specified by giving opening and closing times. Moving objects will blur while the camera shutter is open. The imaging transformation proceeds in several stages. Geometric primitives are specified in the object coordinate system. This canonical coordinate system is the one in which the object is most naturally described. The object coordinates are converted to the world coordinate system by a sequence of *modeling transformations*. The world coordinate system is converted to the camera coordinate system by the camera transformation. Once in camera coordinates, points are projected onto the image plane or screen coordinate system by the projection and its following screen transformation. Points on the screen are finally mapped to a device dependent, integer coordinate system in which the image is sampled. This is referred to as the raster coordinate system and this transformation is referred to as the raster transformation. These various coordinate systems are summarized in Table 4.2 Point Coordinate Systems.

**Table 4.2 Point Coordinate Systems**

| Coordinate System | Description |
|---|---|
| "object" | The coordinate system in which the current geometric primitive is defined. The modeling transformation converts from object coordinates to world coordinates. |
| "world" | The standard reference coordinate system. The camera transformation converts from world coordinates to camera coordinates. |
| "camera" | A coordinate system with the vantage point at the origin and the direction of view along the positive z-axis. The projection and screen transformation convert from camera coordinates to screen coordinates. |
| "screen" | The 2-D normalized coordinate system corresponding to the image plane. The raster transformation converts to raster coordinates. |
| "raster" | The raster or pixel coordinate system. An area of 1 in this coordinate system corresponds to the area of a single pixel. This coordinate system is either inherited from the display or set by selecting the resolution of the image desired. |

These various coordinate systems are established by camera and transformation commands. The order in which camera parameters are set is the opposite of the order in which the imaging process was described above. When **RiBegin** is executed it establishes a complete set of defaults. If the rendering program is designed to produce pictures for a particular piece of hardware, display parameters associated with that piece of hardware are used. If the rendering program is designed to produce picture files, the parameters are set to generate a video-size image. If these are not sufficient, the resolution and pixel aspect ratio can be set to generate a picture for any display device. **RiBegin** also establishes default screen and camera coordinate systems as well. The default projection is orthographic and the screen coordinates assigned to the display are roughly between ± 1.0. The initial camera coordinate system is mapped onto the display such that the +x axis points right, the +y axis points up, and the +z axis points inward, perpendicular to the display surface. Note that this is left-handed. Before any transformation commands are made, the *current transformation matrix* contains the identity matrix as the screen transformation. Usually the first transformation command is an **RiProjection**, which appends the projection matrix onto the screen transformation, saves it, and reinitializes the *current transformation matrix* as the identity camera transformation. This marks the current coordinate system as the camera coordinate system. After the camera coordinate system is established, future transformations move the world coordinate system relative to the camera coordinate system. When an **RiWorldBegin** is executed, the *current transformation matrix* is saved as the camera transformation, and thus the world coordinate system is established. Subsequent transformations inside of an **RiWorldBegin-RiWorldEnd** establish different object coordinate systems. The following example shows how to position a camera:

```c
**RiBegin**();
    **RiFormat**( xres, yres, 1.0 );	/*Raster coordinate system*/
    **RiFrameAspectRatio**( 4.0/3.0 ); /*Screen coordinate system*/
    **RiFrameBegin**(0);
    **RiProjection**("perspective,"...); /*Camera coordinate system*/
    **RiRotate**(... );
    **RiWorldBegin**();	/*World coordinate system*/
    ...
    **RiTransform**(...);	/*Object coordinate system*/
    **RiWorldEnd**();
    **RiFrameEnd**();
**RiEnd**();
```

The various camera procedures are described below, with some of the concepts illustrated in Figure 4.1, Camera-to-Raster Projection Geometry.

```c
**RiFormat**( xresolution, yresolution, pixelaspectratio ) **RtInt**	xresolution, yresolution; **RtFloat**	pixelaspectratio;
```

Set the horizontal (*xresolution*) and vertical (*yresolution*) resolution (in pixels) of the image to be rendered. The upper left hand corner of the image has coordinates (0,0) and the lower right hand corner of the image has coordinates (*xresolution, yresolution*). If the resolution is greater than the maximum resolution of the device, the desired image is clipped to the device boundaries (rather than being shrunk to fit inside the device). This command also sets the pixel aspect ratio. The pixel aspect ratio is the ratio of the physical width to the height of a single pixel. The pixel aspect ratio should normally be set to 1 unless a picture is being computed specifically for a display device with non-square pixels. Implicit in this command is the creation of a display viewport with a The viewport aspect ratio is the ratio of the physical width to the height of the entire image. An image of the desired aspect ratio can be specified in a device independent way using the procedure **RiFrameAspectRatio** described below. The **RiFormat** command should only be used when an image of a specified resolution is needed or an image file is being created. If this command is not given, the resolution defaults to that of the display device being used (see the Displays section, p. 27). Also, if xresolution, yresolution or pixelaspectratio is specified as a nonpositive value, the resolution defaults to that of the display device for that particular parameter.

```
RIB BINDING **Format** xresolution yresolution pixelaspectratio
EXAMPLE **Format** 512 512 1
SEE ALSO **[RiDisplay](#RiDisplay)**, **[RiFrameAspectRatio](#RiFrameAspectRatio)**
```

```c
**RiFrameAspectRatio**( frameaspectratio ) **RtFloat**	frameaspectratio;
```

*frameaspectratio* is the ratio of the width to the height of the desired image. The picture produced is adjusted in size so that it fits into the display area specified with **RiDisplay** or **RiFormat** with the specified frame aspect ratio and is such that the upper left corner is aligned with the upper left corner of the display. If this procedure is not called, the frame aspect ratio defaults to that determined from the resolution and pixel aspect ratio.

```
RIB BINDING **FrameAspectRatio** frameaspectratio
EXAMPLE **RiFrameAspectRatio** (4.0/3.0);
SEE ALSO **[RiDisplay](#RiDisplay)**, **[RiFormat](#RiFormat)**
```

```c
**RiScreenWindow**( left, right, bottom, top ) **RtFloat**	left, right, bottom, top;
```

This procedure defines a rectangle in the image plane that gets mapped to the *raster coordinate system* and that corresponds to the display area selected. The rectangle specified is in the *screen coordinate system*. The values *left, right, bottom*, and *top* are mapped to the respective edges of the display. The default values for the screen window coordinates are:
``` (*-frameaspectratio*, *frameaspectratio*, -1, 1).
```
if *frameaspectratio* is greater than or equal to one, or
``` (-1, 1, -1/*frameaspectratio*, 1/*frameaspectratio*).
```
if *frameaspectratio* is less than or equal to one. For perspective projections, this default gives a centered image with the smaller of the horizontal and vertical fields of view equal to the field of view specified with **RiProjection**. Note that if the camera transformation preserves relative *x* and *y* distances, and if the ratio is not the same as the frame aspect ratio of the display area, the displayed image will be distorted.

```
RIB BINDING **ScreenWindow** left right bottom top **ScreenWindow** [left right bottom top]
EXAMPLE **ScreenWindow** -1 1 -1 1
SEE ALSO **[RiCropWindow](#RiCropWindow), [RiFormat](#RiFormat), [RiFrameAspectRatio](#RiFrameAspectRatio), [RiProjection](#RiProjection)**
```

```c
**RiCropWindow**( xmin, xmax, ymin, ymax ) **RtFloat**	xmin, xmax, ymin, ymax;
```

Render only a subrectangle of the image. This command does not affect the mapping from screen to raster coordinates. This command is used to facilitate debugging regions of an image, and to help in generating panels of a larger image. These values are specified as fractions of the raster window defined by **RiFormat** and **RiFrameAspectRatio**, and therefore lie between 0 and 1. By default the entire raster window is rendered. The integer image locations corresponding to these limits are given by

```c
rxmin	= clamp (ceil ( xresolution*xmin ),	0, xresolution-1);
rxmax	= clamp (ceil ( xresolution*xmax -1 ),	0, xresolution-1);
rymin	= clamp (ceil ( yresolution*ymin ),	0, yresolution-1);
rymax	= clamp (ceil ( yresolution*ymax -1 ),	0, yresolution-1);
```

These regions are defined so that if a large image is generated with tiles of abutting but non-overlapping crop windows, the subimages produced will tile the display with abutting and non-overlapping regions.

```
RIB BINDING **CropWindow** xmin xmax ymin ymax **CropWindow** [xmin xmax ymin ymax]
EXAMPLE **RiCropWindow** (0.0, 0.3, 0.0, 0.5);
SEE ALSO **[RiFrameAspectRatio](#RiFrameAspectRatio), [RiFormat](#RiFormat)**
```

```c
**RiProjection**( name, parameterlist ) **RtToken**	name;
```

The projection determines how camera coordinates are converted to screen coordinates, using the type of projection and the clipping planes to generate a projection matrix. It appends this projection matrix to the *current transformation matrix* and stores this as the screen transformation, then marks the current coordinate system as the camera coordinate system and reinitializes the *current transformation matrix* to the identity camera transformation. The required types of projection are "perspective," "orthographic," and RI_NULL. "perspective" builds a projection matrix that does a perspective projection along the z-axis, using the **RiClipping** values, so that points on the near clipping plane project to z=0 and points on the far clipping plane project to z=1. "perspective" takes one optional parameter, "fov," a single **RtFloat** that indicates he full angle perspective field of view (in degrees) between screen space coordinates (-1,0) and (1,0) (equivalently between (0,-1) and (0,1)). The default is 90 degrees. Note that there is a redundancy in the focal length implied by this procedure and the one set by **RiDepthOfField**. The focal length implied by this command is: "orthographic" builds a simple orthographic projection that scales z using the **RiClipping** values as above. "orthographic" takes no parameters. RI_NULL uses an identity projection matrix, and simply marks camera space in situations where the user has generated his own projection matrices himself using **RiPerspective** or **RiTransform**. This command can also be used to select implementation-specific projections or special projections written in the Shading Language. If a particular implementation does not support the special projection specified, it is ignored and an orthographic projection is used. If **RiProjection** is not called, the screen transformation defaults to the identity matrix, so screen space and camera space are identical.

```
RIB BINDING **Projection** "perspective" parameterlist **Projection** "orthographic" **Projection** name parameterlist
EXAMPLE **RiProjection** (RI_ORTHOGRAPHIC, RI_NULL);
SEE ALSO **[RiPerspective](#RiPerspective), [RiClipping](#RiClipping)**
```

```c
**RiClipping**( near, far ) **RtFloat**	near, far;
```

Sets the position of the near and far clipping planes along the direction of view. *near* and *far* must both be positive numbers. *near* must be greater than or equal to RI_EPSILON and less than *far*. *far* must be greater than *near* and may be equal to RI_INFINITY. These values are used by **RiProjection** to generate a screen projection such that depth values are scaled to equal zero at *z=near* and one at *z=far*. Notice that the rendering system will actually clip geometry which lies outside of *z*=(0,1) in the screen coordinate system, so non-identity screen transforms may affect which objects are actually clipped. For reasons of efficiency, it is generally a good idea to bound the scene tightly with the near and far clipping planes.

```
RIB BINDING **Clipping** near far
EXAMPLE **Clipping** .1 10000
SEE ALSO **[RiBound](#RiBound), [RiProjection](#RiProjection)**
```

```c
**RiDepthOfField**( fstop, focallength, focaldistance ) **RtFloat**	fstop; **RtFloat**	focallength; **RtFloat**	focaldistance;
```

*focaldistance* sets the distance along the direction of view at which objects will be in focus. *focallength* sets the focal length of the camera. These two parameters should have the units of distance along the view direction in camera coordinates. *fstop*, or aperture number, determines the lens diameter: If *fstop* is RI_INFINITY, a pin-hole camera is used and depth of field is effectively turned off. If the *Depth of Field* capability is not supported by a particular implementation, a pin-hole camera model is always used. If depth of field is turned on, points at a particular depth will not image to a single point on the view plane but rather a circle. This circle is called the *circle of confusion*. The diameter of this circle is equal to Note that there is a redundancy in the focal length as specified in this procedure and the one implied by **RiProjection**.

```
RIB BINDING **DepthOfField** fstop focallength focaldistance **DepthOfField** -
```

The second form specifies a pin-hole camera with infinite *fstop*, for which the *focallength* and *focaldistance* parameters are meaningless.

```
EXAMPLE **DepthOfField** 22 45 1200
SEE ALSO **[RiProjection](#RiProjection)**
```

```c
**RiShutter**( min, max ) **RtFloat**	min, max;
```

This procedure sets the times at which the shutter opens and closes. min should be less than max. If *min==max*, no motion blur is done.

```
RIB BINDING **Shutter** min max
EXAMPLE **RiShutter**(0.1, 0.9);
SEE ALSO **[RiMotionBegin](section6.html#RiMotionBegin)**
```

### Displays

The graphics state contains a set of parameters that control the properties of the display process. The complete set of display options is given in Table 4.3, Display Options.

**Table 4.3 Display Options**

| Display Option | Type | Default | Description |
|---|---|---|---|
| Pixel Variance | float | - | Estimated variance of the computed pixel value from the true pixel value. |
| Sampling Rates | 2 floats | 2, 2 | Effective sampling rate in the horizontal and vertical directions. |
| Filter Filter Widths | function 2 float | RiGaussianFilter 2, 2 | Type of filtering and the width of the filter in the horizontal and vertical directions. |
| Exposure gain | float float | 1.0 1.0 | Gain and gamma of the exposure process. |
| Imager | shader | "null" | A procedure defining an image or pixel operator. |
| Color Quantizer one minimum maximum dither amplitude | int int int float | 255 0 255 0.5 | Color and opacity quantization parameters. |
| Depth Quantizer one minimum maximum dither amplitude | int int int float | 0 - - - | Depth quantization parameters. |
| Display Type | token | \* | Whether the display is a frame-buffer or a file. |
| Display Name | string | \* | Name of the display device or file. |
| Display Mode | token | \* | Image output type. |

*\* Implementation-specific*

Rendering programs must be able to produce color, opacity (alpha), and depth images. Display parameters control how the values in these images are converted into a displayable form. Many times it is possible to use none of the procedures described in this section. If this is done, the rendering process and the images it produces are described in a completely device-independent way. If a rendering program is designed for a specific display, it has appropriate defaults for all display parameters. The defaults given in [Table 4.3, Display Options](#Table.4.3) characterize a file to be displayed on a hypothetical video framebuffer.

The output process is different for color, alpha, and depth information. ([See Figure 4.2, Imaging Pipeline](#Figure.4.2)). The hidden-surface algorithm will produce a representation of the light incident on the image plane. This color image is either continuous or sampled at a rate that may be higher than the resolution of the final image. The minimum sampling rate can be controlled directly, or can be indicated by the estimated variance of the pixel values. These color values are filtered with a user-selectable filter and filterwidth, and sampled at the pixel centers. The resulting color values are then multiplied by the gain and passed through an inverse gamma function to simulate the exposure process. The resulting colors are then passed to a quantizer which scales the values and optionally dithers them before converting them to a fixed-point integer. It is also possible to interpose a programmable imager (written in the Shading Language) between the exposure process and quantizer. This imager can be used to perform special effects processing, to compensate for non-linearities in the display media, and to convert to device dependent color spaces (such as CMYK or pseudocolor).

Final output alpha is computed by multiplying the coverage of the pixel (i.e., the sub-pixel area actually covered by a geometric primitive) by the average of the color opacity components. If an alpha image is being output, the color values will be multiplied by this alpha before being passed to the quantizer. Color and alpha use the same quantizer.

Output depth values are the screen-space z values, which lie in the range 0 to 1. Generally, these correspond to camera-space values between the near and far clipping planes. Depth values bypass all the above steps except for the imager and quantization. The depth quantizer has an independent set of parameters from those of the color quantizer.

The color of a pixel computed by the rendering program is an estimate of the true pixel value: the convolution of the continuous image with the filter specified by **RiPixelFilter**. This routine sets the upper bound on the acceptable estimated variance of the pixel values from the true pixel values.

```
RIB BINDING **PixelVariance** variation
EXAMPLE **RiPixelVariance**(.01); SEE ALSO **[RiPixelFilter](#RiPixelFilter), [RiPixelSamples](#RiPixelSamples)**
```

```c
**RiPixelSamples**( xsamples, ysamples ) **RtFloat**	xsamples, ysamples;
```

Set the effective hider sampling rate in the horizontal and vertical directions. The effective number of samples per pixel is *xsamples\*ysamples*. If an analytic hidden surface calculation is being done, the effective sampling rate is RI_INFINITY. Sampling rates less than 1 are clamped to 1.

```
RIB BINDING **PixelSamples** xsamples ysamples
EXAMPLE **PixelSamples** 2 2
SEE ALSO **[RiPixelFilter](#RiPixelFilter), [RiPixelVariance](#RiPixelVariance)**
```

```c
**RiPixelFilter**( filterfunc, xwidth, ywidth ) **RtFloatFunc**	filterfunc; **RtFloat**	xwidth, ywidth;
```

Antialiasing is performed by filtering the geometry (or supersampling) and then sampling at pixel locations. The *filterfunc* controls the type of filter, while *xwidth* and *ywidth* specify the width of the filter in pixels. A value of 1 indicates that the support of the filter is one pixel. RenderMan supports nonrecursive, linear shift-invariant filters. The type of the filter is set by passing a reference to a function that returns a filter kernel value; i.e.,

```c
*filterkernelvalue = (*filterfunc)( x, y, xwidth, ywidth );*
```

(where (*x,y*) is the point at which the filter should be evaluated). The rendering program only requests values in the ranges *-xwidth*/2 to *xwidth*/2 and *-ywidth*/2 to *ywidth*/2. The values returned need not be normalized. The following standard filter functions are available:

```c
**RtFloat RiBoxFilter**();
**RtFloat RiTriangleFilter**();
**RtFloat RiCatmullRomFilter**();
**RtFloat RiGaussianFilter**();
**RtFloat RiSincFilter**();
```

A high-resolution picture is often computed in sections or panels. Each panel is a subrectangle of the final image. It is important that separately computed panels join together without a visible discontinuity or seam. If the filter width is greater than 1 pixel, the rendering program must compute samples outside the visible window to properly filter before sampling.

```
RIB BINDING **PixelFilter** type xwidth ywidth
```

The *type* is one of: "box," "triangle," "catmull-rom" (cubic), "sinc" and "gaussian."

```
EXAMPLE **RiPixelFilter(RiGaussianFilter**, 2.0, 1.0); **PixelFilter** "gaussian" 2 1
SEE ALSO **[RiPixelSamples](#RiPixelSamples), [RiPixelVariance](#RiPixelVariance)**
```

```c
**RiExposure**( gain, gamma ) **RtFloat**	gain; **RtFloat**	gamma;
```

This function controls the sensitivity and non-linearity of the exposure process. Each component of color is passed through the following function:

```
RIB BINDING **Exposure** gain gamma
EXAMPLE **Exposure** 1.5 2.3
SEE ALSO **[RiImager](#RiImager)**
```

```c
**RiImager**( name, parameterlist ) **RtToken**	name;
```

Select an imager function programmed in the Shading Language. *name* is the name of an *imager shader*. If *name* is RI_NULL, no *imager shader* is used.

```
RIB BINDING **Imager** name parameterlist
EXAMPLE **RiImager**("cmyk," RI_NULL);
SEE ALSO **[RiExposure](#RiExposure)**
```

```c
**RiQuantize**( type, one, min, max, ditheramplitude ) **RtToken**	type; **RtInt**	one, min, max; **RtFloat**	ditheramplitude;
```

Set the quantization parameters for colors *or* depth. If *type* is "rgba," then color and opacity quantization are set. If *type* is "z," then depth quantization is set. The value *one* defines the mapping from floating-point values to fixed point values. If *one* is 0, then quantization is not done and values are output as floating point numbers. Dithering is performed by adding a random number to the floating-point values before they are rounded to the nearest integer. The added value is scaled to lie between plus and minus the dither amplitude. If *ditheramplitude* is 0, dithering is turned off. Quantized values are computed using the following formula:

```c
*value* = *round*( one * *value* + ditheramplitude * *random*() );
*value* = *clamp*( *value*, min, max );
```

where *random* returns a random number between ± 1.0, and *clamp* clips its first argument so that it lies between *min* and *max*. By default color pixel values are dithered with an amplitude of .5 and quantization is performed for an 8-bit display with a *one* of 255. Quantization and dithering and not performed for depth values (by default).

```
RIB BINDING **Quantize** type one min max ditheramplitude
EXAMPLE **RiQuantize**(RI_RGBA, 2048, -1024, 3071, 1.0);
SEE ALSO **[RiDisplay](#RiDisplay), [RiImager](#RiImager)**
```

```c
**RiDisplay**( name, type, mode, parameterlist ) char	*name; **RtToken**	type; **RtToken**	mode;
```

Choose a display by name and set the type of output being generated. The *type* of display is either "framebuffer" or "file." *name* is either the name of a picture file or the name of the framebuffer, depending on *type*. A rendering program may output any combination of color, opacity and depth (z) values. Output image selection is controlled by giving any combination (string concatenation) of "rgb" for color (usually red, green and blue intensities unless there are more or less than 3 color samples; see the next section, [Additional options](#Additional.options)), "a" for alpha, and "z" for depth values, in that order. Display options or device-dependent display modes or functions may be set using the parameterlist. One such option is required: "origin," which takes an array of two **RtInt**s, sets the *x* and *y* position of the upper left hand corner of the image in the display's coordinate system; by default the origin is set to (0,0). The default display device is renderer implementation-specific.

```
RIB BINDING **Display** name type mode parameterlist
EXAMPLE **RtInt** origin[2] = { 10, 10 }; **RiDisplay**("pixar0," "framebuffer," "rgba," "origin," (**RtPointer**)origin, RI_NULL);
SEE ALSO **[RiFormat](#RiFormat), [RiQuantize](#RiQuantize)**
```

### Additional options

**Table 4.4 Additional RenderMan Interface Options**

| Option | Type | Default | Description |
|---|---|---|---|
| Hider | token | "hidden" | The type of hidden surface algorithm that is performed. |
| Color Samples | int | 3 | Number of color components in colors. The default is 3 for RGB. |
| Relative Detail | float | 1.0 | A multiplicative factor that can be used to increase or decrease the effective level of detail used to render an object. |

The hider type and parameters control the hidden-surface algorithm.

```c
**RiHider**( type, parameterlist ) **RtToken**	type;
```

The standard types are "hidden," "paint," and "null." "hidden" performs standard hidden-surface computations. "paint" draws the objects in the order in which they are defined. The hider "null" performs no pixel computation and hence produces no output. Other implementation-specific hidden-surface algorithms can also be selected using this routine.

```
RIB BINDING **Hider** type parameterlist
EXAMPLE **RiHider** "paint"
```

Rendering programs compute color values in some *spectral color space*. This implies that multiplying two colors corresponds to interpreting one of the colors as a light and the other as a filter and passing light through the filter. Adding two colors corresponds to adding two lights. The default color space is NTSC-standard RGB; this color space has three samples. Color values of 0 are interpreted as black (or transparent) and values of 1 are interpreted as white (or opaque), although values outside this range are allowed.

```c
**RiColorSamples**( n, nRGB, RGBn ) **RtInt**	n; **RtFloat**	nRGB[], RGBn[];
```

This function controls the number of color components or samples to be used in specifying colors. By default, *n* is 3, which is appropriate for RGB color values. Setting *n* to 1 forces the rendering program to use only a single color component. The array *nRGB* is an *n* by 3 transformation matrix that is used to convert *n* component colors to 3 component NTSC-standard RGB colors. This is needed if the rendering program cannot handle multiple components. The array *RGBn* is a 3 by *n* transformation matrix that is used to convert 3 component NTSC-standard RGB colors to *n* component colors. This is mainly used for transforming constant colors specified as color triples in the Shading Language to the representation being used by the RenderMan Interface. Calling this procedure effectively redefines the type **RtColor** to be

```c
typedef **RtFloat**	**RtColor**[n];
```

After a call to **RiColorSamples**, all subsequent color arguments are assumed to be this size. If the *Spectral Color* capability is not supported by a particular implementation, that implementation will still accept multiple component colors, but will immediately convert them to RGB color space and do all internal calculations with 3 component colors.

```
RIB BINDING **ColorSamples** nRGB RGBn
```

The number of color components, *n*, is derived from the lengths of the *nRGB* and *RGBn* arrays, as described above.

```
EXAMPLE **ColorSamples** [.3.3 .4] [1 1 1] **RtFloat** frommonochr[] = {.3, .3, .4}; **RtFloat** tomonochr[] = {1., 1., 1.}; **RiColorSamples**(1, frommonochr, tomonochr);
SEE ALSO **[RiColor](#RiColor), [RiOpacity](#RiOpacity)**
```

The method of specifying and using level of detail is discussed in the section on [Detail](#Detail).

```c
**RiRelativeDetail**( relativedetail ) **RtFloat**	relativedetail;
```

The relative level of detail scales the results of all level of detail calculations. The level of detail is used to select between different representations of an object. If *relativedetail* is greater than 1, the effective level of detail is increased, and a more detailed representation of all objects will be drawn. If *relativedetail* is less than 1, the effective level of detail is decreased, and a less detailed representation of all objects will be drawn.

```
RIB BINDING **RelativeDetail** relativedetail
EXAMPLE **RelativeDetail** 0.6
SEE ALSO **[RiDetail](#RiDetail), [RiDetailRange](#RiDetailRange)**
```

### Implementation-specific options

Rendering programs may have additional implementation-specific options that control parameters that affect either their performance or operation. These are all set by the following procedure.

```c
**RiOption**( name, parameterlist ) **RtToken**	name;
```

Sets the named implementation-specific option. A rendering system may have certain options that must be set before the renderer is initialized. In this case, **RiOption** may be called before **RiBegin** to set those options only.

```
RIB BINDING **Option** name parameterlist
EXAMPLE **Option** "limits" "gridsize" [32] "bucketsize" [12 12]
SEE ALSO **[RiAttribute](#RiAttribute)**
```

## Attributes

Attributes are parameters in the graphics state that may change while geometric primitives are being defined. The complete set of standard attributes is described in two tables: [Table 4.5, Shading Attributes](#Table.4.5), and [Table 4.9, Geometry Attributes](#Table.4.9). Attributes can be explicitly saved and restored with the following commands. All begin-end blocks implicitly do a save and restore.

```c
**RiAttributeBegin**()
**RiAttributeEnd**()
```

Push and pop the current set of attributes. Pushing attributes also pushes the current transformation. Pushing and popping of attributes must be properly nested with respect to various begin-end constructs.

```
RIB BINDING **AttributeBegin** - **AttributeEnd** -
EXAMPLE **RiAttributeBegin**();
SEE ALSO **[RiFrameBegin](#RiFrameBegin), [RiTransformBegin](#RiTransformBegin), [RiWorldBegin](#RiWorldBegin)**
```

The process of shading is described is detail in [Part II: The RenderMan Shading Language](section8.html). The complete list of attributes related to shading are in [Table 4.5, Shading Attributes](#Table.4.5). The graphics state maintains a list of attributes related to shading. Associated with the shading state are a *current color* and a *current opacity*. The graphics state also contains a *current surface shader*, a *current atmosphere shader*, a *current interior volume shader*, and a *current exterior volume shader*. All geometric primitives use the *current surface shader* for computing the color (shading) of their surfaces and the *current atmosphere shader* for computing the attenuation of light towards the viewer. Solid primitives attach the *current interior* and *exterior volume shaders* to their interior and exterior. The graphics state also contains a *current list of light sources* that are used to illuminate the geometric primitive. Finally, there is a *current area light source*. Geometric primitives can be added to a list of primitives defining this light source.

**Table 4.5 Shading Attributes**

| Shading Attribute | Type | Default | Description |
|---|---|---|---|
| Color | color | color "rgb" (1,1,1) | The reflective color of the object. |
| Opacity | color | color "rgb" (1,1,1) | The opacity of the object. |
| Texture Coordinates | 8 floats | (0,0)(1,0),(0,1),(1,1 | The texture coordinates (s, t) at the 4 corners of a parametric primitive. |
| Light Sources | shader list | "null" | A list of light source shaders that illuminate subsequent primitives. |
| Area Light Source | shader | "null" | An area light source which is being defined. |
| Surface | shader | default surface | A shader controlling the surface shading model. |
| Atmosphere | shader | "null" | A volume shader that specifies how the color of light is changed as it travels from a visible surface to the eye. |
| Interior Volume Exterior Volume | shader shader | "null" "null" | A volume shader that specifies how the color of light is changed as it traverses a volume in space. |
| Effective Shading Rate | float | .25 | Minimum rate of surface shading. |
| Shading Interpolation | token | "constant" | How the results of shading are interpolated across a polygon. |
| Matte Surface Flag | boolean | false | A flag indicating the surfaces of the subsequent primitives are opaque to the rendering program, but transparent on output. |

### Color and opacity

All geometric primitives inherit the current color and opacity from the graphics state, unless color or opacity are defined as part of the primitive. Colors are passed in arrays that are assumed to contain the number of color samples being used (see the section on [Additional options](#Additional.options)).

```c
**RiColor**( color ) **RtColor**	color;
```

Set the current color to color. Normally there are three components in the color (*red, green*, and *blue*), but this may be changed with the colorsamples request.

```
RIB BINDING **Color** c0 c1... cn < **Color** [c0 c1... cn]
EXAMPLE **RtColor** blue = { .2, .3, .9}; **RiColor**(blue); **Color** [.2 .3 .9]
SEE ALSO **[RiOpacity](#RiOpacity), [RiColorSamples](#RiColorSamples)**
```

```c
**RiOpacity**( color ) **RtColor**	color;
```

Set the current opacity to *color*. The color component values must be in the range. No[1]rmally there are three components in the color *(red, green*, and *blue*), but this may be changed with **RiColorSamples**. If the opacity is 1, the object is completely opaque; if the opacity is 0, the object is completely transparent.

```
RIB BINDING **Opacity** c0 c1... cn **Opacity** [c0 c1... cn]
EXAMPLE **Opacity** .5 1 1
SEE ALSO **[RiColorSamples](#RiColorSamples), [RiColor](#RiColor)**
```

### Texture coordinates

The Shading Language allows precalculated images to be accessed by a set of two-dimensional texture coordinates. This general process is referred to as *texture mapping*. Texture access in the Shading Language is very general since the coordinates are allowed to be any legal expression. However, the texture and bump access functions (in Part II, see the sections on [Basic texture maps](section15.html#texture) and [Bump maps](section15.html#bump)) often use default texture coordinates related to the surface parameters. All the parametric geometric primitives have surface parameters (*u,v*) that can be used as their texture coordinates (*s,t*). Surface parameters for different primitives are normally defined to lie in the range 0 to 1. This defines a unit square in parameter space. [Section 5, Geometric Primitives](section5.html) defines the position on each surface primitive that the corners of this unit square lie. The texture coordinates at each corner of this unit square are given by providing a corresponding set of (*s,t*) values. This correspondence uniquely defines a 3x3 homogeneous two-dimensional mapping from parameter space to texture space. Special cases of this mapping occur when the transformation reduces to a scale and an offset, which is often used to piece patches together, or to an affine transformation, which is used to map a collection of triangles onto a common planar texture. The graphics state maintains a *current set of texture coordinates*. The correspondence between these texture coordinates and the corners of the unit square is given by the following table.

| Surface Parameters (u,v) | Texture Coordinates (s,t) |
|---|---|
| (0,0) | (s1,t1) |
| (1,0) | (s2,t2) |
| (0,1) | (s3,t3) |
| (1,1) | (s4,t4) |

By default, the texture coordinates at each corner are the same as the surface parameters (*s=u, t=v*). Note that texture coordinates can also be explicitly attached to geometric primitives. Note also that polygonal primitives are not parametric, and the current set of texture coordinates do not apply to them.

```c
**RiTextureCoordinates**( s1,t1,s2,t2,s3,t3,s4,t4 ) **RtFloat**	s1, t1;< **RtFloat**	s2, t2; **RtFloat**	s3, t3; **RtFloat**	s4, t4;
```

Set the current set of texture coordinates to the values passed as arguments according to the above table.

```
RIB BINDING **TextureCoordinates** s1 t1 s2 t2 s3 t3 s4 t4 **TextureCoordinates** [s1 t1 s2 t2 s3 t3 s4 t4]
EXAMPLE **RiTextureCoordinates**(0.0,0.0, 2.0,-0.5, -0.5,1.75, 3.0,3.0);
SEE ALSO [texture](section15.html#texture)() and [bump](section15.html#bump)() in the Shading Language
```

### Light sources

The graphics state maintains a *current light source list*. The lights in this list illuminate subsequent surfaces. By making this list an attribute different light sources can be used to illuminate different surfaces. Light sources can be added to this list by turning them on and removed from this list by turning them off. Note that popping to a previous graphics state also has the effect of returning the current light list to its previous value. Initially the graphics state does not contain any lights. An area light source is defined by a shader and a collection of geometric primitives. The association between the shader and the geometric primitives is done by having the graphics state maintain a *single current area light source*. Each time a primitive is defined it is added to the list of surfaces that define the area light. *current light source list* or turned on and off just like other light sources. The RenderMan Interface includes four standard types of light sources: "ambientlight," "pointlight," "distantlight," and "spotlight." The definition of these light sources are given in [Appendix A, Standard RenderMan Interface Shaders](appendix.A.html). The parameters controlling these light sources are given in Table 4.6, Standard Light Source Shader Parameters.

**Table 4.6 Standard Light Source Shader Parameters**

| Light Source | Parameter | Type | Default | Description |
|---|---|---|---|---|
| ambientlight | intensity lightcolor | float color | 1.0 color "rgb" (1,1,1) | Light intensity Light color |
| distantlight | intensity lightcolor from to | float color point point | 1.0 color "rgb" (1,1,1) point "shader"(0,0,0) point "shader"(0,0,1) | Light intensity Light color Light position Light direction is from-to |
| pointlight | intensity lightcolor from | float color point | 1.0 color "rgb" (1,1,1) point "shader"(0,0,0) | Light intensity Light color Light position |
| spotlight | intensity lightcolor from to coneangle conedeltaangle beamdistribution | float color point point float float float | 1.0 color "rgb" (1,1,1) point "shader"(0,0,0) point "shader"(0,0,1) radians(30) radians(5) 2.0 | Light intensity Light color Light position Light direction is from-to Light cone angle Light soft edge angle Light beam distribution |

```c
**RtLightHandle** **RiLightSource**( shadername, parameterlist ) **RtToken**	shadername;
```

*shadername* is the name of a light source shader. This procedure creates a non-area light, turns it on, and adds it to the *current light source list*. An **RtLightHandle** value is returned that can be used to turn the light off or on again.

```
RIB BINDING **LightSource** name sequencenumber parameterlist
```

The sequencenumber is a unique light identification number which is provided by the RIB client to the RIB server. Both client and server maintain independent mappings between the sequencenumber and their corresponding **RtLightHandle**s. The number must be in the range 0 to 65535.

```
EXAMPLE **LightSource** "spotlight" 2 "coneangle" [5] **LightSource** "ambientlight" 3 "lightcolor" [.5 0 0] "intensity" [.6]
SEE ALSO **[RiAreaLightSource](#RiAreaLightSource), [RiIlluminate](#RiIlluminate), [RiFrameEnd](#RiFrameEnd), [RiWorldEnd](#RiWorldEnd)**
```

```c
**RtLightHandle** **RiAreaLightSource**( shadername, parameterlist ) **RtToken**	shadername;
```

*shadername* is the name of a light source shader. This procedure creates an area light and makes it the *current area light source*. Each subsequent geometric primitive is added to the list of surfaces that define the area light. **RiAttributeEnd** ends the assembly of the area light source. The light is also turned on and added to the *current light source list.* An **RtLightHandle** value is returned which can be used to turn the light off or on again. If the *Area Light Source* capability is not supported by a particular implementation, this subroutine is equivalent to **RiLightSource**.

```
RIB BINDING **AreaLightSource** name sequencenumber parameterlist
```

The sequencenumber is a unique light identification number which is provided by the RIB client to the RIB server. Both client and server maintain independent mappings between the sequencenumber and their corresponding **RtLightHandle**s. The number must be in the range 0 to 65535.

```
EXAMPLE **RtFloat** decay = .5, intensity = .6; **RtColor** color = {.5,0,0}; **RiAreaLightSource** ( "finite," "decayexponent," (RtPointer)&decay, RI_NULL); **RiAreaLightSource** "ambientlight," "lightcolor," (**RtPointer**)color, "intensity," (**RtPointer**)&intensity, RI_NULL);
SEE ALSO **[RiFrameEnd](#RiFrameEnd), [RiLightSource](#RiLightSource), [RiIlluminate](#RiIlluminate), [RiWorldEnd](#RiWorldEnd)**
```

```c
**RiIlluminate**( light, onoff ) **RtLightHandle**	light; **RtBoolean**	onoff;
```

If *onoff* is RI_TRUE and the light source referred to by the **RtLightHandle** is not currently in the *current light source list*, add it to the list. If *onoff* is RI_FALSE and the light source referred to by the **RtLightHandle** is currently in the *current light source list*, remove it from the list. Note that popping the graphics state restores the onoff value of all lights to their previous values.

```
RIB BINDING **Illuminate** sequencenumber onoff
```

The sequencenumber is the integer light handle defined in a **LightSource** or **AreaLightSource** request.

```
EXAMPLE **LightSource** "main" 3 **Illuminate** 3 0
SEE ALSO **[RiAttributeEnd](#RiAttributeEnd), [RiAreaLightSource](#RiAreaLightSource), [RiLightSource](#RiLightSource)**
```

### Surface shading

The graphics state maintains a *current surface shader*. The *current surface shader* is used to specify the surface properties of subsequent geometric primitives. Initially the *current surface shader* is set to an implementation-dependent default surface shader (but not "null"). The RenderMan Interface includes six standard types of surfaces: "constant," "matte," "metal," "shinymetal," "plastic," and "paintedplastic." The definitions of these surface shading procedures are given in [Appendix A, Standard RenderMan Interface Shaders](appendix.A.html). The parameters controlling these surfaces are given in [Table 4.7, Standard Surface Shader Parameters](#Table.4.7).

```c
**RiSurface**( shadername, parameterlist ) **RtToken**	shadername;
```

*shadername* is the name of a surface shader. This procedure sets the *current surface shader* to be shadername. If the surface shader *shadername* is not defined, some implementation-dependent default surface shader (but not "null") is used.

```
RIB BINDING **Surface** shadername parameterlist
EXAMPLE **RtFloat** rough = 0.3, kd = 1.0; **RiSurface**("wood," "roughness,"(**RtPointer**)&rough, "Kd," (**RtPointer**)&kd, RI_NULL);
SEE ALSO **[RiAtmosphere](#RiAtmosphere), [RiDisplacement](#RiDisplacement)**
```

**Table 4.7 Standard Surface Shader Parameters**

| Surface Name | Parameter | Type | Default | Description |
|---|---|---|---|---|
| constant | - | - | - | - |
| matte | Ka Kd | float float | 1.0 1.0 | Ambient coefficient Diffuse coefficient |
| metal | Ka Ks roughness | float float float | 1.0 1.0 0.1 | Ambient coefficient Specular coefficient Surface roughness |
| shinymetal | Ka Ks Kr roughness texturename | float float float float string | 1.0 1.0 1.0 0.1 "" | Ambient coefficient Specular coefficient Reflection coefficient Surface roughness Environment mapname |
| plastic | Ka Kd Ks roughness specularcolor | float float float float color | 1.0 0.5 0.5 0.1 color "rgb" (1,1,1) | Ambient coefficient Diffuse coefficient Specular coefficient Surface roughness Specular color |
| paintedplastic | Ka Kd Ks roughness specularcolor texturename | float float float float color string | 1.0 0.5 0.5 0.1 color "rgb" (1,1,1) "" | Ambient coefficient Diffuse coefficient Specular coefficient Surface roughness Specular color Texture map name |

### Volume shading

The graphics state contains a *current interior volume shader*, a *current exterior volume shader*, and a *current atmosphere shader*. These shaders are used to modify the colors of rays traveling through volumes in space. The interior and exterior shaders define the material properties on the interior and exterior volumes adjacent to the surface of a geometric primitive. The exterior volume relative to a surface is the region into which the normal points; the interior is the opposite side. Interior volume shaders are only applied to closed solids created with **RiSolidBegin-RiSolidEnd** (see the section on [Solids and Spatial Set Operations](section5.html#Solids)). Exterior volume shaders are applied to all primitives. An atmosphere shader is a special shader which is used to modify rays traveling towards the eye. The RenderMan Interface includes two standard volume shaders: "fog" and "depthcue." The definitions of these volume shaders are given in [Appendix A, Standard RenderMan Interface Shaders](appendix.A.html). The parameters controlling these volumes are given in [Table 4.8, Standard Volume Shader Parameters](#Table.4.8).

```c
**RiAtmosphere**( shadername, parameterlist ) **RtToken**	shadername;
```

This procedure sets the *current atmosphere shader*. *shadername* is the name of an atmosphere shader. If *shadername* is RI_NULL, no atmosphere shader is used.

```
RIB BINDING **Atmosphere** shadername parameterlist
EXAMPLE **Atmosphere** "fog"
SEE ALSO **[RiDisplacement](#RiDisplacement)**, **[RiSurface](#RiSurface)**
```

**Table 4.8 Standard Volume Shader Parameters**

| Volume Name | Parameter | Type | Default | Description |
|---|---|---|---|---|
| depthcue | mindistance maxdistance background | float float color | 0.0 1.0 color "rgb" (0,0,0) | Distance where brightest Distance where dimmest Background color |
| fog | distance background | float color | 1.0 color "rgb" (0,0,0) | Exponential extinction distance Background color |

```c
**RiInterior**( shadername, parameterlist ); **RtToken**	shadername;
```

This procedure sets the *current interior volume shader*. *shadername* is the name of a volume or atmosphere shader. If *shadername* is RI_NULL, the surface will not have an interior shader.

```
RIB BINDING **Interior** shadername parameterlist
EXAMPLE **Interior** "water"
SEE ALSO **[RiExterior](#RiExterior), [RiAtmosphere](#RiAtmosphere)**
```

```c
**RiExterior**( shadername, parameterlist ); **RtToken**	shadername;
```

This procedure sets the *current exterior volume shader*. *shadername* is the name of a volume or atmosphere shader. If *shadername* is RI_NULL, the surface will not have an exterior shader.

```
RIB BINDING **Exterior** shadername parameterlist
EXAMPLE **RiExterior**( "fog," RI_NULL );
SEE ALSO **[RiInterior](#RiInterior), [RiAtmosphere](#RiAtmosphere)**
```

If a particular implementation does not support the *Volume Shading* capability, **RiInterior** and **RiExterior** are ignored; however, **RiAtmosphere** will be available in all implementations.

### Shading rate

The number of shading calculations per primitive is controlled by the *current shading rate.* The shading rate is expressed in pixel area. If geometric primitives are being broken down into polygons and each polygon is shaded once, the shading rate is interpreted as the maximum size of a polygon in pixels. A rendering program will shade *at least at this rate*, although it may shade more often. Whatever the value of the shading rate, at least one shading calculation is done per vertex or surface.

```c
**RiShadingRate**( size ) **RtFloat**	size;
```

Set the *current shading rate* to size. The **current shading rate** is specified as an area in pixels. A shading rate of RI_INFINITY specifies that shading need only be done once per polygon. A shading rate of 1 specifies that shading is done at least once per pixel. This second case is often referred to as *Phong shading*.

```
RIB BINDING **ShadingRate** size
EXAMPLE **RiShadingRate**(1.0);
SEE ALSO **[RiGeometricApproximation](#RiGeometricApproximation)**
```

### Shading interpolation

Shading calculations are performed on individual surface elements. The results can then either be interpolated or constant over the interior of the surface element. This is controlled by the following procedure:

```c
**RiShadingInterpolation**( type ) **RtToken**	type;
```

This function controls how values are interpolated between shading samples (usually across a polygon). If *type* is "constant," the color and opacity of all the pixels inside the polygon are the same. This is often referred to as *flat* or *facetted shading*. If *type* is "smooth," the color and opacity of all the pixels between shaded values are interpolated from the calculated values. This is often referred to as *Gouraud shading*.

```
RIB BINDING **ShadingInterpolation** "constant" **ShadingInterpolation** "smooth"
EXAMPLE **ShadingInterpolation** "smooth"
```

### Matte objects

Matte objects are the functional equivalent of three-dimensional hold-out mattes. Matte objects are not shaded and are set to be completely opaque so that they hide objects behind them. However, regions in the output image where a matte object is visible are treated as transparent.

```c
**RiMatte**( onoff ) **RtBoolean**	onoff;
```

Indicates whether subsequent primitives are matte objects.

```
RIB BINDING **Matte** onoff
EXAMPLE **RiMatte**(RI_TRUE);
SEE ALSO **[RiSurface](#RiSurface)**
```

**Table 4.9 Geometry Attributes**

| Attribute | Type | Default | Description |
|---|---|---|---|
| Object to World | transform | identity | Transformation from object or model coordinates to world coordinates. |
| Bound | 6 floats | infinite | Subsequent geometric primitives lie inside this box. |
| Detail Range | 4 floats | (0,0,infinity,infinity) | Current range of detail. If the current detail is in this range, geometric primitives are rendered. |
| Geometric | token value | Ð | The largest deviation of an Approximation approximation of a surface from the true surface in raster coordinates. |
| Cubic Basis Matrices | 2 matrices | Bezier Bezier | Basis matrices for bicubic patches. There is a separate basis matrix for both the *u* and the *v* directions. |
| Cubic Basis Steps | 2 ints | 3, 3 | Patchmesh basis increments. |
| Trim Curves | Ð | Ð | A list of trim curves which bound NURBS. |
| Orientation | token | "outside" | Whether primitives are defined in a left-handed or right-handed coordinate system. |
| Number of Sides | integer | 2 | Whether subsequent surfaces are considered to have one or two sides. |
| Displacement | shader | "null" | A displacement shader that specifies small changes in surface geometry. |

### Bound

The graphics state maintains a bounding box called the *current bound*. The rendering program may clip or cull primitives to this bound.

```c
**RiBound**( bound ) **RtBound**	bound;
```

This procedure sets the *current bound* to *bound*. The bounding box *bound* is specified in the current object coordinate system. Subsequent output primitives should all lie within this bounding box. This allows the efficient specification of a bounding box for a collection of output primitives.

```
RIB BINDING **Bound** xmin xmax ymin ymax zmin zmax **Bound** [xmin xmax ymin ymax zmin zmax]
EXAMPLE **Bound** [0 0.5 0 0.5 0.9 1] SEE ALSO **[RiDetail](#RiDetail)**
```

### Detail

The graphics state maintains a *relative detail*, a *current detail,* and a *current detail range*. The *current detail* is used to select between multiple representations of objects each characterized by a different range of detail. The *current detail range* is given by 4 values. These four numbers define transition ranges between this range of detail and the neighboring representations. If the *current detail* lies inside the *current detail range*, geometric primitives comprising this representation will be drawn. Suppose there are two object definitions, *foo1* and *foo2*, for an object. The first contains more detail and the second less. These are communicated to the rendering program using the following sequence of calls.

```c
**RiDetail**( bound );
**RiDetailRange**( 0., 0., 10., 20. );
**RiObjectInstance**( foo1 );
**RiDetailRange**( 10., 20., RI_INFINITY, RI_INFINITY );
**RiObjectInstance**( foo2 );
```

The *current detail* is set by **RiDetail**. The detail ranges indicate that object *foo1* will be drawn when the *current detail* is below 10 (thus it is the low detail detail representation) and that object *foo2* will be drawn when the *current detail* is above 20 (thus it is the high detail representation). If the *current detail* is between 10 and 20, the rendering program will provide a smooth transition between the low and high detail representations.

```c
**RiDetail**( bound ) **RtBound**	bound;
```

Set the *current bound* to *bound*. The bounding box *bound* is specified in the current coordinate system. The *current detail* is set to the area of this bounding box as projected into the *raster coordinate system*, times the *relative detail*. Before computing the raster area, the bounding box is clipped to the near clipping plane but not to the edges of the display or the far clipping plane. The raster area outside the field of view is computed so that if the camera zooms in on an object the detail will increase smoothly. Detail is expressed in raster coordinates so that increasing the resolution of the output image will increase the detail.

```
RIB BINDING **Detail** minx maxx miny maxy minz maxz **Detail** [minx maxx miny maxy minz maxz]
EXAMPLE **RtBound** box = { 10.0, 20.0, 42.0, 69.0, 0.0, 1.0 }; **RiDetail**(box);
SEE ALSO **[RiBound](#RiBound), [RiDetailRange](#RiDetailRange), [RiRelativeDetail](#RiRelativeDetail)**
```

```c
**RiDetailRange**( minvisible, lowertransition, uppertransition, maxvisible ) **RtFloat**	minvisible, lowertransition; **RtFloat**	uppertransition, maxvisible;
```

Set the *current detail range*. Primitives are never drawn if the *current detail* is less than *minvisible* or greater than *maxvisible*. Primitives are always drawn if the *current detail* is between *lowertransition* and *uppertransition*. All these numbers should be non-negative and satisfy the following ordering:

```c
*minvisible <=; lowertransition <=; uppertransition <=; maxvisible*.
```

```
RIB BINDING **DetailRange** minvisible lowertransition uppertransition maxvisible **DetailRange** [minvisible lowertransition uppertransition maxvisible]
EXAMPLE **DetailRange** [0 0 10 20]
SEE ALSO **[RiDetail](#RiDetail), [RiRelativeDetail](#RiRelativeDetail)**
```

*If the* Detail capability is not supported by a particular implementation, **RiDetail** is equivalent to **RiBound**, and all object representations which include RI_INFINITY in their detail ranges are rendered.

### Geometric approximation

Geometric primitives are typically approximated by using small surface elements or polygons. The size of these surface elements affects the accuracy of the geometry since large surface elements may introduce straight edges at the silhouettes of curved surfaces or cause particular points on a surface to be projected to the wrong point in the final image.

```c
**RiGeometricApproximation**( type, value ) **RtToken**	type; **RtFloat**	value;
```

The predefined geometric approximation is "flatness." Flatness is expressed as a distance from the true surface to the approximated surface in pixels. Flatness is sometimes called *chordal deviation*.

```
RIB BINDING **GeometricApproximation** "flatness" value **GeometricApproximation** type value
EXAMPLE **GeometricApproximation** "flatness" 2.5
SEE ALSO **[RiShadingRate](#RiShadingRate)**
```

### Orientation and sides

The handedness of a coordinate system is referred to as its *orientation*. The initial "camera" coordinate system is left-handed: x points right, y point up, and z points in. Transformations, however, can flip the orientation of the current coordinate system. An example of a transformation that does not preserve orientation is a reflection. (More generally, a transformation does not preserve orientation if its Jacobian is negative.) Similarly, geometric primitives have an orientation, which determines whether their surface normals are defined using a right-handed or left-handed rule in their object coordinate system. Defining the orientation of a primitive to be opposite that of the object coordinate system causes it to be turned inside-out. If a primitive is inside-out, its normal will be computed so that it points in the opposite direction. This has implications for culling, shading, and solids (see the section on [Solids and Spatial Set Operations](section5.html#Solids)). The outside surface of a primitive is the side from which the normal points outward; the inside surface is the opposite side. The interior of a solid is the volume that is adjacent to the inside surface and the exterior is the region adjacent to the outside. This is discussed further in the section on [Geometric Primitives](section5.html). The *current orientation* of primitives is maintained as part of the graphics state independent of the orientation of the current coordinate system.The *current orientation* is initially set to match the orientation of the initial coordinate system, and always flips whenever the orientation of the current coordinate system flips. It can also be modified directly with RiOrientation and RiReverseOrientation. If the *current orientation* is not the same as the orientation of the current coordinate system, geometric primitives are turned inside out, and their normals are automatically flipped.

```c
**RiOrientation**( orientation ) RtToken	orientation;
```

This procedure sets the *current orientation* to be either "outside" (to match the current coordinate system), "inside" (to be the inverse of the current coordinate system), "lh" (for explicit left-handed orientation) or "rh" (for explicit right-handed orientation).

```
RIB BINDING **Orientation** orientation
EXAMPLE **Orientation** "lh"
SEE ALSO **[RiReverseOrientation](#RiReverseOrientation)**
```

```c
**RiReverseOrientation**()
```

Causes the *current orientation* to be toggled. If the orientation was right-handed it is now left-handed, and *vice versa*.

```
RIB BINDING **ReverseOrientation** -
EXAMPLE **RiReverseOrientation**();
SEE ALSO **[RiOrientation](#RiOrientation)**
```

Objects can be two-sided or one-sided. Both the inside and the outside surface of two-sided objects are visible, whereas only the outside surface of a one-sided object is visible. If the outside of a one-sided surface faces the viewer, the surface is said to be *frontfacing*, and if the outside surface faces away from the viewer, the surface is *backfacing*. Normally closed surfaces should be defined as one-sided and open surfaces should be defined as two-sided. The major exception to this rule is transparent closed objects, where both the inside and the outside are visible.

```c
**RiSides**( sides ) **RtInt**	sides;
```

If *sides* is 2, subsequent surfaces are considered two-sided and both the inside and the outside of the surface will be visible. If *sides* is 1, subsequent surfaces are considered one-sided and only the outside of the surface will be visible.

```
RIB BINDING **Sides** sides
EXAMPLE **Sides** 1
SEE ALSO **[RiOrientation](#RiOrientation)**
```

## Transformations

Transformations are used to transform points between coordinate systems. At various points when defining a scene the *current transformation* is used to define a particular coordinate system. For example, **RiProjection** establishes the camera coordinate system, and **RiWorldBegin** establishes the world coordinate system. The *current transformation* is maintained as part of the graphics state. Commands exist to set and to concatenate specific transformations onto the *current transformation*. These include the basic linear transformations translation, rotation, skew, scale and perspective, and non-linear transformations programmed in the Shading Language. Concaten-ating transformations implies that the *current transformation* is updated in such a way that the new transformation is applied to points *before* the old *current transformation*. Standard linear transformations are given by 4x4 matrices. These matrices are premultiplied by 4-vectors in row format to transform them. Nonlinear transformations are programmed in the RenderMan Shading Language. The following three transformation commands set or concatenate a 4x4 matrix onto the *current transformation*:

```c
**RiIdentity**()
```

Set the *current transformation* to the identity.

```
RIB BINDING **Identity** -
EXAMPLE **RiIdentity**( );
SEE ALSO **[RiTransform](#RiTransform)**
```

```c
**RiTransform**( transform ) **RtMatrix**	transform;
```

Set the *current transformation* to the transformation *transform*.

```
RIB BINDING **Transform** transform
EXAMPLE **Transform** [.5 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1]
SEE ALSO **[RiIdentity](#RiIdentity), [RiConcatTransform](#RiConcatTransform)**
```

```c
**RiConcatTransform**( transform ) **RtMatrix**	transform;
```

Concatenate the transformation *transform onto* the *current transformation*. The transformation is applied before all previously applied transformations, that is, before the *current transformation*.

```
RIB BINDING **ConcatTransform** transform
EXAMPLE **RtMatrix foo** = { 2.0, 0.0, 0.0, 0.0,	0.0, 2.0, 0.0, 0.0,	0.0, 0.0, 2.0, 0.0, 0.0, 0.0, 0.0, 1.0 }; **RiConcatTransform** ( foo );
SEE ALSO **[RiIdentity](#RiIdentity), [RiTransform](#RiTransform), [RiRotate](#RiRotate), [RiScale](#RiScale), [RiSkew](#RiSkew)**
```

The following commands perform local concatenations of common linear transformations onto the *current transformation*.

```c
**RiPerspective**( fov ) **RtFloat**	fov;
```

Concatenate a perspective transformation onto the current transformation. The focal point of the perspective is at the origin and its direction is along the z-axis. The field of view angle, *fov*, specifies the full horizontal field of view. The user must exercise caution when using this transformation, since points behind the eye will generate invalid perspective divides which are dealt with in a renderer-specific manner. To request a perspective projection from camera space to screen space, an **RiProjection** request should be used; **RiPerspective** is used to request a perspective modeling transformation from object space to world space, or from world space to camera space.

```
RIB BINDING **Perspective** fov
EXAMPLE **Perspective** 90
SEE ALSO **[RiConcatTransform](#RiConcatTransform), [RiDepthOfField](#RiDepthOfField), [RiProjection](#RiProjection)**
```

```c
**RiTranslate**( dx, dy, dz ) **RtFloat**	dx, dy, dz;
```

Concatenate a translation onto the *current transformation.*

```
RIB BINDING **Translate** dx dy dz
EXAMPLE **RiTranslate**(0.0, 1.0, 0.0);
SEE ALSO **[RiConcatTransform](#RiConcatTransform), [RiRotate](#RiRotate), [RiScale](#RiScale)**
```

```c
**RiRotate**( angle, dx, dy, dz ) **RtFloat**	angle; **RtFloat**	dx, dy, dz;
```

Concatenate a rotation of *angle* degrees about the given axis onto the *current transformation*.

```
RIB BINDING **Rotate** angle dx dy dz
EXAMPLE **RiRotate**(90.0, 0.0, 1.0, 0.0);
SEE ALSO **[RiConcatTransform](#RiConcatTransform), [RiScale](#RiScale), [RiTranslate](#RiTranslate)**
```

```c
**RiScale**( sx, sy, sz ) **RtFloat**	sx, sy, sz;
```

Concatenate a scaling onto the *current transformation*.

```
RIB BINDING **Scale** sx sy sz
EXAMPLE **Scale** .5 1 1
SEE ALSO **[RiConcatTransform](#RiConcatTransform), [RiRotate](#RiRotate), [RiSkew](#RiSkew), [RiTranslate](#RiTranslate)**
```

```c
**RiSkew**( angle, dx1, dy1, dz1, dx2, dy2, dz2 ) **RtFloat**	angle; **RtFloat**	dx1, dy1, dz1; **RtFloat**	dx2, dy2, dz2;
```

Concatenate a skew onto the *current transformation*. This operation shifts all points along lines parallel to the axis vector (*dx2, dy2, dz2*). Points along the axis vector (*dx1, dy1, dz1*) are mapped onto the vector (*x, y, z*), where *angle* specifies the angle (in degrees) between the vectors (*dx1, dy1, dz1*) and (*x, y, z*), The two axes are not required to be perpendicular, however it is an error to specify an angle that is greater than or equal to the angle between them. A negative angle can be specified, but it must be greater than 180 degrees minus the angle between the two axes.

```
RIB BINDING **Skew** angle dx1 dy1 dz1 dx2 dy2 dz2 **Skew** [angle dx1 dy1 dz1 dx2 dy2 dz2]
EXAMPLE **RiSkew**(45.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.0);
SEE ALSO **[RiRotate](#RiRotate), [RiScale](#RiScale), [RiTransform](#RiTransform)**
```

All non-linear transformations are concatenated onto the *current transformation*. They are not representable as 4x4 transformation matrices, but they act as though they were, that is, they deform the current coordinate system and hold their place in the overall transformation hierarchy.

```c
**RiDeformation**( shadername, parameterlist ) **RtToken**	shadername;
```

Concatenate the named transformation onto the *current transformation*. *shadername* is the name of a deformation shader and parameterlist contain variables determining the transformation. If a particular implementation does not support the *Deformations* capability, this shader is ignored.

```
RIB BINDING **Deformation** shadername parameterlist
EXAMPLE **Deformation** "warpit"
SEE ALSO **[RiConcatTransform](#RiConcatTransform), [RiDisplacement](#RiDisplacement), [RiSurface](#RiSurface)**
```

The graphics state maintains a *current displacement shader*. Displacement shaders are procedures that can be used to modify geometry before the lighting stage. A displacement transformation is different from a deformation in that it only affects shape of surface geometry, not all facets of the current coordinate system. The RenderMan Interface includes one standard displacement shader: "bumpy." The definition of this displacement shader is given in [Appendix A, Standard RenderMan Interface Shaders](appendix.A.html). The parameters controlling this displacement is given in [Table 4.10](#Table.4.10).

```c
**RiDisplacement**( shadername, parameterlist ) **RtToken**	shadername;
```

Set the *current displacement shader* to the named shader. *shadername* is the name of a displacement shader. If a particular implementation does not support the Displacements capability, displacement shaders can only change the normal vectors to generate bump mapping, and the surface geometry itself is not modified (see [Displacement Shaders](section12.html#Displacement.shaders)).

```
RIB BINDING **Displacement** shadername parameterlist
EXAMPLE **RiDisplacement**("displaceit," RI_NULL);
SEE ALSO **[RiDeformation](#RiDeformation), [RiMakeBump](section7.html#RiMakeBump), [RiSurface](#RiSurface)**
```

**Table 4.10 Standard Displacement Shader Parameters**

| Shader Name | Parameter | Type | Default | Description |
|---|---|---|---|---|
| bumpy | amplitude texturename | float string | 1.0 "" | Bump scaling factor Displacement map name |

### Named coordinate systems

Shaders often need to perform calculations in non-standard coordinate systems. The coordinate systems with predefined names are: "raster," "screen," "camera," "world," and "object." At any time, the current coordinate system can be marked for future reference.

```c
**RiCoordinateSystem**( space ) **RtToken**	space;
```

This function marks the coordinate system defined by the current transformation with the name space and saves it. This coordinate system can then be referred to by name in subsequent shaders, or in RiTransformPoints. A shader cannot refer to a coordinate system that has not already been named. The list of named coordinate systems is global.

```
RIB BINDING **CoordinateSystem** space
EXAMPLE **CoordinateSystem** "lamptop"
SEE ALSO **[RiTransformPoints](#RiTransformPoints)**
```

```c
**RtPoint \***
**RiTransformPoints**( fromspace, tospace, n, points ) **RtToken**	fromspace, tospace; **RtInt**	n; **RtPoint**	points[];
```

This procedure transforms the array of points from the coordinate system *fromspace* to the coordinate system *tospace*. This array contains *n* points. If the transformation is successful, the array *points* is returned. If the transformation cannot be computed for any reason (e.g., one of the space names is unknown or the transformation requires the inversion of a noninvertable transformation), NULL is returned.

```c
EXAMPLE **RtPoint** four_points[4]; **RiTransformPoints**("current," "lamptop," 4, four_points);
SEE ALSO **[RiCoordinateSystem](#RiCoordinateSystem), [RiProjection](#RiProjection), [RiWorldBegin](#RiWorldBegin)**
```

### Transformation stack

Transformations can be saved and restored recursively. Note that pushing and popping the attributes also pushes and pops the current transformation.

```c
**RiTransformBegin**()
**RiTransformEnd**()
```

Push and pop the current transformation. Pushing and popping must be properly nested with respect to the various begin-end constructs.

```
RIB BINDING **TransformBegin** - **TransformEnd** -
EXAMPLE **RiTransformBegin**();
SEE ALSO **[RiAttributeBegin](#RiAttributeBegin)**
```

## Implementation-specific Attributes

Rendering programs may have additional implementation-specific attributes that control parameters that affect primitive appearance or interpretation. These are all set by the following procedure.

```c
**RiAttribute**( name, parameterlist ); **RtToken**	name;
```

Set the parameters of the attribute name, using the values specified in the token-value list *parameterlist*.

```
RIB BINDING **Attribute** name parameterlist
EXAMPLE **Attribute** "bound" "displacement" [2.0]
SEE ALSO **[RiAttributeBegin](#RiAttributeBegin)**
```
# SECTION 5 - GEOMETRIC PRIMITIVES

*   Polygons
*   Patches
*   Quadrics
*   Procedural Primitives
*   Implementation-specific Geometric Primitives
*   Solids and Spatial Set Operations
*   Retained Geometry

The RenderMan® Interface supports only surface- and solid-defining geometric primitives. Solid primitives are created from surfaces and combined using set operations. The geometric primitives include:

*   planar convex polygons,
*   general planar concave polygons with holes,
*   collections of planar convex or general planar concave polygons with holes which share vertices (polyhedra),
*   bilinear patches and patch meshes,
*   bicubic patches and patch meshes with an arbitrary basis,
*   non-uniform rational B-spline surfaces of arbitrary degree (NURBS),
*   quadric surfaces, tori, and disks.

Points are used to construct polygons, patches and NURBS. Point positions can be either an (*x,y,z*) triplet ("P") or an (*x,y,z,w*) 4-vector ("Pw"). If the vertex is part of a patch mesh, the position may be used to define a height field. In this case the vertex point contains only a (*z*) coordinate ("Pz"), and the *(x,y*)s of points of the height field are set equal to the parametric surface parameters of the mesh. All primitives have well-defined geometric surface normals, so normals need not be provided with any primitive. The surface normal for a polygon is the perpendicular to the plane containing the polygon. The surface normal for a parametric curved surface is computed by taking the cross product of the surface's parametric derivatives: (*dP/du)x(dP/dv*). As mentioned in the section Orientation and sides, if the *current orientation* does not match the orientation of the current coordinate system, normals will be flipped. The geometric plane normal of a polygon or bilinear patch can be supplied explicitly ("Np"), in which case that normal is used, and the normals are not computed. It is also possible to provide additional shading normals ("N") at polygon and bilinear patch vertices to help make the surface appear smooth. All primitives have well-defined two-dimensional surface parameters. All the points on the surface of each primitive are functions of these parameters *(u,v*). Except for NURBS and polygons, the domain of the surface parameters is the unit square from 0 to 1. Texture coordinates may be attached to primitives by assigning four sets of texture coordinates, one set to each corner of this unit square. This is done by setting the *current set of texture coordinates* or by defining texture coordinates with the geometric primitives as described below. All geometric primitives normally inherit their color and opacity from the graphics state. However, explicit colors and opacities can be provided when defining the primitive ("Cs" and "Os"). Associated with each geometric primitive definition are additional *primitive variables* that are passed to their shaders. These variables may define quantities that are constant over the surface (class *uniform*), or are bilinearly interpolated (class *varying*). If the primitive variable is uniform, there is one value per surface facet. If the primitive variable is varying, there are four values per surface facet, one for each corner of the unit square in parameter space (except polygons, which are a special case). On parametric primitives (quadrics and patches), varying primitive variables are bilinearly interpolated across the surface of the primitive. Colors, opacities, and shading normals are all examples of varying primitive variables. The standard predefined primitive variables are defined in Table 5.1, Standard Geometric Primitive Variables. Other primitive variables may be predefined by specific implementations or defined by the user with the **RiDeclare** function. Primitive variables which are declared to be of type *point* (including the three predefined position variables) are specified in object space, and will be transformed by the current transformation matrix. If point variables in another space are desired, **RiTransformPoints** may be used. The two predefined normal variables will be transformed by the equivalent transformation matrix for normal vectors. Primitive variables which are declared to be of type *color* must contain the correct number of floating point values as defined in **RiColorSamples**. More information about how to use primitive variables is contained in Part II: The RenderMan Shading Language.

**Table 5.1 Standard Geometric Primitive Variables**

| Information | Name | Type | Class | Floats |
|---|---|---|---|---|
| Position | "P" "Pz" "Pw" | point point hpoint | vertex vertex vertex | 3 1 4 |
| Normal | "N" "Np" | normal normal | varying uniform | 3 3 |
| Color | "Cs" | color | varying | (3) |
| Opacity | "Os" | color | varying | (3) |
| Texture Coordinates | "s" "t" "st" | float float 2 float | varying varying varying | 1 1 2 |

### Polygons

The RenderMan Interface supports two basic types of polygons: a convex polygon and a general concave polygon with holes. In both cases the polygon must be planar. Collections of polygons can be passed by giving a list of points and an array that indexes these points. The geometric normal of the polygon is computed by computing the normal of the plane containing the polygon (unless it is explicitly specified). If the *current orientation* is left-handed, then a polygon whose vertices were specified in clockwise order (from the point of view of the camera) will be a front-facing polygon (that is, will have a normal vector which points toward the camera). If the *current orientation* is right-handed, then polygons whose vertices were specified in counterclockwise order will be front-facing. The shading normal is set to the geometric normal unless it is explicitly specified at the vertices. The surface parameters of a polygon are its (*x,y*) coordinates. This is because the height *z* of a plane is naturally parameterized by its (*x,y*) coordinates, unless it is vertical. Texture coordinates are set equal to the surface parameters unless texture coordinates are given explicitly, one set per vertex. Polygons do *not* inherit texture coordinates from the graphics state. The rules for primitive variable interpolation and texture coordinates are different for polygons than for all other geometric primitives. Uniform primitive variables are supplied for each polygon. Varying primitive variables are supplied for each polygon vertex, and are interpolated across the interior without regard to the artificial surface parameters defined above. Note that interpolating values across polygons is inherently ill-defined. However, linearly interpolating values across a triangle is always well defined. Thus, for the purposes of interpolation, polygons are always decomposed into triangles. However, the details of how this decomposition is done is implementation-dependent and may depend on the view.

```c
**RiPolygon**( nvertices, parameterlist )	**RtInt**	nvertices;
```

*nvertices* is the number of vertices in a single closed planar convex polygon. *parameterlist* is a list of token-array pairs where each token is one of the standard geometric primitive variables or a variable which has been defined with **RiDeclare**. The parameter list must include at least position ("P") information. If a primitive variable is varying, the array contains *nvertices* elements of the type corresponding to the token. If the variable is uniform, the array contains a single element. The number of floats associated with each type is given in Table 5.1, Standard Geometric Primitive Variables. No checking is done by the RenderMan Interface to ensure that polygons are planar, convex and nondegenerate. The rendering program will attempt to render invalid polygons but the results are unpredictable.

```
RIB BINDING	**Polygon** parameterlist
```

The number of vertices in the polygon is determined implicitly by the number of elements in the required position array.

```
EXAMPLE	**RtPoint** points =[1] ( 0.0, 1.0, 0.0, 0.0, 1.0, 1.0,	0.0, 0.0, 1.0, 0.0, 0.0, 0.0); **RiPolygon**(4, RI_P, (RtPointer)points, RI_NULL); SEE ALSO **[RiGeneralPolygon](#RiGeneralPolygon), [RiPointsGeneralPolygons](#RiPointsGeneralPolygons), [RiPointsPolygons](#RiPointsPolygons)**
```

An example of the definition of a "Gouraud-shaded" polygon is:

```c
**RtPoint**	points;
**RtColor**	colors;
**RiPolygon**( 4, "P", (**RtPointer**)points, "Cs", (**RtPointer**)colors, RI_NULL );
```

A "Phong-shaded" polygon is given by:

```c
**RtPoint**	points;
**RtPoint**	normals;
**RiPolygon**( 4, "P", (**RtPointer**)points, "N", (**RtPointer**)normals, RI_NULL );
```

A "Phong-shaded" polygon with a precomputed plane normal is:

```c
**RtPoint**	points;
**RtPoint**	normals;
**RtPoint**	plane_normal;
**RiPolygon**( 4, "P", (**RtPointer**)points, "N", (**RtPointer**)normals,	"Np", (**RtPointer**)plane_normal, RI_NULL );
```

```c
**RiGeneralPolygon**( nloops, nvertices, parameterlist )	**RtInt**	nloops;	**RtInt**	nvertices[];
```

Define a general planar concave polygon with holes. This polygon is specified by giving *nloops* lists of vertices. The first loop is the outer boundary of the polygon; all additional loops are holes. The array *nvertices* contains the number of vertices in each loop, and has length *nloops*. The vertices in all the loops are concatenated into a single vertex array. The length of this array, *n*, is equal to the sum of all the values in the array *nvertices*. *parameterlist* is a list of token-array pairs where each token is one of the standard geometric primitive variables or a variable that has been defined with **RiDeclare**. The parameter list must include at least position ("P") information. If a primitive variable is varying, the array contains *n* elements of the type corresponding to the token. If the variable is uniform, there is a single element of that type. The number of floats associated with each type is given in Table 5.1, Standard Geometric Primitive Variables. The interpretation of these variables is the same as for a convex polygon. No checking is done by the RenderMan Interface to ensure that polygons are planar and nondegenerate. The rendering program will attempt to render invalid polygons but the results are unpredictable.

```
RIB BINDING	**GeneralPolygon** nvertices parameterlist
```

The number of loops in the general polygon is determined implicitly by the length of the *nvertices* array.

```
EXAMPLE	**GeneralPolygon** [4 3] "P" [	0 0 0 0 1 0 0 1 1 0 0 1	0 0.25 0.5 0 0.75 0.75 0 0.75 0.25 ]
SEE ALSO	**[RiPolygon](#RiPolygon), [RiPointsPolygons](#RiPointsPolygons), [RiPointsGeneralPolygons](#RiPointsGeneralPolygons)**
```

```c
**RiPointsPolygons**( npolys, nvertices, vertices, parameterlist )<	**RtInt**	npolys;<	**RtInt**	nvertices[];	**RtInt**	vertices[];
```

Define *npolys* planar convex polygons that share vertices. The array *nvertices* contains the number of vertices in each polygon and has length *npolys*. The array *vertices* contains, for each polygon vertex, an index into the varying primitive variable arrays. The varying arrays are 0-based. *vertices* has length equal to the sum of all of the values in the *nvertices* array. Individual vertices in the *parameterlist* are thus accessed indirectly through the indices in the array *vertices*. *parameterlist* is a list of token-array pairs where each token is one of the standard geometric primitive variables or a variable that has been defined with **RiDeclare**. The parameter list must include at least position ("P") information. If a primitive variable is varying, the array contains *n* elements of the type corresponding to the token, where the number *n* is equal to the maximum value in the array *vertices* plus one. If the variable is uniform, the array contains *npolys* elements of the associated type. The number of floats associated with each type is given in Table 5.1, Standard Geometric Primitive Variables. The interpretation of these variables is the same as for a convex polygon. No checking is done by the RenderMan Interface to ensure that polygons are planar, convex and nondegenerate. The rendering program will attempt to render invalid polygons but the results are unpredictable.

```
RIB BINDING **PointsPolygons** nvertices vertices parameterlist
```

The number of polygons is determined implicitly by the length of the *nvertices* array.

```
EXAMPLE	**PointsPolygons** [3 3 3] [0 3 2 0 1 3 1 4 3]	"P" [0 1 1 0 3 1 0 0 0 0 2 0 0 4 0]	"Cs" [0 .3 .4 0 .3 .9 .2 .2 .2 .5 .2 0 .9 .8 0]
SEE ALSO	**[RiGeneralPolygon](#RiGeneralPolygon), [RiPointsGeneralPolygons](#RiPointsGeneralPolygons), [RiPolygon](#RiPolygon)**
```

```c
**RiPointsGeneralPolygons** ( npolys, nloops, nvertices, vertices, parameterlist )	**RtInt**	npolys;	**RtInt**	nloops[];	**RtInt**	nvertices[];	**RtInt**	vertices[];
```

Define *npolys* general planar concave polygons, with holes, that share vertices. The array *nloops* indicates the number of loops comprising each polygon and has a length *npolys*. The array *nvertices* contains the number of vertices in each loop and has a length equal to the sum of all the values in the array *nloops*. The array *vertices* contains, for each loop vertex, an index into the varying primitive variable arrays. All of the arrays are 0-based. *vertices* has a length equal to the sum of all the values in the array *nvertices*. Individual vertices in the *parameterlist* are thus accessed indirectly through the indices in the array *vertices*. parameterlist is a list of token-array pairs where each token is one of the standard geometric primitive variables or a variable that has been defined with **RiDeclare**. The parameter list must include at least position ("P") information. If a primitive variable is varying, the array contains *n* elements of the type corresponding to the token. The number *n* is equal to the maximum value in the array *vertices* plus one. If the variable is uniform, the array contains *npolys* elements of the associated type. The number of floats associated with each type is given in Table 5.1, Standard Geometric Primitive Variables. The interpretation of these variables is the same as for a convex polygon. No checking is done by the RenderMan Interface to ensure that polygons are planar and nondegenerate. The rendering program will attempt to render invalid polygons but the results are unpredictable.

```
RIB BINDING	**PointsGeneralPolygons** nloops nvertices vertices parameterlist
```

The number of polygons is determined implicitly by the length of the nloops array.

```
EXAMPLE	**PointsGeneralPolygons** [2 2] [4 3 4 3]	[0 1 3 4 6 7 8 1 2 5 4 9 10 11]	"P" [0 0 1 0 1 1 0 2 1 0 0 0 0 1 0 0 2 0	0 0.25 0.5 0 .75 .75 0 1.75 .25	0 1.25 0.5 0 1.75 .75 0 1.75 .25]
SEE ALSO	**[RiGeneralPolygon](#RiGeneralPolygon), [RiPointsPolygons](#RiPointsPolygons), [RiPolygon](#RiPolygon)**
```

### Patches

Patches can be either *uniform* or *non-uniform* (contain different knot values). Patches can also be *rational* or *non-rational* depending on whether the control points are (x,y,z) or (x,y,z,w). Patches may also be bilinear or bicubic. The graphics state maintains two 4x4 matrices that define the bicubic patch basis matrices. One of these is the *current u-basis* and the other is the *current v-basis*. Basis matrices are used to transform from the power basis to the preferred basis.

```c
**RiBasis**( ubasis, ustep, vbasis, vstep )	**RtBasis**	ubasis, vbasis;	**RtInt**	ustep, vstep;
```

Set the *current u-basis* to *ubasis* and the *current v-basis* to *vbasis*. Predefined basis matrices exist for the common types:

```c
**RtBasis RiBezierBasis; RtBasis RiBSplineBasis; RtBasis RiCatmullRomBasis; RtBasis RiHermiteBasis; RtBasis RiPowerBasis;**
```

The variables *ustep* and *vstep* specify the number of control points that should be skipped in the *u* and *v* directions, respectively, to get to the next patch in a bicubic patch mesh. The appropriate step values for the predefined cubic basis matrices are:

| Basis | Step |
|---|---|
| **RiBezierBasis** | 3 |
| **RiBSplineBasis** | 1 |
| **RiCatmullRomBasis** | 1 |
| **RiHermiteBasis** | 2 |
| **RiPowerBasis** | 4 |

The default basis matrix is **RiBezierBasis** in both directions.

```
RIB BINDING	**Basis** uname ustep vname vstep	**Basis** uname ustep vbasis vstep	**Basis** ubasis ustep vname vstep	**Basis** ubasis ustep vbasis vstep
```

For each basis, either the name of a predefined basis (as a string) or a matrix may be supplied. If a basis name specified, it must be one of: "bezier", "b-spline", "catmull-rom", "hermite", or "power."

```
EXAMPLE	**Basis** "b-spline" 1 [-1 3 -3 1 3 -6 3 0 -3 3 0 0 1 0 0 0] 1
SEE ALSO	**[RiPatch](#RiPatch), [RiPatchMesh](#RiPatchMesh)**
```

Note that the geometry vector used with the **RiHermiteBasis** basis matrix must be (point0, vector0, point1, vector1), which is a permutation of the Hermite geometry vector often found in mathematics texts. Using this formulation permits a step value of 2 to correctly increment over data in Hermite patch meshes.

```c
**RiPatch**( type, parameterlist )	**RtToken**	type;
```

Define a single patch. type can be either "bilinear" or "bicubic". *parameterlist* is a list of token-array pairs where each token is one of the standard geometric primitive variables or a variable which has been defined with **RiDeclare**. The parameter list must include at least position ("P", "Pw" or "Pz") information. Patch arrays are specified such that u varies faster than v. Four points define a bilinear patch, and 16 define a bicubic patch. The order of vertices for a bilinear patch is (0,0),(1,0),(0,1),(1,1). Note that the order of points defining a quadrilateral is different depending on whether it is a bilinear patch or a polygon. The vertices of a polygon would normally be in clockwise (0,0),(0,1),(1,1),(1,0) order. Patch primitive variables which are *uniform* should supply one value, which is constant over the patch. Primitive variables which are *varying* should supply four values, one for each parametric corner of the patch. The actual size of each array is this number of values times the size of the type associated with the variable.

```
RIB BINDING	**Patch** type parameterlist
EXAMPLE	**Patch** "bilinear" "P" [ -0.08 0.04 0.05 0 0.04 0.05	-0.08 0.03 0.05 0 0.03 0.05]
SEE ALSO	**[RiBasis](#RiBasis), [RiNuPatch](#RiNuPatch), [RiPatchMesh](#RiPatchMesh)**
```

```c
**RiPatchMesh**( type, nu, uwrap, nv, vwrap, parameterlist )	**RtToken**	type;	**RtToken**	uwrap, vwrap;	**RtInt**	nu, nv;
```

This primitive is a compact way of specifying a quadrilateral mesh of patches. Each individual patch behaves as if it had been specified with **RiPatch**. *type* can be either "bilinear" or "bicubic." *parameterlist* is a list of token-array pairs where each token is one of the geometric primitive variables or a variable which has been defined with **RiDeclare**. The parameter list must include at least position ("P", "Pw" or "Pz") information. Patch mesh vertex data is supplied in first u and then v order just as for patches. The number of control points in a patch mesh is (*nu*)\*(*nv*). Meshes can wrap around in the *u* or *v* direction, or in both directions. If meshes wrap, they close upon themselves at the ends and the first control points will be automatically repeated. As many as three control points may be repeated, depending on the basis matrix of the mesh. The way in which meshes wrap is indicated by giving a wrap mode value of either "periodic" or "nonperiodic." The actual number of patches produced by this request depends on the type of the patch and the wrap modes specified. For bilinear patches, the number of patches in the u direction, *nupatches*, is given by while for bicubic patches, The same rules hold in the v direction. The total number of patches produced is equal to the product of the number of patches in each direction. If a variable other than position varies, it contains *n* values, one for each patch corner, where *n* is defined by: (with *nupatches* and *nvpatches* defined as given above). If a variable is uniform, it contains *nupatches\*nvpatches* elements of its type, one for each patch. (See Figure 5.2) A patch mesh is parameterized by a (*u,v*) which goes from 0 to 1 for the entire mesh. Texture maps that are assigned to meshes that wrap should also wrap so that filtering at the seams can be done correctly (see the section on Texture Map Utilities). If texture coordinates are inherited from the graphics state, they correspond to the corners of the mesh. Height fields can be specified by giving just a *z* coordinate at each vertex (using "Pz"); the *x* and *y* coordinates are set equal to the parametric surface parameters. Height fields cannot be periodic.

```
RIB BINDING	**PatchMesh** type nu uwrap nv vwrap parameterlist
EXAMPLE	**RtPoint** pts	**RtFloat** foos;	**RtFloat** bars;	**RiBasis**(**RiBezierBasis**, 3, **RiBezierBasis**, 3);	**RiDeclare**("foo", "uniform float");	**RiDeclare**("bar", "varying float");	**RiPatchMesh**("bicubic", 7, "nonperiodic", 4, "nonperiodic",	"P", (**RtPointer**)pts, "foo", (**RtPointer**)foos,	"bar", (**RtPointer**)bars, RI_NULL);
SEE ALSO	**[RiBasis](#RiBasis), [RiNuPatch](#RiNuPatch), [RiPatch](#RiPatch)**
```

Non-uniform B-spline patches are also supported by the RenderMan Interface. Rational quadratic B-splines provide exact representations of many different surfaces including general quadrics, tori, surfaces of revolution, tabulated cylinders, and ruled surfaces.

```c
**RiNuPatch**( nu, uorder, uknot, umin, umax,	nv, vorder, vknot, vmin, vmax, parameterlist )	**RtInt**	nu, nv;	**RtInt**	uorder, vorder;	**RtFloat**	uknot[], vknot[];	**RtFloat**	umin, umax, vmin, vmax;
```

This procedure creates a tensor product rational or polynomial non-uniform B-spline surface patch mesh. *parameterlist* is a list of token-array pairs where each token is one of the standard geometric primitive variables or a variable that has been defined with **RiDeclare**. The parameter list must include at least position ("P" or "Pw") information. The surface specified is rational if the positions of the vertices are 4-vectors (*x,y,z,w*), and polynomial if the positions are 3-vectors (*x,y,z*). The number of control points in the *u* direction equals *nu* and the number in the *v* direction equals *nv*. The total number of vertices is thus equal to (*nu*)\*(*nv*). The *order* must be positive and is equal to the degree of the polynomial basis plus 1. There may be different orders in each parametric direction. The number of control points should be at least as large as the order of the polynomial basis. If not, a spline of order equal to the number of control points is computed. The knot vectors associated with each control point (*uknot*[], *vknot*[]) must also be specified. Each value in these arrays must be greater than or equal to the previous value. The number of knots is equal to the number of control points plus the order of the spline. The surface is defined in the range *umin* to *umax* and *vmin* to *vmax*. This is different from other geometric primitives where the parameter values are always assumed to lie between 0 and 1. Each *min* must be less than its max. *min* must also be greater than or equal to the corresponding (*order*-1)th knot value. *max* must be less than or equal to the nth knot value. If texture coordinates primitive variables are not present, the *current texture coordinates* are assigned to corners defined by the rectangle (*umin*,*umax*) and (*vmin*,*vmax*) in parameter space.

```
RIB BINDING	**NuPatch** nu uorder uknot umin umax	nv vorder vknot vmin vmax parameterlist
EXAMPLE	**NuPatch** 9 3 [ 0 0 0 1 1 2 2 3 3 4 4 4 ] 0 4	2 2 [ 0 0 1 1 ] 0 1	"Pw" [ 1 0 0 1 1 1 0 1 0 2 0 2	-1 1 0 1 -1 0 0 1 -1 -1 0 1 0 -2 0 2 1 -1 0 1 1 0 0 1 1 0 -3 1 1 1 -3 1 0 2 -6 2	-1 1 -3 1 -1 0 -3 1 -1 -1 -3 1 0 -2 -6 2 1 -1 -3 1 1 0 -3 1 ]
SEE ALSO **[RiPatch](#RiPatch), [RiPatchMesh](#RiPatchMesh)**
```

NURBS may contain holes that are specified by giving a single closed curve in parameter space.

```c
**RiTrimCurve**( nloops, ncurves, order, knot, min, max, n, u, v, w )	**RtInt**	nloops	**RtInt**	ncurves[];	**RtInt**	order[];	**RtFloat**	knot[];	**RtFloat**	min[], max[];	**RtInt**	n[];	**RtFloat**	u[], v[], w[];
```

Set the *current trim curve*. The trim curve contains *nloops* loops, and each of these loops contains *ncurves* curves. The total number of curves is equal to the sum of all the values in *ncurves*. Each of the trimming curves is a non-uniform rational B-spline curve in homogeneous parameter space (*u,v,w*). The curves of a loop connect in head-to-tail fashion and must be explicitly closed. The arrays *order, knot, min, max, n, u, v, w* contain the parameters describing each trim curve. All the trim curve parameters are concatenated together into single large arrays. The meanings of these parameters are the same as the corresponding meanings for a non-uniform B-spline surface. Trim curves exclude certain areas from the non-uniform B-spline surface definition. The inside must be specified consistently using two rules: an odd winding rule that states that the inside consists of all regions for which an infinite ray from any point in the region will intersect the trim curve an odd number of times, and a curve orientation rule that states that the inside consists of the regions to the ãleftä as the curve is traced. Trim curves are typically used to specify boundary representations of solid models. Since trim curves are approximations and not exact, some artifacts may occur at the boundaries between intersecting output primitives. A more accurate method is to specify solids using spatial set operators or constructive solid geometry (CSG). This is described in the section on Solids and Spatial Set Operations. If the particular implementation does not support *Trim Curves*, all trim curves are ignored and the entire NURB surface is always rendered.

```
RIB BINDING	**TrimCurve** ncurves order knot min max n u v w
```

The number of loops is determined implicitly by the length of the *ncurves* array.

```
EXAMPLE	**RtInt** nloops = 1;	**RtInt** ncurves = { 1 };	**RtInt** order = { 3 };	**RtFloat** knot = { 0,0,0,1,1,2,2,3,3,4,4,4 };	**RtFloat** min = { 0 };	**RtFloat** max = { 4 };	**RtInt** n = { 9 };	**RtFloat** u = { 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0 };	**RtFloat** v = { 0.5, 1.0, 2.0, 1.0, 0.5, 0.0, 0.0, 0.0, 0.5 };	**RtFloat** w = { 1.0, 1.0, 2.0, 1.0, 1.0, 1.0, 2.0, 1.0, 1.0 };	**RiTrimCurve**(nloops, ncurves, order, knot, min, max, n, u, v, w);
SEE ALSO	**[RiNuPatch](#RiNuPatch), [RiSolidBegin](#RiSolidBegin)**
```

### Quadrics

Many common shapes can be modeled with quadrics. Although it is possible to convert quadrics to patches, they are defined as primitives because special-purpose rendering programs render them directly and because their surface parameters are not necessarily preserved if they are converted to patches. Quadric primitives are particularly useful in solid and molecular modeling applications. All the following quadrics are rotationally symmetric about the z axis (see Figure 5.3). In all the quadrics *u* and *v* are assumed to run from 0 to 1. These primitives all define a bounded region on a quadric surface. It is not possible to define infinite quadrics. Note that each quadric is defined relative to the origin of the object coordinate system. To position them at another point or with their symmetry axis in another direction requires the use a modeling transformation. The geometric normal to the surface points "outward" from the z-axis, if the *current orientation* matches the orientation of the *current transformation* and "inward" if they don't match. The sense of a quadric can be reversed by giving negative parameters. For example, giving a negative *thetamax* parameter in any of the following definitions will turn the quadric inside-out. Each quadric has a *parameterlist*. This is a list of token-array pairs where each token is one of the standard geometric primitive variables or a variable which has been defined with **RiDeclare**. Position variables should not be given with quadrics. All angular arguments to these functions are given in degrees. The trigonometric functions used in their definitions are assumed to also accept angles in degrees.

```c
**RiSphere**( radius, zmin, zmax, thetamax, parameterlist )	**RtFloat**	radius;	**RtFloat**	zmin, zmax;	**RtFloat**	thetamax;
```

Requests a sphere defined by the following equations: Note that if zmin > -radius or zmax < radius, the bottom or top of the sphere is open, and that if thetamax is not equal to 360 degrees, the sides are also open.

```
RIB BINDING	**Sphere** radius zmin zmax thetamax parameterlist	**Sphere** [radius zmin zmax thetamax] parameterlist
EXAMPLE	**RiSphere**(0.5, 0.0, 0.5, 360.0, RI_NULL);
SEE ALSO	**[RiTorus](#RiTorus)**
```

```c
**RiCone**( height, radius, thetamax, parameterlist )	**RtFloat**	height;	**RtFloat**	radius;	**RtFloat**	thetamax;
```

Requests a cone defined by the following equations: Note that the bottom of the cone is open, and if *thetamax* is not equal to 360 degrees, the sides are open.

```
RIB BINDING	**Cone** height radius thetamax parameterlist	**Cone** [height radius thetamax] parameterlist
EXAMPLE	**RtColor** four_colors;	**RiCone**(0.5, 0.5, 270.0, "Cs", (**RtPointer**)four_colors, RI_NULL);
SEE ALSO	**[RiCylinder](#RiCylinder), [RiDisk](#RiDisk), [RiHyperboloid](#RiHyperboloid)**
```

```c
**RiCylinder**( radius, zmin, zmax, thetamax, parameterlist )	**RtFloat**	radius;	**RtFloat**	zmin, zmax;	**RtFloat**	thetamax;
```

Requests a cylinder defined by the following equations: Note that the cylinder is open at the top and bottom, and if *thetamax* is not equal to 360 degrees, the sides also are open.

```
RIB BINDING	**Cylinder** radius zmin zmax thetamax parameterlist	**Cylinder** [radius zmin zmax thetamax] parameterlist
EXAMPLE	**Cylinder** .5 .2 1 360
SEE ALSO	**[RiCone](#RiCone), [RiHyperboloid](#RiHyperboloid)**
```

```c
**RiHyperboloid**( point1, point2, thetamax, parameterlist )	**RtPoint**	point1, point2;	**RtFloat**	thetamax;
```

Requests a hyperboloid defined by the following equations: assuming that point1 = (x1,y1, z1) and point2 = (x2, y2, z2). The cone, disk and cylinder are special cases of this surface. Note that the top and bottom of the hyperboloid are open when *point1* and *point2*, respectively, are not on the z-axis. Also, if thetamax is not equal to 360 degrees, the sides are open.

```
RIB BINDING	**Hyperboloid** x1 y1 z1 x2 y2 z2 thetamax parameterlist	**Hyperboloid** [x1 y1 z1 x2 y2 z2 thetamax] parameterlist
EXAMPLE	**Hyperboloid** 0 0 0 .5 0 0 270 "Cs" [1 1 1 .5 .9 1 .2 .9 0 .5 .2 0]
SEE ALSO **[RiCone](#RiCone), [RiCylinder](#RiCylinder), [RiDisk](#RiDisk)**
```

```c
**RiParaboloid**( rmax, zmin, zmax, thetamax, parameterlist )	**RtFloat**	rmax;	**RtFloat**	zmin, zmax;	**RtFloat**	thetamax;
```

Requests a paraboloid defined by the following equations: Note that the top of the paraboloid is open, and if *thetamax* is not equal to 360 degrees, the sides are also open.

```
RIB BINDING	**Paraboloid** rmax zmin zmax thetamax parameterlist	**Paraboloid** [rmax zmin zmax thetamax] parameterlist
EXAMPLE	**Paraboloid** .5 .2 .7 270
SEE ALSO	**[RiHyperboloid](#RiHyperboloid)**
```

```c
**RiDisk**( height, radius, thetamax, parameterlist )	**RtFloat**	height	**RtFloat**	radius;	**RtFloat**	thetamax;
```

Requests a disk defined by the following equations: Note that the surface normal of the disk points in the positive *z* direction when *thetamax* is positive.

```
RIB BINDING	**Disk** height radius thetamax parameterlist	**Disk** [height radius thetamax] parameterlist
EXAMPLE	**RiDisk**(1.0, 0.5, 270.0, RI_NULL);
SEE ALSO	**[RiCone](#RiCone), [RiHyperboloid](#RiHyperboloid)**
```

```c
**RiTorus**( majorradius, minorradius, phimin, phimax,	thetamax, parameterlist )	**RtFloat**	majorradius, minorradius;	**RtFloat**	phimin, phimax;	**RtFloat**	thetamax;
```

Requests a torus defined by the following equations: Note that if *phimax-phimin* or *thetamax* is not equal to 360 degrees, the torus is open.

```
RIB BINDING	**Torus** rmajor rminor phimin phimax thetamax parameterlist	**Torus** [rmajor rminor phimin phimax thetamax] parameterlist
EXAMPLE	**Torus** 1 .3 60 90 360
SEE ALSO	**[RiSphere](#RiSphere)**
```

### Procedural Primitives

Procedural primitives can be specified as follows:

```c
**RiProcedural**( data, bound, subdividefunc, freefunc )	**RtPointer**	data;	**RtBound**	bound;	**RtFunc**	subdividefunc;	**RtFunc**	freefunc;
```

This defines a procedural primitive. The *data* parameter is a pointer to an opaque data structure that defines the primitive. (The rendering program does not look inside *data*, it simply records it for later use by the procedural primitive.) *bound* is an array of floats that define the bounding box of the primitive in object space. *subdividefunc* is the routine that the renderer should call (when necessary) to have the primitive subdivided. A bucket-based rendering scheme can potentially save memory space by delaying this call until the bounding box overlaps a bucket that must be rendered. The calling sequence for *subdividefunc* is:

```c
(*subdividefunc)( data, detail )	**RtPointer**	data;	**RtFloat**	detail;
```

where *data* is the parameter that was supplied in defining the primitive, and *detail* is the screen area of the *bound* of the primitive. When *subdividefunc* is called, it is expected to subdivide the primitive into other smaller procedural primitives or into any number of non-procedural primitives. If the renderer can not determine the true detail of the *bound* (e.g., if the geometric primitive data is being archived to a file), *subdividefunc* may be called with a *detail* value equal to RI_INFINITY. This should be interpreted by the *subdividefunc* as a request for the immediate full generation of the procedura primitive. *freefunc* is a procedure that the rendering program calls to free the primitive when the *data* is no longer needed.. The calling sequence for *freefunc* is:

```c
(*freefunc)( data )	**RtPointer**	data;
```

Note that the rendering program may call back multiple times with the same procedural primitive, so the data area should not be overwritten or freed until the *freefunc* is called.

### Implementation-specific Geometric Primitives

Additional geometric primitives can be specified using the following procedure.

```c
**RiGeometry**( type, parameterlist )	**RtToken**	type;
```

This procedure provides a standard way of defining an implementation-specific geometric primitive. The values supplied in the parameter list for each primitive is implementation specific.

```
RIB BINDING **Geometry** name parameterlist
EXAMPLE	**RiGeometry**("teapot," RI_NULL);
```

### Solids and Spatial Set Operations

All of the previously described geometric primitives can be used to define a solid by bracketing a collection of surfaces with **RiSolidBegin** and **RiSolidEnd**. This is often referred to as the *boundary representation* of a solid. When specifying a volume it is important that boundary surfaces completely enclose the interior. Normally it will take several surfaces to completely enclose a volume since, except for the sphere, the torus, and potentially a periodic patch or patch mesh, none of the geometric primitives used by the rendering interface completely enclose a volume. A set of surfaces that are closed and non-self-intersecting unambiguously defines a volume. However, the RenderMan Interface performs no explicit checking to ensure that these conditions are met. The inside of the volume is the region or set of regions that have finite volume; the region with infinite volume is considered outside the solid. For consistency the normals of a solid should always point outwards.

```c
**RiSolidBegin**( operation )	**RtToken**	operation;
**RiSolidEnd**()
```

**RiSolidBegin** the definition of a solid. *operation* may be one of the following tokens: "primitive," "intersection," "union," "difference." Intersection and union operations form the set intersection and union of the specified solids. Difference operations require at least 2 parameter solids and subtract the last *n-1* solids from the first (where *n* is the number of parameter solids). When the innermost solid block is a "primitive" block, no other **RiSolidBegin** calls are legal. When the innermost solid block uses any other operation, no geometric primitives are legal. **RiSolidEnd** terminates the definition of the solid.

```
RIB BINDING	**SolidBegin** operation	**SolidEnd** -
EXAMPLE	**SolidBegin** "union"
SEE ALSO	**[RiInterior](section4.html#RiInterior), [RiTrimCurve](#RiTrimCurve)**
```

A single solid sphere can be created using

```c
**RiSolidBegin**( "primitive" );
**RiSphere**( 1.0, -1.0, 1.0, 360.0, RI_NULL );
**RiSolidEnd**();
```

Note that if the same sphere is defined outside of a **RiSolidBegin-RiSolidEnd** block, it is not treated as a volume-containing solid. A solid hemisphere can be created with

```c
**RiSolidBegin**( "primitive" );
**RiSphere**( 1.0, 0.0, 1.0, 360.0, RI_NULL );
**RiDisk**( 0.0, 1.0, -360.0, RI_NULL );
**RiSolidEnd**();
```

(Note that the -360 causes the surface normal of the disk to point towards negative *z*.) A composite solid is one formed using spatial set operations. The allowed set operations are "intersection," "union," and "difference." A spatial set operation has *n* operands, each of which is either a primitive solid defined using **RiSolidBegin**("primitive")-**RiSolidEnd**, or a composite solid that is the result of another set operation. For example, a closed cylinder would be subtracted from a sphere as follows:

```c
**RiSolidBegin**( "difference" );
**RiSolidBegin**( "primitive" );
**RiSphere**( 1.0, -1.0, 1.0, 360.0, RI_NULL );
**RiSolidEnd**();
**RiSolidBegin**( "primitive" );
**RiDisk**( 2.0, 0.5, 360.0, RI_NULL );
**RiCylinder**( 0.5, -2.0, 2.0, 360.0, RI_NULL );
**RiDisk**( -2.0, 0.5, -360.0, RI_NULL );
**RiSolidEnd**();
**RiSolidEnd**();
```

When performing a difference the sense of the orientation of the surfaces being subtracted is automatically reversed. Attributes may be changed freely inside solids. Each section of a solid's surface can have a different surface shader and color. For consistency a single solid should have a single interior and exterior volume shader. If the *Solid Modeling* optional capability is not supported by a particular implementation, all primitives are rendered as a collection of surfaces, and the spatial set operators are ignored.

### Retained Geometry

A single geometric primitive or a list of geometric primitives (all of the same type) may be retained by enclosing them with **RiObjectBegin** and **RiObjectEnd**. The RenderMan Interface allocates and returns an **RtObjectHandle** for each retained object defined in this way. This handle can subsequently be used to reference the object when creating *instances* with **RiObjectInstance**. Objects are not rendered when they are defined within an **RiObjectBegin-RiObjectEnd** block; only an internal definition is created. All of an object's attributes are inherited at the time it is instanced, not at the time at which it is created.

```c
**RtObjectHandle**
**RiObjectBegin**()
**RiObjectEnd**()
```

**RiObjectBegin** starts the definition of an *object* and return a handle for later use with **RiObjectInstance**. If the handle returned is NULL, an object could not be created. **RiObjectEnd** ends the definition of the current object.

```
RIB BINDING	**ObjectBegin** sequencenumber	**ObjectEnd** -
```

The *sequencenumber* is a unique object identification number which is provided by the RIB client to the RIB server. Both client and server maintain independent mappings between the *sequencenumber* and their corresponding **RtObject-Handles**. If *sequencenumber* has been used to define a previous object, that object is replaced with the new definition. The number must be in the range 0 to 65535.

```
EXAMPLE	**ObjectBegin** 2	**Sphere** 1 -1 1 360	**ObjectEnd**
SEE ALSO **[RiFrameEnd](section4.html#RiFrameEnd), [RiObjectInstance](#RiObjectInstance), [RiWorldEnd](section4.html#RiWorldEnd)**
```

```c
**RiObjectInstance**( handle );	**RtObjectHandle** handle;
```

Create an *instance* of a previously defined object. The object inherits the current set of attributes defined in the graphics state.

```
RIB BINDING	**ObjectInstance** sequencenumber
```

The object must have been defined to have a handle *sequencenumber* with a previous **RiObjectBegin**.

```
EXAMPLE **ObjectInstance** 2
SEE ALSO	**[RiFrameEnd](section4.html#RiFrameEnd), [RiObjectBegin](#RiObjectBegin), [RiWorldEnd](section4.html#RiWorldEnd)**
```
# SECTION 6 - MOTION

Some rendering programs are capable of performing temporal antialiasing and motion blur. Motion blur is specified through *moving transformations* and *moving geometric primitives*. Appearance parameters, such as color, opacity, and shader variables can also be changed during a frame. To specify objects that vary over time several copies of the same object are created, each with different parameters at different times within a frame. The times that actually contribute to the motion blur are set with the **RiShutter** command. Parameter values change linearly over the intervals between knots. There is no limit to the number of time values associated with a motion-blurred primitive, although two is usually sufficient. Rigid body motions and other transformation-based movements are model using moving coordinate systems. Moving coordinate systems are created by giving a sequence of transformations at different times and can be concatenated and nested hierarchically. All output primitives are defined in the current object coordinate system and, if that coordinate system is moving, the primitives will also be moving. The extreme case is when the camera is moving, since then all objects in the scene appear to be moving. Moving lights also are handled by placing them in a moving coordinate system. Deforming geometric primitives can also be modeled by giving their parameters at different times. Moving geometry is created by bracketing the definitions at different times between **RiMotionBegin** and **RiMotionEnd** calls.

```c
**RiMotionBegin**( n, t0, t1,..., tnminus1 ) **RtInt**	n; **RtFloat**	t0, t1,..., tnminus1;
**RiMotionEnd**()
```

**RiMotionBegin** starts the definition of a moving primitive. *n* is the number of time steps associated with this moving primitive. The times should be in increasing order. Only one type of RenderMan Interface command can be executed within this sequence and only numerical values may be interpolated. **RiMotionEnd** terminates the definition of the moving primitive.

```
RIB BINDING **MotionBegin** [ t0 t1... tn-1 ] **MotionEnd** -
SEE ALSO [**RiShutter**](section4.html#RiShutter)
```

For example, assume the following list of commands creates a static translated sphere:

```c
**RtFloat** Kd = 0.8;
**RiSurface**( "leather", "Kd", (**RtPointer**)&Kd, RI_NULL );
**RiTranslate**( 1., 2., 3. );
**RiSphere**( 1., -1., 1., 360., RI_NULL );
```

To create a moving, deforming sphere with changing surface qualities, the following might be used:

```c
**RtFloat** Kd[] = { 0.8, 0.7 };
**RiMotionBegin**( 2, 0., 1. );
    **RiSurface**( "leather", "Kd", (**RtPointer**)Kd, RI_NULL );
    **RiSurface**( "leather", "Kd", (**RtPointer**)(Kd+1), RI_NULL );
**RiMotionEnd**();
**RiMotionBegin**( 2, 0., 1. );
    **RiTranslate**( 1., 2., 3. );
    **RiTranslate**( 2., 3., 4. );
**RiMotionEnd**();
**RiMotionBegin**( 2, 0., 1. );
    **RiSphere**( 1., -1., 1., 360., RI_NULL );
    **RiSphere**( 2., -2., 2., 360., RI_NULL );
**RiMotionEnd**();
```

[Table 6.2, Moving Commands](#Table.6.2), shows which commands may be specified inside a **RiMotionBegin-RiMotionEnd** block. If the *Motion Blur* capability is not supported by a particular implementation, only the transformations, geometry and shading parameters from t0 are used to render each moving object.

**Table 6.2 Moving Commands**

| Transformations | Geometry | Shading |
| --- | --- | --- |
| [RiTransform](section4.html#RiTransform) [RiConcatTransform](section4.html#RiConcatTransform) [RiPerspective](section4.html#RiPerspective) [RiTranslate](section4.html#RiTranslate) [RiRotate](section4.html#RiRotate) [RiScale](section4.html#RiScale) [RiSkew](section4.html#RiSkew) [RiProjection](section4.html#RiProjection) [RiDeformation](section4.html#RiDeformation) | [RiBound](section4.html#RiBound) [RiDetail](section4.html#RiDetail) [RiPolygon](section5.html#RiPolygon) [RiGeneralPolygon](section5.html#RiGeneralPolygon) [RiPointsPolygons](section5.html#RiPointsPolygons) [RiPointsGeneralPolygons](section5.html#RiPointsGeneralPolygons) [RiPatch](section5.html#RiPatch) [RiPatchMesh](section5.html#RiPatchMesh) [RiNuPatch](section5.html#RiNuPatch) [RiSphere](section5.html#RiSphere) [RiCone](section5.html#RiCone) [RiCylinder](section5.html#RiCylinder) [RiHyperboloid](section5.html#RiHyperboloid) [RiParaboloid](section5.html#RiParaboloid) [RiDisk](section5.html#RiDisk) [RiTorus](section5.html#RiTorus) | [RiColor](section4.html#RiColor) [RiOpacity](section4.html#RiOpacity) [RiLightSource](section4.html#RiLightSource) [RiAreaLightSource](section4.html#RiAreaLightSource) [RiSurface](section4.html#RiSurface) [RiInterior](section4.html#RiInterior) [RiExterior](section4.html#RiExterior) [RiAtmosphere](section4.html#RiAtmosphere) [RiDisplacement](section4.html#RiDisplacement) |
# SECTION 7 - EXTERNAL RESOURCES

*   [Texture Map Utilities](#Texture.map)
*   [Errors](#Errors)
*   [Archive Files](#Archive)

## Texture Map Utilities

The format of the various texture map files is implementation dependent. However, there are standard utilities that convert image files into texture map files. During two-dimensional texture access, texture coordinates (*s, t*) are mapped onto the texture such that *s=0* maps to *xmin, s=1* maps to *xmax+1, t=0* maps to *ymin*, and *t=1* maps to *ymax+1*. To be precise, all accesses to the half-open interval [0,1) in *s* and *t* will lie within the picture data. A *wrapmode* describes how the texture is accessed if the texture coordinates are outside the unit square (less than zero, or greater than or equal to one). The *swrap* and *twrap* strings specify the wrapping behavior of the *s* and *t* coordinates. The standard wrapping behavior for *s* and *t*, "black," is to return the value zero for all accesses outside the unit square. (Thus an RGBa texture will be transparent black, zero on all four channels.) The keyword "`periodic`" indicates that values of *s* (or *t*) outside [0,1) will be mapped into [0,1) by subtracting the largest integer less than or equal to the coordinate (the ãfloorä of the coordinate). This will wrap the value 1 back to 0, the value 1.25 to 0.25, and the value -0.1 to 0.9. The result will be to repeat the texture as a tile that fills texture space in the s (or t) direction. The keyword "`clamp`" indicates that values of *s* (or *t*) outside [0,1) will be mapped into [0,1) by clamping them at their minimum and maximum values. All values below zero will be clamped to zero and all values greater than or equal to one will be clamped to a value slightly less than one (at the last texture pixel). Textures are often prefiltered so that subsequent antialiasing calculations can be done more quickly at run-time. This is controlled by giving a *filterfunc*, which is the same as the *filterfunc* used in **RiPixelFilter**, and an *swidth* and *twidth*.

### Making texture maps

Surface textures are used to modify the properties of a surface, such as color and opacity. A surface texture is accessed using the surface texture coordinates (see the section on [Texture coordinates](section4.html#Texture.coordinates)) or any other two-dimensional coordinates computed by a user-defined shader. A surface texture consists of one or more *channels*. A single channel or a group of *n* channels (usually an RGB color) can be accessed using the *texture* function of the Shading Language. The *texture* function requires the name of a *texture file* containing the texture.

```c
**RiMakeTexture**( picturename, texturename, swrap, twrap,	filterfunc, swidth, twidth, parameterlist ) char	*picturename; char	*texturename; **RtToken**	swrap, twrap; **RtFloatFunc**	filterfunc; **RtFloat**	swidth, twidth;
```

Convert an image in a standard picture file whose name is *picturename* into a texture file whose name is *texturename*. All channels of the picture file will be converted (in order) to texture *channels*. The storage format of the texture file and the precision of stored texture channels are implementation-dependent. The picture file used as input is not changed or otherwise affected **by RiMakeTexture**.

```
RIB BINDING **MakeTexture** picturename texturename swrap twrap filter swidth	twidth parameterlist
```

The *filter* parameter should be one of "box," "triangle," "catmull-rom," "b-spline," "gaussian" and "sinc." These correspond to the predefined filter functions described in **RiPixelFilter**.

```
EXAMPLE **RiMakeTexture**("globe.pic," "globe.tx," "periodic," "clamp," **RiGaussianFilter**, 2.0, 2.0, RI_NULL);
SEE ALSO **[RiTextureCoordinates](section4.html#RiTextureCoordinates)**, *[texture](section15.html#texture)()* in the Shading Language
```

### Making bump maps

Bump maps are used to perturb surface normals to simulate a bumpy surface without actually moving the points on the surface. A bump map is accessed using the surface texture coordinates (see the section on [Texture coordinates](section4.html#Texture.coordinates)) or any other two-dimensional coordinates computed by a user-defined shader. A bump map image consists of one channel of data which indicates the relative displacement of the surface. A bump map texture can be accessed using the *bump* function of the Shading Language. The *bump* function requires the name of a *texture file* containing the texture.

```c
**RiMakeBump**( picturename, texturename, swrap, twrap,	filterfunc, swidth, twidth, parameterlist ) char	*picturename; char	*texturename; **RtToken**	swrap, twrap; **RtFloatFunc**	filterfunc; **RtFloat**	swidth, twidth;
```

Convert a height field image in a standard picture file whose name is *picturename* into a bump map file whose name is *texturename*. The storage format of the texture file and the precision of stored texture channels are implementation-dependent. The picture file used as input is not changed or otherwise affected by **RiMakeBump**.

```
RIB BINDING **MakeBump** picturename texturename swrap twrap filter swidth	twidth parameterlist
```

The *filter* parameter should be one of "box," "triangle," "catmull-rom," "b-spline," "gaussian" and "sinc." These correspond to the predefined filter functions described with **RiPixelFilter**.

```
EXAMPLE **Bump** "hills.pic" "hills.tx" "periodic" "clamp" "catmull-rom" 3 3
SEE ALSO **[RiTextureCoordinates](section4.html#RiTextureCoordinates)**, *[bump](section15.html#bump)()* in the Shading Language
```

### Making environment maps

Environment maps are images representing the color of an environment in a particular direction. An environment map is accessed using a point representing direction; this direction is often the direction of a mirror reflection and hence environment maps are often referred to as reflection maps. However, any direction can be computed by a user-defined shader. An environment map image consists of one or more *channels*. A single channel or a group of *n* channels (usually an RGB color) can be accessed using the *environment* function in the Shading Language. Environment maps can be input in two formats. The first is as a single latitude-longitude image. Environment maps in this form are fairly easy to create using a paint system. The second format is a set of six cube face projections. Environment maps in this form are naturally created by the rendering program.

```c
**RiMakeLatLongEnvironment**( picturename, texturename,	filterfunc, swidth, twidth, parameterlist ); char	*picturename: char	*texturename: **RtFloatFunc**	filterfunc; **RtFloat**	swidth, twidth
```

Convert an image in a standard picture file representing a latitude-longitude map whose name is *picturename* into an environment map whose name is *texturename*. The storage format of the texture file and the precision of stored texture channels are implementation-dependent. This image has longitude equal to 0 degrees at the left, and 360 degrees at the right. The latitude at the bottom is -90 degrees and at the top is 90 degrees. The bottom of the picture is at the south pole and the top the north pole. The direction in space corresponding to each of the points on the image is given by:

Notice that latitude-longitude environment maps are sensitive to the handedness of the coordinate system in which they will be accessed. Environment maps which are intended to be accessed in a right-handed coordinate system will, if displayed, appear as a mirror image of those intended to be accessed in a left-handed coordinate system.

```
RIB BINDING **MakeLatLongEnvironment** picturename texturename filter swidth	twidth parameterlist
```

The *filter* parameter should be one of "box," "triangle," "catmull-rom," "b-spline," "gaussian" and "sinc." These correspond to the predefined filter functions described with **RiPixelFilter**.

```
EXAMPLE **MakeLatLongEnvironment** "long.pic" "long.tx""catmull-rom" 3 3
SEE ALSO **[RiMakeCubeFaceEnvironment](#RiMakeCubeFaceEnvironment)**, *[environment](section15.html#environment)()* in the Shading Language
```

```c
**RiMakeCubeFaceEnvironment**( px, nx, py, ny, pz, nz, texturename, fov,	filterfunc, swidth, twidth, parameterlist ); char	*px, *nx, *py, *ny, *pz, *nz; char	*texturefile; **RtFloat**	fov; **RtFloatFunc**	filterfunc; **RtFloat**	swidth, twidth;
```

Convert six images in standard picture files representing six viewing directions into an environment map whose name is *texturename*. The image *pz* (*nz*) is the image as viewed in the positive (negative) *z* direction. The remaining images are those viewed along the positive and negative *x* and *y* directions. The storage format of the texture file and the precision of stored texture channels are implementation-dependent. Each image is normally produced by a rendering program by placing the eye at the center of the environment (usually the origin) and generating a picture in each of the six directions. These pictures are the projection of the environment onto a set of cube faces. Each face is usually assumed to be unit distance from the eye point. Cube face environment maps should be generated with the following orientations:

| Image | Forward Axis | Up Axis | Right Axis |
|---|---|---|---|
| px | +X | +Y | -Z |
| nx | -X | +Y | +Z |
| py | +Y | -Z | +X |
| ny | -Y | +Z | +X |
| pz | +Z | +Y | +X |
| nz | -Z | +Y | -X |

Notice that cube face environment maps are sensitive to the handedness of the coordinate system in which they will be accessed. Environment maps which are intended to be accessed in a right-handed coordinate system will, if displayed, appear as a mirror image of those intended to be accessed in a left-handed coordinate system. The *fov* is the full horizontal field of view used to generate these images. A value of 90 degrees will cause the cube face edges to meet exactly. Using a slightly larger value will cause the cube faces to intersect. Having a slight overlap helps remove artifacts along the seams where the different pictures are joined.

```
RIB BINDING **MakeCubeFaceEnvironment** px nx py ny pz nz texturename fov filter swidth twidth parameterlist
```

The *filter* parameter should be one of "box," "triangle," "catmull-rom," "b-spline," "gaussian" and "sinc." These correspond to the predefined filter functions described with **RiPixelFilter**.

```
EXAMPLE **RiMakeCubeFaceEnvironment**("foo.x," "foo.nx," "foo.y," "foo.ny,"	"foo.z," "foo.nz," "foo.env," 95.0, **RiTriangleFilter**,	2.0, 2.0, RI_NULL);
SEE ALSO **[RiMakeLatLongEnvironment](#RiMakeLatLongEnvironment)**, *[environment](section15.html#environment)()* in the Shading Language
```

### Making shadow maps

Shadow maps are depth buffer images from a particular view. They are generally used in light source shaders to cast shadows onto objects. A shadow map is accessed by point in the camera coordinate system corresponding to that view. This point must be computed in the shader. A shadow map texture can be accessed using the *shadow* function of the Shading Language. The *shadow* function requires the name of a *texture file* containing the texture.

```c
**RiMakeShadow**( picturename, texturename, parameterlist ) char	*picturename; char	*texturename;
```

Create a depth image file named *picturename* into a shadow map whose name is *texturename*. The storage format of the shadow map texture file and the precision of stored texture channels are implementation-dependent.

```
RIB BINDING **MakeShadow** picturename texturename parameterlist
EXAMPLE **MakeShadow** "shadow.pic" "shadow.tex"
SEE ALSO *[shadow](section15.html#shadow)()* in the Shading Language
```

## Errors

RenderMan Interface procedures do not return error status codes. Instead, the user may specify an error handling routine that will be called whenever an error is encountered.

```c
**RiErrorHandler**( handler ) **RtFunc**	handler;
```

This procedure sets the error handling procedure invoked by the renderer when an error is detected. Error handling procedures have the following form:

```c
**RtVoid** handler( code, severity, message ) **RtInt**	code, severity; char	*message;
```

*code* indicates the type of error, and *severity* indicates how serious the error is. Values for *code* and severity are defined in *<ri.h>*. The *message* is a character string containing an error message formatted by the renderer which can be printed or displayed, as the handler desires. The following standard error handlers are defined:

```c
**RtVoid RiErrorIgnore**;
**RtVoid RiErrorPrint**;
**RtVoid RiErrorAbort**;
**RtInt RiLastError**;
```

If **RiErrorIgnore** is specified, all errors are ignored and no diagnostic messages are generated. If **RiErrorPrint** is specified, a diagnostic message is generated for each error. The rendering system will attempt to ignore the erroneous information and continue rendering. If **RiErrorAbort** is specified, the first error will cause a diagnostic message to be generated and the rendering system will immediately terminate. Each of the standard error handlers saves the last error code in the global variable **RiLastError**. This procedure can be called outside an **RiBegin-RiEnd** block.

```
RIB BINDING **ErrorHandler** "ignore" **ErrorHandler** "print" **ErrorHandler** "abort"
```

If "`ignore`," "`print`" or "`abort`" is specified, the equivalent predefined error handling procedure will be invoked in the RIB server. Notice that the RIB parser process may detect RIB stream syntax errors which make it impossible to correctly parse a request. In this case, the error procedure will be invoked and the parser will do its best to resynchronize the input stream by scanning for the next recognizable token.

```
EXAMPLE **ErrorHandler** "ignore"
```

## Archive Files

One important use of the RIB protocol is to store a scene description in an archive file for rendering at a later time or in a remote location from the modeling application. [Appendix D, RenderMan Interface Bytestream Conventions](appendix.D.html), outlines a structuring conventions to make these archives as portable and useful as possible.

```c
**RiArchiveRecord**( type, format [, arg ...] ) **RtToken**	type char	*format;
```

This call writes a user data record (data which is outside the scope of the requests described in the rest of Part I of this document) into a RIB archive file or stream. *type* is either "`comment`" or "`structure`". "`comment`" begins the user data record with a RIB comment marker and terminates it with a newline. "`structure`" begins the user data record with a RIB structuring convention preface and terminates it with a newline. The user data record itself is supplied as a `printf()` format string with optional arguments. It is an error to embed newline characters in the format or any of its string arguments.
