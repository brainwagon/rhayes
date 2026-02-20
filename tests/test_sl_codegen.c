#define _POSIX_C_SOURCE 200809L
/*
 * test_sl_codegen.c -- Unit tests for the shading language code generator.
 *
 * Compiles RSL source through lex -> parse -> sema -> codegen,
 * then executes via the VM and checks outputs.
 *
 * Build:  gcc -std=c99 -Wall -Wextra -Werror -pedantic -D_POSIX_C_SOURCE=200809L
 *         -Iinclude -O2 tests/test_sl_codegen.c -Llib -lrh -lri -lm
 *         -o tests/test_sl_codegen
 *
 * Run:    ./tests/test_sl_codegen
 */

#include "rh_sl_codegen.h"
#include "rh_sl_parse.h"
#include "rh_sl_sema.h"
#include "rh_sl_vm.h"
#include "rh_sl_opcodes.h"
#include "rh_shader.h"
#include "ri_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Safe string copy (no strncpy truncation warnings) */
static void sl_strcpy(char* dst, size_t dstsz, const char* src) {
    size_t len = strlen(src);
    if (len >= dstsz) len = dstsz - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* ------------------------------------------------------------------ */
/*  Test infrastructure                                                */
/* ------------------------------------------------------------------ */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TOL 1e-4f

static void check_float(const char* test, const char* name,
                         float got, float expected, float tol) {
    tests_run++;
    float diff = fabsf(got - expected);
    if (diff <= tol) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL [%s] %s: got %.6f, expected %.6f (diff %.6f)\n",
               test, name, got, expected, diff);
    }
}

static void check_color(const char* test, const char* name,
                         RhColor got, RhColor expected, float tol) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s.r", name);
    check_float(test, buf, got.r, expected.r, tol);
    snprintf(buf, sizeof(buf), "%s.g", name);
    check_float(test, buf, got.g, expected.g, tol);
    snprintf(buf, sizeof(buf), "%s.b", name);
    check_float(test, buf, got.b, expected.b, tol);
}

/* ------------------------------------------------------------------ */
/*  Helper: compile shader source to RhSLProgram                       */
/* ------------------------------------------------------------------ */

static RhSLProgram* compile_shader(const char* src) {
    RhSLParser parser;
    rh_sl_parse_init(&parser, src);
    RhSLNode* ast = rh_sl_parse(&parser);
    if (!ast || parser.num_errors > 0) {
        if (parser.num_errors > 0) {
            printf("    Parse error: %s\n", parser.errors[0]);
        }
        if (ast) rh_sl_node_free(ast);
        return NULL;
    }

    RhSLSema sema;
    rh_sl_sema_init(&sema);
    if (rh_sl_sema_analyze(&sema, ast) != 0) {
        printf("    Sema error: %s\n", sema.errors[0]);
        rh_sl_node_free(ast);
        return NULL;
    }

    RhSLCodegenErrors errs;
    RhSLProgram* prog = rh_sl_codegen(ast, &errs);
    if (!prog && errs.num_errors > 0) {
        printf("    Codegen error: %s\n", errs.errors[0]);
    }

    rh_sl_node_free(ast);
    return prog;
}

/* Helper: set up a default shader context */
static void init_ctx(RhShaderContext* ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->P = (RhVec3){0.0f, 0.0f, 5.0f};
    ctx->N = (RhVec3){0.0f, 0.0f, -1.0f};
    ctx->I = (RhVec3){0.0f, 0.0f, 1.0f};
    ctx->Cs = (RhColor){0.8f, 0.2f, 0.1f};
    ctx->Os = (RhColor){1.0f, 1.0f, 1.0f};
    ctx->u = 0.5f;
    ctx->v = 0.5f;
    ctx->du = 0.01f;
    ctx->dv = 0.01f;
}

