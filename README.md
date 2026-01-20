# Rhayes: REYES-style Software Renderer in C

![Image created by work in progress, January 14, 2026](images/20260114.png)

## What is this all about?

I'm Mark VandeWettering, a former Pixar Software Engineer and Technical director.  Back in 1991, I was originally
hired at Pixar to work on Pixar's Photorealistic RenderMan project, and spent a decade as 
part of the RenderMan team, extending and supporting that product for use in our studio and to sell to 
other studios. 

Now, some 35 years later, I find myself in retirement, and with the entire universe talking about AI based coding
and how it is replacing software engineers.  I've done some minor experiments with having AI agents (mostly 
Google's Gemini, but more recently Claude AI) as coding agents.  I became interested in trying to understand 
whether agents could create something as complex as a renderer without any user coding.

This is my (IP) based attempt.

A few caveats:

- There is no code in renderer which is copied from any source which is not public.  The RenderMan Interface was publicly
published precisely so people could write compliant tools and implementation.   The header files used are derived from
that document.  I have no access to any proprietary code from Pixar Animation Studios, and this code uses no such code.
- The algorithms used are documented in [the 1987 Siggraph paper by Cook, Carpenter and Catmull](https://graphics.pixar.com/library/Reyes/), and form the basis of the AI implementation (and largely my own, and probably everyone's) 
understanding of the Reyes algorithm.
- There was a patent granted to Pixar for the use of jittered sampling ([U.S. Patent 4,897,806 ("Pseudo-random point sampling techniques in computer graphics")](https://patents.google.com/patent/US4897806A/en)).  This patent expired in
2007, and this renderer now implements jittered sampling for improved anti-aliasing (enabled by default, controllable via `RiHider`).
- While I am striving for compatibility with this (rather antique and old) version of the specification 
(judging whether an AI can succeed in implementing to a rather complex specification is really the point of this 
project) no guarantees, warranties or the like as to its compatibility or use for any particular purpose is implied
or given.

## AI Assistants

As I mentioned before: this code is was written largely by [Claude Code](https://code.claude.com/docs/en/overview)
with some minor bits perhaps written by [Gemini-CLI](https://geminicli.com/docs/).  I am currently using the $20
a month "Pro" option of Claude Code, and the $20 per month option of Gemini CLI as well.  My early experience
is that Claude is a much better and much less frustrating experience, but the rather spartan limits of the $20 
per month plan for Claude mean that I'm only getting one or two hours per day where I can use it, often with enforced
rate limits kicking in after just thirty minutes at the keyboard.   Nevertheless, it is clear to me that Claude 
does a much better job of creating code, and has a much decreased tendency to get lost in loops or stray off into
irrelevant fixes which have as great a likelihood to break as fix the code.

I have written other interesting bits of code using Gemini 
(see the projects listed/hosted [on mvandewettering](https://mvandewettering.com/pages/) 
for some publicly shared examples, mostly in the realm of single page HTML applications.

## AI is evil!  You're doing evil!

Probably.  I've no doubt that the appearance of AI in the world has been and will continue to be enormously disruptive
to the lives of software engineers and those in the film and media fields.  I can say this with some certainty 
based upon my personal experience in those industries, and my premature departure into retirement somewhat 
earlier than I planned.  Part of this experiment was to determine the degree to which my experience in guiding 
Claude is important, even though I am largely freed from the mechanics of coding individual lines of C, and whether
it represents a true boost in my productivity, and whether code written by such a process can be considered valuable.

I've some preliminary conclusions (now just a few days into the project) but wish to continue a bit longer before
I make them public.

There is a considerable ethical question of course which goes beyond the practical questions.  Coding AIs are only
enabled because they loot the considerable corpus of work that is available on the Internet, a lot of which is 
covered by licenses which may prohibit this kind of reuse as training data (either implicitly or explicitly).  
The degree to which this code may be considered an illegal derivative of some other (unknown to me) work is a 
question that has not yet been resolved legally, and which people can reasonably argue about ethically.  I'm 
happy to engage in such conversations with thoughtful individuals.  Reach out to [me via email](mailto:mvandewettering.com) if you have something you'd like me to consider in this topic.  Respectful conversations appreciated.


## The name "Rhayes"

It's a bit of a joke.  REYES stands for "Render Everything You Ever Saw".  In that spirit, "RHAYES" stands for
"Renders Hardly Anything You've Ever Seen."

I've wanted to make a renderer called rhayes for quite some time.

## Overview
Rhayes is a fast, portable software renderer based on the REYES (Render Everything You Ever Saw) architecture. It is written in strict C99 with no external dependencies beyond the C standard library, ensuring maximum portability.

The renderer implements a classic REYES pipeline: recursive splitting of primitives, dicing into micropolygon grids, shading at vertices, and stochastic sampling for visibility and anti-aliasing.

## Features
- **Pure C99**: Minimal external dependencies (lodepng for PNG I/O).
- **REYES Pipeline**:
    - Recursive splitting and dicing of primitives.
    - Shading in object/eye space before visibility testing.
    - Z-buffer based rasterization with sub-pixel sampling.
- **RenderMan-like API**: Implementation of a subset of the Ri specification.
- **Geometric Primitives**:
    - Quadrics: Sphere, Cylinder, Cone, Disk, Torus, Paraboloid, Hyperboloid.
    - Bicubic Patches and custom "teapot" geometry.
    - Polygons and Polyline support.
- **Basis Matrices**: Support for `RiBasis` (Bezier, B-Spline, Catmull-Rom, etc.).
- **Shading & Lighting**:
    - Surface shaders: `matte`, `plastic`, `metal`, `paintedplastic` (textured), and diagnostic shaders (`random`, `randomgrid`).
    - Light sources: Point, Distant, and Ambient lights.
- **Motion Blur**:
    - Temporal sampling with configurable shutter interval (`RiShutter`).
    - Transform motion blur via `MotionBegin`/`MotionEnd` blocks.
    - Correct shutter-to-motion time remapping for partial motion ranges.
    - Stratified temporal sampling to reduce noise.
- **Jittered Sampling**:
    - Spatial jitter for improved anti-aliasing at edges.
    - Temporal jitter for smoother motion blur.
    - Configurable via `RiHider` API (`Hider "hidden" "jitter" [0/1]`).
    - Enabled by default; deterministic per-pixel hash for reproducible renders.
- **Primitive Variables (Primvars)**:
    - User-defined variables attached to primitives (`RiDeclare`).
    - Support for all storage classes: constant, uniform, varying, vertex.
    - Automatic interpolation during dicing and shading.
- **Texture Mapping**:
    - PNG texture loading with automatic mipmap generation.
    - Box-filter decimation for mipmap pyramid.
    - Bilinear texture sampling with automatic mip level selection.
    - Screen-space texture derivatives for proper filtering at all orientations.
    - Correct handling of parametric singularities (e.g., sphere poles).
    - Support for grayscale, RGB, and RGBA textures.
- **RIB Support**:
    - RIB parser for scene description with motion blur support.
    - RIB output for scene serialization (`scene2rib` utility).
    - Round-trip parsing and serialization (`catrib` utility).
- **Output**: PNG image format with alpha channel and bKGD chunk support.

## Building
Rhayes uses a standard Makefile.

```sh
# Build everything (demo executable and all utilities)
make

# Clean build artifacts
make clean

# Run the test suite
make test
```

This builds:
- `rhayes` - Demo executable with built-in test scene
- `bin/render` - RIB file renderer
- `bin/catrib` - RIB parser/serializer utility
- `bin/scene2rib` - C API to RIB converter

## Usage

### Built-in Demo
Run the default demo which renders a teapot on a checkerboard floor:
```sh
./rhayes
```
This generates `output.png`.

### RIB Renderer
Render any RIB (RenderMan Interface Bytestream) file:
```sh
./bin/render scene.rib
```

### RIB Utilities

**catrib** - Parse and re-serialize RIB files, useful for debugging or pretty-printing:
```sh
./bin/catrib input.rib -o output.rib
```

**scene2rib** - Convert C API scene descriptions to RIB format:
```sh
./bin/scene2rib > scene.rib
```

## Testing
The project includes a comprehensive test suite that verifies parsing, rendering (against reference images), and round-trip serialization.

```sh
make test
```

## Project Structure
- `src/`: Core implementation of the REYES pipeline and utilities.
- `include/`: API headers and internal configuration.
- `tests/`: RIB test files, reference images, and test runner.
- `textures/`: Sample textures for testing.
- `bin/`: Compiled executables.
- `lib/`: Compiled static libraries.
- `obj/`: Object files.
- `FSD.md`: Functional System Description.
- `RISPEC.md` / `PARTI.md`: RenderMan Interface Specification documentation.
- `rispec_variables.md`: Reference for RiDeclare variable types and interpolation.
- `PLAN.md`: Project development plan.
- `CLAUDE.md`: AI assistant guidance for this codebase.

## Work in Progress

Current development focus:
- **Additional Shaders**: Extend texture support to `shinymetal` and other shaders.
- **Trilinear Filtering**: Add interpolation between mip levels for smoother LOD transitions.
- **Displacement Mapping**: Support for procedural and texture-based surface displacement.
- **Deformation Motion Blur**: Extend motion blur to support deforming geometry (not just transforms).
- **Depth of Field**: Implement lens sampling for depth of field effects.
- **Transparency & Compositing**: Alpha-based visibility with proper depth sorting.

## License
MIT License.
