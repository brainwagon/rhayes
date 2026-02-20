# Dead Code Analysis for `bin/render`

This document lists functions and files that are part of the project source tree but are **not called** or **not linked** into the `bin/render` executable.

## Unlinked Source Files
The following files are present in the `src/` directory but are not compiled into `bin/render`. They are used by other tools (`rhayes`, `catrib`, `scene2rib`).

- `src/main.c` (Entry point for `rhayes` standalone demo)
- `src/main_rib.c` (Entry point for `scene2rib`)
- `src/catrib.c` (Entry point for `catrib`)
- `src/rib_output.c` (RIB serialization library, used by `catrib` and `scene2rib`)

## Unused Library Functions
The following functions are compiled and linked into `bin/render` (part of `librh.a` or `libri.a`) but are not reachable from the `main` entry point or the RIB parsing loop.

### `src/rh_image.c`
- `rh_image_save_ppm`: The renderer exclusively uses PNG output.
- `rh_image_save_png`: The renderer calls `rh_image_save_png_channels` directly to handle RGBA/RGB selection based on `RiDisplay` settings.

### `src/ri_options.c`
- `RiDisplayV`: The `RiCallbacks` interface for `Display` currently only accepts name, type, and mode, dropping any parameter list. Thus, the vector version of the internal API is never called.
- `RiOption` (Varargs version): The parser calls `RiOptionV`.
- `RiProjection` (Varargs version): The parser calls `RiProjectionV`.
- `RiHider` (Varargs version): The parser calls `RiHiderV`.

### `src/ri_light.c`
- `RiLightSource` (Varargs version): The parser calls `RiLightSourceV`.

### `src/ri_primitive.c`
The parser and `render.c` callbacks exclusively use the Vector (`V`) versions of primitive functions to handle token/value arrays from the RIB file. The following convenience varargs wrappers are unused:

- `RiSphere`
- `RiCylinder`
- `RiCone`
- `RiParaboloid`
- `RiDisk`
- `RiTorus`
- `RiHyperboloid`
- `RiPolygon`
- `RiPatch`
- `RiGeometry`
- `RiSurface`

### `src/ri_state.c`
- `RiMotionBegin` (Varargs version): The parser calls `RiMotionBeginV`.

## Notes
- **Varargs Wrappers:** The RenderMan API (`Ri*`) typically provides both a varargs interface (e.g., `RiSphere(radius, ..., RI_NULL)`) and a vector interface (`RiSphereV(radius, ..., n, tokens, values)`). `bin/render` is a RIB interpreter, so it parses lists of arguments into arrays and calls the `V` functions. The varargs wrappers exist for C-API users (like `src/main.c`) but are dead code within the context of the RIB renderer.
