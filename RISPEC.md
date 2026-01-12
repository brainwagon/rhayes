# RenderMan Interface Specification (Version 3.1)

**Note:** This document contains a partial conversion of Part I of the RenderMan Interface Specification. Due to tool limitations, Sections 4 through 7 are not included.

---

# Part I: The RenderMan Interface

## Section 1: INTRODUCTION

The RenderMan Interface is a standard interface between modeling programs and rendering programs capable of producing photorealistic quality images. A rendering program implementing the RenderMan Interface differs from an implementation of earlier graphics standards in that:

*   A photorealistic rendering program must simulate a real camera and its many attributes besides just position and direction of view. High quality implies that the simulation does not introduce artifacts from the computational process. Expressed in the terminology of computer graphics, this means that a photorealistic rendering program must be capable of:
    *   hidden surface removal so that only visible objects appear in the computed image,
    *   spatial filtering so that aliasing artifacts are not present,
    *   dithering so that quantization artifacts are not noticeable,
    *   temporal filtering so that the opening and closing of the shutter causes moving objects to be blurred,
    *   and depth of field so that only objects at the current focal distance are sharply in focus.

*   A photorealistic rendering program must also accept curved geometric primitives so that not only can geometry be accurately displayed, but also so that the basic shapes are rich enough to include the diversity of man-made and natural objects. This requires patches, quadrics, and representations of solids, as well as the ability to deal with complicated scenes containing on the order of 10,000 to 1,000,000 geometric primitives.

*   A photorealistic rendering program must be capable of simulating the optical properties of different materials and light sources. This includes surface shading models that describe how light interacts with a surface made of a given material, volume shading models that describe how light is scattered as it traverses a region in space, and light source models that describe the color and intensity of light emitted in different directions. Achieving greater realism often requires that the surface properties of an object vary. These properties are often controlled by texture mapping an image onto a surface. Texture maps are used in many different ways: direct image mapping to change the surface's color, transparency mapping, bump mapping for changing its normal vector, displacement mapping for modifying position, environment or reflection mapping for efficiently calculating global illumination, and shadow maps for simulating the presence of shadows.

The RenderMan Interface is designed so that the information needed to specify a photorealistic image can be passed to different rendering programs compactly and efficiently. The interface itself is designed to drive different hardware devices, software implementations and rendering algorithms. Many types of rendering systems are accommodated by this interface, including z-buffer-based, scanline-based, ray tracing, terrain rendering, molecule or sphere rendering and the Reyes rendering architecture.

In order to achieve this, the interface does not specify how a picture is rendered, but instead specifies what picture is desired. The interface is designed to be used by both batch-oriented and real-time interactive rendering systems. Real-time rendering is accommodated by ensuring that all the information needed to draw a particular geometric primitive is available when the primitive is defined. Both batch and real-time rendering is accommodated by making limited use of inquiry functions and call-backs.

The RenderMan Interface is meant to be complete, but minimal, in its transfer of scene descriptions from modeling programs to rendering programs. The interface usually provides only a single way to communicate a parameter; it is expected that the modeling front end will provide other convenient variations. An example is color coordinate systems — the RenderMan Interface supports multiple-component color models because a rendering program intrinsically computes with an n-component color model. However, the RenderMan Interface does not support all color coordinate systems because there are so many and because they must normally be immediately converted to the color representation used by the rendering program. Another example is geometric primitives — the primitives defined by the RenderMan Interface are considered to be rendering primitives, not modeling primitives. The primitives were chosen either because special graphics algorithms or hardware is available to draw those primitives, or because they allow for a compact representation of a large database. The task of converting higher-level modeling primitives to rendering primitives must be done by the modeling program.

The RenderMan Interface is not designed to be a complete three-dimensional interactive programming environment. Such an environment would include many capabilities not addressed in this interface. These include: 1) screen space or two-dimensional primitives such as annotation text, markers, and 2-D lines and curves, 2) non-surface primitives such as 3-D lines and curves, and 3) user-interface issues such as window systems, input devices, events, selecting, highlighting, and incremental redisplay.

The RenderMan Interface is a collection of procedures to transfer the description of a scene to the rendering program. These procedures are described in Part I. A rendering program takes this input and produces an image. This image can be immediately displayed on a given display device or saved in an image file. The output image may contain color as well as coverage and depth information for postprocessing. Image files are also used to input texture maps. This document does not specify a ''standard format'' for image files.

The RenderMan Shading Language is a programming language for extending the predefined functionality of the RenderMan Interface. New materials and light sources can be created using this language. This language is also used to specify deformations, special camera projections, and simple image processing functions. All required shading functionality is also expressed in this language. A shading language is an essential part of a high-quality rendering program. No single material lighting equation can ever hope to model the complexity of all possible material models. The RenderMan Shading Language is described in Part II of this document.