/* Helper: compile, create shader, execute, return Ci/Oi */
static int run_shader(const char* src, RhShaderContext* ctx,
                      RhLight* lights, int num_lights) {
    RhSLProgram* prog = compile_shader(src);
    if (!prog) return -1;

    RhSLShader* shader = rh_sl_shader_create(prog);
    ctx->light_list = lights;
    ctx->num_lights = num_lights;
    rh_sl_vm_shader_exec(ctx, shader);
    rh_sl_shader_free(shader);
    rh_sl_program_free(prog);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test: empty shader                                                 */
/* ------------------------------------------------------------------ */

static void test_empty_shader(void) {
    const char* src =
        "surface empty() { }";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [empty_shader] compilation failed\n");
        return;
    }
    /* Ci should remain 0,0,0 (default) and Oi 1,1,1 */
    check_color("empty_shader", "Ci", ctx.Ci, (RhColor){0, 0, 0}, TOL);
    check_color("empty_shader", "Oi", ctx.Oi, (RhColor){1, 1, 1}, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: constant color assignment                                    */
/* ------------------------------------------------------------------ */

static void test_constant_assign(void) {
    const char* src =
        "surface red() {\n"
        "    Ci = color(1, 0, 0);\n"
        "    Oi = Os;\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [constant_assign] compilation failed\n");
        return;
    }
    check_color("constant_assign", "Ci", ctx.Ci, (RhColor){1, 0, 0}, TOL);
    check_color("constant_assign", "Oi", ctx.Oi, (RhColor){1, 1, 1}, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: float literal assignment                                     */
/* ------------------------------------------------------------------ */

static void test_float_literal(void) {
    const char* src =
        "surface flit() {\n"
        "    Ci = color(0.5, 0.5, 0.5);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [float_literal] compilation failed\n");
        return;
    }
    check_color("float_literal", "Ci", ctx.Ci, (RhColor){0.5f, 0.5f, 0.5f}, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: Cs passthrough (variable reference)                          */
/* ------------------------------------------------------------------ */

static void test_cs_passthrough(void) {
    const char* src =
        "surface pass() {\n"
        "    Ci = Cs;\n"
        "    Oi = Os;\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [cs_passthrough] compilation failed\n");
        return;
    }
    check_color("cs_passthrough", "Ci", ctx.Ci, ctx.Cs, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: float arithmetic                                             */
/* ------------------------------------------------------------------ */

static void test_float_arith(void) {
    const char* src =
        "surface arith() {\n"
        "    float a = 3.0;\n"
        "    float b = 2.0;\n"
        "    float c = (a + b) * 0.5;\n"
        "    Ci = color(c, a - b, a / b);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [float_arith] compilation failed\n");
        return;
    }
    check_float("float_arith", "Ci.r", ctx.Ci.r, 2.5f, TOL);
    check_float("float_arith", "Ci.g", ctx.Ci.g, 1.0f, TOL);
    check_float("float_arith", "Ci.b", ctx.Ci.b, 1.5f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: tuple arithmetic                                             */
/* ------------------------------------------------------------------ */

static void test_tuple_arith(void) {
    const char* src =
        "surface tuparith() {\n"
        "    color a = color(1, 2, 3);\n"
        "    color b = color(0.5, 0.5, 0.5);\n"
        "    Ci = a * b;\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [tuple_arith] compilation failed\n");
        return;
    }
    check_color("tuple_arith", "Ci", ctx.Ci, (RhColor){0.5f, 1.0f, 1.5f}, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: scalar * tuple promotion                                     */
/* ------------------------------------------------------------------ */

static void test_scalar_tuple_mul(void) {
    const char* src =
        "surface stmul() {\n"
        "    color a = color(1, 2, 3);\n"
        "    Ci = a * 0.5;\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [scalar_tuple_mul] compilation failed\n");
        return;
    }
    check_color("scalar_tuple_mul", "Ci", ctx.Ci, (RhColor){0.5f, 1.0f, 1.5f}, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: float * tuple (left operand float)                           */
/* ------------------------------------------------------------------ */

static void test_float_times_tuple(void) {
    const char* src =
        "surface ftmul() {\n"
        "    color a = color(1, 2, 3);\n"
        "    Ci = 0.5 * a;\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [float_times_tuple] compilation failed\n");
        return;
    }
    check_color("float_times_tuple", "Ci", ctx.Ci, (RhColor){0.5f, 1.0f, 1.5f}, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: compound assignment                                          */
/* ------------------------------------------------------------------ */

static void test_compound_assign(void) {
    const char* src =
        "surface cmpd() {\n"
        "    float x = 1.0;\n"
        "    x += 2.0;\n"
        "    x *= 3.0;\n"
        "    Ci = color(x, x, x);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [compound_assign] compilation failed\n");
        return;
    }
    /* (1 + 2) * 3 = 9 */
    check_float("compound_assign", "Ci.r", ctx.Ci.r, 9.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: negation (float and tuple)                                   */
/* ------------------------------------------------------------------ */

static void test_negation(void) {
    const char* src =
        "surface neg() {\n"
        "    color c = -color(1, 2, 3);\n"
        "    Ci = c;\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [negation] compilation failed\n");
        return;
    }
    check_color("negation", "Ci", ctx.Ci, (RhColor){-1.0f, -2.0f, -3.0f}, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: if/else                                                      */
/* ------------------------------------------------------------------ */

static void test_if_else(void) {
    const char* src =
        "surface ifelse() {\n"
        "    float x = 1.0;\n"
        "    float y;\n"
        "    if (x > 0) {\n"
        "        y = 1.0;\n"
        "    } else {\n"
        "        y = 0.0;\n"
        "    }\n"
        "    Ci = color(y, y, y);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [if_else] compilation failed\n");
        return;
    }
    check_float("if_else", "Ci.r", ctx.Ci.r, 1.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: if without else (false branch)                               */
/* ------------------------------------------------------------------ */

static void test_if_no_else(void) {
    const char* src =
        "surface ifnoelse() {\n"
        "    float y = 0.0;\n"
        "    if (0) {\n"
        "        y = 1.0;\n"
        "    }\n"
        "    Ci = color(y, y, y);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [if_no_else] compilation failed\n");
        return;
    }
    check_float("if_no_else", "Ci.r", ctx.Ci.r, 0.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: while loop                                                   */
/* ------------------------------------------------------------------ */

static void test_while_loop(void) {
    const char* src =
        "surface whl() {\n"
        "    float sum = 0;\n"
        "    float i = 0;\n"
        "    while (i < 5) {\n"
        "        sum += 1;\n"
        "        i += 1;\n"
        "    }\n"
        "    Ci = color(sum, sum, sum);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [while_loop] compilation failed\n");
        return;
    }
    check_float("while_loop", "Ci.r", ctx.Ci.r, 5.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: for loop                                                     */
/* ------------------------------------------------------------------ */

static void test_for_loop(void) {
    const char* src =
        "surface forl() {\n"
        "    float sum = 0;\n"
        "    float i;\n"
        "    for (i = 0; i < 4; i += 1) {\n"
        "        sum += i;\n"
        "    }\n"
        "    Ci = color(sum, sum, sum);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [for_loop] compilation failed\n");
        return;
    }
    /* 0+1+2+3 = 6 */
    check_float("for_loop", "Ci.r", ctx.Ci.r, 6.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: break                                                        */
/* ------------------------------------------------------------------ */

static void test_break(void) {
    const char* src =
        "surface brk() {\n"
        "    float sum = 0;\n"
        "    float i = 0;\n"
        "    while (i < 10) {\n"
        "        if (i >= 3) break;\n"
        "        sum += 1;\n"
        "        i += 1;\n"
        "    }\n"
        "    Ci = color(sum, sum, sum);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [break] compilation failed\n");
        return;
    }
    check_float("break", "Ci.r", ctx.Ci.r, 3.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: continue                                                     */
/* ------------------------------------------------------------------ */

static void test_continue(void) {
    const char* src =
        "surface cont() {\n"
        "    float sum = 0;\n"
        "    float i;\n"
        "    for (i = 0; i < 6; i += 1) {\n"
        "        if (i == 2) continue;\n"
        "        if (i == 4) continue;\n"
        "        sum += 1;\n"
        "    }\n"
        "    Ci = color(sum, sum, sum);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [continue] compilation failed\n");
        return;
    }
    /* iterations 0,1,3,5 contribute -> sum=4 */
    check_float("continue", "Ci.r", ctx.Ci.r, 4.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: nested loops                                                 */
/* ------------------------------------------------------------------ */

static void test_nested_loops(void) {
    const char* src =
        "surface nested() {\n"
        "    float sum = 0;\n"
        "    float i;\n"
        "    float j;\n"
        "    for (i = 0; i < 3; i += 1) {\n"
        "        for (j = 0; j < 3; j += 1) {\n"
        "            sum += 1;\n"
        "        }\n"
        "    }\n"
        "    Ci = color(sum, sum, sum);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [nested_loops] compilation failed\n");
        return;
    }
    check_float("nested_loops", "Ci.r", ctx.Ci.r, 9.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: normalize                                                    */
/* ------------------------------------------------------------------ */

static void test_normalize(void) {
    const char* src =
        "surface norm() {\n"
        "    vector v = vector(3, 0, 4);\n"
        "    vector n = normalize(v);\n"
        "    Ci = color(n);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [normalize] compilation failed\n");
        return;
    }
    check_float("normalize", "Ci.r", ctx.Ci.r, 0.6f, TOL);
    check_float("normalize", "Ci.g", ctx.Ci.g, 0.0f, TOL);
    check_float("normalize", "Ci.b", ctx.Ci.b, 0.8f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: dot product                                                  */
/* ------------------------------------------------------------------ */

static void test_dot(void) {
    const char* src =
        "surface dottest() {\n"
        "    vector a = vector(1, 0, 0);\n"
        "    vector b = vector(0, 1, 0);\n"
        "    float d = a . b;\n"
        "    Ci = color(d, d, d);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [dot] compilation failed\n");
        return;
    }
    check_float("dot", "Ci.r", ctx.Ci.r, 0.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: dot function call                                            */
/* ------------------------------------------------------------------ */

static void test_dot_func(void) {
    const char* src =
        "surface dotf() {\n"
        "    vector a = vector(1, 2, 3);\n"
        "    vector b = vector(4, 5, 6);\n"
        "    float d = dot(a, b);\n"
        "    Ci = color(d, d, d);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [dot_func] compilation failed\n");
        return;
    }
    /* 1*4 + 2*5 + 3*6 = 32 */
    check_float("dot_func", "Ci.r", ctx.Ci.r, 32.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: length                                                       */
/* ------------------------------------------------------------------ */

static void test_length(void) {
    const char* src =
        "surface lentest() {\n"
        "    vector v = vector(3, 4, 0);\n"
        "    float l = length(v);\n"
        "    Ci = color(l, l, l);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [length] compilation failed\n");
        return;
    }
    check_float("length", "Ci.r", ctx.Ci.r, 5.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: faceforward                                                  */
/* ------------------------------------------------------------------ */

static void test_faceforward(void) {
    const char* src =
        "surface ff() {\n"
        "    normal Nf = faceforward(normalize(N), I);\n"
        "    Ci = color(Nf);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    /* N = (0,0,-1), I = (0,0,1)
     * dot(N, I) = -1 < 0, so faceforward returns N as-is */
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [faceforward] compilation failed\n");
        return;
    }
    check_float("faceforward", "Ci.r", ctx.Ci.r, 0.0f, TOL);
    check_float("faceforward", "Ci.g", ctx.Ci.g, 0.0f, TOL);
    check_float("faceforward", "Ci.b", ctx.Ci.b, -1.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: type constructor splat: color(0.5)                           */
/* ------------------------------------------------------------------ */

static void test_type_constructor_splat(void) {
    const char* src =
        "surface splat() {\n"
        "    Ci = color(0.5);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [type_constructor_splat] compilation failed\n");
        return;
    }
    check_color("type_constructor_splat", "Ci", ctx.Ci,
                (RhColor){0.5f, 0.5f, 0.5f}, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: type constructor 3-arg: color(r, g, b)                       */
/* ------------------------------------------------------------------ */

static void test_type_constructor_3arg(void) {
    const char* src =
        "surface tc3() {\n"
        "    Ci = color(0.1, 0.2, 0.3);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [type_constructor_3arg] compilation failed\n");
        return;
    }
    check_color("type_constructor_3arg", "Ci", ctx.Ci,
                (RhColor){0.1f, 0.2f, 0.3f}, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: shader parameter defaults                                    */
/* ------------------------------------------------------------------ */

static void test_param_defaults(void) {
    const char* src =
        "surface pdefault(float Ka = 0.5; float Kd = 0.7) {\n"
        "    Ci = color(Ka, Kd, Ka + Kd);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [param_defaults] compilation failed\n");
        return;
    }
    check_float("param_defaults", "Ci.r", ctx.Ci.r, 0.5f, TOL);
    check_float("param_defaults", "Ci.g", ctx.Ci.g, 0.7f, TOL);
    check_float("param_defaults", "Ci.b", ctx.Ci.b, 1.2f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: parameter override                                           */
/* ------------------------------------------------------------------ */

static void test_param_override(void) {
    const char* src =
        "surface pover(float Ka = 1; float Kd = 1) {\n"
        "    Ci = color(Ka, Kd, Ka + Kd);\n"
        "}";
    RhSLProgram* prog = compile_shader(src);
    if (!prog) {
        tests_run++; tests_failed++;
        printf("  FAIL [param_override] compilation failed\n");
        return;
    }

    RhSLShader* shader = rh_sl_shader_create(prog);
    float ka = 0.3f;
    float kd = 0.7f;
    rh_sl_shader_set_param(shader, "Ka", &ka, 1);
    rh_sl_shader_set_param(shader, "Kd", &kd, 1);

    RhShaderContext ctx;
    init_ctx(&ctx);
    rh_sl_vm_shader_exec(&ctx, shader);

    check_float("param_override", "Ci.r", ctx.Ci.r, 0.3f, TOL);
    check_float("param_override", "Ci.g", ctx.Ci.g, 0.7f, TOL);
    check_float("param_override", "Ci.b", ctx.Ci.b, 1.0f, TOL);

    rh_sl_shader_free(shader);
    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test: color parameter default                                      */
/* ------------------------------------------------------------------ */

static void test_color_param(void) {
    const char* src =
        "surface cparam(color tint = color(1, 0, 0)) {\n"
        "    Ci = Cs * tint;\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [color_param] compilation failed\n");
        return;
    }
    /* Cs=(0.8, 0.2, 0.1) * tint=(1,0,0) = (0.8, 0, 0) */
    check_float("color_param", "Ci.r", ctx.Ci.r, 0.8f, TOL);
    check_float("color_param", "Ci.g", ctx.Ci.g, 0.0f, TOL);
    check_float("color_param", "Ci.b", ctx.Ci.b, 0.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: ambient() function (no lights -> default 0.2)                */
/* ------------------------------------------------------------------ */

static void test_ambient_no_lights(void) {
    const char* src =
        "surface amb() {\n"
        "    Ci = ambient();\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [ambient_no_lights] compilation failed\n");
        return;
    }
    check_color("ambient_no_lights", "Ci", ctx.Ci,
                (RhColor){0.2f, 0.2f, 0.2f}, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: ambient() with ambient light                                 */
/* ------------------------------------------------------------------ */

static void test_ambient_with_light(void) {
    const char* src =
        "surface amblight() {\n"
        "    Ci = ambient();\n"
        "}";
    RhLight light;
    memset(&light, 0, sizeof(light));
    sl_strcpy(light.type, sizeof(light.type), "ambientlight");
    light.color = (RhColor){0.5f, 0.5f, 0.5f};
    light.intensity = 1.0f;

    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, &light, 1) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [ambient_with_light] compilation failed\n");
        return;
    }
    check_color("ambient_with_light", "Ci", ctx.Ci,
                (RhColor){0.5f, 0.5f, 0.5f}, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: diffuse() with distant light                                 */
/* ------------------------------------------------------------------ */

static void test_diffuse_distant(void) {
    const char* src =
        "surface diff() {\n"
        "    normal Nf = faceforward(normalize(N), I);\n"
        "    Ci = diffuse(Nf);\n"
        "}";
    RhLight light;
    memset(&light, 0, sizeof(light));
    sl_strcpy(light.type, sizeof(light.type), "distantlight");
    light.direction = (RhVec3){0.0f, 0.0f, -1.0f};
    light.color = (RhColor){1.0f, 1.0f, 1.0f};
    light.intensity = 1.0f;

    RhShaderContext ctx;
    init_ctx(&ctx);
    /* N=(0,0,-1), I=(0,0,1), Nf=faceforward(N,I) = N since dot(N,I)<0
     * L=(0,0,-1), dot(Nf, L) = (-1)(-1)=1 */
    if (run_shader(src, &ctx, &light, 1) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [diffuse_distant] compilation failed\n");
        return;
    }
    /* diffuse = Cl * dot(Nf, L) = (1,1,1) * 1 = (1,1,1) */
    check_color("diffuse_distant", "Ci", ctx.Ci,
                (RhColor){1.0f, 1.0f, 1.0f}, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: specular()                                                   */
/* ------------------------------------------------------------------ */

static void test_specular(void) {
    const char* src =
        "surface spec() {\n"
        "    normal Nf = faceforward(normalize(N), I);\n"
        "    vector V = -normalize(I);\n"
        "    Ci = specular(Nf, V, 0.1);\n"
        "}";
    RhLight light;
    memset(&light, 0, sizeof(light));
    sl_strcpy(light.type, sizeof(light.type), "distantlight");
    light.direction = (RhVec3){0.0f, 0.0f, -1.0f};
    light.color = (RhColor){1.0f, 1.0f, 1.0f};
    light.intensity = 1.0f;

    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, &light, 1) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [specular] compilation failed\n");
        return;
    }
    /* Specular should be nonzero for head-on light */
    tests_run++;
    if (ctx.Ci.r > 0.0f) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL [specular] expected nonzero specular, got %.6f\n", ctx.Ci.r);
    }
}

/* ------------------------------------------------------------------ */
/*  Test: ternary expression                                           */
/* ------------------------------------------------------------------ */

static void test_ternary(void) {
    const char* src =
        "surface tern() {\n"
        "    float x = 1;\n"
        "    float y = x > 0 ? 10 : 20;\n"
        "    Ci = color(y, y, y);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [ternary] compilation failed\n");
        return;
    }
    check_float("ternary", "Ci.r", ctx.Ci.r, 10.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: ternary false branch                                         */
/* ------------------------------------------------------------------ */

static void test_ternary_false(void) {
    const char* src =
        "surface ternf() {\n"
        "    float x = 0;\n"
        "    float y = x > 0 ? 10 : 20;\n"
        "    Ci = color(y, y, y);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [ternary_false] compilation failed\n");
        return;
    }
    check_float("ternary_false", "Ci.r", ctx.Ci.r, 20.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: builtin math functions                                       */
/* ------------------------------------------------------------------ */

static void test_math_builtins(void) {
    const char* src =
        "surface mathb() {\n"
        "    float a = sqrt(4.0);\n"
        "    float b = abs(-3.0);\n"
        "    float c = floor(2.7);\n"
        "    Ci = color(a, b, c);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [math_builtins] compilation failed\n");
        return;
    }
    check_float("math_builtins", "Ci.r (sqrt4)", ctx.Ci.r, 2.0f, TOL);
    check_float("math_builtins", "Ci.g (abs-3)", ctx.Ci.g, 3.0f, TOL);
    check_float("math_builtins", "Ci.b (floor2.7)", ctx.Ci.b, 2.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: min, max, pow                                                */
/* ------------------------------------------------------------------ */

static void test_minmaxpow(void) {
    const char* src =
        "surface mmp() {\n"
        "    float a = min(3, 5);\n"
        "    float b = max(3, 5);\n"
        "    float c = pow(2, 3);\n"
        "    Ci = color(a, b, c);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [minmaxpow] compilation failed\n");
        return;
    }
    check_float("minmaxpow", "Ci.r (min)", ctx.Ci.r, 3.0f, TOL);
    check_float("minmaxpow", "Ci.g (max)", ctx.Ci.g, 5.0f, TOL);
    check_float("minmaxpow", "Ci.b (pow)", ctx.Ci.b, 8.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: clamp                                                        */
/* ------------------------------------------------------------------ */

static void test_clamp(void) {
    const char* src =
        "surface clamptest() {\n"
        "    float a = clamp(-1, 0, 1);\n"
        "    float b = clamp(0.5, 0, 1);\n"
        "    float c = clamp(2, 0, 1);\n"
        "    Ci = color(a, b, c);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [clamp] compilation failed\n");
        return;
    }
    check_float("clamp", "Ci.r (below)", ctx.Ci.r, 0.0f, TOL);
    check_float("clamp", "Ci.g (inside)", ctx.Ci.g, 0.5f, TOL);
    check_float("clamp", "Ci.b (above)", ctx.Ci.b, 1.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: mix                                                          */
/* ------------------------------------------------------------------ */

static void test_mix(void) {
    const char* src =
        "surface mixtest() {\n"
        "    float a = mix(0, 10, 0.3);\n"
        "    float b = mix(0, 10, 1);\n"
        "    float c = mix(0, 10, 0);\n"
        "    Ci = color(a, b, c);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [mix] compilation failed\n");
        return;
    }
    check_float("mix", "Ci.r", ctx.Ci.r, 3.0f, TOL);
    check_float("mix", "Ci.g", ctx.Ci.g, 10.0f, TOL);
    check_float("mix", "Ci.b", ctx.Ci.b, 0.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: smoothstep                                                   */
/* ------------------------------------------------------------------ */

static void test_smoothstep(void) {
    const char* src =
        "surface sstest() {\n"
        "    float a = smoothstep(0, 1, -0.5);\n"
        "    float b = smoothstep(0, 1, 0.5);\n"
        "    float c = smoothstep(0, 1, 1.5);\n"
        "    Ci = color(a, b, c);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [smoothstep] compilation failed\n");
        return;
    }
    check_float("smoothstep", "Ci.r (below)", ctx.Ci.r, 0.0f, TOL);
    /* smoothstep(0,1,0.5) = 0.5*0.5*(3-2*0.5) = 0.25*2 = 0.5 */
    check_float("smoothstep", "Ci.g (mid)", ctx.Ci.g, 0.5f, TOL);
    check_float("smoothstep", "Ci.b (above)", ctx.Ci.b, 1.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: step                                                         */
/* ------------------------------------------------------------------ */

static void test_step(void) {
    const char* src =
        "surface steptest() {\n"
        "    float a = step(0.5, 0.3);\n"
        "    float b = step(0.5, 0.5);\n"
        "    float c = step(0.5, 0.7);\n"
        "    Ci = color(a, b, c);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [step] compilation failed\n");
        return;
    }
    check_float("step", "Ci.r (below)", ctx.Ci.r, 0.0f, TOL);
    check_float("step", "Ci.g (edge)", ctx.Ci.g, 1.0f, TOL);
    check_float("step", "Ci.b (above)", ctx.Ci.b, 1.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: cross product                                                */
/* ------------------------------------------------------------------ */

static void test_cross(void) {
    const char* src =
        "surface crosstest() {\n"
        "    vector a = vector(1, 0, 0);\n"
        "    vector b = vector(0, 1, 0);\n"
        "    vector c = a ^ b;\n"
        "    Ci = color(c);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [cross] compilation failed\n");
        return;
    }
    /* (1,0,0) x (0,1,0) = (0,0,1) */
    check_color("cross", "Ci", ctx.Ci, (RhColor){0, 0, 1}, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: distance                                                     */
/* ------------------------------------------------------------------ */

static void test_distance(void) {
    const char* src =
        "surface disttest() {\n"
        "    point a = point(0, 0, 0);\n"
        "    point b = point(3, 4, 0);\n"
        "    float d = distance(a, b);\n"
        "    Ci = color(d, d, d);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [distance] compilation failed\n");
        return;
    }
    check_float("distance", "Ci.r", ctx.Ci.r, 5.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: trig functions                                               */
/* ------------------------------------------------------------------ */

static void test_trig(void) {
    const char* src =
        "surface trigtest() {\n"
        "    float a = sin(0);\n"
        "    float b = cos(0);\n"
        "    float c = exp(0);\n"
        "    Ci = color(a, b, c);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [trig] compilation failed\n");
        return;
    }
    check_float("trig", "Ci.r (sin0)", ctx.Ci.r, 0.0f, TOL);
    check_float("trig", "Ci.g (cos0)", ctx.Ci.g, 1.0f, TOL);
    check_float("trig", "Ci.b (exp0)", ctx.Ci.b, 1.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: logical operators                                            */
/* ------------------------------------------------------------------ */

static void test_logical(void) {
    const char* src =
        "surface logictest() {\n"
        "    float a = 1 && 1;\n"
        "    float b = 1 && 0;\n"
        "    float c = 0 || 1;\n"
        "    Ci = color(a, b, c);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [logical] compilation failed\n");
        return;
    }
    check_float("logical", "Ci.r (1&&1)", ctx.Ci.r, 1.0f, TOL);
    check_float("logical", "Ci.g (1&&0)", ctx.Ci.g, 0.0f, TOL);
    check_float("logical", "Ci.b (0||1)", ctx.Ci.b, 1.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: not operator                                                 */
/* ------------------------------------------------------------------ */

static void test_not(void) {
    const char* src =
        "surface nottest() {\n"
        "    float a = !1;\n"
        "    float b = !0;\n"
        "    Ci = color(a, b, 0);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [not] compilation failed\n");
        return;
    }
    check_float("not", "Ci.r (!1)", ctx.Ci.r, 0.0f, TOL);
    check_float("not", "Ci.g (!0)", ctx.Ci.g, 1.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: comparison operators                                         */
/* ------------------------------------------------------------------ */

static void test_comparisons(void) {
    const char* src =
        "surface cmptest() {\n"
        "    float a = 3 > 2;\n"
        "    float b = 3 < 2;\n"
        "    float c = 3 == 3;\n"
        "    Ci = color(a, b, c);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [comparisons] compilation failed\n");
        return;
    }
    check_float("comparisons", "Ci.r (3>2)", ctx.Ci.r, 1.0f, TOL);
    check_float("comparisons", "Ci.g (3<2)", ctx.Ci.g, 0.0f, TOL);
    check_float("comparisons", "Ci.b (3==3)", ctx.Ci.b, 1.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: float->tuple assignment promotion                            */
/* ------------------------------------------------------------------ */

static void test_float_to_tuple_assign(void) {
    const char* src =
        "surface f2t() {\n"
        "    Ci = 0.5;\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [float_to_tuple_assign] compilation failed\n");
        return;
    }
    check_color("float_to_tuple_assign", "Ci", ctx.Ci,
                (RhColor){0.5f, 0.5f, 0.5f}, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: read builtin u, v                                            */
/* ------------------------------------------------------------------ */

static void test_read_uv(void) {
    const char* src =
        "surface uvtest() {\n"
        "    Ci = color(u, v, 0);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    ctx.u = 0.25f;
    ctx.v = 0.75f;
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [read_uv] compilation failed\n");
        return;
    }
    check_float("read_uv", "Ci.r (u)", ctx.Ci.r, 0.25f, TOL);
    check_float("read_uv", "Ci.g (v)", ctx.Ci.g, 0.75f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: color tuple division                                         */
/* ------------------------------------------------------------------ */

static void test_tuple_div(void) {
    const char* src =
        "surface tdiv() {\n"
        "    color a = color(1, 2, 3);\n"
        "    Ci = a / 2;\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [tuple_div] compilation failed\n");
        return;
    }
    check_color("tuple_div", "Ci", ctx.Ci,
                (RhColor){0.5f, 1.0f, 1.5f}, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: tuple subtraction                                            */
/* ------------------------------------------------------------------ */

static void test_tuple_sub(void) {
    const char* src =
        "surface tsub() {\n"
        "    color a = color(1, 2, 3);\n"
        "    color b = color(0.5, 0.5, 0.5);\n"
        "    Ci = a - b;\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [tuple_sub] compilation failed\n");
        return;
    }
    check_color("tuple_sub", "Ci", ctx.Ci,
                (RhColor){0.5f, 1.5f, 2.5f}, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: color compound assign += color                               */
/* ------------------------------------------------------------------ */

static void test_compound_assign_tuple(void) {
    const char* src =
        "surface cmpdt() {\n"
        "    color c = color(1, 2, 3);\n"
        "    c += color(0.1, 0.2, 0.3);\n"
        "    Ci = c;\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [compound_assign_tuple] compilation failed\n");
        return;
    }
    check_color("compound_assign_tuple", "Ci", ctx.Ci,
                (RhColor){1.1f, 2.2f, 3.3f}, TOL);
}

/* ------------------------------------------------------------------ */
/*  Golden Test: compiled matte vs C matte shader                      */
/* ------------------------------------------------------------------ */

static void test_matte_golden(void) {
    const char* src =
        "surface matte(float Ka = 1; float Kd = 1) {\n"
        "    normal Nf = faceforward(normalize(N), I);\n"
        "    Oi = Os;\n"
        "    Ci = Os * Cs * (Ka * ambient() + Kd * diffuse(Nf));\n"
        "}";

    RhSLProgram* prog = compile_shader(src);
    if (!prog) {
        tests_run++; tests_failed++;
        printf("  FAIL [matte_golden] compilation failed\n");
        return;
    }

    /* Test with no lights */
    {
        RhSLShader* shader = rh_sl_shader_create(prog);
        RhShaderContext vm_ctx;
        init_ctx(&vm_ctx);
        rh_sl_vm_shader_exec(&vm_ctx, shader);

        RhShaderContext c_ctx;
        init_ctx(&c_ctx);
        RhMatteParams mp = {1.0f, 1.0f};
        rh_shader_surface_matte(&c_ctx, &mp);

        check_color("matte_golden_nolight", "Ci", vm_ctx.Ci, c_ctx.Ci, 1e-5f);
        check_color("matte_golden_nolight", "Oi", vm_ctx.Oi, c_ctx.Oi, 1e-5f);
        rh_sl_shader_free(shader);
    }

    /* Test with distant light */
    {
        RhSLShader* shader = rh_sl_shader_create(prog);
        RhLight light;
        memset(&light, 0, sizeof(light));
        sl_strcpy(light.type, sizeof(light.type), "distantlight");
        light.direction = (RhVec3){0.0f, 0.0f, -1.0f};
        light.color = (RhColor){1.0f, 1.0f, 1.0f};
        light.intensity = 1.0f;

        RhShaderContext vm_ctx;
        init_ctx(&vm_ctx);
        vm_ctx.light_list = &light;
        vm_ctx.num_lights = 1;
        rh_sl_vm_shader_exec(&vm_ctx, shader);

        RhShaderContext c_ctx;
        init_ctx(&c_ctx);
        c_ctx.light_list = &light;
        c_ctx.num_lights = 1;
        RhMatteParams mp = {1.0f, 1.0f};
        rh_shader_surface_matte(&c_ctx, &mp);

        check_color("matte_golden_distant", "Ci", vm_ctx.Ci, c_ctx.Ci, 1e-5f);
        check_color("matte_golden_distant", "Oi", vm_ctx.Oi, c_ctx.Oi, 1e-5f);
        rh_sl_shader_free(shader);
    }

    /* Test with ambient + point light */
    {
        RhSLShader* shader = rh_sl_shader_create(prog);
        RhLight lights[2];
        memset(lights, 0, sizeof(lights));

        sl_strcpy(lights[0].type, sizeof(lights[0].type), "ambientlight");
        lights[0].color = (RhColor){0.3f, 0.3f, 0.3f};
        lights[0].intensity = 1.0f;

        sl_strcpy(lights[1].type, sizeof(lights[1].type), "pointlight");
        lights[1].position = (RhVec3){2.0f, 3.0f, 0.0f};
        lights[1].color = (RhColor){1.0f, 0.9f, 0.8f};
        lights[1].intensity = 1.5f;

        RhShaderContext vm_ctx;
        init_ctx(&vm_ctx);
        vm_ctx.N = (RhVec3){0.0f, 0.707f, -0.707f};
        vm_ctx.Cs = (RhColor){0.5f, 0.5f, 0.5f};
        vm_ctx.light_list = lights;
        vm_ctx.num_lights = 2;
        rh_sl_vm_shader_exec(&vm_ctx, shader);

        RhShaderContext c_ctx;
        init_ctx(&c_ctx);
        c_ctx.N = (RhVec3){0.0f, 0.707f, -0.707f};
        c_ctx.Cs = (RhColor){0.5f, 0.5f, 0.5f};
        c_ctx.light_list = lights;
        c_ctx.num_lights = 2;
        RhMatteParams mp = {1.0f, 1.0f};
        rh_shader_surface_matte(&c_ctx, &mp);

        check_color("matte_golden_ambpoint", "Ci", vm_ctx.Ci, c_ctx.Ci, 1e-5f);
        check_color("matte_golden_ambpoint", "Oi", vm_ctx.Oi, c_ctx.Oi, 1e-5f);
        rh_sl_shader_free(shader);
    }

    /* Test with custom parameters Ka=0.3, Kd=0.7 */
    {
        RhSLShader* shader = rh_sl_shader_create(prog);
        float ka = 0.3f, kd = 0.7f;
        rh_sl_shader_set_param(shader, "Ka", &ka, 1);
        rh_sl_shader_set_param(shader, "Kd", &kd, 1);

        RhLight light;
        memset(&light, 0, sizeof(light));
        sl_strcpy(light.type, sizeof(light.type), "distantlight");
        light.direction = (RhVec3){0.577f, 0.577f, -0.577f};
        light.color = (RhColor){1.0f, 1.0f, 1.0f};
        light.intensity = 1.0f;

        RhShaderContext vm_ctx;
        init_ctx(&vm_ctx);
        vm_ctx.N = (RhVec3){0.0f, 0.707f, -0.707f};
        vm_ctx.Cs = (RhColor){1.0f, 0.0f, 0.0f};
        vm_ctx.light_list = &light;
        vm_ctx.num_lights = 1;
        rh_sl_vm_shader_exec(&vm_ctx, shader);

        RhShaderContext c_ctx;
        init_ctx(&c_ctx);
        c_ctx.N = (RhVec3){0.0f, 0.707f, -0.707f};
        c_ctx.Cs = (RhColor){1.0f, 0.0f, 0.0f};
        c_ctx.light_list = &light;
        c_ctx.num_lights = 1;
        RhMatteParams mp = {0.3f, 0.7f};
        rh_shader_surface_matte(&c_ctx, &mp);

        check_color("matte_golden_custom", "Ci", vm_ctx.Ci, c_ctx.Ci, 1e-5f);
        check_color("matte_golden_custom", "Oi", vm_ctx.Oi, c_ctx.Oi, 1e-5f);
        rh_sl_shader_free(shader);
    }

    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test: compiled plastic shader                                      */
/* ------------------------------------------------------------------ */

static void test_plastic(void) {
    const char* src =
        "surface plastic(float Ka = 1; float Kd = 0.5; float Ks = 0.5;\n"
        "                float roughness = 0.1; color specularcolor = color(1,1,1)) {\n"
        "    normal Nf = faceforward(normalize(N), I);\n"
        "    vector V = -normalize(I);\n"
        "    Oi = Os;\n"
        "    Ci = Os * (Cs * (Ka * ambient() + Kd * diffuse(Nf))\n"
        "              + specularcolor * Ks * specular(Nf, V, roughness));\n"
        "}";

    RhLight light;
    memset(&light, 0, sizeof(light));
    sl_strcpy(light.type, sizeof(light.type), "distantlight");
    light.direction = (RhVec3){0.0f, 0.0f, -1.0f};
    light.color = (RhColor){1.0f, 1.0f, 1.0f};
    light.intensity = 1.0f;

    RhShaderContext ctx;
    init_ctx(&ctx);
    ctx.light_list = &light;
    ctx.num_lights = 1;
    if (run_shader(src, &ctx, &light, 1) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [plastic] compilation failed\n");
        return;
    }
    /* Should have ambient + diffuse + specular contributions */
    tests_run++;
    if (ctx.Ci.r > 0.0f && ctx.Ci.g > 0.0f && ctx.Ci.b > 0.0f) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL [plastic] expected nonzero Ci, got (%.4f, %.4f, %.4f)\n",
               ctx.Ci.r, ctx.Ci.g, ctx.Ci.b);
    }
}

/* ------------------------------------------------------------------ */
/*  Test: marble shader (noise-based vein pattern)                     */
/* ------------------------------------------------------------------ */

static void test_marble(void) {
    const char* src =
        "surface marble(\n"
        "    float Ka = 1; float Kd = 0.8; float Ks = 0.2;\n"
        "    float roughness = 0.08;\n"
        "    color specularcolor = color(1, 1, 1);\n"
        "    color veincolor = color(0.1, 0.08, 0.06);\n"
        "    float scale = 4; float veinfreq = 1.5;\n"
        ") {\n"
        "    normal Nf = faceforward(normalize(N), I);\n"
        "    vector V = -normalize(I);\n"
        "    float t = noise(scale * P);\n"
        "    t += 0.5 * noise(2.0 * scale * P);\n"
        "    t += 0.25 * noise(4.0 * scale * P);\n"
        "    t += 0.125 * noise(8.0 * scale * P);\n"
        "    t = t / 1.875;\n"
        "    float stripe = sin(veinfreq * 3.14159265 * (xcomp(scale * P) + t));\n"
        "    stripe = (stripe + 1.0) * 0.5;\n"
        "    stripe = smoothstep(0.3, 0.7, stripe);\n"
        "    color c = mix(Cs, veincolor, stripe);\n"
        "    Oi = Os;\n"
        "    Ci = Os * (c * (Ka * ambient() + Kd * diffuse(Nf)) +\n"
        "               specularcolor * Ks * specular(Nf, V, roughness));\n"
        "}";

    RhLight light;
    memset(&light, 0, sizeof(light));
    sl_strcpy(light.type, sizeof(light.type), "distantlight");
    light.direction = (RhVec3){0.0f, 0.0f, -1.0f};
    light.color = (RhColor){1.0f, 1.0f, 1.0f};
    light.intensity = 1.0f;

    /* Test 1: marble shader compiles and produces nonzero output */
    RhShaderContext ctx1;
    init_ctx(&ctx1);
    if (run_shader(src, &ctx1, &light, 1) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [marble] compilation failed\n");
        return;
    }
    tests_run++;
    if (ctx1.Ci.r > 0.0f || ctx1.Ci.g > 0.0f || ctx1.Ci.b > 0.0f) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL [marble] expected nonzero Ci at default P\n");
    }

    /* Test 2: outputs differ at two different positions (noise varies).
     * P.x shifts by 0.1 world units; with scale=4 that's 0.4 in noise-space,
     * which is not a multiple of the vein period (2/(4*1.5) = 1/3), so
     * the stripe value and thus Ci will differ. */
    RhShaderContext ctx2;
    init_ctx(&ctx2);
    ctx2.P = (RhVec3){0.1f, 0.0f, 5.0f};
    if (run_shader(src, &ctx2, &light, 1) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [marble] run at second position failed\n");
        return;
    }
    tests_run++;
    if (ctx1.Ci.r != ctx2.Ci.r || ctx1.Ci.g != ctx2.Ci.g || ctx1.Ci.b != ctx2.Ci.b) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL [marble] expected different Ci at different P positions\n");
    }
}

