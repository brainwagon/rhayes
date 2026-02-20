# Shading Language Compiler and Runtime Plan

## 1. Overview

This document describes a plan for adding a RenderMan Shading Language (RSL) compiler and
bytecode runtime to the rhayes renderer.  The system compiles `.sl` shader source files
into a compact bytecode program.  At render time, a virtual machine executes the bytecode
**per-vertex**, matching the existing shading architecture -- the VM is invoked once per
micropolygon grid vertex through the same `RhShaderFunc` interface used by the current C
shaders.

### Design Principles

- **Pure C99**, consistent with the rest of rhayes.  No external dependencies beyond libm.
- **Per-vertex execution**: the VM executes the full shader program for a single shading
  point at a time.  Registers hold scalar floats and 3-component tuples, not arrays.
  Control flow uses conventional jumps and branches.  This matches the existing per-vertex
  shading loop in `ri_render.c` and requires no restructuring of the renderer.
- **Integration with existing C shaders**: compiled bytecode shaders coexist with the
  current C function pointer shaders.  The VM executor has the same `RhShaderFunc` signature.
- **8-byte fixed-width instructions**: simple to decode, register-based.
- **2-operand instruction limit**: 3-operand built-in functions (clamp, mix, smoothstep)
  are decomposed into sequences of 2-operand instructions by the compiler.

### Scope

The initial implementation targets:
- Surface shaders
- Light source shaders
- Displacement shaders

Volume/atmosphere and imager shaders are deferred to a later phase.

## 2. Language Subset (Phase 1)

### 2.1 Data Types

| Type     | Components | Description                              |
|----------|-----------|------------------------------------------|
| `float`  | 1         | Scalar floating point                    |
| `color`  | 3         | RGB triple                               |
| `point`  | 3         | 3D position (transforms as point)        |
| `vector` | 3         | 3D direction (transforms as vector)      |
| `normal` | 3         | 3D normal (transforms as inverse-transpose) |
| `matrix` | 16        | 4x4 homogeneous transformation           |
| `string` | -         | Used only for texture filenames and coordinate system names |

No integer type -- floats serve for integer and boolean operations (following the RSL spec).
Arrays of the above types are supported with fixed compile-time sizes.

### 2.2 Storage Classes

| Class     | Meaning                                      |
|-----------|----------------------------------------------|
| `uniform` | Constant across the primitive; a hint to the compiler |
| `varying` | May differ per shading point                 |

Since the VM executes per-vertex, both uniform and varying variables are stored
as ordinary registers.  The distinction is preserved in the language for RSL
compatibility and for potential future optimization (e.g., hoisting uniform
computations out of the per-vertex invocation loop), but it does not affect
the VM's execution model.

Shader parameters default to `uniform`.  Local variables default to `varying`.
An `output` qualifier allows shader-to-shader communication.

### 2.3 Built-in Global Variables

These are loaded from `RhShaderContext` into fixed VM registers before each
shader invocation.

**Surface shader:**

| Variable | Type    | Access | Description                    |
|----------|---------|--------|--------------------------------|
| `P`      | point   | RW     | Surface position (camera space)|
| `N`      | normal  | RW     | Shading normal                 |
| `Ng`     | normal  | R      | Geometric normal               |
| `I`      | vector  | R      | Incident ray direction         |
| `E`      | point   | R      | Camera position                |
| `Cs`     | color   | RW     | Surface color                  |
| `Os`     | color   | RW     | Surface opacity                |
| `Ci`     | color   | RW     | Output: incident color         |
| `Oi`     | color   | RW     | Output: incident opacity       |
| `s`, `t` | float   | RW     | Texture coordinates            |
| `u`, `v` | float   | R      | Parametric coordinates         |
| `du`,`dv`| float   | R      | Parametric derivatives         |
| `L`      | vector  | R      | Light direction (in illuminance)|
| `Cl`     | color   | R      | Light color (in illuminance)   |
| `dPdu`   | vector  | R      | Derivative of P w.r.t. u       |
| `dPdv`   | vector  | R      | Derivative of P w.r.t. v       |

**Light shader:**

| Variable | Type    | Access | Description                    |
|----------|---------|--------|--------------------------------|
| `Ps`     | point   | R      | Surface point being illuminated|
| `P`      | point   | RW     | Light position/evaluation point|
| `N`      | normal  | R      | Surface normal at Ps           |
| `L`      | vector  | W      | Output: light direction        |
| `Cl`     | color   | W      | Output: light color/intensity  |

**Displacement shader:**

