#ifndef RH_SL_VM_H
#define RH_SL_VM_H

#include "rh_sl_opcodes.h"
#include "rh_shader.h"

/*
 * Shading Language Virtual Machine
 *
 * Per-vertex execution model: the VM executes the full shader program for
 * a single shading point.  Registers hold scalar floats; tuple types occupy
 * 3 consecutive register slots.  Control flow uses conventional jumps.
 *
 * The VM executor has the RhShaderFunc signature and plugs directly into
 * the existing per-vertex shading loop in ri_render.c.
 */

/* Maximum number of shader parameters */
#define RH_SL_MAX_PARAMS  32

/* --- Compiled shader program (shared across all invocations) --- */

typedef struct {
    char    name[64];     /* Parameter name */
    int     type;         /* RH_SL_TYPE_FLOAT, _COLOR, etc. */
    int     reg;          /* Register index */
    int     default_idx;  /* Index into const_pool for default value */
    int     num_components; /* 1 for float, 3 for color/point/vector/normal */
} RhSLParamInfo;

typedef struct {
    /* Bytecode program */
    uint64_t*   code;         /* Instruction array */
    int         code_len;     /* Number of instructions */

    /* Constant pool */
    float*      const_pool;   /* Float constants referenced by FCONST/VCONST */
    int         const_count;  /* Number of floats in pool */

    /* String table */
    char**      string_table; /* Strings (texture names, etc.) */
    int         string_count;

    /* Register requirements */
    int         num_regs;     /* Total registers needed */

    /* Shader metadata */
    int         shader_type;  /* RH_SL_SHADER_SURFACE, etc. */
    char        shader_name[64];

    /* Parameter metadata */
    RhSLParamInfo params[RH_SL_MAX_PARAMS];
    int         num_params;
} RhSLProgram;

/* --- Per-instance shader (holds overridden parameter values) --- */

typedef struct {
    RhSLProgram* program;       /* Compiled bytecode (shared) */
    float*       param_values;  /* Instance parameter values (or NULL for defaults) */
    int          num_params;
    void**       textures;      /* Pre-loaded texture handles */
    int          num_textures;
    char**       string_overrides;     /* Per-instance string overrides (indexed by string table slot) */
    int          num_string_overrides;
} RhSLShader;

/* --- Per-invocation execution state (stack-allocated) --- */

typedef struct {
    float*    regs;             /* Register file */
    int       pc;               /* Program counter */

    /* Call stack for user function calls */
    int       call_stack[RH_SL_MAX_CALL_DEPTH];
    int       call_sp;

    /* Light iteration state */
    int       current_light;
    int       num_lights;
    void*     light_list;

    /* Back-reference to shader context (for shadow lookups, etc.) */
    RhShaderContext* shader_ctx;

    /* Back-reference to shader instance (for texture caching) */
    void* shader;
} RhSLExecState;

/* --- API --- */

/* Create a program from code, constants, and metadata.
 * Takes ownership of the code and const_pool arrays (caller must not free). */
RhSLProgram* rh_sl_program_create(void);
void         rh_sl_program_free(RhSLProgram* prog);

/* Create a shader instance from a compiled program.
 * The program is shared and must outlive the shader. */
RhSLShader*  rh_sl_shader_create(RhSLProgram* program);
void         rh_sl_shader_free(RhSLShader* shader);

/* Set an instance parameter value (overriding the compiled default).
 * name: parameter name, value: pointer to float(s), count: number of floats. */
int rh_sl_shader_set_param(RhSLShader* shader, const char* name,
                           const float* value, int count);

/* Set a string parameter override for texture/shadow lookups.
 * Finds the param by name, determines its string table slot, and stores
 * the override string. Returns 0 on success, -1 if not found. */
int rh_sl_shader_set_string_param(RhSLShader* shader, const char* name,
                                  const char* value);

/* Execute the VM program for a single shading point.
 * This has the RhShaderFunc signature: void(RhShaderContext*, void*).
 * The void* params argument must point to an RhSLShader. */
void rh_sl_vm_shader_exec(RhShaderContext* ctx, void* params);

/* Low-level: execute bytecode with a pre-initialized state.
 * Normally called internally by rh_sl_vm_shader_exec(). */
void rh_sl_vm_execute(const RhSLProgram* program, RhSLExecState* state);

#endif /* RH_SL_VM_H */
