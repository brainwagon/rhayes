#define _POSIX_C_SOURCE 200809L
/*
 * test_sl_slo.c -- Unit tests for .slo file I/O (round-trip serialization).
 *
 * Build:  gcc -std=c99 -Wall -Wextra -Werror -pedantic -D_POSIX_C_SOURCE=200809L
 *         -Iinclude -O2 tests/test_sl_slo.c -Llib -lrh -lri -lm
 *         -o tests/test_sl_slo
 *
 * Run:    ./tests/test_sl_slo
 */

#include "rh_sl_slo.h"
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
#include <unistd.h>

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

#define PASS() do { tests_run++; tests_passed++; } while(0)
#define FAIL(test, msg) do { \
    tests_run++; tests_failed++; \
    printf("  FAIL [%s] %s\n", (test), (msg)); \
} while(0)

#define CHECK(test, cond, msg) do { \
    tests_run++; \
    if (cond) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL [%s] %s\n", (test), (msg)); } \
} while(0)

static void check_float(const char* test, const char* name,
                         float got, float expected, float tol) {
    tests_run++;
    float diff = fabsf(got - expected);
    if (diff <= tol) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL [%s] %s: got %.6f, expected %.6f\n",
               test, name, got, expected);
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
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static const char* tmp_path = "/tmp/test_sl_slo.slo";

