# Handoff: SL Solar Block Bug Fixes

**Date**: 2026-02-19
**Branch**: main
**Status**: Fixes applied, most tests passing, small residual differences under investigation

---

## Background

Running `RHAYES_SHADER_PATH=.:/home/markv/rhayes/shaders:BUILTIN make test` caused ~37 test
failures that do not appear with the default search path.  The difference is that the custom
path forces SL-compiled `.slo` shaders (`distantlight.slo`, `plastic.slo`, etc.) to be used
instead of the C built-in fallbacks.

The root cause was two bugs in the `solar(axis, angle)` block implementation used by
`distantlight.sl`:

```sl
light distantlight(...) {
    solar(to - from, 0) {
        Cl = intensity * lightcolor;
    }
}
```

---

## Bugs Fixed

### Bug 1: OP_SOLAR_BEGIN set `R_L = -axis` (wrong sign)

**File**: `src/rh_sl_vm.c`, `case OP_SOLAR_BEGIN`

The VM negated the axis before storing it in `R_L`.  Convention (matching `illuminate` blocks
and both call sites `eval_light_any` / `calculate_lights`) is:

- `R_L` / `L_out` = **light-to-surface** direction
- Callers negate `L_out` to obtain **surface-to-light** for N·L calculations

With the wrong sign, callers computed N · (surface-to-light negate of wrong vector) = wrong dot product → surfaces appeared unlit or lit from the wrong direction.

**Fix**: Removed negation. `R_L = axis` (not `-axis`).

---

### Bug 2 (Primary): Cone check always emitted for `solar(axis, 0)` — body never reached

**File**: `src/rh_sl_codegen.c`, `case SL_NODE_SOLAR`

The codegen guard was:

```c
if (node->u.solar.angle) { /* emit cone check */ }
```

This checks whether the AST node is non-NULL, **not** whether its value is nonzero.  For
`solar(to-from, 0)`, the literal `0` produces a non-NULL node, so the cone check **was**
emitted.

The cone check computed `actual_angle > 0` for every surface point not exactly on the axis —
which is always true — so execution jumped to `after_end` **before** `OP_SOLAR_BEGIN` was
reached.  Result:

- `R_L` was never set (stayed zero from `memset`)
- `R_CL` was never set (body never ran)
- `eval_light_any` saw `Llen2 < 1e-24` and returned zero → no light at all

Per the RenderMan spec, `solar(axis, 0)` means **no cone restriction** — illuminate all surfaces.

**Fix**: Added `angle_is_zero` check to skip cone check when angle is a zero (or negative) literal:

```c
int angle_is_zero = (node->u.solar.angle &&
                     node->u.solar.angle->node_type == SL_NODE_FLOAT_LIT &&
                     node->u.solar.angle->u.float_lit.value <= 0.0f);
if (node->u.solar.angle && !angle_is_zero) {
    /* emit cone check */
}
```

---

### Bug 3: Solar axis in world space, surface normals in camera space

**File**: `src/rh_sl_vm.c`, `case OP_SOLAR_BEGIN`; `src/rh_shader.c`, `calculate_lights`

After fixes 1 & 2, `distantlight.slo` still produced ~6407 differing pixels vs the C builtin.

The C builtin stores `l->direction` in **camera space** (transformed via view matrix in
`ri_render.c`).  The SL shader computes `axis = to - from` from world-space parameters, so
`R_L` ended up in world space while surface normals `N` were in camera space.  The dot product
`N · L` used completely mismatched coordinate frames.

**Fix**: Transform the solar axis to camera space inside `OP_SOLAR_BEGIN`, using the
`world_to_camera` matrix from `state->transform_ctx`:

```c
if (state->transform_ctx) {
    RhVec3 aw = {ax, ay, az};
    RhVec3 ac = rh_mat4_mul_dir(state->transform_ctx->world_to_camera, aw);
    ax = ac.x; ay = ac.y; az = ac.z;
}
```

To make `transform_ctx` available inside the light shader VM, it must be threaded through at
both call sites:

- **`eval_light_any`** (`src/rh_sl_vm.c`): `ctx_light.transform_ctx = ctx->transform_ctx;`
- **`calculate_lights`** (`src/rh_shader.c`): `ctx_light.transform_ctx = ctx->transform_ctx;`

**Why illuminate blocks are unaffected**: `illuminate(from, axis, angle)` blocks set
`R_L = Ps - from`, where both `Ps` and `from` are already in camera space (light params are
camera-space-transformed before being stored, and `Ps` = ctx->P in camera space).  Only the
`solar` axis (`to - from`) is computed in world space inside the shader, so only solar needs
the transform.

**Result**: Distantlight pixel differences reduced from **6407 → 5 pixels** (small FP noise).