### Features and Capabilities

The RenderMan Interface was designed in a top-down fashion by asking what information is needed to specify a scene in enough detail so that a photorealistic image can be created. Photorealistic image synthesis is quite challenging and many rendering programs cannot implement all of the features provided by the RenderMan Interface. This section describes which features are required and which are considered optional capabilities. The set of required features is extensive in order that application writers and end-users may reasonably expect basic compatibility between, and a high level of performance from, all implementations of the RenderMan Interface. Capabilities are optional only in situations where it is reasonable to expect that some rendering programs are algorithmically incapable of supporting that capability, or where the capability is so advanced that it is reasonable to expect that most rendering implementations will not be able to provide it.

#### Required features

All rendering programs which implement the RenderMan Interface must implement the interface as specified in this document. Implementations which are provided as a linkable C library must provide entry points for all of the subroutines and functions, accepting the parameters as described in this specification. All of the predefined types, variables and constants (including the entire set of constant `RtToken` variables for the predefined string arguments to the various RenderMan Interface subroutines) must be provided. The C header file `ri.h` (see Appendix C, Language Binding Details) describes these data items.

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

Rendering programs which implement the RenderMan Interface receive all of their data through the interface. There will be no additional subroutines required to control or provide data to the rendering program. Data items which are substantially similar to items already described in this specification will be supplied through the normal mechanisms, and not through any of the implementation-specific extension mechanisms (`RiAttribute`, `RiGeometry` or `RiOption`). Rendering programs will not provide non-standard alternatives to the existing mechanisms, such as any alternate language for programmable shading.

#### Optional capabilities

Rendering programs may also provide one or more of the following optional capabilities. If a capability is not provided by an implementation, a specific default is required (as described in the individual sections). A subset of the full functionality of a capability may be provided by a rendering program. For example, a rendering program might implement Motion Blur, but only of simple transformations, or only using a limited range of shutter times. Rendering programs should describe their implementation of the following optional capabilities using the terminology in the following list.

*   **Solid Modeling.** The ability to define solid models as collections of surfaces and combine them using the set operations intersection, union and difference. (See the section on Solids and Spatial Set Operations, p. 80.)
*   **Trim Curves.** The ability to specify a subset of a parametric surface by giving a region in parameter space. (See the section on Patches, p. 65.)
*   **Level of Detail.** The ability to specify several definitions of the same model and have one selected based on the estimated screen size of the model. (See the section on Detail, p. 48.)
*   **Motion Blur.** The ability to process moving primitives and antialias them in time. (See Section 6, Motion, p. 83.)
*   **Depth of Field.** The ability to simulate focusing at different depths. (See the section on Camera, p. 18.)
*   **Programmable Shading.** The ability to perform shading calculations using user-supplied RenderMan Shading Language programs. (See Part II, The RenderMan Shading Language, p. 95.)
*   **Special Camera Projections.** The ability to perform nonstandard camera projections such as spherical or Omnimax projections. (See the section on Camera, p. 18.)
*   **Deformations.** The ability to handle nonlinear transformations such as bends and twists. (See the section on Transformations, p. 52.)
*   **Displacements.** The ability to handle displacements. (See the section on Transformations, p. 52.)
*   **Spectral Colors.** The ability to calculate colors with an arbitrary number of spectral color samples. (See the section on Additional options, p. 33.)
*   **Texture Mapping.** The ability to index a texture map with the surface's texture coordinates. (See the section on Basic texture maps, p. 129.)
*   **Environment Mapping.** The ability to model the environmental illumination by indexing a texture map with a direction vector. (See the section on Environment maps, p. 130.)
*   **Bump Mapping.** The ability to perturb just surface normals by giving a displacement map. (See the section on Bump maps , p. 130.)
*   **Shadow Depth Mapping.** The ability to index a shadow map with a position. (See the section on Shadow depth maps , p. 131.)
*   **Volume Shading.** The ability to attach and evaluate volumetric shading procedures. (See the section on Volume shading , p. 43.)
*   **Ray Tracing.** The ability to evaluate global illumination models using ray tracing. (See the section on Shading and Lighting Functions , p. 126.)
*   **Radiosity.** The ability to evaluate global illumination models using radiosity. (See the section on Illuminance and Illuminate Statements , p. 116.)
*   **Area Light Sources.** The ability to illuminate surfaces with area light sources. (See the section on Light sources , p. 40.)