| Variable | Type    | Access | Description                    |
|----------|---------|--------|--------------------------------|
| `P`      | point   | RW     | Surface position (to be modified)|
| `N`      | normal  | RW     | Surface normal (to be recalculated)|
| `s`, `t` | float   | R      | Texture coordinates            |
| `u`, `v` | float   | R      | Parametric coordinates         |

### 2.4 Shader and Function Syntax

```
surface matte(float Ka = 1; float Kd = 1)
{
    normal Nf = faceforward(normalize(N), I);
    Oi = Os;
    Ci = Os * Cs * (Ka * ambient() + Kd * diffuse(Nf));
}
```

```
light pointlight(float intensity = 1;
                 color lightcolor = 1;
                 point from = point "shader" (0, 0, 0))
{
    illuminate(from) {
        Cl = intensity * lightcolor;
        L = Ps - from;   /* unnormalized: length encodes distance */
    }
}
```

User-defined functions are supported:

```
color my_blend(color a, b; float t)
{
    return mix(a, b, t);
}
```

Functions may not be recursive (enforced by the compiler).

### 2.5 Control Flow

| Construct                          | Description                              |
|------------------------------------|------------------------------------------|
| `if (expr) { } else { }`          | Conditional branch                       |
| `while (expr) { }`                | Loop                                     |
| `for (init; test; inc) { }`       | Loop; desugared to while                 |
| `break`, `continue`               | Loop control                             |
| `illuminance(P, N, angle) { }`    | Light integration loop (surface shaders) |
| `illuminate(from) { }`            | Light emission (light shaders)           |
| `illuminate(from, axis, angle) { }` | Spot light emission                    |
| `solar() { }`                     | Distant light emission                   |
| `solar(axis, angle) { }`          | Directional light emission with cone     |

No `goto`, `switch`, or recursion.

### 2.6 Built-in Functions (Phase 1 Subset)

**Math:**
`abs`, `ceil`, `floor`, `round`, `min`, `max`, `clamp`, `mod`,
`sqrt`, `pow`, `exp`, `log`, `sign`,
`sin`, `cos`, `tan`, `asin`, `acos`, `atan` (1 and 2 arg),
`radians`, `degrees`,
`mix`, `step`, `smoothstep`

**Geometric:**
`dot`, `cross`, `length`, `normalize`, `distance`,
`faceforward`, `reflect`,
`xcomp`, `ycomp`, `zcomp`, `setxcomp`, `setycomp`, `setzcomp`,
`calculatenormal`, `area`

**Derivatives:**
`Du`, `Dv`, `Deriv`

**Color:**
`comp`, `setcomp`, `ctransform`

**Texture:**
`texture`, `shadow`, `environment`

**Noise:**
`noise` (1D, 2D, 3D, 4D overloads), `pnoise` (periodic noise),
`cellnoise`

**Illumination helpers:**
`ambient`, `diffuse`, `specular`, `phong`, `trace`
(These are convenience wrappers around illuminance loops in the standard spec,
but we implement them as VM built-in operations for efficiency.)

**String/coordinate:**
`transform`, `ntransform`, `vtransform`

**Misc:**
`printf` (debug output only)

## 3. Compiler Architecture

### 3.1 Pipeline

```
  .sl source file
       |
       v
  +-----------+
  |   Lexer   |  Hand-written tokenizer
  +-----------+
       |  token stream
       v
  +-----------+
  |  Parser   |  Recursive-descent parser
  +-----------+
       |  AST (abstract syntax tree)
       v
  +------------------+
  | Semantic Analysis |  Type checking, scope resolution,
  +------------------+  function resolution
       |  annotated AST
       v
  +----------------+
  | Code Generator |  Bytecode emission
  +----------------+
       |
       v
  .slo bytecode file (or in-memory blob)
```

### 3.2 Lexer (`rh_sl_lex.c`)

Hand-written scanner producing tokens:
- Keywords: `surface`, `light`, `displacement`, `volume`, `float`, `color`,
  `point`, `vector`, `normal`, `matrix`, `string`, `uniform`, `varying`,
  `output`, `if`, `else`, `while`, `for`, `return`, `break`, `continue`,
  `illuminance`, `illuminate`, `solar`
- Identifiers, numeric literals, string literals
- Operators: `+`, `-`, `*`, `/`, `.` (dot product), `^` (cross product),
  `=`, `==`, `!=`, `<`, `>`, `<=`, `>=`, `&&`, `||`, `!`, `?:`, `+=`, `-=`, `*=`, `/=`
- Punctuation: `(`, `)`, `{`, `}`, `[`, `]`, `;`, `,`
- Comments: `/* ... */` and `//`

### 3.3 Parser (`rh_sl_parse.c`)

