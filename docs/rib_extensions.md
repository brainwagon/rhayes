# RIB Extensions

This document describes extensions to the RenderMan Interface Bytestream (RIB) 3.1 specification implemented in rhayes.

## Inline Parameter Type Declarations

### Overview

RIB 3.1 requires that custom shader parameters be pre-declared with an explicit `Declare` statement before they can carry type information in parameter lists. This implementation extends the RIB parser to support **inline type syntax**, which embeds the type directly in the parameter token string.

### Syntax

The inline type syntax uses the form `"[class] type name"`, where the last space-separated word is the parameter name and everything before it is the type declaration:

```rib
"float scale"             # declares "scale" as uniform float
"color veincolor"         # declares "veincolor" as uniform color
"uniform float roughness" # declares "roughness" as uniform float
"varying color Cs"        # declares "Cs" as varying color
```

### Usage Examples

**Custom surface shader parameters:**

```rib
Surface "marble" "float scale" [6.0] "color veincolor" [0.12 0.09 0.06]
```

This is equivalent to the explicit Declare form:

```rib
Declare "scale" "float"
Declare "veincolor" "color"
Surface "marble" "scale" [6.0] "veincolor" [0.12 0.09 0.06]
```

**Custom attribute values:**

```rib
Attribute "displacementbound" "float sphere" [0.5]
```

**Light source parameters:**

```rib
LightSource "mylight" "float intensity" [2.0] "color lightcolor" [1 0.8 0.6]
```

### Supported Commands

Inline type syntax is supported in parameter lists for all commands that accept token-value pairs:

- `Surface`
- `Atmosphere`
- `LightSource`
- `Attribute`
- `Option`
- `Hider`
- `Projection`
- `Polygon`
- `Patch`

### Array Values

The `parse_Surface` parser (and all other param-list parsers) supports bracket-array `[...]` values in addition to bare scalars:

```rib
Surface "marble" "float scale" [6.0] "color veincolor" [0.12 0.09 0.06]
```

### RIB Output

When using the RIB output callbacks (e.g. via `catrib`), user-declared parameters are written using inline type syntax automatically. An explicit `Declare "foo" "float"` call followed by `Surface "s" "foo" [1.0]` will roundtrip as `Surface "s" "float foo" [1.0]`.

Standard built-in parameters (P, N, Cs, Ka, Kd, etc.) are always emitted with their bare names since their types are universally known.