In order to accommodate modeling application writers who require more compatibility between rendering programs, Programmable Shading and Texture Mapping will become required features of photorealistic rendering programs in the RenderMan Interface Specification Version 4.0.

### Structure of this Document

Part I of this document describes the scene description interface. Section 2 describes the language binding and conventions used in this document. Section 3 provides a brief introduction to the RenderMan Shading Language and its relationship to the RenderMan Interface. Section 4 describes the graphics state maintained by the interface. The state is divided into options which control the overall rendering process, and attributes which describe the properties of individual geometric primitives. Rendering options include camera and display options as well as the type of hidden surface algorithm being used. Rendering attributes include shading (light sources, surface shading functions, colors, etc.) and geometric attributes including transformations. Section 5 describes the basic geometric surfaces and solid modeling representations used by the RenderMan Interface. Section 6 describes the specification of moving geometry and time-varying shading parameters. Finally, Section 7 describes the process of generating texture maps from standard image files, reporting errors, and manipulating archive files.

## Section 2: LANGUAGE BINDING SUMMARY

In this document, the RenderMan Interface is described in the C language, as originally specified by Kernighan and Ritchie. Other language bindings will be proposed in the future.

### C Binding

All types, procedures, tokens, predefined variables and utility procedures mentioned in this document are required to be present in all C implementations that conform to this specification. The C header file which declares all of these required names, `ri.h`, is listed in Appendix C, Language Binding Details.

The RenderMan Interface requires the following types:

```c
typedef short RtBoolean;
typedef long RtInt;
typedef float RtFloat;
typedef char *RtToken;
typedef RtFloat RtColor;
typedef RtFloat RtPoint;
typedef RtFloat RtMatrix[4][4];
typedef RtFloat RtBasis[4][4];
typedef RtFloat RtBound[6];
typedef char *RtString;
typedef void *RtPointer;
typedef void RtVoid;
typedef RtFloat (*RtFloatFunc)();
typedef RtVoid (*RtFunc)();
typedef RtPointer RtObjectHandle;
typedef RtPointer RtLightHandle;
```

All procedures and values defined in the interface are prefixed with `Ri` (for RenderMan Interface). All types are prefixed with `Rt` (for RenderMan type). Boolean values are either `RI_FALSE` or `RI_TRUE`. Special floating point values `RI_INFINITY` and `RI_EPSILON` are defined. The expression `–RI_INFINITY` has the obvious meaning. The number of components in a color is initially three, but can be changed (See the section Additional options, p. 33). A bound is a bounding box and is specified by 6 floating point values in the order `xmin`, `xmax`, `ymin`, `ymax`, `zmin`, `zmax`. A matrix is an array of 16 numbers describing a 4 by 4 transformation matrix. All multidimensional arrays are specified in row-major order. For example, a 4 by 4 translation matrix to the location (2,3,4) is specified with:

```c
{{ 1.0, 0.0, 0.0, 0.0},
 { 0.0, 1.0, 0.0, 0.0},
 { 0.0, 0.0, 1.0, 0.0},
 { 2.0, 3.0, 4.0, 1.0} }
```

Tokens are strings that have a special meaning to procedures implementing the interface. These meanings are described with each procedure. The capabilities of the RenderMan Interface can be extended by defining new tokens and passing them to various procedures. The most important of these are the tokens identifying variables defined by procedures called shaders, written in the Shading Language. Variables passed through the RenderMan Interface are bound by name to shader variables. To make the standard predeclared tokens and user-defined tokens similar, RenderMan Interface tokens are represented by strings. Associated with each of the standard predefined tokens, however, is a predefined string constant that the RenderMan Interface procedures can use for efficient parsing. The names of these string constants are derived from the token names used in this document by prepending an `RI_` to a capitalized version of the string. For example, the predefined constant token for "rgb" is `RI_RGB`. The special predefined token `RI_NULL` is used to specify a null token.

In the C binding presented in this document, parameters are passed by value or by reference. C implementations of the RenderMan Interface are expected to make copies of any parameters whose values are to be retained across procedure invocations.

Many procedures in the RenderMan Interface have variable length parameter lists. These are indicated by the syntactical construct `parameterlist` in the procedure's argument list. In the C binding described, `parameterlist` is a sequence of pairs of arguments, the first being an `RtToken` and the second being an `RtPointer`, an untyped pointer to an array of either `RtFloat`, `RtString` or other values. The list is terminated by the special token `RI_NULL`.

In addition, each such procedure has an alternate vector interface, which passes the `parameterlist` as three arguments: an `RtInt` indicating the length of the parameter list; an array of that length that contains the `RtTokens`; and another array of the same length that contains the `RtPointers`. This alternate procedure is denoted by appending an uppercase `V` to the procedure name.