Recursive-descent parser building an AST.  The grammar is essentially C with:
- No pointers, structs, unions, or typedef
- Additional types (`color`, `point`, `vector`, `normal`, `matrix`)
- Special loop constructs (`illuminance`, `illuminate`, `solar`)
- Shader declarations instead of `main()`
- Semicolons separate parameters (not commas) inside shader declarations
- Parameters require default values

AST node types:
- `SL_NODE_SHADER` -- top-level shader (type, name, params, body)
- `SL_NODE_FUNCTION` -- user function definition
- `SL_NODE_PARAM` -- parameter declaration with default
- `SL_NODE_VAR_DECL` -- local variable declaration
- `SL_NODE_ASSIGN` -- assignment
- `SL_NODE_BINOP` -- binary operator
- `SL_NODE_UNOP` -- unary operator
- `SL_NODE_CALL` -- function/builtin call
- `SL_NODE_IF` -- conditional
- `SL_NODE_WHILE` -- while loop
- `SL_NODE_FOR` -- for loop
- `SL_NODE_RETURN` -- return from function
- `SL_NODE_BREAK`, `SL_NODE_CONTINUE`
- `SL_NODE_ILLUMINANCE` -- illuminance loop
- `SL_NODE_ILLUMINATE` -- illuminate block
- `SL_NODE_SOLAR` -- solar block
- `SL_NODE_IDENT` -- variable reference
- `SL_NODE_LITERAL` -- numeric/string constant
- `SL_NODE_TYPECAST` -- explicit type construction `color(r,g,b)`
- `SL_NODE_COMPONENT` -- component access for tuples

### 3.4 Semantic Analysis (`rh_sl_sema.c`)

Responsibilities:
1. **Type checking**: verify operand types, insert implicit promotions
   (float -> color/point/vector/normal by replication)
2. **Scope resolution**: nested scopes for shader body, blocks, functions.
   Built-in globals resolved to fixed register slots.
3. **Function resolution**: match call signatures to built-in or user functions.
   Detect and reject recursion (direct or indirect).
4. **Illuminance/illuminate validation**: ensure these appear only in the
   correct shader types; verify parameter types.
5. **Read/write validation**: ensure read-only globals are not written.

### 3.5 Code Generator (`rh_sl_codegen.c`)

Generates a linear bytecode program.  Key design decisions:

**Register-based VM** (not stack-based):
- Fewer instructions than stack-based (no push/pop for every operand)
- Registers are virtual; the compiler allocates as many as needed
- Each register holds a single float or the first component of a 3-component
  tuple (with the next 2 register slots holding components [1] and [2])

**Register allocation:**
- Fixed registers 0..K for built-in globals (P, N, I, Cs, Os, Ci, Oi, etc.)
  Tuple globals occupy 3 consecutive register slots each.
- Shader parameters in registers K+1..K+P
- Temporaries allocated sequentially from K+P+1 onward
- Simple linear scan; no need for graph coloring since register count is unlimited

**Bytecode format:**
Each instruction is a fixed-size 8-byte word:

```
| opcode (8 bits) | flags (8 bits) | dst (16 bits) | src1 (16 bits) | src2 (16 bits) |
```

The `flags` field encodes:
- Bits 0-4: component index (for VCOMP/VSETCOMP), or sub-opcode
- Bits 5-7: reserved

**Decomposition of 3-operand functions:**

The compiler decomposes 3-operand operations into 2-operand sequences:

```
clamp(x, lo, hi)  -->  FMAX tmp, x, lo
                       FMIN dst, tmp, hi

mix(a, b, t)      -->  FSUB tmp1, b, a       # tmp1 = b - a
                       FMUL tmp2, tmp1, t     # tmp2 = (b-a)*t
                       FADD dst, a, tmp2      # dst = a + (b-a)*t

smoothstep(e0, e1, x) -->  (decomposed into clamp + Hermite polynomial)
```

## 4. Bytecode Instruction Set

### 4.1 Arithmetic (float)

| Opcode | Mnemonic   | Operation            |
|--------|-----------|----------------------|
| 0x01   | `FADD`    | dst = src1 + src2    |
| 0x02   | `FSUB`    | dst = src1 - src2    |
| 0x03   | `FMUL`    | dst = src1 * src2    |
| 0x04   | `FDIV`    | dst = src1 / src2    |
| 0x05   | `FNEG`    | dst = -src1          |
| 0x06   | `FABS`    | dst = abs(src1)      |
| 0x07   | `FMOD`    | dst = mod(src1,src2) |
| 0x08   | `FPOW`    | dst = pow(src1,src2) |
| 0x09   | `FSQRT`   | dst = sqrt(src1)     |
| 0x0A   | `FMIN`    | dst = min(src1,src2) |
| 0x0B   | `FMAX`    | dst = max(src1,src2) |
| 0x0C   | `FFLOOR`  | dst = floor(src1)    |
| 0x0D   | `FCEIL`   | dst = ceil(src1)     |
| 0x0E   | `FSIGN`   | dst = sign(src1)     |
| 0x0F   | `FROUND`  | dst = round(src1)    |

