# Rhayes: REYES-style Software Renderer in C

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

- There is no code in renderer which derives from any source which is not public.  The RenderMan Interface was publicly
published precisely so people could write compliant tools and implementation.   The header files used are derived from
that document.  I have no access to any proprietary code from Pixar Animation Studios, and this code uses no such code.
- The algorithms used are documented in [the 1987 Siggraph paper by Cook, Carpenter and Catmull](https://graphics.pixar.com/library/Reyes/), and form the basis of the AI implementation (and largely my own, and probably everyone's) 
understanding of the Reyes algorithm.
- There was a patent granted to Pixar for the use of jittered sampling ([U.S. Patent 4,897,806 ("Pseudo-random point sampling techniques in computer graphics")](https://patents.google.com/patent/US4897806A/en)).  This patent expired in 
2007, and so a future version of this code is likely to implement such techniques to improve anti-aliasing.
- While I am striving for compatibility with this (rather antique and old) version of the specification 
(judging whether an AI can succeed in implementing to a rather complex specification is really the point of this 
project) no guarantees, warranties or the like as to its compatibility or use for any particular purpose is implied
or given.

## The name "Rhayes"

It's a bit of a joke.  REYES stands for "Render Everything You Ever Saw".  In that spirit, "RHAYES" stands for
"Renders Hardly Anything You've Ever Seen."

I've wanted to make a renderer called rhayes for quite some time.

## Overview
Rhayes is a fast, portable software renderer based on the REYES (Render Everything You Ever Saw) architecture. It is written in strict C99 with no external dependencies beyond the C standard library, ensuring maximum portability.

The renderer implements a classic REYES pipeline: recursive splitting of primitives, dicing into micropolygon grids, shading at vertices, and stochastic sampling for visibility and anti-aliasing.

## Features
- **Pure C99**: No C++, no external libraries.
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
    - Surface shaders: `matte`, `plastic`, `metal`, and diagnostic shaders (`random`, `randomgrid`).
    - Light sources: Point and Distant lights.
- **RIB Support**:
    - RIB parser for scene description.
    - RIB output for scene serialization.
- **Output**: PPM image format.

## Building
Rhayes uses a standard Makefile.

```sh
# Build the legacy demo executable (rhayes)
make

# Build all utilities (bin/render and bin/catrib)
make programs

# Build everything including libraries
make all programs libs
```

## Usage

### Built-in Demo
Run the default demo which renders a teapot on a checkerboard floor:
```sh
./rhayes
```
This generates `output.ppm`.

### RIB Renderer
Render any RIB (RenderMan Interface Bytestream) file:
```sh
./bin/render scene.rib
```

### RIB Utility (catrib)
A tool to parse and re-serialize RIB files, useful for debugging or pretty-printing:
```sh
./bin/catrib input.rib -o output.rib
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
- `bin/`: Compiled executables.
- `lib/`: Compiled static libraries.
- `obj/`: Object files.
- `FSD.md`: Functional System Description.
- `RISPEC.md` / `PARTI.md`: RenderMan Interface Specification documentation.
- `PLAN.md`: Project development plan.

## License
MIT License.
