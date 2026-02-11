#define _POSIX_C_SOURCE 200809L
/*
 * test_sl_vm.c -- Unit test for the Shading Language VM
 *
 * Hand-assembles a matte shader in bytecode, runs it via the VM, and
 * compares the output against the C matte shader (rh_shader_surface_matte).
 *
 * Build:  gcc -std=c99 -Wall -Wextra -Werror -pedantic -Iinclude -O2
 *         tests/test_sl_vm.c -Llib -lrh -lri -lm -o tests/test_sl_vm
 *
 * Run:    ./tests/test_sl_vm
 */

#include "rh_sl_vm.h"
#include "rh_sl_opcodes.h"
#include "rh_shader.h"
#include "ri_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/*  Test infrastructure                                                */
/* ------------------------------------------------------------------ */

static int tests_run = 0;
static int tests_passed = 0;

static void check_float(const char* name, float got, float expected, float tol) {
    tests_run++;
    float diff = fabsf(got - expected);
    if (diff <= tol) {
        tests_passed++;
    } else {
        printf("  FAIL: %s: got %.6f, expected %.6f (diff %.6f > tol %.6f)\n",
               name, got, expected, diff, tol);
    }
}

static void check_color(const char* name, RhColor got, RhColor expected, float tol) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s.r", name);
    check_float(buf, got.r, expected.r, tol);
    snprintf(buf, sizeof(buf), "%s.g", name);
    check_float(buf, got.g, expected.g, tol);
    snprintf(buf, sizeof(buf), "%s.b", name);
    check_float(buf, got.b, expected.b, tol);
}

/* ------------------------------------------------------------------ */
/*  Build a bytecode matte shader program                              */
/*                                                                     */
/*  RSL matte shader:                                                  */
/*    surface matte(float Ka = 1; float Kd = 1) {                     */
/*        normal Nf = faceforward(normalize(N), I);                   */
/*        Oi = Os;                                                    */
/*        Ci = Os * Cs * (Ka * ambient() + Kd * diffuse(Nf));         */
/*    }                                                                */
/* ------------------------------------------------------------------ */