static RhSLProgram* compile_shader(const char* src) {
    RhSLParser parser;
    rh_sl_parse_init(&parser, src);
    RhSLNode* ast = rh_sl_parse(&parser);
    if (!ast || parser.num_errors > 0) {
        if (parser.num_errors > 0)
            printf("    Parse error: %s\n", parser.errors[0]);
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
    if (!prog && errs.num_errors > 0)
        printf("    Codegen error: %s\n", errs.errors[0]);
    rh_sl_node_free(ast);
    return prog;
}

/* Compile RSL source, write to .slo, read back, return loaded program. */
static RhSLProgram* round_trip(const char* src) {
    RhSLProgram* orig = compile_shader(src);
    if (!orig) return NULL;

    if (rh_sl_slo_write(tmp_path, orig) != 0) {
        rh_sl_program_free(orig);
        return NULL;
    }
    rh_sl_program_free(orig);

    return rh_sl_slo_read(tmp_path);
}

/* Compile, write, read, return both original and loaded programs. */
static int round_trip_both(const char* src, RhSLProgram** orig_out,
                            RhSLProgram** loaded_out) {
    *orig_out = compile_shader(src);
    if (!*orig_out) return -1;

    if (rh_sl_slo_write(tmp_path, *orig_out) != 0) {
        rh_sl_program_free(*orig_out);
        return -1;
    }

    *loaded_out = rh_sl_slo_read(tmp_path);
    if (!*loaded_out) {
        rh_sl_program_free(*orig_out);
        return -1;
    }
    return 0;
}

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

/* ------------------------------------------------------------------ */
/*  Test: empty shader round-trip                                      */
/* ------------------------------------------------------------------ */

static void test_empty_shader(void) {
    const char* src = "surface empty() { }";
    RhSLProgram* prog = round_trip(src);
    CHECK("empty_shader", prog != NULL, "round-trip returned NULL");
    if (!prog) return;

    CHECK("empty_shader", prog->shader_type == RH_SL_SHADER_SURFACE,
          "wrong shader type");
    CHECK("empty_shader", strcmp(prog->shader_name, "empty") == 0,
          "wrong shader name");
    CHECK("empty_shader", prog->num_params == 0, "expected 0 params");
    CHECK("empty_shader", prog->code_len > 0, "expected code");

    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test: constant shader round-trip                                   */
/* ------------------------------------------------------------------ */

static void test_constant_shader(void) {
    const char* src =
        "surface red() {\n"
        "    Ci = color(1, 0, 0);\n"
        "    Oi = Os;\n"
        "}";
    RhSLProgram* orig, *loaded;
    if (round_trip_both(src, &orig, &loaded) != 0) {
        FAIL("constant_shader", "round-trip failed");
        return;
    }

    CHECK("constant_shader", loaded->code_len == orig->code_len,
          "code_len mismatch");
    CHECK("constant_shader", loaded->const_count == orig->const_count,
          "const_count mismatch");
    CHECK("constant_shader", loaded->num_regs == orig->num_regs,
          "num_regs mismatch");

    rh_sl_program_free(orig);
    rh_sl_program_free(loaded);
}

/* ------------------------------------------------------------------ */
/*  Test: shader with parameters                                       */
/* ------------------------------------------------------------------ */

static void test_shader_with_params(void) {
    const char* src =
        "surface test_params(float Ka = 0.5; float Kd = 0.8) {\n"
        "    Ci = Cs * Ka;\n"
        "    Oi = Os;\n"
        "}";
    RhSLProgram* orig, *loaded;
    if (round_trip_both(src, &orig, &loaded) != 0) {
        FAIL("shader_with_params", "round-trip failed");
        return;
    }

    CHECK("shader_with_params", loaded->num_params == 2, "expected 2 params");
    CHECK("shader_with_params",
          strcmp(loaded->params[0].name, "Ka") == 0, "param[0] name");
    CHECK("shader_with_params",
          strcmp(loaded->params[1].name, "Kd") == 0, "param[1] name");
    CHECK("shader_with_params",
          loaded->params[0].type == RH_SL_TYPE_FLOAT, "param[0] type");
    CHECK("shader_with_params",
          loaded->params[0].num_components == 1, "param[0] components");

    rh_sl_program_free(orig);
    rh_sl_program_free(loaded);
}

/* ------------------------------------------------------------------ */
/*  Test: metadata preservation                                        */
/* ------------------------------------------------------------------ */

static void test_metadata(void) {
    const char* src =
        "surface myshader(float x = 1) {\n"
        "    Ci = Cs * x;\n"
        "    Oi = Os;\n"
        "}";
    RhSLProgram* orig, *loaded;
    if (round_trip_both(src, &orig, &loaded) != 0) {
        FAIL("metadata", "round-trip failed");
        return;
    }

    CHECK("metadata", loaded->shader_type == orig->shader_type,
          "shader_type mismatch");
    CHECK("metadata", strcmp(loaded->shader_name, orig->shader_name) == 0,
          "shader_name mismatch");
    CHECK("metadata", loaded->num_regs == orig->num_regs,
          "num_regs mismatch");
    CHECK("metadata", loaded->num_params == orig->num_params,
          "num_params mismatch");

    rh_sl_program_free(orig);
    rh_sl_program_free(loaded);
}

/* ------------------------------------------------------------------ */
/*  Test: float param default value                                    */
/* ------------------------------------------------------------------ */

static void test_float_param_default(void) {
    const char* src =
        "surface fp(float Ka = 0.75) {\n"
        "    Ci = Cs * Ka;\n"
        "    Oi = Os;\n"
        "}";
    RhSLProgram* orig, *loaded;
    if (round_trip_both(src, &orig, &loaded) != 0) {
        FAIL("float_param_default", "round-trip failed");
        return;
    }

    CHECK("float_param_default", loaded->num_params == 1, "expected 1 param");
    int idx = loaded->params[0].default_idx;
    CHECK("float_param_default", idx >= 0 && idx < loaded->const_count,
          "default_idx out of range");
    if (idx >= 0 && idx < loaded->const_count) {
        check_float("float_param_default", "default",
                     loaded->const_pool[idx], 0.75f, TOL);
    }

    rh_sl_program_free(orig);
    rh_sl_program_free(loaded);
}

/* ------------------------------------------------------------------ */
/*  Test: color param default                                          */
/* ------------------------------------------------------------------ */

static void test_color_param_default(void) {
    const char* src =
        "surface cp(color tint = color(0.1, 0.2, 0.3)) {\n"
        "    Ci = tint;\n"
        "    Oi = Os;\n"
        "}";
    RhSLProgram* orig, *loaded;
    if (round_trip_both(src, &orig, &loaded) != 0) {
        FAIL("color_param_default", "round-trip failed");
        return;
    }

    CHECK("color_param_default", loaded->num_params == 1, "expected 1 param");
    CHECK("color_param_default",
          loaded->params[0].type == RH_SL_TYPE_COLOR, "expected color type");
    CHECK("color_param_default",
          loaded->params[0].num_components == 3, "expected 3 components");

    int idx = loaded->params[0].default_idx;
    CHECK("color_param_default", idx >= 0 && idx + 2 < loaded->const_count,
          "default_idx out of range");
    if (idx >= 0 && idx + 2 < loaded->const_count) {
        check_float("color_param_default", "r", loaded->const_pool[idx], 0.1f, TOL);
        check_float("color_param_default", "g", loaded->const_pool[idx+1], 0.2f, TOL);
        check_float("color_param_default", "b", loaded->const_pool[idx+2], 0.3f, TOL);
    }

    rh_sl_program_free(orig);
    rh_sl_program_free(loaded);
}

/* ------------------------------------------------------------------ */
/*  Test: multiple params                                              */
/* ------------------------------------------------------------------ */

static void test_multiple_params(void) {
    const char* src =
        "surface multi(float a = 1; float b = 2; float c = 3) {\n"
        "    Ci = Cs * (a + b + c);\n"
        "    Oi = Os;\n"
        "}";
    RhSLProgram* orig, *loaded;
    if (round_trip_both(src, &orig, &loaded) != 0) {
        FAIL("multiple_params", "round-trip failed");
        return;
    }

    CHECK("multiple_params", loaded->num_params == 3, "expected 3 params");
    for (int i = 0; i < loaded->num_params; i++) {
        CHECK("multiple_params",
              loaded->params[i].reg == orig->params[i].reg, "reg mismatch");
        CHECK("multiple_params",
              loaded->params[i].default_idx == orig->params[i].default_idx,
              "default_idx mismatch");
    }

    rh_sl_program_free(orig);
    rh_sl_program_free(loaded);
}

/* ------------------------------------------------------------------ */
/*  Test: all param types                                              */
/* ------------------------------------------------------------------ */

static void test_param_types(void) {
    const char* src =
        "surface ptypes(float f = 1;\n"
        "               color c = color(1,0,0);\n"
        "               point pp = point(0,0,0);\n"
        "               vector vv = vector(1,0,0);\n"
        "               normal nn = normal(0,1,0)) {\n"
        "    Ci = c * f + color(xcomp(pp), xcomp(vv), xcomp(nn));\n"
        "    Oi = Os;\n"
        "}";
    RhSLProgram* loaded = round_trip(src);
    if (!loaded) { FAIL("param_types", "round-trip failed"); return; }

    CHECK("param_types", loaded->num_params == 5, "expected 5 params");
    CHECK("param_types", loaded->params[0].type == RH_SL_TYPE_FLOAT, "f type");
    CHECK("param_types", loaded->params[1].type == RH_SL_TYPE_COLOR, "c type");
    CHECK("param_types", loaded->params[2].type == RH_SL_TYPE_POINT, "p type");
    CHECK("param_types", loaded->params[3].type == RH_SL_TYPE_VECTOR, "v type");
    CHECK("param_types", loaded->params[4].type == RH_SL_TYPE_NORMAL, "n type");
    CHECK("param_types", loaded->params[0].num_components == 1, "f comps");
    CHECK("param_types", loaded->params[1].num_components == 3, "c comps");

    rh_sl_program_free(loaded);
}

/* ------------------------------------------------------------------ */
/*  Test: code fidelity                                                */
/* ------------------------------------------------------------------ */

static void test_code_fidelity(void) {
    const char* src =
        "surface codef(float Ka = 1) {\n"
        "    Ci = Cs * Ka;\n"
        "    Oi = Os;\n"
        "}";
    RhSLProgram* orig, *loaded;
    if (round_trip_both(src, &orig, &loaded) != 0) {
        FAIL("code_fidelity", "round-trip failed");
        return;
    }

    CHECK("code_fidelity", loaded->code_len == orig->code_len,
          "code_len mismatch");
    if (loaded->code_len == orig->code_len) {
        int match = (memcmp(loaded->code, orig->code,
                            orig->code_len * sizeof(uint64_t)) == 0);
        CHECK("code_fidelity", match, "instructions differ");
    }

    rh_sl_program_free(orig);
    rh_sl_program_free(loaded);
}

/* ------------------------------------------------------------------ */
/*  Test: constant pool fidelity                                       */
/* ------------------------------------------------------------------ */

static void test_const_pool_fidelity(void) {
    const char* src =
        "surface cpf(float a = 3.14; float b = 2.718) {\n"
        "    Ci = Cs * (a + b);\n"
        "    Oi = Os;\n"
        "}";
    RhSLProgram* orig, *loaded;
    if (round_trip_both(src, &orig, &loaded) != 0) {
        FAIL("const_pool_fidelity", "round-trip failed");
        return;
    }

    CHECK("const_pool_fidelity", loaded->const_count == orig->const_count,
          "const_count mismatch");
    if (loaded->const_count == orig->const_count) {
        int match = (memcmp(loaded->const_pool, orig->const_pool,
                            orig->const_count * sizeof(float)) == 0);
        CHECK("const_pool_fidelity", match, "constants differ");
    }

    rh_sl_program_free(orig);
    rh_sl_program_free(loaded);
}

/* ------------------------------------------------------------------ */
/*  Test: string table fidelity                                        */
/* ------------------------------------------------------------------ */

static void test_string_table_fidelity(void) {
    /* Use a shader that produces strings in the string table.
     * ambient/diffuse don't produce strings, but we can test
     * with shader name and param names at minimum. */
    const char* src =
        "surface strf(float x = 1) {\n"
        "    Ci = Cs * x;\n"
        "    Oi = Os;\n"
        "}";
    RhSLProgram* orig, *loaded;
    if (round_trip_both(src, &orig, &loaded) != 0) {
        FAIL("string_table_fidelity", "round-trip failed");
        return;
    }

    CHECK("string_table_fidelity",
          loaded->string_count == orig->string_count,
          "string_count mismatch");
    for (int i = 0; i < loaded->string_count && i < orig->string_count; i++) {
        int match = (strcmp(loaded->string_table[i],
                            orig->string_table[i]) == 0);
        CHECK("string_table_fidelity", match, "string mismatch");
    }

    rh_sl_program_free(orig);
    rh_sl_program_free(loaded);
}

/* ------------------------------------------------------------------ */
/*  Test: read nonexistent file                                        */
/* ------------------------------------------------------------------ */

static void test_read_nonexistent(void) {
    RhSLProgram* prog = rh_sl_slo_read("/tmp/nonexistent_test_slo.slo");
    CHECK("read_nonexistent", prog == NULL, "expected NULL for missing file");
}

/* ------------------------------------------------------------------ */
/*  Test: read corrupt file                                            */
/* ------------------------------------------------------------------ */

static void test_read_corrupt(void) {
    /* Write garbage to a file */
    FILE* f = fopen(tmp_path, "wb");
    if (f) {
        const char* garbage = "this is not a valid slo file";
        fwrite(garbage, 1, strlen(garbage), f);
        fclose(f);
    }
    RhSLProgram* prog = rh_sl_slo_read(tmp_path);
    CHECK("read_corrupt", prog == NULL, "expected NULL for corrupt file");
    if (prog) rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test: read truncated file                                          */
/* ------------------------------------------------------------------ */

static void test_read_truncated(void) {
    /* Write just the magic bytes and nothing else */
    FILE* f = fopen(tmp_path, "wb");
    if (f) {
        fwrite("RHSL", 1, 4, f);
        fclose(f);
    }
    RhSLProgram* prog = rh_sl_slo_read(tmp_path);
    CHECK("read_truncated", prog == NULL, "expected NULL for truncated file");
    if (prog) rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test: write to invalid path                                        */
/* ------------------------------------------------------------------ */

static void test_write_invalid_path(void) {
    const char* src = "surface w() { }";
    RhSLProgram* prog = compile_shader(src);
    if (!prog) { FAIL("write_invalid_path", "compile failed"); return; }

    int ret = rh_sl_slo_write("/no/such/dir/test.slo", prog);
    CHECK("write_invalid_path", ret == -1, "expected -1 for bad path");

    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test: write NULL program                                           */
/* ------------------------------------------------------------------ */

static void test_write_null(void) {
    int ret = rh_sl_slo_write(tmp_path, NULL);
    CHECK("write_null", ret == -1, "expected -1 for NULL program");
}

/* ------------------------------------------------------------------ */
/*  Test: read NULL path                                               */
/* ------------------------------------------------------------------ */

static void test_read_null(void) {
    RhSLProgram* prog = rh_sl_slo_read(NULL);
    CHECK("read_null", prog == NULL, "expected NULL");
}

/* ------------------------------------------------------------------ */
/*  Test: wrong magic                                                  */
/* ------------------------------------------------------------------ */

static void test_wrong_magic(void) {
    FILE* f = fopen(tmp_path, "wb");
    if (f) {
        fwrite("XXXX", 1, 4, f);
        /* Write enough data for the rest of header */
        uint16_t v = 1; fwrite(&v, 2, 1, f);
        uint8_t t = 1; fwrite(&t, 1, 1, f);
        uint16_t z = 1; fwrite(&z, 2, 1, f);
        uint16_t np = 0; fwrite(&np, 2, 1, f);
        uint16_t nr = 10; fwrite(&nr, 2, 1, f);
        uint32_t cl = 0; fwrite(&cl, 4, 1, f);
        uint32_t cc = 0; fwrite(&cc, 4, 1, f);
        uint16_t sc = 0; fwrite(&sc, 2, 1, f);
        fwrite("\0", 1, 1, f);  /* shader name */
        fclose(f);
    }
    RhSLProgram* prog = rh_sl_slo_read(tmp_path);
    CHECK("wrong_magic", prog == NULL, "expected NULL for wrong magic");
    if (prog) rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test: wrong version                                                */
/* ------------------------------------------------------------------ */

static void test_wrong_version(void) {
    FILE* f = fopen(tmp_path, "wb");
    if (f) {
        fwrite("RHSL", 1, 4, f);
        uint16_t v = 99; fwrite(&v, 2, 1, f);  /* bad version */
        uint8_t t = 1; fwrite(&t, 1, 1, f);
        uint16_t z = 1; fwrite(&z, 2, 1, f);
        uint16_t np = 0; fwrite(&np, 2, 1, f);
        uint16_t nr = 10; fwrite(&nr, 2, 1, f);
        uint32_t cl = 0; fwrite(&cl, 4, 1, f);
        uint32_t cc = 0; fwrite(&cc, 4, 1, f);
        uint16_t sc = 0; fwrite(&sc, 2, 1, f);
        fwrite("\0", 1, 1, f);
        fclose(f);
    }
    RhSLProgram* prog = rh_sl_slo_read(tmp_path);
    CHECK("wrong_version", prog == NULL, "expected NULL for wrong version");
    if (prog) rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test: golden test -- matte shader execution after round-trip       */
/* ------------------------------------------------------------------ */

static void test_golden_matte(void) {
    const char* src =
        "surface matte(float Ka = 1; float Kd = 1) {\n"
        "    Ci = Cs * (Ka * ambient() + Kd * diffuse(faceforward(normalize(N), I)));\n"
        "    Oi = Os;\n"
        "}";

    /* Compile directly */
    RhSLProgram* direct = compile_shader(src);
    if (!direct) { FAIL("golden_matte", "direct compile failed"); return; }

    /* Round-trip through .slo */
    if (rh_sl_slo_write(tmp_path, direct) != 0) {
        FAIL("golden_matte", "write failed");
        rh_sl_program_free(direct);
        return;
    }
    RhSLProgram* loaded = rh_sl_slo_read(tmp_path);
    if (!loaded) {
        FAIL("golden_matte", "read failed");
        rh_sl_program_free(direct);
        return;
    }

    /* Set up a light */
    RhLight light;
    memset(&light, 0, sizeof(light));
    sl_strcpy(light.type, sizeof(light.type), "distantlight");
    light.color = (RhColor){1.0f, 1.0f, 1.0f};
    light.intensity = 1.0f;
    light.direction = rh_vec3_normalize((RhVec3){-1.0f, -1.0f, 1.0f});

    /* Execute direct program */
    RhShaderContext ctx1;
    init_ctx(&ctx1);
    RhSLShader* shader1 = rh_sl_shader_create(direct);
    ctx1.light_list = &light;
    ctx1.num_lights = 1;
    rh_sl_vm_shader_exec(&ctx1, shader1);
    rh_sl_shader_free(shader1);

    /* Execute loaded program */
    RhShaderContext ctx2;
    init_ctx(&ctx2);
    RhSLShader* shader2 = rh_sl_shader_create(loaded);
    ctx2.light_list = &light;
    ctx2.num_lights = 1;
    rh_sl_vm_shader_exec(&ctx2, shader2);
    rh_sl_shader_free(shader2);

    /* Compare outputs */
    check_color("golden_matte", "Ci", ctx2.Ci, ctx1.Ci, TOL);
    check_color("golden_matte", "Oi", ctx2.Oi, ctx1.Oi, TOL);

    rh_sl_program_free(direct);
    rh_sl_program_free(loaded);
}

/* ------------------------------------------------------------------ */
/*  Test: empty constant pool (no constants)                           */
/* ------------------------------------------------------------------ */

static void test_no_constants(void) {
    const char* src =
        "surface noconst() {\n"
        "    Ci = Cs;\n"
        "    Oi = Os;\n"
        "}";
    RhSLProgram* orig, *loaded;
    if (round_trip_both(src, &orig, &loaded) != 0) {
        FAIL("no_constants", "round-trip failed");
        return;
    }

    CHECK("no_constants", loaded->const_count == orig->const_count,
          "const_count mismatch");
    CHECK("no_constants", loaded->code_len == orig->code_len,
          "code_len mismatch");

    rh_sl_program_free(orig);
    rh_sl_program_free(loaded);
}

/* ------------------------------------------------------------------ */
/*  Test: no string table                                              */
/* ------------------------------------------------------------------ */

static void test_no_strings(void) {
    const char* src =
        "surface nostr() {\n"
        "    Ci = Cs;\n"
        "    Oi = Os;\n"
        "}";
    RhSLProgram* orig, *loaded;
    if (round_trip_both(src, &orig, &loaded) != 0) {
        FAIL("no_strings", "round-trip failed");
        return;
    }

    CHECK("no_strings", loaded->string_count == orig->string_count,
          "string_count mismatch");

    rh_sl_program_free(orig);
    rh_sl_program_free(loaded);
}

/* ------------------------------------------------------------------ */
/*  Test: execution after round-trip -- simple color                   */
/* ------------------------------------------------------------------ */

static void test_exec_roundtrip(void) {
    const char* src =
        "surface halfcs() {\n"
        "    Ci = Cs * 0.5;\n"
        "    Oi = Os;\n"
        "}";
    RhSLProgram* loaded = round_trip(src);
    if (!loaded) { FAIL("exec_roundtrip", "round-trip failed"); return; }

    RhShaderContext ctx;
    init_ctx(&ctx);
    RhSLShader* shader = rh_sl_shader_create(loaded);
    rh_sl_vm_shader_exec(&ctx, shader);
    rh_sl_shader_free(shader);

    check_color("exec_roundtrip", "Ci", ctx.Ci,
                (RhColor){0.4f, 0.1f, 0.05f}, TOL);
    check_color("exec_roundtrip", "Oi", ctx.Oi,
                (RhColor){1.0f, 1.0f, 1.0f}, TOL);

    rh_sl_program_free(loaded);
}

/* ------------------------------------------------------------------ */
/*  Test: execution with param override after round-trip               */
/* ------------------------------------------------------------------ */

static void test_exec_param_override(void) {
    const char* src =
        "surface scalecs(float scale = 1) {\n"
        "    Ci = Cs * scale;\n"
        "    Oi = Os;\n"
        "}";
    RhSLProgram* loaded = round_trip(src);
    if (!loaded) { FAIL("exec_param_override", "round-trip failed"); return; }

    RhShaderContext ctx;
    init_ctx(&ctx);
    RhSLShader* shader = rh_sl_shader_create(loaded);
    float scale = 0.25f;
    rh_sl_shader_set_param(shader, "scale", &scale, 1);
    rh_sl_vm_shader_exec(&ctx, shader);
    rh_sl_shader_free(shader);

    check_color("exec_param_override", "Ci", ctx.Ci,
                (RhColor){0.2f, 0.05f, 0.025f}, TOL);

    rh_sl_program_free(loaded);
}

/* ------------------------------------------------------------------ */
/*  Test: parameter register indices preserved                         */
/* ------------------------------------------------------------------ */

static void test_param_registers(void) {
    const char* src =
        "surface preg(float a = 1; color c = color(1,1,1)) {\n"
        "    Ci = c * a;\n"
        "    Oi = Os;\n"
        "}";
    RhSLProgram* orig, *loaded;
    if (round_trip_both(src, &orig, &loaded) != 0) {
        FAIL("param_registers", "round-trip failed");
        return;
    }

    for (int i = 0; i < loaded->num_params; i++) {
        CHECK("param_registers",
              loaded->params[i].reg == orig->params[i].reg,
              "register index mismatch");
    }

    rh_sl_program_free(orig);
    rh_sl_program_free(loaded);
}

/* ------------------------------------------------------------------ */
/*  Test: integration with rh_slc (compile .sl to .slo via driver)     */
/* ------------------------------------------------------------------ */

static void test_slc_integration(void) {
    const char* sl_path = "/tmp/test_slc_integ.sl";
    const char* slo_path = "/tmp/test_slc_integ.slo";

    /* Write a .sl file */
    FILE* f = fopen(sl_path, "w");
    if (!f) { FAIL("slc_integration", "cannot write .sl"); return; }
    fprintf(f, "surface integ(float Ka = 1; float Kd = 1) {\n"
               "    Ci = Cs * (Ka * ambient() + Kd * diffuse(faceforward(normalize(N), I)));\n"
               "    Oi = Os;\n"
               "}\n");
    fclose(f);

    /* Compile with rh_slc */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "bin/rh_slc -o %s %s 2>&1", slo_path, sl_path);
    int ret = system(cmd);
    CHECK("slc_integration", ret == 0, "rh_slc returned non-zero");
    if (ret != 0) {
        unlink(sl_path);
        return;
    }

    /* Load and execute */
    RhSLProgram* prog = rh_sl_slo_read(slo_path);
    CHECK("slc_integration", prog != NULL, "rh_sl_slo_read returned NULL");
    if (!prog) {
        unlink(sl_path);
        unlink(slo_path);
        return;
    }

    CHECK("slc_integration",
          strcmp(prog->shader_name, "integ") == 0, "shader name");
    CHECK("slc_integration", prog->num_params == 2, "num params");
    CHECK("slc_integration", prog->code_len > 0, "code present");

    /* Execute */
    RhLight light;
    memset(&light, 0, sizeof(light));
    sl_strcpy(light.type, sizeof(light.type), "distantlight");
    light.color = (RhColor){1.0f, 1.0f, 1.0f};
    light.intensity = 1.0f;
    light.direction = rh_vec3_normalize((RhVec3){-1.0f, -1.0f, 1.0f});

    RhShaderContext ctx;
    init_ctx(&ctx);
    RhSLShader* shader = rh_sl_shader_create(prog);
    ctx.light_list = &light;
    ctx.num_lights = 1;
    rh_sl_vm_shader_exec(&ctx, shader);
    rh_sl_shader_free(shader);

    /* Ci should be non-zero (lit matte) */
    CHECK("slc_integration", ctx.Ci.r > 0.01f, "Ci.r should be non-zero");
    CHECK("slc_integration", ctx.Oi.r > 0.99f, "Oi.r should be ~1");

    rh_sl_program_free(prog);
    unlink(sl_path);
    unlink(slo_path);
}

/* ------------------------------------------------------------------ */
/*  Test: light shader type preserved                                  */
/* ------------------------------------------------------------------ */

static void test_light_shader_type(void) {
    const char* src =
        "light mylight(float intensity = 1;\n"
        "              color lightcolor = color(1,1,1);\n"
        "              point from = point(0,0,0)) {\n"
        "    illuminate(from) {\n"
        "        Cl = lightcolor * intensity;\n"
        "    }\n"
        "}";
    RhSLProgram* loaded = round_trip(src);
    if (!loaded) { FAIL("light_shader_type", "round-trip failed"); return; }

    CHECK("light_shader_type",
          loaded->shader_type == RH_SL_SHADER_LIGHT, "expected light type");
    CHECK("light_shader_type",
          strcmp(loaded->shader_name, "mylight") == 0, "wrong name");
    CHECK("light_shader_type", loaded->num_params == 3, "expected 3 params");

    rh_sl_program_free(loaded);
}

/* ------------------------------------------------------------------ */
/*  Test: shader with arithmetic (more code + constants)               */
/* ------------------------------------------------------------------ */

static void test_arithmetic_shader(void) {
    const char* src =
        "surface arith(float a = 2; float b = 3) {\n"
        "    float x = a * b + 1.5;\n"
        "    Ci = Cs * x;\n"
        "    Oi = Os;\n"
        "}";
    RhSLProgram* orig, *loaded;
    if (round_trip_both(src, &orig, &loaded) != 0) {
        FAIL("arithmetic_shader", "round-trip failed");
        return;
    }

    /* Verify full bytecode match */
    CHECK("arithmetic_shader", loaded->code_len == orig->code_len,
          "code_len mismatch");
    if (loaded->code_len == orig->code_len && loaded->code_len > 0) {
        int match = (memcmp(loaded->code, orig->code,
                            orig->code_len * sizeof(uint64_t)) == 0);
        CHECK("arithmetic_shader", match, "bytecode differs");
    }

    /* Execute loaded and verify */
    RhShaderContext ctx;
    init_ctx(&ctx);
    RhSLShader* shader = rh_sl_shader_create(loaded);
    rh_sl_vm_shader_exec(&ctx, shader);
    rh_sl_shader_free(shader);

    /* a*b+1.5 = 2*3+1.5 = 7.5; Ci = Cs * 7.5 */
    check_color("arithmetic_shader", "Ci", ctx.Ci,
                (RhColor){6.0f, 1.5f, 0.75f}, TOL);

    rh_sl_program_free(orig);
    rh_sl_program_free(loaded);
}

/* ------------------------------------------------------------------ */
/*  Test: control flow shader round-trip                               */
/* ------------------------------------------------------------------ */

static void test_control_flow(void) {
    const char* src =
        "surface ctrlflow(float x = 0.5) {\n"
        "    float r;\n"
        "    if (x > 0.3) {\n"
        "        r = 1;\n"
        "    } else {\n"
        "        r = 0;\n"
        "    }\n"
        "    Ci = color(r, 0, 0);\n"
        "    Oi = Os;\n"
        "}";
    RhSLProgram* loaded = round_trip(src);
    if (!loaded) { FAIL("control_flow", "round-trip failed"); return; }

    RhShaderContext ctx;
    init_ctx(&ctx);
    RhSLShader* shader = rh_sl_shader_create(loaded);
    rh_sl_vm_shader_exec(&ctx, shader);
    rh_sl_shader_free(shader);

    /* x=0.5 > 0.3, so r=1 */
    check_float("control_flow", "Ci.r", ctx.Ci.r, 1.0f, TOL);

    rh_sl_program_free(loaded);
}

/* ------------------------------------------------------------------ */
/*  Test: rh_slc default output name                                   */
/* ------------------------------------------------------------------ */

static void test_slc_default_output(void) {
    const char* sl_path = "/tmp/test_default_out.sl";

    FILE* f = fopen(sl_path, "w");
    if (!f) { FAIL("slc_default_output", "cannot write .sl"); return; }
    fprintf(f, "surface defout() { Ci = Cs; Oi = Os; }\n");
    fclose(f);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "bin/rh_slc %s 2>&1", sl_path);
    int ret = system(cmd);
    CHECK("slc_default_output", ret == 0, "rh_slc returned non-zero");

    /* Check that .slo was created */
    const char* slo_path = "/tmp/test_default_out.slo";
    RhSLProgram* prog = rh_sl_slo_read(slo_path);
    CHECK("slc_default_output", prog != NULL, "default .slo not created");

    if (prog) rh_sl_program_free(prog);
    unlink(sl_path);
    unlink(slo_path);
}

/* ------------------------------------------------------------------ */
/*  Test: rh_slc error handling                                        */
/* ------------------------------------------------------------------ */

static void test_slc_errors(void) {
    const char* sl_path = "/tmp/test_slc_errors.sl";

    FILE* f = fopen(sl_path, "w");
    if (!f) { FAIL("slc_errors", "cannot write .sl"); return; }
    fprintf(f, "surface bad( { }\n");  /* syntax error */
    fclose(f);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "bin/rh_slc %s 2>/dev/null", sl_path);
    int ret = system(cmd);
    CHECK("slc_errors", ret != 0, "rh_slc should fail on bad source");

    unlink(sl_path);
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== .slo File I/O Tests ===\n\n");

    /* Round-trip basics */
    test_empty_shader();
    test_constant_shader();
    test_shader_with_params();
    test_no_constants();
    test_no_strings();

    /* Metadata */
    test_metadata();
    test_light_shader_type();

    /* Parameter fidelity */
    test_float_param_default();
    test_color_param_default();
    test_multiple_params();
    test_param_types();
    test_param_registers();

    /* Code fidelity */
    test_code_fidelity();
    test_const_pool_fidelity();
    test_string_table_fidelity();

    /* Error handling */
    test_read_nonexistent();
    test_read_corrupt();
    test_read_truncated();
    test_write_invalid_path();
    test_write_null();
    test_read_null();
    test_wrong_magic();
    test_wrong_version();

    /* Execution after round-trip */
    test_exec_roundtrip();
    test_exec_param_override();
    test_arithmetic_shader();
    test_control_flow();

    /* Golden test */
    test_golden_matte();

    /* Integration tests (need rh_slc binary) */
    test_slc_integration();
    test_slc_default_output();
    test_slc_errors();

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf("\n");

    /* Clean up temp file */
    unlink(tmp_path);

    return tests_failed > 0 ? 1 : 0;
}