/* ------------------------------------------------------------------ */
/*  Test: constant shader (simplest surface shader)                    */
/* ------------------------------------------------------------------ */

static void test_constant_shader(void) {
    const char* src =
        "surface constant() {\n"
        "    Oi = Os;\n"
        "    Ci = Os * Cs;\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [constant_shader] compilation failed\n");
        return;
    }
    /* Ci = Os * Cs = (1,1,1)*(0.8,0.2,0.1) = (0.8,0.2,0.1) */
    check_color("constant_shader", "Ci", ctx.Ci, (RhColor){0.8f, 0.2f, 0.1f}, TOL);
    check_color("constant_shader", "Oi", ctx.Oi, (RhColor){1.0f, 1.0f, 1.0f}, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: variable scoping                                             */
/* ------------------------------------------------------------------ */

static void test_scoping(void) {
    const char* src =
        "surface scope() {\n"
        "    float x = 1;\n"
        "    {\n"
        "        float x = 2;\n"
        "        Ci = color(x, x, x);\n"
        "    }\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [scoping] compilation failed\n");
        return;
    }
    /* Inner x=2 should be used */
    check_float("scoping", "Ci.r", ctx.Ci.r, 2.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: tuple add and subtract                                       */
/* ------------------------------------------------------------------ */

static void test_tuple_add(void) {
    const char* src =
        "surface tadd() {\n"
        "    color a = color(0.1, 0.2, 0.3);\n"
        "    color b = color(0.4, 0.5, 0.6);\n"
        "    Ci = a + b;\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [tuple_add] compilation failed\n");
        return;
    }
    check_color("tuple_add", "Ci", ctx.Ci,
                (RhColor){0.5f, 0.7f, 0.9f}, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: multiple parameters of different types                       */
/* ------------------------------------------------------------------ */

static void test_multi_params(void) {
    const char* src =
        "surface mp(float Ka = 0.5; color tint = color(1, 0.5, 0)) {\n"
        "    Ci = Ka * tint;\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [multi_params] compilation failed\n");
        return;
    }
    check_color("multi_params", "Ci", ctx.Ci,
                (RhColor){0.5f, 0.25f, 0.0f}, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: illuminance loop with surface shader                         */
/* ------------------------------------------------------------------ */

static void test_illuminance(void) {
    /* Manual illuminance instead of diffuse() */
    const char* src =
        "surface illum() {\n"
        "    normal Nf = faceforward(normalize(N), I);\n"
        "    color Cdiff = 0;\n"
        "    illuminance(P, Nf, 1.5707963) {\n"
        "        Cdiff += Cl * normalize(L) . Nf;\n"
        "    }\n"
        "    Ci = Cs * Cdiff;\n"
        "    Oi = Os;\n"
        "}";
    RhLight light;
    memset(&light, 0, sizeof(light));
    sl_strcpy(light.type, sizeof(light.type), "distantlight");
    light.direction = (RhVec3){0.0f, 0.0f, -1.0f};
    light.color = (RhColor){1.0f, 1.0f, 1.0f};
    light.intensity = 1.0f;

    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, &light, 1) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [illuminance] compilation failed\n");
        return;
    }
    /* Should be nonzero */
    tests_run++;
    if (ctx.Ci.r > 0.0f) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL [illuminance] expected nonzero Ci, got %.6f\n", ctx.Ci.r);
    }
}