### 4.2 Trigonometric (float)

| Opcode | Mnemonic   | Operation            |
|--------|-----------|----------------------|
| 0x10   | `FSIN`    | dst = sin(src1)      |
| 0x11   | `FCOS`    | dst = cos(src1)      |
| 0x12   | `FTAN`    | dst = tan(src1)      |
| 0x13   | `FASIN`   | dst = asin(src1)     |
| 0x14   | `FACOS`   | dst = acos(src1)     |
| 0x15   | `FATAN`   | dst = atan(src1)     |
| 0x16   | `FATAN2`  | dst = atan2(src1,src2)|
| 0x17   | `FEXP`    | dst = exp(src1)      |
| 0x18   | `FLOG`    | dst = log(src1)      |

### 4.3 Tuple Operations (color/point/vector/normal)

These operate on 3-component tuples.  A tuple register occupies 3 consecutive
float register slots.  For example, register `r` for a point means:
`r+0` = x, `r+1` = y, `r+2` = z.

| Opcode | Mnemonic     | Operation                          |
|--------|--------------|------------------------------------|
| 0x20   | `VADD`       | dst = src1 + src2 (component-wise) |
| 0x21   | `VSUB`       | dst = src1 - src2                  |
| 0x22   | `VMUL`       | dst = src1 * src2 (component-wise) |
| 0x23   | `VDIV`       | dst = src1 / src2 (component-wise) |
| 0x24   | `VNEG`       | dst = -src1                        |
| 0x25   | `VSMUL`      | dst(tuple) = src1(tuple) * src2(float) |
| 0x26   | `VSDIV`      | dst(tuple) = src1(tuple) / src2(float) |
| 0x27   | `DOT`        | dst(float) = dot(src1, src2)       |
| 0x28   | `CROSS`      | dst = cross(src1, src2)            |
| 0x29   | `LENGTH`     | dst(float) = length(src1)          |
| 0x2A   | `NORMALIZE`  | dst = normalize(src1)              |
| 0x2B   | `DISTANCE`   | dst(float) = distance(src1, src2)  |
| 0x2C   | `FACEFORWARD`| dst = faceforward(N, I)            |
| 0x2D   | `REFLECT`    | dst = reflect(I, N)                |
| 0x2E   | `VCOMP`      | dst(float) = src1[flags.component] |
| 0x2F   | `VSETCOMP`   | dst[flags.component] = src1(float) |

### 4.4 Comparison and Logic

| Opcode | Mnemonic   | Operation                |
|--------|-----------|--------------------------|
| 0x30   | `FEQ`     | dst = (src1 == src2)     |
| 0x31   | `FNE`     | dst = (src1 != src2)     |
| 0x32   | `FLT`     | dst = (src1 < src2)      |
| 0x33   | `FLE`     | dst = (src1 <= src2)     |
| 0x34   | `FGT`     | dst = (src1 > src2)      |
| 0x35   | `FGE`     | dst = (src1 >= src2)     |
| 0x36   | `AND`     | dst = src1 && src2       |
| 0x37   | `OR`      | dst = src1 \|\| src2     |
| 0x38   | `NOT`     | dst = !src1              |

Comparisons produce 1.0 (true) or 0.0 (false).

### 4.5 Data Movement

| Opcode | Mnemonic   | Operation                         |
|--------|-----------|-----------------------------------|
| 0x40   | `FMOV`    | dst = src1 (float copy)           |
| 0x41   | `VMOV`    | dst = src1 (tuple copy, 3 floats) |
| 0x42   | `FCONST`  | dst = const_pool[src1]            |
| 0x43   | `VCONST`  | dst = const_pool[src1..src1+2]    |
| 0x44   | `FTOV`    | tuple dst = (src1, src1, src1)    |

`FCONST` and `VCONST` use `src1` as an index into the constant pool.

### 4.6 Control Flow

Standard branching for a per-vertex VM.

| Opcode | Mnemonic     | Operation                                 |
|--------|-------------|-------------------------------------------|
| 0x50   | `JUMP`      | PC = src1 (unconditional jump)            |
| 0x51   | `JUMP_IF`   | if (src1 != 0) PC = dst (branch if true)  |
| 0x52   | `JUMP_IFNOT`| if (src1 == 0) PC = dst (branch if false) |