---

## Files Modified

| File | Change |
|------|--------|
| `src/rh_sl_codegen.c` | Bug 2: `angle_is_zero` check, skip cone check for `solar(axis,0)` |
| `src/rh_sl_vm.c` | Bug 1: remove negation in `OP_SOLAR_BEGIN`; Bug 3: transform axis to camera space |
| `src/rh_shader.c` | Bug 3: pass `transform_ctx` to light shader context in `calculate_lights` |

Shaders were recompiled after the codegen fix:

```bash
touch shaders/*.sl && make shaders
```

---

## Test Results After All Fixes

### Default search path (`make test`)

```
154 pass, 0 fail
```

No regression from any fix.

### SL search path (`RHAYES_SHADER_PATH=.:/home/markv/rhayes/shaders:BUILTIN make test`)

Before fixes: ~37 failures
After all three fixes: **36 failures** (118 pass)

The `luxo` test went from FAIL → PASS, confirming the solar fix works end-to-end.

The remaining 36 failures are concentrated in two categories:

1. **Bilinear patch tests** (~23): Should be fixed now from recent changes to bilinear vertex ordering.  Retest.

2. **Shader subdirectory tests** (~13): Small pixel-count differences between SL and C
   implementations. Examples:
   - `matte`: 2 pixels differ, max_diff=1 (silhouette edge)
   - `distantlight`: 5 pixels differ, max_diff=5 (edge pixels)
   - `rotate`: 13 pixels differ, max_diff=~31 (boundary region)

---

## Remaining Work: Shader Subdirectory Failures

### What was investigated

The small per-pixel differences in the shader tests do not appear to come from the solar bugs
(those are fixed).  Hypotheses examined:

- **Ambient**: VM `builtin_ambient` returns 0.2 when no ambient lights exist — matches C
  matte behavior. Ruled out.
- **Specular formula**: VM `builtin_specular` and C `calculate_lights` use identical Phong
  formulas. Ruled out.
- **Solar axis transform**: For `matte` test, camera = `Translate 0 0 -3` with no rotation,
  so `world_to_camera * axis = axis` (identity rotation). Solar transform makes no change.
  Ruled out for `matte`.

### Open question

Are these differences:

**(a) Unavoidable FP precision differences** inherent in running the same math through a VM
(accumulates float ops differently) vs native C code, requiring reference image regeneration?

**(b) A remaining correctable bug** in the SL implementation?

A max_diff of 1–5 on silhouette/edge pixels strongly suggests (a) — these are pixels where
the interpolated value is near a rounding boundary.  max_diff=31 for the `rotate` test warrants
deeper investigation.

### Recommended next steps

1. **Batch-compare all shaders/ test failures**: For each failing test, capture the per-pixel
   max difference magnitude and differing pixel count.  Classify:
   - max_diff ≤ 2, few pixels → FP noise → regenerate reference image
   - max_diff > 10 or many pixels → likely a real bug → investigate

2. **Investigate `rotate` test** specifically (13 pixels, max_diff~31):  Run with debug output
   to compare SL vs C values for the specific differing pixels.  Check whether the transform
   applied to light direction in `rotate` scenario is correct.

3. **If noise-only**, ask user: **run `make generate-refs`** with the SL search path to
   regenerate reference images for these tests.

4. **Add solar unit tests**: The plan noted there are currently no unit tests for `solar` blocks
   in `tests/test_sl_codegen.c` or `tests/test_sl_vm.c`.  Tests should cover:
   - `solar(axis, 0)` — body executes, Cl is set, L = axis
   - `solar(axis, pi/4)` — cone check, only surfaces within cone lit
   - Correct L direction (camera space) after Bug 1 fix

---

## Key Architecture Notes for Context

- **Light shader call sites**: `eval_light_any()` in `rh_sl_vm.c` (called from surface
  shader VM via `builtin_diffuse`/`builtin_specular`) and `calculate_lights()` in
  `rh_shader.c` (called from C surface shaders).  Both must be kept in sync.

- **Coordinate spaces**: `ctx->P` = camera space, `ctx->P_world` = world space.  Light params
  (`from`, `to`) are transformed to camera space by `light_search_cb()` before being stored.
  Solar axis is world-space until `OP_SOLAR_BEGIN` transforms it.

- **`illuminate` blocks**: `Ps` in the VM = `ctx->P` = camera space.  `from` param = camera
  space.  `L = Ps - from` is therefore camera-space.  Do NOT change Ps to world space — this
  breaks `shadowspotlight` which has reference images generated with the current behavior.

- **Sign convention**: `L_out` from light shaders = **light-to-surface** (same direction
  light travels).  Both `eval_light_any` and `calculate_lights` negate it before the N·L dot
  product.