For example the procedure `RiFoo` declared as:

```c
RiFoo( parameterlist )
```

could be called in the following ways:

```c
RtColor colors;
RtPoint points;
RtFloat one_float;
RtToken tokens;
RtPointer values;

RiFoo( RI_NULL );

RiFoo( (RtToken)"P", (RtPointer)points, (RtToken)"Cs", (RtPointer)colors,
       (RtToken)"Kd", (RtPointer)&one_float, RI_NULL );

RiFoo( RI_P, (RtPointer)points, RI_CS, (RtPointer)colors,
       RI_KD, (RtPointer)&one_float, RI_NULL );

tokens = RI_P; values = (RtPointer)points;
tokens = RI_CS; values = (RtPointer)colors;
tokens = RI_KD; values = (RtPointer)&one_float;
RiFooV( 3, tokens, values);
```

It is not the intent of this document to propose that other language bindings use an identical mechanism for passing parameter lists. For example, a Fortran or Pascal binding might pass parameters using four arguments: an integer indicating the length of the parameter list, an array of that length that contains the tokens, an array of the same length containing integer indices into the final array containing the real values. A Common Lisp binding would be particularly simple because it has intrinsic support for variable length argument lists.

The ANSI Standard C binding of RenderMan Interface is different from the K&R C binding presented in the document only in the normally expected ways. The semantics of the types, procedures and predefined variables are identical, and the necessary function prototype modifications are presented in a version of `ri.h` also listed in Appendix C, Language Binding Details.

### Bytestream Protocol

This document also describes a byte stream representation of the RenderMan Interface, known as the RenderMan Interface Bytestream, or RIB. This byte stream serves as both a network transport protocol for modeling system clients to communicate requests to a remote rendering service, and an archive file format to save requests for later submission to a renderer.

The RIB protocol provides both an ASCII and binary encoding of each request, in order to satisfy needs for both an understandable (potentially) interactive interface to a rendering server and a compact encoded format which minimizes transmission time and file storage costs. Some requests have multiple versions, for efficiency or to denote special cases of the request.

The semantics of each RIB request are identical to the corresponding C entry point, except as specifically noted in the text. In Part I of this document, each RIB request is presented in its ASCII encoding, using the following format:

**RIB BINDING**
```
Request parameter1 parameter2... parameterN
```

Explanation of the special semantics of the RIB protocol for this request.

At the top of the description, `parameter1` through `parameterN` are the parameters that the request requires. The notation `–` in the parameter position indicates that the request expects no parameters. Normally the parameter names suggest their purpose, e.g., x, y, or angle.

In RIB, square brackets (`[` and `]`) delimit arrays. Integers will be automatically promoted if supplied for parameters which require floating point values. A parameter list is simply a sequence of string-array pairs. There is no explicit termination symbol as in the C binding. Example parameter lists are:

```
"P" [0 1 2 3 4 5 6 7 8 9 10 11]
"distance" [.5] "roughness" [1.2]
```

The details of the lexical syntax of both the ASCII and binary encodings of the RIB protocol are presented in Appendix C, Language Binding Details.

### Additional Information

Finally, the description of each RenderMan Interface request provides an example and cross-reference in the following format:

**EXAMPLE**

```c
Request 7 22.9
```

**SEE ALSO**

`RiOtherRequest`

Some examples are presented in C, others in RIB, and a few are presented in both bindings (for comparison). It should be obvious from the syntax which binding is which.

## Section 3: RELATIONSHIP TO THE RenderMan SHADING LANGUAGE

The capabilities of the RenderMan Interface can be extended by using the Shading Language. The Shading Language is described in Part II of this document. This section describes the interaction between the RenderMan Interface and the Shading Language.

Special procedures, called shaders, are declared in this language. The argument list of a shader declares variables that can be passed through the RenderMan Interface to a shader. For example, in the shading language a shader called `weird` might be declared as follows:

```c
surface weird( float f = 1.0; point p = (0,0,0) )
{
    Cs = Ci * mod( length(P-p)*f - s + t, 1.0 );
}
```

The shader `weird` is referred to by name and so are its variables.

```c
RtFloat foo;
RtPoint bar;
RiSurface( "weird", "f", (RtPointer)&foo, "p", (RtPointer)bar, RI_NULL );
```

passes the value of `foo` to the Shading Language variable `f` and the value `bar` to the variable `p`. Note that since all parameters are passed as arrays, the single float must be passed by reference.

In order to pass shading language variables, the RenderMan Interface must know the type of each variable defined in a shader. All predefined shaders predeclare the types...

**(Text truncated due to source availability issues)**