**How conditionals work:**

```sl
if (x > 0) {
    y = 1;
} else {
    y = 0;
}
```

Compiles to:
```
  FGT       r_cond, r_x, r_zero     # r_cond = (x > 0) ? 1.0 : 0.0
  JUMP_IFNOT else_label, r_cond     # if false, jump to else
  FCONST    r_y, [1.0]              # y = 1
  JUMP      end_label               # skip else
else_label:
  FCONST    r_y, [0.0]              # y = 0
end_label:
```

**How loops work:**

```sl
while (x > 0) {
    x = x - 1;
}
```

Compiles to:
```
loop_top:
  FGT       r_cond, r_x, r_zero     # test condition
  JUMP_IFNOT loop_end, r_cond       # exit if false
  FCONST    r_one, [1.0]
  FSUB      r_x, r_x, r_one         # x = x - 1
  JUMP      loop_top                 # repeat
loop_end:
```

`break` compiles to `JUMP loop_end`.  `continue` compiles to `JUMP loop_top`.

### 4.7 Lighting

| Opcode | Mnemonic           | Operation                                    |
|--------|--------------------|----------------------------------------------|
| 0x60   | `ILLUMINANCE_BEGIN`| Begin illuminance loop; sets up light iteration |
| 0x61   | `ILLUMINANCE_END` | Advance to next light; jump back if more     |
| 0x62   | `ILLUMINATE_BEGIN`| Begin illuminate block (light shader)         |
| 0x63   | `ILLUMINATE_END`  | End illuminate block                          |
| 0x64   | `SOLAR_BEGIN`     | Begin solar block (distant light shader)      |
| 0x65   | `SOLAR_END`       | End solar block                               |
| 0x66   | `AMBIENT`         | dst = accumulated ambient light contribution  |
| 0x67   | `DIFFUSE`         | dst = diffuse(Nf) using illuminance loop      |
| 0x68   | `SPECULAR`        | dst = specular(Nf, V, roughness)              |

**How illuminance works (per-vertex):**

`ILLUMINANCE_BEGIN` initializes a light iterator using the light list from
`RhShaderContext`.  It takes registers for P (surface point), N (surface normal),
and an angle (cone half-angle, or PI/2 for hemisphere).

For each light:
1. Compute `L` (light direction) and `Cl` (light color * intensity) based on
   light type (ambient, distant, point, spot)
2. Apply shadow attenuation if the light has a shadow map
3. Test the illuminance cone: `dot(normalize(L), N) > cos(angle)`
4. If the light passes the cone test, load `L` and `Cl` into their global
   registers and execute the loop body
5. At `ILLUMINANCE_END`, advance to the next light and jump back to the body
6. When all lights are exhausted, fall through past `ILLUMINANCE_END`

This reuses the existing light evaluation logic from `calculate_lights()` in
`rh_shader.c`, factored into a callable helper.

### 4.8 Texture and Noise

| Opcode | Mnemonic    | Operation                                |
|--------|------------|------------------------------------------|
| 0x70   | `TEXTURE`  | dst = texture(name, s, t)                |
| 0x71   | `SHADOW`   | dst = shadow(name, P)                    |
| 0x72   | `ENVMAP`   | dst = environment(name, direction)       |
| 0x73   | `NOISE1`   | dst = noise(float)                       |
| 0x74   | `NOISE2`   | dst = noise(float, float)                |
| 0x75   | `NOISE3`   | dst = noise(point)                       |
| 0x76   | `PNOISE`   | dst = pnoise(x, period)                  |
| 0x77   | `CELLNOISE`| dst = cellnoise(x)                       |

Texture opcodes use `src2` as an index into the string table to identify
the texture file.  The texture handle is resolved at shader load time and
cached in the `RhSLShader` structure.

### 4.9 Special Operations

| Opcode | Mnemonic     | Operation                                    |
|--------|-------------|----------------------------------------------|
| 0x90   | `TRANSFORM` | dst = transform("space", src1)               |
| 0x91   | `NTRANSFORM`| dst = ntransform("space", src1) (normal xform)|
| 0x92   | `VTRANSFORM`| dst = vtransform("space", src1) (vector xform)|
| 0x93   | `DU`        | dst = Du(src1) -- u derivative               |
| 0x94   | `DV`        | dst = Dv(src1) -- v derivative               |
| 0x95   | `AREA`      | dst = area(P) -- micropolygon area           |
| 0x96   | `CALCNORMAL`| dst = calculatenormal(P) -- recompute normal |
| 0x97   | `PRINTF`    | Debug print (string_table[src1] + args)      |

