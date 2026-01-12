# Rhayes: REYES-style Software Renderer in C

## Overview
Rhayes is a fast, portable software renderer based on the REYES (Render Everything You Ever Saw) architecture. It is written in strict C99 with no external dependencies (only the C standard library).

## Features
- **Pure C**: No C++, no external libraries.
- **REYES Pipeline**:
    - Recursive splitting of primitives.
    - Dicing into Micropolygon Grids.
    - Shading in object/eye space.
    - Rasterization with Z-buffer visibility.
- **Primitives**: Parametric Spheres.
- **Output**: PPM Image format.

## Building
Rhayes uses a standard Makefile.

```sh
make
```

## Running
After building, run the executable:

```sh
./rhayes
```

This will generate an image file named `output.ppm` in the current directory.

## Viewing the Output
You can view the `.ppm` file with most image viewers (like GIMP, Photoshop, `feh`, or `display` on Linux).

## Project Structure
- `src/`: Source code.
- `include/`: Header files.
- `obj/`: Compiled object files.
- `FSD.md`: Functional System Description.

## License
MIT License.