/* ------------------------------------------------------------------ */
/*  Test: read s, t texture coordinates                                */
/* ------------------------------------------------------------------ */

static void test_read_st(void) {
    const char* src =
        "surface sttest() {\n"
        "    Ci = color(s, t, 0);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    /* s defaults to u, t defaults to v in VM */
    ctx.u = 0.3f;
    ctx.v = 0.7f;
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [read_st] compilation failed\n");
        return;
    }
    check_float("read_st", "Ci.r (s)", ctx.Ci.r, 0.3f, TOL);
    check_float("read_st", "Ci.g (t)", ctx.Ci.g, 0.7f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: reflect()                                                    */
/* ------------------------------------------------------------------ */

static void test_reflect(void) {
    const char* src =
        "surface refltest() {\n"
        "    vector I2 = vector(1, -1, 0);\n"
        "    vector N2 = vector(0, 1, 0);\n"
        "    vector R = reflect(I2, N2);\n"
        "    Ci = color(R);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [reflect] compilation failed\n");
        return;
    }
    /* reflect(I, N) = I - 2*dot(I,N)*N
     * I=(1,-1,0), N=(0,1,0), dot(I,N)=-1
     * R = (1,-1,0) - 2*(-1)*(0,1,0) = (1,-1,0)+(0,2,0) = (1,1,0) */
    check_float("reflect", "Ci.r", ctx.Ci.r, 1.0f, TOL);
    check_float("reflect", "Ci.g", ctx.Ci.g, 1.0f, TOL);
    check_float("reflect", "Ci.b", ctx.Ci.b, 0.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: color mix (tuple version)                                    */
/* ------------------------------------------------------------------ */

static void test_color_mix(void) {
    const char* src =
        "surface cmix() {\n"
        "    color a = color(1, 0, 0);\n"
        "    color b = color(0, 0, 1);\n"
        "    Ci = mix(a, b, 0.5);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [color_mix] compilation failed\n");
        return;
    }
    /* mix(a,b,0.5) = a*0.5 + b*0.5 = (0.5,0,0.5) */
    check_color("color_mix", "Ci", ctx.Ci,
                (RhColor){0.5f, 0.0f, 0.5f}, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: mod function                                                 */
/* ------------------------------------------------------------------ */

static void test_mod(void) {
    const char* src =
        "surface modtest() {\n"
        "    float a = mod(7, 3);\n"
        "    float b = mod(10, 5);\n"
        "    Ci = color(a, b, 0);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [mod] compilation failed\n");
        return;
    }
    check_float("mod", "Ci.r (7%%3)", ctx.Ci.r, 1.0f, TOL);
    check_float("mod", "Ci.g (10%%5)", ctx.Ci.g, 0.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: sign function                                                */
/* ------------------------------------------------------------------ */

static void test_sign(void) {
    const char* src =
        "surface signtest() {\n"
        "    float a = sign(5);\n"
        "    float b = sign(-3);\n"
        "    float c = sign(0);\n"
        "    Ci = color(a, b, c);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [sign] compilation failed\n");
        return;
    }
    check_float("sign", "Ci.r", ctx.Ci.r, 1.0f, TOL);
    check_float("sign", "Ci.g", ctx.Ci.g, -1.0f, TOL);
    check_float("sign", "Ci.b", ctx.Ci.b, 0.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: ceil and round                                               */
/* ------------------------------------------------------------------ */

static void test_ceil_round(void) {
    const char* src =
        "surface cr() {\n"
        "    float a = ceil(2.3);\n"
        "    float b = round(2.5);\n"
        "    float c = round(2.4);\n"
        "    Ci = color(a, b, c);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [ceil_round] compilation failed\n");
        return;
    }
    check_float("ceil_round", "Ci.r (ceil2.3)", ctx.Ci.r, 3.0f, TOL);
    /* round(2.5) is implementation-defined but typically 2 or 3 */
    tests_run++;
    if (ctx.Ci.g == 2.0f || ctx.Ci.g == 3.0f) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL [ceil_round] round(2.5) = %.1f\n", ctx.Ci.g);
    }
    check_float("ceil_round", "Ci.b (round2.4)", ctx.Ci.b, 2.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: compilation succeeds for program metadata                    */
/* ------------------------------------------------------------------ */

static void test_program_metadata(void) {
    const char* src =
        "surface myshader(float Ka = 0.5; color albedo = color(1, 0.5, 0)) {\n"
        "    Ci = albedo;\n"
        "}";
    RhSLProgram* prog = compile_shader(src);
    tests_run++;
    if (!prog) {
        tests_failed++;
        printf("  FAIL [program_metadata] compilation failed\n");
        return;
    }

    /* Check metadata */
    int ok = 1;
    if (prog->shader_type != RH_SL_SHADER_SURFACE) {
        printf("  FAIL [program_metadata] wrong shader_type %d\n", prog->shader_type);
        ok = 0;
    }
    if (strcmp(prog->shader_name, "myshader") != 0) {
        printf("  FAIL [program_metadata] wrong name '%s'\n", prog->shader_name);
        ok = 0;
    }
    if (prog->num_params != 2) {
        printf("  FAIL [program_metadata] expected 2 params, got %d\n", prog->num_params);
        ok = 0;
    }
    if (prog->num_params >= 1 && strcmp(prog->params[0].name, "Ka") != 0) {
        printf("  FAIL [program_metadata] param 0 name '%s'\n", prog->params[0].name);
        ok = 0;
    }
    if (prog->num_params >= 2 && strcmp(prog->params[1].name, "albedo") != 0) {
        printf("  FAIL [program_metadata] param 1 name '%s'\n", prog->params[1].name);
        ok = 0;
    }
    if (ok) tests_passed++;
    else tests_failed++;

    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test: xcomp, ycomp, zcomp                                         */
/* ------------------------------------------------------------------ */

static void test_comp_access(void) {
    const char* src =
        "surface comptest() {\n"
        "    vector v = vector(1, 2, 3);\n"
        "    float x = xcomp(v);\n"
        "    float y = ycomp(v);\n"
        "    float z = zcomp(v);\n"
        "    Ci = color(x, y, z);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [comp_access] compilation failed\n");
        return;
    }
    check_float("comp_access", "Ci.r (xcomp)", ctx.Ci.r, 1.0f, TOL);
    check_float("comp_access", "Ci.g (ycomp)", ctx.Ci.g, 2.0f, TOL);
    check_float("comp_access", "Ci.b (zcomp)", ctx.Ci.b, 3.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: setxcomp/setycomp/setzcomp                                   */
/* ------------------------------------------------------------------ */

static void test_set_comp(void) {
    const char* src =
        "surface setctest() {\n"
        "    vector v = vector(0, 0, 0);\n"
        "    setxcomp(v, 1);\n"
        "    setycomp(v, 2);\n"
        "    setzcomp(v, 3);\n"
        "    Ci = color(v);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [set_comp] compilation failed\n");
        return;
    }
    check_float("set_comp", "Ci.r", ctx.Ci.r, 1.0f, TOL);
    check_float("set_comp", "Ci.g", ctx.Ci.g, 2.0f, TOL);
    check_float("set_comp", "Ci.b", ctx.Ci.b, 3.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: cross function call                                          */
/* ------------------------------------------------------------------ */

static void test_cross_func(void) {
    const char* src =
        "surface crossf() {\n"
        "    vector a = vector(1, 0, 0);\n"
        "    vector b = vector(0, 1, 0);\n"
        "    vector c = cross(a, b);\n"
        "    Ci = color(c);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [cross_func] compilation failed\n");
        return;
    }
    check_color("cross_func", "Ci", ctx.Ci, (RhColor){0, 0, 1}, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: displacement shader type                                     */
/* ------------------------------------------------------------------ */

static void test_displacement_shader(void) {
    const char* src =
        "displacement bump(float magnitude = 0.1) {\n"
        "    P = P + normalize(N) * magnitude;\n"
        "    N = calculatenormal(P);\n"
        "}";
    RhSLProgram* prog = compile_shader(src);
    tests_run++;
    if (!prog) {
        tests_failed++;
        printf("  FAIL [displacement_shader] compilation failed\n");
        return;
    }
    if (prog->shader_type == RH_SL_SHADER_DISPLACEMENT) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL [displacement_shader] wrong type %d\n", prog->shader_type);
    }
    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test: nested if/else                                               */
/* ------------------------------------------------------------------ */

static void test_nested_if(void) {
    const char* src =
        "surface nif() {\n"
        "    float x = 5;\n"
        "    float y;\n"
        "    if (x > 3) {\n"
        "        if (x > 7) {\n"
        "            y = 3;\n"
        "        } else {\n"
        "            y = 2;\n"
        "        }\n"
        "    } else {\n"
        "        y = 1;\n"
        "    }\n"
        "    Ci = color(y, y, y);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [nested_if] compilation failed\n");
        return;
    }
    check_float("nested_if", "Ci.r", ctx.Ci.r, 2.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: color(tuple) type coercion                                   */
/* ------------------------------------------------------------------ */

static void test_tuple_type_coerce(void) {
    const char* src =
        "surface coerce() {\n"
        "    normal n = normalize(N);\n"
        "    Ci = color(n);\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    /* N=(0,0,-1), normalized=(0,0,-1) */
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [tuple_type_coerce] compilation failed\n");
        return;
    }
    check_float("tuple_type_coerce", "Ci.r", ctx.Ci.r, 0.0f, TOL);
    check_float("tuple_type_coerce", "Ci.g", ctx.Ci.g, 0.0f, TOL);
    check_float("tuple_type_coerce", "Ci.b", ctx.Ci.b, -1.0f, TOL);
}

/* ------------------------------------------------------------------ */
/*  Texture test helpers                                               */
/* ------------------------------------------------------------------ */

static RhTexture* make_synthetic_texture(float r, float g, float b) {
    float* pixel = malloc(3 * sizeof(float));
    pixel[0] = r; pixel[1] = g; pixel[2] = b;
    RhMipLevel* mip = malloc(sizeof(RhMipLevel));
    mip->width = 1; mip->height = 1; mip->data = pixel;
    RhTexture* tex = calloc(1, sizeof(RhTexture));
    tex->base_width = 1; tex->base_height = 1;
    tex->channels = 3; tex->num_levels = 1;
    tex->levels = mip;
    return tex;
}

static int find_str_idx(const RhSLProgram* prog, const char* name) {
    for (int i = 0; i < prog->string_count; i++) {
        if (strcmp(prog->string_table[i], name) == 0) return i;
    }
    return -1;
}

static void inject_texture(RhSLShader* shader, const RhSLProgram* prog,
                           int str_idx, RhTexture* tex) {
    if (!shader->textures) {
        shader->num_textures = prog->string_count;
        shader->textures = calloc((size_t)prog->string_count, sizeof(void*));
    }
    if (str_idx >= 0 && str_idx < shader->num_textures)
        shader->textures[str_idx] = tex;
}

static int find_texture_flags(const RhSLProgram* prog) {
    for (int i = 0; i < prog->code_len; i++) {
        if (RH_SL_DECODE_OP(prog->code[i]) == OP_TEXTURE)
            return (int)RH_SL_DECODE_FLAGS(prog->code[i]);
    }
    return -1;
}

/* Helper: check (bool), increment counters */
static void check_bool(const char* test, const char* desc, int cond) {
    tests_run++;
    if (cond) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL [%s] %s\n", test, desc);
    }
}