### 4.A Function Calls

| Opcode | Mnemonic | Operation                              |
|--------|---------|----------------------------------------|
| 0xA0   | `CALL`  | Call user function at offset; args in registers |
| 0xA1   | `RET`   | Return from function                   |

User function calls are inlined by default when small.  For larger functions,
`CALL` pushes the return address onto an internal call stack and jumps to the
function body.  Arguments and return values are passed in designated registers
(set up by the compiler before the CALL).

### 4.B End

| Opcode | Mnemonic | Operation               |
|--------|---------|--------------------------|
| 0xFF   | `HALT`  | End of shader execution  |

## 5. Virtual Machine Runtime

### 5.1 VM State (`RhSLVM`)

```c
typedef struct {
    /* Program */
    uint64_t* code;          /* Bytecode instruction array */
    int       code_len;      /* Number of instructions */
    float*    const_pool;    /* Constant pool (floats) */
    int       const_count;
    char**    string_table;  /* String table (texture names, etc.) */
    int       string_count;

    /* Register file */
    int       num_regs;      /* Total register count */

    /* Shader metadata */
    int       shader_type;   /* SURFACE=1, LIGHT=2, DISPLACEMENT=3 */
    int       num_params;    /* Number of shader parameters */

    /* Parameter metadata (for binding RIB params to registers) */
    struct {
        char    name[64];    /* Parameter name */
        int     type;        /* float=1, color=2, ... */
        int     reg;         /* Register index */
        int     default_idx; /* Index into const_pool for default value */
    } params[32];            /* Fixed max; could be dynamic */
} RhSLVM;
```

### 5.2 Per-Invocation Execution State

Allocated on the stack (or in a small arena) for each shader call:

```c
typedef struct {
    float*    regs;          /* Register file: regs[num_regs] */
    int       pc;            /* Program counter */

    /* Call stack (for user function calls) */
    int       call_stack[16];/* Return addresses */
    int       call_sp;

    /* Light iteration state */
    int       current_light; /* Index into light list */
    int       num_lights;
    void*     light_list;    /* Pointer to light array from context */
} RhSLExecState;
```

The register file is a flat `float[]` array.  A 3-component tuple stored at
register `r` occupies `regs[r]`, `regs[r+1]`, `regs[r+2]`.

### 5.3 Execution Loop

The core of `rh_sl_vm_execute()`:

```c
while (state->pc < vm->code_len) {
    uint64_t instr = vm->code[state->pc++];
    uint8_t opcode = (instr >> 56) & 0xFF;
    uint8_t flags  = (instr >> 48) & 0xFF;
    uint16_t dst   = (instr >> 32) & 0xFFFF;
    uint16_t src1  = (instr >> 16) & 0xFFFF;
    uint16_t src2  = (instr >>  0) & 0xFFFF;

    switch (opcode) {
        case OP_FADD:
            regs[dst] = regs[src1] + regs[src2];
            break;
        case OP_VADD:
            regs[dst+0] = regs[src1+0] + regs[src2+0];
            regs[dst+1] = regs[src1+1] + regs[src2+1];
            regs[dst+2] = regs[src1+2] + regs[src2+2];
            break;
        case OP_JUMP_IFNOT:
            if (regs[src1] == 0.0f) state->pc = dst;
            break;
        // ...
        case OP_HALT:
            return;
    }
}
```

This is a straightforward bytecode interpreter.  Each instruction does a small
amount of work on scalar values.  No mask checks, no per-point loops.

### 5.4 Integration with Renderer

The VM executor has the same signature as C shaders:

```c
void rh_sl_vm_shader_exec(RhShaderContext* ctx, void* params);
```

Where `params` points to an `RhSLShader` structure:

```c
typedef struct {
    RhSLVM*     vm;              /* Compiled bytecode program */
    float*      param_values;    /* Instance parameter values (overrides) */
    int         num_params;
    void**      textures;        /* Pre-loaded texture handles */
    int         num_textures;
} RhSLShader;
```

The executor function `rh_sl_vm_shader_exec()`:
1. Allocates an `RhSLExecState` with register file on the stack (or from a
   small fixed-size buffer for shaders needing many registers)
2. Loads built-in globals from `RhShaderContext` into fixed registers:
   - `regs[R_P+0..2]` = ctx->P.x, ctx->P.y, ctx->P.z
   - `regs[R_N+0..2]` = ctx->N.x, ctx->N.y, ctx->N.z
   - `regs[R_CS+0..2]` = ctx->Cs.r, ctx->Cs.g, ctx->Cs.b
   - etc.