static RhSLProgram* build_matte_program(void) {
    RhSLProgram* prog = rh_sl_program_create();

    prog->shader_type = RH_SL_SHADER_SURFACE;
    strncpy(prog->shader_name, "matte", sizeof(prog->shader_name) - 1);

    /* Parameters: Ka at register 51, Kd at register 52 */
    prog->num_params = 2;
    strncpy(prog->params[0].name, "Ka", sizeof(prog->params[0].name) - 1);
    prog->params[0].type = RH_SL_TYPE_FLOAT;
    prog->params[0].reg = R_PARAMS_START;      /* reg 51 */
    prog->params[0].default_idx = 0;           /* const_pool[0] = 1.0 */
    prog->params[0].num_components = 1;

    strncpy(prog->params[1].name, "Kd", sizeof(prog->params[1].name) - 1);
    prog->params[1].type = RH_SL_TYPE_FLOAT;
    prog->params[1].reg = R_PARAMS_START + 1;  /* reg 52 */
    prog->params[1].default_idx = 1;           /* const_pool[1] = 1.0 */
    prog->params[1].num_components = 1;

    /* Constant pool */
    prog->const_count = 2;
    prog->const_pool = malloc(sizeof(float) * 2);
    prog->const_pool[0] = 1.0f;  /* default Ka */
    prog->const_pool[1] = 1.0f;  /* default Kd */

    /* No strings */
    prog->string_count = 0;
    prog->string_table = NULL;

    /* Register assignments:
     * 0-50:  built-in globals (R_P through R_DPDV)
     * 51:    Ka (param)
     * 52:    Kd (param)
     * 53-55: Nf (temp tuple)
     * 56-58: amb (temp tuple) -- ambient() result
     * 59-61: diff (temp tuple) -- diffuse() result
     * 62-64: tmp1 (temp tuple)
     * 65-67: tmp2 (temp tuple)
     * 68-70: tmp3 (temp tuple)
     */
    prog->num_regs = 71;

    enum {
        R_KA   = R_PARAMS_START,      /* 51 */
        R_KD   = R_PARAMS_START + 1,  /* 52 */
        R_NF   = 53,                  /* 53-55 */
        R_AMB  = 56,                  /* 56-58 */
        R_DIFF = 59,                  /* 59-61 */
        R_TMP1 = 62,                  /* 62-64 */
        R_TMP2 = 65,                  /* 65-67 */
        R_TMP3 = 68                   /* 68-70 */
    };

    /*
     * Bytecode:
     *  0: NORMALIZE Nf, N          -- Nf = normalize(N)
     *  1: FACEFORWARD Nf, Nf, I    -- Nf = faceforward(Nf, I)
     *  2: AMBIENT amb              -- amb = ambient()
     *  3: DIFFUSE diff, Nf         -- diff = diffuse(Nf)
     *  4: VSMUL tmp1, amb, Ka      -- tmp1 = Ka * ambient
     *  5: VSMUL tmp2, diff, Kd     -- tmp2 = Kd * diffuse
     *  6: VADD tmp3, tmp1, tmp2    -- tmp3 = Ka*amb + Kd*diff
     *  7: VMUL tmp1, CS, tmp3      -- tmp1 = Cs * (Ka*amb + Kd*diff)
     *  8: VMOV OI, OS              -- Oi = Os
     *  9: VMUL CI, tmp1, OI        -- Ci = tmp1 * Oi (premultiply)
     * 10: HALT
     */
    prog->code_len = 11;
    prog->code = malloc(sizeof(uint64_t) * 11);

    prog->code[0]  = RH_SL_INSTR(OP_NORMALIZE, R_NF, R_N, 0);
    prog->code[1]  = RH_SL_INSTR(OP_FACEFORWARD, R_NF, R_NF, R_I);
    prog->code[2]  = RH_SL_INSTR(OP_AMBIENT, R_AMB, 0, 0);
    prog->code[3]  = RH_SL_INSTR(OP_DIFFUSE, R_DIFF, R_NF, 0);
    prog->code[4]  = RH_SL_INSTR(OP_VSMUL, R_TMP1, R_AMB, R_KA);
    prog->code[5]  = RH_SL_INSTR(OP_VSMUL, R_TMP2, R_DIFF, R_KD);
    prog->code[6]  = RH_SL_INSTR(OP_VADD, R_TMP3, R_TMP1, R_TMP2);
    prog->code[7]  = RH_SL_INSTR(OP_VMUL, R_TMP1, R_CS, R_TMP3);
    prog->code[8]  = RH_SL_INSTR(OP_VMOV, R_OI, R_OS, 0);
    prog->code[9]  = RH_SL_INSTR(OP_VMUL, R_CI, R_TMP1, R_OI);
    prog->code[10] = RH_SL_INSTR(OP_HALT, 0, 0, 0);

    return prog;
}

/* ------------------------------------------------------------------ */
/*  Test: matte shader with no lights (default ambient only)           */
/* ------------------------------------------------------------------ */