/* ------------------------------------------------------------------ */
/*  Test: texture basic (flags=0)                                      */
/* ------------------------------------------------------------------ */

static void test_texture_basic(void) {
    const char* src =
        "surface textest() {\n"
        "    Ci = texture(\"t.tex\", s, t);\n"
        "    Oi = Os;\n"
        "}";
    RhSLProgram* prog = compile_shader(src);
    if (!prog) {
        tests_run++; tests_failed++;
        printf("  FAIL [texture_basic] compilation failed\n");
        return;
    }
    check_bool("texture_basic", "flags==0", find_texture_flags(prog) == 0);

    int str_idx = find_str_idx(prog, "t.tex");
    RhTexture* tex = make_synthetic_texture(0.5f, 0.3f, 0.8f);
    RhSLShader* shader = rh_sl_shader_create(prog);
    inject_texture(shader, prog, str_idx, tex);

    RhShaderContext ctx;
    init_ctx(&ctx);
    ctx.u = 0.5f; ctx.v = 0.5f;
    rh_sl_vm_shader_exec(&ctx, shader);

    check_color("texture_basic", "Ci", ctx.Ci, (RhColor){0.5f, 0.3f, 0.8f}, TOL);

    /* tex will be freed by rh_sl_shader_free via rh_texture_destroy */
    rh_sl_shader_free(shader);
    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test: texture with blur param (flags=1)                            */
/* ------------------------------------------------------------------ */

static void test_texture_blur(void) {
    const char* src =
        "surface texblur() {\n"
        "    Ci = texture(\"t.tex\", s, t, \"blur\", 0.5);\n"
        "    Oi = Os;\n"
        "}";
    RhSLProgram* prog = compile_shader(src);
    if (!prog) {
        tests_run++; tests_failed++;
        printf("  FAIL [texture_blur] compilation failed\n");
        return;
    }
    check_bool("texture_blur", "flags==1", find_texture_flags(prog) == 1);

    int str_idx = find_str_idx(prog, "t.tex");
    RhTexture* tex = make_synthetic_texture(0.2f, 0.4f, 0.6f);
    RhSLShader* shader = rh_sl_shader_create(prog);
    inject_texture(shader, prog, str_idx, tex);

    RhShaderContext ctx;
    init_ctx(&ctx);
    rh_sl_vm_shader_exec(&ctx, shader);

    check_color("texture_blur", "Ci", ctx.Ci, (RhColor){0.2f, 0.4f, 0.6f}, TOL);

    rh_sl_shader_free(shader);
    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test: texture with width param (flags=1)                           */
/* ------------------------------------------------------------------ */

static void test_texture_width(void) {
    const char* src =
        "surface texwidth() {\n"
        "    Ci = texture(\"t.tex\", s, t, \"width\", 2.0);\n"
        "    Oi = Os;\n"
        "}";
    RhSLProgram* prog = compile_shader(src);
    if (!prog) {
        tests_run++; tests_failed++;
        printf("  FAIL [texture_width] compilation failed\n");
        return;
    }
    check_bool("texture_width", "flags==1", find_texture_flags(prog) == 1);

    int str_idx = find_str_idx(prog, "t.tex");
    RhTexture* tex = make_synthetic_texture(0.7f, 0.1f, 0.3f);
    RhSLShader* shader = rh_sl_shader_create(prog);
    inject_texture(shader, prog, str_idx, tex);

    RhShaderContext ctx;
    init_ctx(&ctx);
    rh_sl_vm_shader_exec(&ctx, shader);

    check_color("texture_width", "Ci", ctx.Ci, (RhColor){0.7f, 0.1f, 0.3f}, TOL);

    rh_sl_shader_free(shader);
    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test: texture with sblur/tblur params (flags=1)                   */
/* ------------------------------------------------------------------ */

static void test_texture_sblur_tblur(void) {
    const char* src =
        "surface texsblur() {\n"
        "    Ci = texture(\"t.tex\", s, t, \"sblur\", 0.1, \"tblur\", 0.2);\n"
        "    Oi = Os;\n"
        "}";
    RhSLProgram* prog = compile_shader(src);
    if (!prog) {
        tests_run++; tests_failed++;
        printf("  FAIL [texture_sblur_tblur] compilation failed\n");
        return;
    }
    check_bool("texture_sblur_tblur", "flags==1", find_texture_flags(prog) == 1);

    int str_idx = find_str_idx(prog, "t.tex");
    RhTexture* tex = make_synthetic_texture(0.9f, 0.5f, 0.1f);
    RhSLShader* shader = rh_sl_shader_create(prog);
    inject_texture(shader, prog, str_idx, tex);

    RhShaderContext ctx;
    init_ctx(&ctx);
    rh_sl_vm_shader_exec(&ctx, shader);

    check_color("texture_sblur_tblur", "Ci", ctx.Ci, (RhColor){0.9f, 0.5f, 0.1f}, TOL);

    rh_sl_shader_free(shader);
    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test: texture 4-point form (flags=2)                               */
/* ------------------------------------------------------------------ */

static void test_texture_4point(void) {
    const char* src =
        "surface tex4pt() {\n"
        "    float ds = 0.01;\n"
        "    Ci = texture(\"t.tex\",\n"
        "        s-ds, t-ds,\n"
        "        s+ds, t-ds,\n"
        "        s-ds, t+ds,\n"
        "        s+ds, t+ds);\n"
        "    Oi = Os;\n"
        "}";
    RhSLProgram* prog = compile_shader(src);
    if (!prog) {
        tests_run++; tests_failed++;
        printf("  FAIL [texture_4point] compilation failed\n");
        return;
    }
    check_bool("texture_4point", "flags==2", find_texture_flags(prog) == 2);

    int str_idx = find_str_idx(prog, "t.tex");
    RhTexture* tex = make_synthetic_texture(0.3f, 0.6f, 0.9f);
    RhSLShader* shader = rh_sl_shader_create(prog);
    inject_texture(shader, prog, str_idx, tex);

    RhShaderContext ctx;
    init_ctx(&ctx);
    ctx.u = 0.5f; ctx.v = 0.5f;
    rh_sl_vm_shader_exec(&ctx, shader);

    check_color("texture_4point", "Ci", ctx.Ci, (RhColor){0.3f, 0.6f, 0.9f}, TOL);

    rh_sl_shader_free(shader);
    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Tests: printf builtin                                              */
/* ------------------------------------------------------------------ */

static void test_printf_no_args(void) {
    /* printf with no value args should compile and run without crashing */
    const char* src =
        "surface pf_noargs() {\n"
        "    printf(\"hello\\n\");\n"
        "    Ci = color(1, 0, 0);\n"
        "    Oi = Os;\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [printf_no_args] compilation failed\n");
        return;
    }
    check_color("printf_no_args", "Ci", ctx.Ci, (RhColor){1, 0, 0}, TOL);
}

static void test_printf_float_args(void) {
    /* printf with a float argument; verify Ci is still computed correctly */
    const char* src =
        "surface pf_float() {\n"
        "    float val = 0.5;\n"
        "    printf(\"val=%f\\n\", val);\n"
        "    Ci = color(val, 0, 0);\n"
        "    Oi = Os;\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [printf_float_args] compilation failed\n");
        return;
    }
    check_color("printf_float_args", "Ci", ctx.Ci, (RhColor){0.5f, 0, 0}, TOL);
}

static void test_printf_tuple_arg(void) {
    /* printf with a tuple (normal) argument; verify Ci is correct */
    const char* src =
        "surface pf_tuple() {\n"
        "    printf(\"N=%n\\n\", N);\n"
        "    Ci = Cs;\n"
        "    Oi = Os;\n"
        "}";
    RhShaderContext ctx;
    init_ctx(&ctx);
    if (run_shader(src, &ctx, NULL, 0) != 0) {
        tests_run++; tests_failed++;
        printf("  FAIL [printf_tuple_arg] compilation failed\n");
        return;
    }
    check_color("printf_tuple_arg", "Ci", ctx.Ci, (RhColor){0.8f, 0.2f, 0.1f}, TOL);
}

/* ------------------------------------------------------------------ */
/*  Test: user-defined functions                                       */
/* ------------------------------------------------------------------ */

static void test_user_functions(void) {
    /* 1. Float return: square(x) = x*x */
    {
        const char* src =
            "float square(float x) { return x * x; }\n"
            "surface uf_square() {\n"
            "    float r = square(3.0);\n"
            "    Ci = color(r, 0, 0);\n"
            "    Oi = Os;\n"
            "}";
        RhShaderContext ctx;
        init_ctx(&ctx);
        if (run_shader(src, &ctx, NULL, 0) != 0) {
            tests_run++; tests_failed++;
            printf("  FAIL [uf_square] compilation failed\n");
        } else {
            check_float("uf_square", "Ci.r", ctx.Ci.r, 9.0f, TOL);
        }
    }

    /* 2. Color return: tint(c, f) = c * f */
    {
        const char* src =
            "color tint(color c; float f) { return c * f; }\n"
            "surface uf_tint() {\n"
            "    Ci = tint(color(1, 0.5, 0.25), 2.0);\n"
            "    Oi = Os;\n"
            "}";
        RhShaderContext ctx;
        init_ctx(&ctx);
        if (run_shader(src, &ctx, NULL, 0) != 0) {
            tests_run++; tests_failed++;
            printf("  FAIL [uf_tint] compilation failed\n");
        } else {
            check_color("uf_tint", "Ci", ctx.Ci,
                        (RhColor){2.0f, 1.0f, 0.5f}, TOL);
        }
    }

    /* 3. Output parameter: addone(output x) increments x */
    {
        const char* src =
            "void addone(output float x) { x = x + 1; }\n"
            "surface uf_addone() {\n"
            "    float v = 4.0;\n"
            "    addone(v);\n"
            "    Ci = color(v, 0, 0);\n"
            "    Oi = Os;\n"
            "}";
        RhShaderContext ctx;
        init_ctx(&ctx);
        if (run_shader(src, &ctx, NULL, 0) != 0) {
            tests_run++; tests_failed++;
            printf("  FAIL [uf_addone] compilation failed\n");
        } else {
            check_float("uf_addone", "v_after", ctx.Ci.r, 5.0f, TOL);
        }
    }

    /* 4. Multiple functions: square and tint both called */
    {
        const char* src =
            "float square(float x) { return x * x; }\n"
            "color tint(color c; float f) { return c * f; }\n"
            "surface uf_multi() {\n"
            "    float s = square(2.0);\n"
            "    Ci = tint(color(1, 1, 1), s);\n"
            "    Oi = Os;\n"
            "}";
        RhShaderContext ctx;
        init_ctx(&ctx);
        if (run_shader(src, &ctx, NULL, 0) != 0) {
            tests_run++; tests_failed++;
            printf("  FAIL [uf_multi] compilation failed\n");
        } else {
            check_color("uf_multi", "Ci", ctx.Ci,
                        (RhColor){4.0f, 4.0f, 4.0f}, TOL);
        }
    }

    /* 5. Void function called as statement: sets Ci */
    {
        const char* src =
            "void setci(color c) { Ci = c; }\n"
            "surface uf_setci() {\n"
            "    setci(color(0.1, 0.2, 0.3));\n"
            "    Oi = Os;\n"
            "}";
        RhShaderContext ctx;
        init_ctx(&ctx);
        if (run_shader(src, &ctx, NULL, 0) != 0) {
            tests_run++; tests_failed++;
            printf("  FAIL [uf_setci] compilation failed\n");
        } else {
            check_color("uf_setci", "Ci", ctx.Ci,
                        (RhColor){0.1f, 0.2f, 0.3f}, TOL);
        }
    }

    /* 6. Overloaded functions: float vs point dispatch */
    {
        const char* src =
            "float overloaded(float x) { return x * 2.0; }\n"
            "float overloaded(point p) { return xcomp(p); }\n"
            "surface uf_overload() {\n"
            "    float a = overloaded(3.0);\n"
            "    float b = overloaded(point(1.0, 2.0, 3.0));\n"
            "    Ci = color(a, b, 0);\n"
            "    Oi = Os;\n"
            "}";
        RhShaderContext ctx;
        init_ctx(&ctx);
        if (run_shader(src, &ctx, NULL, 0) != 0) {
            tests_run++; tests_failed++;
            printf("  FAIL [uf_overload] compilation failed\n");
        } else {
            check_float("uf_overload", "float_variant", ctx.Ci.r, 6.0f, TOL);
            check_float("uf_overload", "point_variant",  ctx.Ci.g, 1.0f, TOL);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== Shading Language Code Generator Tests ===\n\n");

    /* Compilation basics */
    test_empty_shader();
    test_constant_assign();
    test_float_literal();
    test_cs_passthrough();
    test_program_metadata();

    /* Arithmetic */
    test_float_arith();
    test_tuple_arith();
    test_tuple_add();
    test_tuple_sub();
    test_tuple_div();
    test_scalar_tuple_mul();
    test_float_times_tuple();
    test_compound_assign();
    test_compound_assign_tuple();
    test_negation();

    /* Builtins: math */
    test_math_builtins();
    test_minmaxpow();
    test_trig();
    test_sign();
    test_ceil_round();
    test_mod();

    /* Builtins: geometry */
    test_normalize();
    test_dot();
    test_dot_func();
    test_length();
    test_distance();
    test_faceforward();
    test_reflect();
    test_cross();
    test_cross_func();
    test_comp_access();
    test_set_comp();

    /* Builtins: composite */
    test_clamp();
    test_smoothstep();
    test_step();
    test_mix();
    test_color_mix();

    /* Type constructors */
    test_type_constructor_splat();
    test_type_constructor_3arg();
    test_tuple_type_coerce();
    test_float_to_tuple_assign();

    /* Control flow */
    test_if_else();
    test_if_no_else();
    test_nested_if();
    test_while_loop();
    test_for_loop();
    test_break();
    test_continue();
    test_nested_loops();
    test_ternary();
    test_ternary_false();
    test_logical();
    test_not();
    test_comparisons();
    test_scoping();

    /* Parameters */
    test_param_defaults();
    test_param_override();
    test_color_param();
    test_multi_params();

    /* Builtins: lighting */
    test_ambient_no_lights();
    test_ambient_with_light();
    test_diffuse_distant();
    test_specular();

    /* Illuminance */
    test_illuminance();

    /* Globals */
    test_read_uv();
    test_read_st();

    /* Full shader tests */
    test_constant_shader();
    test_plastic();
    test_marble();
    test_displacement_shader();

    /* Golden test */
    test_matte_golden();

    /* Texture builtin */
    test_texture_basic();
    test_texture_blur();
    test_texture_width();
    test_texture_sblur_tblur();
    test_texture_4point();

    /* printf builtin */
    test_printf_no_args();
    test_printf_float_args();
    test_printf_tuple_arg();

    /* User-defined functions */
    test_user_functions();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");

    return (tests_failed == 0) ? 0 : 1;
}