3. Loads shader instance parameters from `param_values` (or defaults from
   `const_pool` if no override was provided)
4. Sets up light list pointer from ctx
5. Calls `rh_sl_vm_execute()`
6. Reads back output registers into `RhShaderContext`:
   - ctx->Ci = {regs[R_CI+0], regs[R_CI+1], regs[R_CI+2]}
   - ctx->Oi = {regs[R_OI+0], regs[R_OI+1], regs[R_OI+2]}

**No changes to ri_render.c** -- the existing per-vertex shading loop calls
`item->shader(&shctx, item->shader_params)` which works identically whether
the shader is a C function or the VM executor.

### 5.5 Displacement Shader Integration

Displacement shaders execute before surface shading.  The VM modifies P and N
in registers; the executor writes these back to `RhShaderContext`, and the
existing renderer pipeline picks up the modified values.  The renderer should
recalculate bounding boxes after displacement.

### 5.6 Memory Management

- The register file for typical shaders (< 256 registers = 1KB) fits on the
  C stack via a fixed-size array or VLA
- For shaders requiring more registers, a small malloc'd buffer is used
  (allocated once per `RhSLShader` and reused across invocations)
- Compiled bytecode (`RhSLVM`) is allocated once at shader load time and
  shared across all vertices using that shader
- No per-vertex heap allocation in the hot path

## 6. Shader Compiler Tool

### 6.1 `rh_slc` -- Shader Language Compiler

A standalone command-line tool (or built into the renderer):

```
rh_slc matte.sl -o matte.slo
```

Reads `.sl` source, outputs `.slo` (shader object) bytecode.

The `.slo` format:

```
Header:
  magic: "RHSL" (4 bytes)
  version: uint16
  shader_type: uint8 (surface=1, light=2, displacement=3)
  shader_name_offset: uint16 (into string table)
  num_params: uint16
  num_registers: uint16
  code_offset: uint32
  code_length: uint32 (number of 8-byte instructions)
  const_pool_offset: uint32
  const_pool_length: uint32 (number of floats)
  string_table_offset: uint32
  string_table_length: uint32 (total bytes)

Parameter Table:
  For each parameter:
    name_offset: uint16 (into string table)
    type: uint8 (float=1, color=2, point=3, vector=4, normal=5, matrix=6, string=7)
    register_index: uint16
    default_value_offset: uint32 (into const pool)
    num_components: uint8 (1 for float, 3 for color/point/vector/normal, 16 for matrix)

Code Section:
  Array of 8-byte instructions

Constant Pool:
  Array of floats

String Table:
  Concatenated null-terminated strings
```

### 6.2 Integration with RIB Parser

The RIB parser's `RiSurface`, `RiLightSource`, and `RiDisplacement` commands
look up shader names.  The lookup order is:

1. Check built-in C shaders (matte, plastic, metal, etc.)
2. Search shader path for `<name>.slo` bytecode
3. If `.slo` not found, search for `<name>.sl` and compile on-the-fly

When an `.slo` is loaded, an `RhSLShader` is created and the shader function
pointer is set to `rh_sl_vm_shader_exec`.  Parameter values from the
RIB token/value pairs override the compiled defaults.

## 7. Implementation Phases

### Phase 1: Minimal Viable VM

**Goal**: Execute a hand-assembled bytecode shader equivalent to `matte`.

Files:
- `src/rh_sl_vm.c`, `include/rh_sl_vm.h` -- VM execution engine
- `include/rh_sl_opcodes.h` -- opcode definitions

Steps:
1. Define the register file, opcode enum, and instruction encoding
2. Implement the execution loop for arithmetic and data movement opcodes
3. Implement branching (JUMP, JUMP_IF, JUMP_IFNOT)
4. Implement the illuminance loop (integrating with existing light infrastructure)
5. Hand-code a matte shader in bytecode; test against the C matte shader output
6. Verify integration via `rh_sl_vm_shader_exec()` -- no changes to ri_render.c

### Phase 2: Compiler Frontend

**Goal**: Parse `.sl` files into AST.

Files:
- `src/rh_sl_lex.c`, `include/rh_sl_lex.h` -- lexer
- `src/rh_sl_parse.c`, `include/rh_sl_parse.h` -- parser
- `include/rh_sl_ast.h` -- AST node types

Steps:
1. Implement lexer with complete token set
2. Implement recursive-descent parser for shader declarations, statements,
   expressions
3. Build AST
4. Write test harness: parse standard shaders (matte, plastic, pointlight)
   and dump AST