static void test_matte_no_lights(void) {
    printf("Test: matte shader with no lights...\n");

    RhSLProgram* prog = build_matte_program();
    RhSLShader* shader = rh_sl_shader_create(prog);

    /* Set up shader context */
    RhShaderContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.P = (RhVec3){0.0f, 0.0f, 5.0f};
    ctx.N = (RhVec3){0.0f, 0.0f, -1.0f};
    ctx.I = (RhVec3){0.0f, 0.0f, 1.0f};
    ctx.Cs = (RhColor){0.8f, 0.2f, 0.1f};
    ctx.Os = (RhColor){1.0f, 1.0f, 1.0f};
    ctx.light_list = NULL;
    ctx.num_lights = 0;

    /* Run VM shader */
    RhShaderContext vm_ctx = ctx;
    rh_sl_vm_shader_exec(&vm_ctx, shader);

    /* Run C shader */
    RhShaderContext c_ctx = ctx;
    RhMatteParams mp = {1.0f, 1.0f};
    rh_shader_surface_matte(&c_ctx, &mp);

    /* Compare */
    check_color("Ci", vm_ctx.Ci, c_ctx.Ci, 1e-5f);
    check_color("Oi", vm_ctx.Oi, c_ctx.Oi, 1e-5f);

    rh_sl_shader_free(shader);
    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test: matte shader with a distant light                            */
/* ------------------------------------------------------------------ */

static void test_matte_distant_light(void) {
    printf("Test: matte shader with distant light...\n");

    RhSLProgram* prog = build_matte_program();
    RhSLShader* shader = rh_sl_shader_create(prog);

    /* Create a distant light (matching RhLight layout from ri_internal.h) */
    RhLight light;
    memset(&light, 0, sizeof(light));
    strncpy(light.type, "distantlight", sizeof(light.type) - 1);
    light.direction = (RhVec3){0.0f, 0.0f, -1.0f}; /* shining toward camera */
    light.color = (RhColor){1.0f, 1.0f, 1.0f};
    light.intensity = 1.0f;

    RhShaderContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.P = (RhVec3){0.0f, 0.0f, 5.0f};
    ctx.N = (RhVec3){0.0f, 0.0f, -1.0f};
    ctx.I = (RhVec3){0.0f, 0.0f, 1.0f};
    ctx.Cs = (RhColor){0.8f, 0.2f, 0.1f};
    ctx.Os = (RhColor){1.0f, 1.0f, 1.0f};
    ctx.light_list = &light;
    ctx.num_lights = 1;

    /* Run VM shader */
    RhShaderContext vm_ctx = ctx;
    rh_sl_vm_shader_exec(&vm_ctx, shader);

    /* Run C shader */
    RhShaderContext c_ctx = ctx;
    RhMatteParams mp = {1.0f, 1.0f};
    rh_shader_surface_matte(&c_ctx, &mp);

    /* Compare */
    check_color("Ci", vm_ctx.Ci, c_ctx.Ci, 1e-5f);
    check_color("Oi", vm_ctx.Oi, c_ctx.Oi, 1e-5f);

    rh_sl_shader_free(shader);
    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test: matte shader with ambient + point light                      */
/* ------------------------------------------------------------------ */

static void test_matte_ambient_and_point(void) {
    printf("Test: matte shader with ambient + point light...\n");

    RhSLProgram* prog = build_matte_program();
    RhSLShader* shader = rh_sl_shader_create(prog);

    /* Two lights: ambient + point */
    RhLight lights[2];
    memset(lights, 0, sizeof(lights));

    strncpy(lights[0].type, "ambientlight", sizeof(lights[0].type) - 1);
    lights[0].color = (RhColor){0.3f, 0.3f, 0.3f};
    lights[0].intensity = 1.0f;

    strncpy(lights[1].type, "pointlight", sizeof(lights[1].type) - 1);
    lights[1].position = (RhVec3){2.0f, 3.0f, 0.0f};
    lights[1].color = (RhColor){1.0f, 0.9f, 0.8f};
    lights[1].intensity = 1.5f;

    RhShaderContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.P = (RhVec3){0.0f, 0.0f, 5.0f};
    ctx.N = (RhVec3){0.0f, 0.707f, -0.707f};
    ctx.I = (RhVec3){0.0f, 0.0f, 1.0f};
    ctx.Cs = (RhColor){0.5f, 0.5f, 0.5f};
    ctx.Os = (RhColor){1.0f, 1.0f, 1.0f};
    ctx.light_list = lights;
    ctx.num_lights = 2;

    /* Run VM shader */
    RhShaderContext vm_ctx = ctx;
    rh_sl_vm_shader_exec(&vm_ctx, shader);

    /* Run C shader */
    RhShaderContext c_ctx = ctx;
    RhMatteParams mp = {1.0f, 1.0f};
    rh_shader_surface_matte(&c_ctx, &mp);

    /* Compare */
    check_color("Ci", vm_ctx.Ci, c_ctx.Ci, 1e-5f);
    check_color("Oi", vm_ctx.Oi, c_ctx.Oi, 1e-5f);

    rh_sl_shader_free(shader);
    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test: matte shader with custom Ka/Kd parameters                    */
/* ------------------------------------------------------------------ */

static void test_matte_custom_params(void) {
    printf("Test: matte shader with custom Ka=0.3, Kd=0.7...\n");

    RhSLProgram* prog = build_matte_program();
    RhSLShader* shader = rh_sl_shader_create(prog);

    /* Override parameters */
    float ka = 0.3f;
    float kd = 0.7f;
    rh_sl_shader_set_param(shader, "Ka", &ka, 1);
    rh_sl_shader_set_param(shader, "Kd", &kd, 1);

    /* Distant light */
    RhLight light;
    memset(&light, 0, sizeof(light));
    strncpy(light.type, "distantlight", sizeof(light.type) - 1);
    light.direction = (RhVec3){0.577f, 0.577f, -0.577f};
    light.color = (RhColor){1.0f, 1.0f, 1.0f};
    light.intensity = 1.0f;

    RhShaderContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.P = (RhVec3){1.0f, 2.0f, 5.0f};
    ctx.N = (RhVec3){0.0f, 0.707f, -0.707f};
    ctx.I = (RhVec3){0.0f, 0.0f, 1.0f};
    ctx.Cs = (RhColor){1.0f, 0.0f, 0.0f};
    ctx.Os = (RhColor){1.0f, 1.0f, 1.0f};
    ctx.light_list = &light;
    ctx.num_lights = 1;

    /* Run VM shader */
    RhShaderContext vm_ctx = ctx;
    rh_sl_vm_shader_exec(&vm_ctx, shader);

    /* Run C shader */
    RhShaderContext c_ctx = ctx;
    RhMatteParams mp = {0.3f, 0.7f};
    rh_shader_surface_matte(&c_ctx, &mp);

    /* Compare */
    check_color("Ci", vm_ctx.Ci, c_ctx.Ci, 1e-5f);
    check_color("Oi", vm_ctx.Oi, c_ctx.Oi, 1e-5f);

    rh_sl_shader_free(shader);
    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test: VM arithmetic instructions                                   */
/* ------------------------------------------------------------------ */

static void test_arithmetic(void) {
    printf("Test: arithmetic instructions...\n");

    RhSLProgram* prog = rh_sl_program_create();
    prog->shader_type = RH_SL_SHADER_SURFACE;
    prog->num_params = 0;

    /* Constants: pool[0]=3.0, pool[1]=2.0, pool[2]=0.5 */
    prog->const_count = 3;
    prog->const_pool = malloc(sizeof(float) * 3);
    prog->const_pool[0] = 3.0f;
    prog->const_pool[1] = 2.0f;
    prog->const_pool[2] = 0.5f;

    /* Use registers starting at R_PARAMS_START (51) */
    enum {
        RA = R_PARAMS_START,      /* 51: a = 3.0 */
        RB = R_PARAMS_START + 1,  /* 52: b = 2.0 */
        RC = R_PARAMS_START + 2,  /* 53: c = 0.5 */
        RD = R_PARAMS_START + 3   /* 54: result */
    };
    prog->num_regs = RD + 1;

    prog->code_len = 8;
    prog->code = malloc(sizeof(uint64_t) * 8);
    prog->code[0] = RH_SL_INSTR(OP_FCONST, RA, 0, 0);   /* a = 3.0 */
    prog->code[1] = RH_SL_INSTR(OP_FCONST, RB, 1, 0);   /* b = 2.0 */
    prog->code[2] = RH_SL_INSTR(OP_FCONST, RC, 2, 0);   /* c = 0.5 */
    prog->code[3] = RH_SL_INSTR(OP_FADD, RD, RA, RB);   /* d = 3+2 = 5 */
    prog->code[4] = RH_SL_INSTR(OP_FMUL, RD, RD, RC);   /* d = 5*0.5 = 2.5 */
    prog->code[5] = RH_SL_INSTR(OP_FSQRT, RD, RD, 0);   /* d = sqrt(2.5) */
    prog->code[6] = RH_SL_INSTR(OP_FMOV, R_CI, RD, 0);  /* Ci.r = d */
    prog->code[7] = RH_SL_INSTR(OP_HALT, 0, 0, 0);

    RhSLShader* shader = rh_sl_shader_create(prog);

    RhShaderContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.Os = (RhColor){1, 1, 1};

    rh_sl_vm_shader_exec(&ctx, shader);

    check_float("Ci.r (sqrt(2.5))", ctx.Ci.r, sqrtf(2.5f), 1e-6f);

    rh_sl_shader_free(shader);
    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test: control flow (if/else)                                       */
/* ------------------------------------------------------------------ */

static void test_control_flow(void) {
    printf("Test: control flow (if/else)...\n");

    RhSLProgram* prog = rh_sl_program_create();
    prog->shader_type = RH_SL_SHADER_SURFACE;
    prog->num_params = 0;

    /* Constants: pool[0]=1.0, pool[1]=0.0, pool[2]=0.5, pool[3]=0.25 */
    prog->const_count = 4;
    prog->const_pool = malloc(sizeof(float) * 4);
    prog->const_pool[0] = 1.0f;
    prog->const_pool[1] = 0.0f;
    prog->const_pool[2] = 0.5f;
    prog->const_pool[3] = 0.25f;

    enum {
        RX = R_PARAMS_START,      /* 51 */
        RY = R_PARAMS_START + 1,  /* 52 */
        RCOND = R_PARAMS_START + 2, /* 53 */
        RZERO = R_PARAMS_START + 3  /* 54 */
    };
    prog->num_regs = RZERO + 1;

    /*
     *  Test: x = 0.5;  if (x > 0) { y = 1.0 } else { y = 0.25 }
     *  Expected: y = 1.0
     */
    prog->code_len = 8;
    prog->code = malloc(sizeof(uint64_t) * 8);
    prog->code[0] = RH_SL_INSTR(OP_FCONST, RX, 2, 0);        /* x = 0.5 */
    prog->code[1] = RH_SL_INSTR(OP_FCONST, RZERO, 1, 0);     /* zero = 0.0 */
    prog->code[2] = RH_SL_INSTR(OP_FGT, RCOND, RX, RZERO);   /* cond = (x > 0) */
    prog->code[3] = RH_SL_INSTR(OP_JUMP_IFNOT, 6, RCOND, 0); /* if !cond goto 6 */
    prog->code[4] = RH_SL_INSTR(OP_FCONST, RY, 0, 0);        /* y = 1.0 */
    prog->code[5] = RH_SL_INSTR(OP_JUMP, 7, 0, 0);           /* goto 7 */
    prog->code[6] = RH_SL_INSTR(OP_FCONST, RY, 3, 0);        /* y = 0.25 */
    prog->code[7] = RH_SL_INSTR(OP_HALT, 0, 0, 0);

    RhSLShader* shader = rh_sl_shader_create(prog);

    RhShaderContext ctx;
    memset(&ctx, 0, sizeof(ctx));

    /* We need to read y back. Put it into Ci.r */
    /* Actually, let's just add one more instruction to copy y to Ci */
    /* Easier: rebuild with the copy */
    rh_sl_shader_free(shader);
    free(prog->code);

    prog->code_len = 9;
    prog->code = malloc(sizeof(uint64_t) * 9);
    prog->code[0] = RH_SL_INSTR(OP_FCONST, RX, 2, 0);        /* x = 0.5 */
    prog->code[1] = RH_SL_INSTR(OP_FCONST, RZERO, 1, 0);     /* zero = 0.0 */
    prog->code[2] = RH_SL_INSTR(OP_FGT, RCOND, RX, RZERO);   /* cond = (x > 0) */
    prog->code[3] = RH_SL_INSTR(OP_JUMP_IFNOT, 6, RCOND, 0); /* if !cond goto 6 */
    prog->code[4] = RH_SL_INSTR(OP_FCONST, RY, 0, 0);        /* y = 1.0 */
    prog->code[5] = RH_SL_INSTR(OP_JUMP, 7, 0, 0);           /* goto 7 */
    prog->code[6] = RH_SL_INSTR(OP_FCONST, RY, 3, 0);        /* y = 0.25 */
    prog->code[7] = RH_SL_INSTR(OP_FMOV, R_CI, RY, 0);       /* Ci.r = y */
    prog->code[8] = RH_SL_INSTR(OP_HALT, 0, 0, 0);

    shader = rh_sl_shader_create(prog);
    rh_sl_vm_shader_exec(&ctx, shader);

    check_float("y (should be 1.0)", ctx.Ci.r, 1.0f, 1e-6f);

    rh_sl_shader_free(shader);
    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test: tuple operations (normalize, dot, cross)                     */
/* ------------------------------------------------------------------ */

static void test_tuple_ops(void) {
    printf("Test: tuple operations...\n");

    RhSLProgram* prog = rh_sl_program_create();
    prog->shader_type = RH_SL_SHADER_SURFACE;
    prog->num_params = 0;

    /* Constants: pool[0..2] = (3, 0, 4) for a vector of length 5 */
    prog->const_count = 3;
    prog->const_pool = malloc(sizeof(float) * 3);
    prog->const_pool[0] = 3.0f;
    prog->const_pool[1] = 0.0f;
    prog->const_pool[2] = 4.0f;

    enum {
        RV1 = R_PARAMS_START,        /* 51-53: input vector */
        RV2 = R_PARAMS_START + 3,    /* 54-56: normalized */
        RLEN = R_PARAMS_START + 6    /* 57: length */
    };
    prog->num_regs = RLEN + 1;

    prog->code_len = 5;
    prog->code = malloc(sizeof(uint64_t) * 5);
    prog->code[0] = RH_SL_INSTR(OP_VCONST, RV1, 0, 0);     /* v1 = (3,0,4) */
    prog->code[1] = RH_SL_INSTR(OP_LENGTH, RLEN, RV1, 0);   /* len = 5.0 */
    prog->code[2] = RH_SL_INSTR(OP_NORMALIZE, RV2, RV1, 0); /* v2 = (0.6, 0, 0.8) */
    prog->code[3] = RH_SL_INSTR(OP_FMOV, R_CI, RLEN, 0);   /* Ci.r = len */
    prog->code[4] = RH_SL_INSTR(OP_HALT, 0, 0, 0);

    RhSLShader* shader = rh_sl_shader_create(prog);

    RhShaderContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    rh_sl_vm_shader_exec(&ctx, shader);

    check_float("length((3,0,4))", ctx.Ci.r, 5.0f, 1e-5f);

    /* To check normalize, we need to read the register file directly.
     * Instead, let's make a second program that writes the normalized
     * components to Ci. */
    rh_sl_shader_free(shader);
    free(prog->code);

    prog->code_len = 6;
    prog->code = malloc(sizeof(uint64_t) * 6);
    prog->code[0] = RH_SL_INSTR(OP_VCONST, RV1, 0, 0);
    prog->code[1] = RH_SL_INSTR(OP_NORMALIZE, RV2, RV1, 0);
    prog->code[2] = RH_SL_INSTR(OP_VMOV, R_CI, RV2, 0);    /* Ci = normalized */
    prog->code[3] = RH_SL_INSTR(OP_HALT, 0, 0, 0);

    /* (only need 4 instructions) */
    prog->code_len = 4;

    shader = rh_sl_shader_create(prog);
    memset(&ctx, 0, sizeof(ctx));
    rh_sl_vm_shader_exec(&ctx, shader);

    check_float("normalize((3,0,4)).r", ctx.Ci.r, 0.6f, 1e-5f);
    check_float("normalize((3,0,4)).g", ctx.Ci.g, 0.0f, 1e-5f);
    check_float("normalize((3,0,4)).b", ctx.Ci.b, 0.8f, 1e-5f);

    rh_sl_shader_free(shader);
    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== Shading Language VM Tests ===\n\n");

    test_arithmetic();
    test_control_flow();
    test_tuple_ops();
    test_matte_no_lights();
    test_matte_distant_light();
    test_matte_ambient_and_point();
    test_matte_custom_params();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