### Phase 3: Semantic Analysis and Code Generation

**Goal**: Compile `.sl` to `.slo` bytecode.

Files:
- `src/rh_sl_sema.c` -- semantic analysis
- `src/rh_sl_codegen.c` -- code generation
- `src/rh_slc.c` -- compiler driver (main)

Steps:
1. Type checking and promotion rules
2. Register allocation
3. Bytecode emission for expressions, assignments, control flow
4. Bytecode emission for illuminance/illuminate/solar
5. Decomposition of 3-operand built-ins (clamp, mix, smoothstep)
6. `.slo` file writer
7. End-to-end test: compile matte.sl -> matte.slo -> render -> compare

### Phase 4: Standard Shader Library

**Goal**: Provide RSL implementations of all standard shaders.

Files:
- `shaders/matte.sl`
- `shaders/plastic.sl`
- `shaders/metal.sl`
- `shaders/shinymetal.sl`
- `shaders/paintedplastic.sl`
- `shaders/ambientlight.sl`
- `shaders/distantlight.sl`
- `shaders/pointlight.sl`
- `shaders/spotlight.sl`

Steps:
1. Write each shader in RSL
2. Compile and verify against existing C shader output
3. Add regression tests comparing VM-rendered output to reference images

### Phase 5: Advanced Features

**Goal**: Complete language support.

Steps:
1. User-defined functions with inlining
2. Noise functions (Perlin noise, cellnoise)
3. Texture lookups with filtering (integrate with existing `rh_texture_sample()`)
4. Shadow map lookups (integrate with existing shadow map infrastructure)
5. Displacement shaders with geometry modification
6. `printf` for shader debugging
7. Coordinate space transforms (`transform`, `ntransform`, `vtransform`)
8. On-the-fly `.sl` compilation when `.slo` is not found
9. Shader search path (`RiOption "searchpath" "shader"`)

## 8. Testing Strategy

### Unit Tests

- **Lexer tests**: verify tokenization of all language constructs
- **Parser tests**: verify AST construction for representative shaders
- **VM instruction tests**: test each opcode in isolation with known inputs
  and expected outputs

### Integration Tests

- **Shader equivalence**: for each standard shader, render a test scene with
  both the C shader and the compiled RSL shader; the output images must be
  bit-identical (or within a small tolerance for floating-point ordering differences)
- **Custom shader test**: write novel RSL shaders (procedural patterns, etc.)
  and verify they produce expected output
- **Displacement test**: verify that displacement shaders correctly modify
  geometry before surface shading

### Performance Tests

- **VM vs C shader**: benchmark VM execution vs C shader execution to verify
  that the overhead of interpretation is acceptable.  For a simple shader like
  matte, the VM should be no more than ~5-10x slower than the native C version
  (interpretation overhead is dominated by the actual rendering work).

## 9. Resolved Design Decisions

1. **Instruction encoding**: 8-byte fixed-width instructions.  Simple to
   decode, total bytecode size is negligible for typical shaders.

2. **3-operand instructions**: Decomposed by the compiler into 2-operand
   sequences.  Keeps the instruction format uniform.

3. **Execution model**: Per-vertex, not per-grid SIMD.  Matches the existing
   shading loop in ri_render.c.  No mask stack, no varying/uniform register
   arrays.  Registers hold single scalar values.

4. **Atmosphere/volume shaders**: Deferred to a later phase.  The existing
   C implementations (depthcue, fog) continue to work unchanged.

5. **String handling**: Resolved at compile time to indices into a string
   table embedded in the bytecode.  Texture handles are resolved at shader
   load time (when the .slo is loaded and the RhSLShader is created).

## 10. References

- [RenderMan Interface Specification v3.2](https://paulbourke.net/dataformats/rib/RISpec3_2.pdf)
  -- Chapters 8-16 cover Part II: The RenderMan Shading Language
- [RenderMan Shading Language (Wikipedia)](https://en.wikipedia.org/wiki/RenderMan_Shading_Language)
- [Aqsis Shading Language documentation](https://www.aqsis.org/documentation/user_manual/part_i/ri_standard/shading_language.html)
- [cwbaker/reyes](https://github.com/cwbaker/reyes) -- Toy RenderMan renderer with
  RSL compiler and VM (C++, useful reference for compiler architecture)
- [PRMan RSL Extensions](https://hradec.com/ebooks/CGI/RPS_13.5/prman_technical_rendering/users_guide/rslextensions.html)
- [CMU Lecture 13: Reyes Architecture](https://www.cs.cmu.edu/afs/cs/academic/class/15869-f11/www/lectures/13_reyes.pdf)
