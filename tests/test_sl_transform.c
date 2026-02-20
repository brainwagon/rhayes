#define _POSIX_C_SOURCE 200809L
/*
 * test_sl_transform.c -- Unit tests for coordinate space transforms in the VM.
 *
 * Tests transform(), ntransform(), vtransform() with 2-arg and 3-arg forms.
 * Compiles SL source through the full pipeline (lex->parse->sema->codegen->VM).
 *
 * Build:  gcc -std=c99 -Wall -Wextra -Werror -pedantic -D_POSIX_C_SOURCE=200809L
 *         -Iinclude -O2 tests/test_sl_transform.c -Llib -lsl -lri -lrh -lm
 *         -o tests/test_sl_transform
 *
 * Run:    ./tests/test_sl_transform
 */

#include "rh_sl_vm.h"
#include "rh_sl_opcodes.h"
#include "rh_sl_lex.h"
#include "rh_sl_parse.h"
#include "rh_sl_sema.h"
#include "rh_sl_codegen.h"
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

static void check_vec3(const char* name, RhVec3 got, RhVec3 expected, float tol) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s.x", name);
    check_float(buf, got.x, expected.x, tol);
    snprintf(buf, sizeof(buf), "%s.y", name);
    check_float(buf, got.y, expected.y, tol);
    snprintf(buf, sizeof(buf), "%s.z", name);
    check_float(buf, got.z, expected.z, tol);
}

/* ------------------------------------------------------------------ */
/*  Helper: compile SL source to program                               */
/* ------------------------------------------------------------------ */

static RhSLProgram* compile_shader(const char* source) {
    RhSLParser parser;
    rh_sl_parse_init(&parser, source);

    RhSLNode* ast = rh_sl_parse(&parser);
    if (!ast) {
        printf("  Parse failed for source\n");
        for (int i = 0; i < parser.num_errors; i++)
            printf("    parse error: %s\n", parser.errors[i]);
        return NULL;
    }

    RhSLSema sema;
    rh_sl_sema_init(&sema);
    if (rh_sl_sema_analyze(&sema, ast) != 0) {
        printf("  Sema failed:\n");
        for (int i = 0; i < sema.num_errors; i++)
            printf("    sema error: %s\n", sema.errors[i]);
        rh_sl_node_free(ast);
        return NULL;
    }

    RhSLCodegenErrors cg_err;
    memset(&cg_err, 0, sizeof(cg_err));
    RhSLProgram* prog = rh_sl_codegen(ast, &cg_err);
    rh_sl_node_free(ast);
    if (!prog) {
        printf("  Codegen failed\n");
        for (int i = 0; i < cg_err.num_errors; i++)
            printf("    codegen error: %s\n", cg_err.errors[i]);
        return NULL;
    }

    return prog;
}

/* ------------------------------------------------------------------ */
/*  Helper: set up a transform context with known matrices             */
/* ------------------------------------------------------------------ */

static void setup_transform_context(RhTransformContext* tc,
                                    RhNamedCoordSys* named, int num_named) {
    /* View matrix: translate (0, 0, -5) = camera at z=-5 looking +z */
    RhMat4 view = rh_mat4_translate(0.0f, 0.0f, 5.0f);
    /* Actually, view_matrix = inverse(camera transform).
     * If camera is at (0,0,-5), view = translate(0,0,5) */
    tc->world_to_camera = view;
    tc->camera_to_world = rh_mat4_inverse(view);

    /* Object: translate (2, 0, 0) then scale 2x */
    RhMat4 obj = rh_mat4_mul(rh_mat4_translate(2.0f, 0.0f, 0.0f),
                              rh_mat4_scale(2.0f, 2.0f, 2.0f));
    tc->object_to_world = obj;
    tc->world_to_object = rh_mat4_inverse(obj);

    /* Simple perspective projection */
    tc->camera_to_screen = rh_mat4_identity();
    tc->camera_to_screen.m[2][2] = 1.0f;
    tc->camera_to_screen.m[2][3] = 1.0f;
    tc->camera_to_screen.m[3][3] = 0.0f;

    tc->named_systems = named;
    tc->num_named_systems = num_named;
    tc->xres = 640;
    tc->yres = 480;
    tc->near_clip = 0.1f;
    tc->far_clip = 1000.0f;
}

/* ------------------------------------------------------------------ */
/*  Test 1: transform("camera", P) should be identity                  */
/* ------------------------------------------------------------------ */

static void test_transform_camera(void) {
    printf("Test: transform(\"camera\", P) = identity\n");

    const char* src =
        "surface test_xform() {\n"
        "    point Pw = transform(\"camera\", P);\n"
        "    Ci = color(xcomp(Pw), ycomp(Pw), zcomp(Pw));\n"
        "    Oi = Os;\n"
        "}\n";

    RhSLProgram* prog = compile_shader(src);
    if (!prog) return;

    RhSLShader* shader = rh_sl_shader_create(prog);

    RhTransformContext tc;
    setup_transform_context(&tc, NULL, 0);

    RhShaderContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.P = rh_vec3_create(1.0f, 2.0f, 3.0f);
    ctx.N = rh_vec3_create(0.0f, 0.0f, -1.0f);
    ctx.I = ctx.P;
    ctx.Cs = (RhColor){1, 1, 1};
    ctx.Os = (RhColor){1, 1, 1};
    ctx.transform_ctx = &tc;

    rh_sl_vm_shader_exec(&ctx, shader);

    /* Should be unchanged */
    RhVec3 expected = {1.0f, 2.0f, 3.0f};
    RhVec3 got = {ctx.Ci.r, ctx.Ci.g, ctx.Ci.b};
    check_vec3("transform(camera,P)", got, expected, 0.001f);

    rh_sl_shader_free(shader);
    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test 2: transform("world", P) should convert camera->world         */
/* ------------------------------------------------------------------ */

static void test_transform_world(void) {
    printf("Test: transform(\"world\", P) = camera->world\n");

    const char* src =
        "surface test_xform() {\n"
        "    point Pw = transform(\"world\", P);\n"
        "    Ci = color(xcomp(Pw), ycomp(Pw), zcomp(Pw));\n"
        "    Oi = Os;\n"
        "}\n";

    RhSLProgram* prog = compile_shader(src);
    if (!prog) return;

    RhSLShader* shader = rh_sl_shader_create(prog);

    RhTransformContext tc;
    setup_transform_context(&tc, NULL, 0);

    /* P in camera space = (1, 2, 3) */
    RhShaderContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.P = rh_vec3_create(1.0f, 2.0f, 3.0f);
    ctx.N = rh_vec3_create(0.0f, 0.0f, -1.0f);
    ctx.I = ctx.P;
    ctx.Cs = (RhColor){1, 1, 1};
    ctx.Os = (RhColor){1, 1, 1};
    ctx.transform_ctx = &tc;

    rh_sl_vm_shader_exec(&ctx, shader);

    /* camera_to_world = inverse(translate(0,0,5)) = translate(0,0,-5)
     * So world P = (1, 2, 3-5) = (1, 2, -2) */
    RhVec3 expected = rh_mat4_mul_point(tc.camera_to_world, ctx.P);
    RhVec3 got = {ctx.Ci.r, ctx.Ci.g, ctx.Ci.b};
    check_vec3("transform(world,P)", got, expected, 0.001f);

    rh_sl_shader_free(shader);
    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test 3: transform("object", P) should convert camera->object       */
/* ------------------------------------------------------------------ */

static void test_transform_object(void) {
    printf("Test: transform(\"object\", P) = camera->object\n");

    const char* src =
        "surface test_xform() {\n"
        "    point Po = transform(\"object\", P);\n"
        "    Ci = color(xcomp(Po), ycomp(Po), zcomp(Po));\n"
        "    Oi = Os;\n"
        "}\n";

    RhSLProgram* prog = compile_shader(src);
    if (!prog) return;

    RhSLShader* shader = rh_sl_shader_create(prog);

    RhTransformContext tc;
    setup_transform_context(&tc, NULL, 0);

    RhShaderContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.P = rh_vec3_create(1.0f, 2.0f, 3.0f);
    ctx.N = rh_vec3_create(0.0f, 0.0f, -1.0f);
    ctx.I = ctx.P;
    ctx.Cs = (RhColor){1, 1, 1};
    ctx.Os = (RhColor){1, 1, 1};
    ctx.transform_ctx = &tc;

    rh_sl_vm_shader_exec(&ctx, shader);

    /* camera->world->object = world_to_object * camera_to_world * P */
    RhMat4 cam_to_obj = rh_mat4_mul(tc.world_to_object, tc.camera_to_world);
    RhVec3 expected = rh_mat4_mul_point(cam_to_obj, ctx.P);
    RhVec3 got = {ctx.Ci.r, ctx.Ci.g, ctx.Ci.b};
    check_vec3("transform(object,P)", got, expected, 0.001f);

    rh_sl_shader_free(shader);
    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test 4: ntransform("world", N) for normals                         */
/* ------------------------------------------------------------------ */

static void test_ntransform_world(void) {
    printf("Test: ntransform(\"world\", N)\n");

    const char* src =
        "surface test_xform() {\n"
        "    normal Nw = ntransform(\"world\", N);\n"
        "    Ci = color(xcomp(Nw), ycomp(Nw), zcomp(Nw));\n"
        "    Oi = Os;\n"
        "}\n";

    RhSLProgram* prog = compile_shader(src);
    if (!prog) return;

    RhSLShader* shader = rh_sl_shader_create(prog);

    RhTransformContext tc;
    setup_transform_context(&tc, NULL, 0);

    RhShaderContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.P = rh_vec3_create(1.0f, 2.0f, 3.0f);
    ctx.N = rh_vec3_create(0.0f, 1.0f, 0.0f);
    ctx.I = ctx.P;
    ctx.Cs = (RhColor){1, 1, 1};
    ctx.Os = (RhColor){1, 1, 1};
    ctx.transform_ctx = &tc;

    rh_sl_vm_shader_exec(&ctx, shader);

    /* Normal transform: transpose(inverse(M)) * n
     * For camera_to_world (pure translation), normal is unchanged */
    RhMat4 inv_t = rh_mat4_transpose(rh_mat4_inverse(tc.camera_to_world));
    RhVec3 expected = rh_mat4_mul_dir(inv_t, ctx.N);
    RhVec3 got = {ctx.Ci.r, ctx.Ci.g, ctx.Ci.b};
    check_vec3("ntransform(world,N)", got, expected, 0.001f);

    rh_sl_shader_free(shader);
    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test 5: vtransform("world", V) for vectors                         */
/* ------------------------------------------------------------------ */

static void test_vtransform_world(void) {
    printf("Test: vtransform(\"world\", I)\n");

    const char* src =
        "surface test_xform() {\n"
        "    vector Vw = vtransform(\"world\", I);\n"
        "    Ci = color(xcomp(Vw), ycomp(Vw), zcomp(Vw));\n"
        "    Oi = Os;\n"
        "}\n";

    RhSLProgram* prog = compile_shader(src);
    if (!prog) return;

    RhSLShader* shader = rh_sl_shader_create(prog);

    RhTransformContext tc;
    setup_transform_context(&tc, NULL, 0);

    RhShaderContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.P = rh_vec3_create(1.0f, 2.0f, 3.0f);
    ctx.N = rh_vec3_create(0.0f, 0.0f, -1.0f);
    ctx.I = rh_vec3_create(0.5f, 0.3f, 0.8f);
    ctx.Cs = (RhColor){1, 1, 1};
    ctx.Os = (RhColor){1, 1, 1};
    ctx.transform_ctx = &tc;

    rh_sl_vm_shader_exec(&ctx, shader);

    /* Vector transform: M * v (w=0), no translation */
    RhVec3 expected = rh_mat4_mul_dir(tc.camera_to_world, ctx.I);
    RhVec3 got = {ctx.Ci.r, ctx.Ci.g, ctx.Ci.b};
    check_vec3("vtransform(world,I)", got, expected, 0.001f);

    rh_sl_shader_free(shader);
    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test 6: 3-arg transform("world", "object", P)                     */
/* ------------------------------------------------------------------ */

static void test_transform_3arg(void) {
    printf("Test: transform(\"world\", \"object\", P) = world->object in camera space\n");

    /* This shader first converts P from "world" to current (camera),
     * then from current to "object".
     * But since P is already in camera space, the first step (INV "world")
     * converts camera->world->camera = identity? No.
     *
     * 3-arg: transform("from", "to", val)
     * Decomposition:
     *   1. OP_TRANSFORM_INV tmp, val, "from" -> convert val FROM "from" TO current
     *   2. OP_TRANSFORM dst, tmp, "to" -> convert tmp FROM current TO "to"
     *
     * So transform("world", "object", P):
     *   P is in camera space (current).
     *   Step 1: INV "world" means: inverse(camera_to_world) * P = world_to_camera * P
     *           This converts camera-space P to... wait.
     *
     * Actually: OP_TRANSFORM_INV computes inverse of the current-to-target matrix.
     * current-to-target for "world" = camera_to_world.
     * inverse = world_to_camera.
     * So step 1: tmp = world_to_camera * P. But P is already in camera space.
     * That seems wrong...
     *
     * Wait - the semantics of transform("from", "to", val) means:
     * val is in "from" space. Convert it to "to" space.
     * So if val is actually in world space, we want: obj_from_world * val.
     *
     * The decomposition: first convert from "from" to current, then current to "to":
     * result = to_from_current(current_from_from(val))
     *
     * Let's test with a known world-space point. We'll put the world-space
     * coords into a parameter and transform it.
     */

    const char* src =
        "surface test_xform(point Pworld = point(0,0,0)) {\n"
        "    point Po = transform(\"world\", \"object\", Pworld);\n"
        "    Ci = color(xcomp(Po), ycomp(Po), zcomp(Po));\n"
        "    Oi = Os;\n"
        "}\n";

    RhSLProgram* prog = compile_shader(src);
    if (!prog) return;

    RhSLShader* shader = rh_sl_shader_create(prog);

    /* Set Pworld parameter to a known world-space point */
    float pw[3] = {4.0f, 1.0f, -2.0f};
    rh_sl_shader_set_param(shader, "Pworld", pw, 3);

    RhTransformContext tc;
    setup_transform_context(&tc, NULL, 0);

    RhShaderContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.P = rh_vec3_create(1.0f, 2.0f, 3.0f);
    ctx.N = rh_vec3_create(0.0f, 0.0f, -1.0f);
    ctx.I = ctx.P;
    ctx.Cs = (RhColor){1, 1, 1};
    ctx.Os = (RhColor){1, 1, 1};
    ctx.transform_ctx = &tc;

    rh_sl_vm_shader_exec(&ctx, shader);

    /* The 3-arg decomposes to:
     * Step 1: INV "world": inv(camera_to_world) * Pworld = world_to_camera * Pworld
     *   This converts Pworld (which the shader treats as being in "world" space)
     *   into camera space (current).
     * Step 2: FWD "object": camera_to_object * tmp
     *   world_to_object * camera_to_world... wait no.
     *
     * Let me trace through carefully:
     * OP_TRANSFORM_INV with space="world":
     *   mat = camera_to_world (the current-to-target matrix for "world")
     *   inverse=true: mat = inverse(camera_to_world) = world_to_camera
     *   result = world_to_camera * Pworld  (point in camera space)
     *
     * OP_TRANSFORM with space="object":
     *   mat = world_to_object * camera_to_world (current-to-target for "object")
     *   result = (world_to_object * camera_to_world) * (world_to_camera * Pworld)
     *          = world_to_object * Pworld
     *
     * So the result is world_to_object * Pworld. Which is correct!
     */
    RhVec3 pworld = {4.0f, 1.0f, -2.0f};
    RhVec3 expected = rh_mat4_mul_point(tc.world_to_object, pworld);
    RhVec3 got = {ctx.Ci.r, ctx.Ci.g, ctx.Ci.b};
    check_vec3("transform(world,object,Pworld)", got, expected, 0.001f);

    rh_sl_shader_free(shader);
    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test 7: Named coordinate system                                    */
/* ------------------------------------------------------------------ */

static void test_transform_named(void) {
    printf("Test: transform(\"myspace\", P) with named coordinate system\n");

    const char* src =
        "surface test_xform() {\n"
        "    point Pm = transform(\"myspace\", P);\n"
        "    Ci = color(xcomp(Pm), ycomp(Pm), zcomp(Pm));\n"
        "    Oi = Os;\n"
        "}\n";

    RhSLProgram* prog = compile_shader(src);
    if (!prog) return;

    RhSLShader* shader = rh_sl_shader_create(prog);

    /* Set up a named coordinate system "myspace" with a known transform */
    RhNamedCoordSys named;
    memcpy(named.name, "myspace", 8);
    /* Named space is rotated 90 deg and translated (10, 0, 0) in world space */
    named.matrix = rh_mat4_translate(10.0f, 0.0f, 0.0f);

    RhTransformContext tc;
    setup_transform_context(&tc, &named, 1);

    RhShaderContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.P = rh_vec3_create(1.0f, 2.0f, 3.0f);
    ctx.N = rh_vec3_create(0.0f, 0.0f, -1.0f);
    ctx.I = ctx.P;
    ctx.Cs = (RhColor){1, 1, 1};
    ctx.Os = (RhColor){1, 1, 1};
    ctx.transform_ctx = &tc;

    rh_sl_vm_shader_exec(&ctx, shader);

    /* current-to-target for "myspace":
     * inv(named_to_world) * camera_to_world
     * = inv(translate(10,0,0)) * translate(0,0,-5)
     * = translate(-10,0,0) * translate(0,0,-5)
     * = translate(-10, 0, -5)
     *
     * result = translate(-10, 0, -5) * (1, 2, 3) = (-9, 2, -2)
     */
    RhMat4 inv_named = rh_mat4_inverse(named.matrix);
    RhMat4 cam_to_named = rh_mat4_mul(inv_named, tc.camera_to_world);
    RhVec3 expected = rh_mat4_mul_point(cam_to_named, ctx.P);
    RhVec3 got = {ctx.Ci.r, ctx.Ci.g, ctx.Ci.b};
    check_vec3("transform(myspace,P)", got, expected, 0.001f);

    rh_sl_shader_free(shader);
    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test 8: transform("current", P) and transform("shader", P)        */
/* ------------------------------------------------------------------ */

static void test_transform_current_shader(void) {
    printf("Test: transform(\"current\"/\"shader\", P) = identity\n");

    const char* src =
        "surface test_xform() {\n"
        "    point Pc = transform(\"current\", P);\n"
        "    Ci = color(xcomp(Pc), ycomp(Pc), zcomp(Pc));\n"
        "    Oi = Os;\n"
        "}\n";

    RhSLProgram* prog = compile_shader(src);
    if (!prog) return;

    RhSLShader* shader = rh_sl_shader_create(prog);

    RhTransformContext tc;
    setup_transform_context(&tc, NULL, 0);

    RhShaderContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.P = rh_vec3_create(5.0f, -3.0f, 7.0f);
    ctx.N = rh_vec3_create(0.0f, 0.0f, -1.0f);
    ctx.I = ctx.P;
    ctx.Cs = (RhColor){1, 1, 1};
    ctx.Os = (RhColor){1, 1, 1};
    ctx.transform_ctx = &tc;

    rh_sl_vm_shader_exec(&ctx, shader);

    RhVec3 expected = {5.0f, -3.0f, 7.0f};
    RhVec3 got = {ctx.Ci.r, ctx.Ci.g, ctx.Ci.b};
    check_vec3("transform(current,P)", got, expected, 0.001f);

    rh_sl_shader_free(shader);
    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  Test 9: RiCoordinateSystem storage                                 */
/* ------------------------------------------------------------------ */

static void test_coordinate_system_storage(void) {
    printf("Test: RiCoordinateSystem storage\n");

    RiBegin(NULL);
    RiTranslate(5.0f, 3.0f, 1.0f);
    RiCoordinateSystem("testspace");

    RiContextData* ctx = ri_get_ctx();
    tests_run++;
    if (ctx->num_named_coord_sys == 1 &&
        strcmp(ctx->named_coord_sys[0].name, "testspace") == 0) {
        tests_passed++;
    } else {
        printf("  FAIL: CoordinateSystem not stored (count=%d)\n",
               ctx->num_named_coord_sys);
    }

    /* Verify the stored matrix includes the current transform */
    RhMat4 expected = ri_curr()->transform;
    tests_run++;
    if (rh_mat4_equal(ctx->named_coord_sys[0].matrix, expected)) {
        tests_passed++;
    } else {
        printf("  FAIL: CoordinateSystem matrix doesn't match CTM\n");
    }

    /* Test overwrite */
    RiTranslate(1.0f, 0.0f, 0.0f);
    RiCoordinateSystem("testspace");
    tests_run++;
    if (ctx->num_named_coord_sys == 1) {
        tests_passed++;
    } else {
        printf("  FAIL: CoordinateSystem should overwrite (count=%d)\n",
               ctx->num_named_coord_sys);
    }

    RiEnd();
}

/* ------------------------------------------------------------------ */
/*  Test 10: 3-arg vtransform                                          */
/* ------------------------------------------------------------------ */

static void test_vtransform_3arg(void) {
    printf("Test: vtransform(\"camera\", \"world\", I)\n");

    const char* src =
        "surface test_xform() {\n"
        "    vector Vw = vtransform(\"camera\", \"world\", I);\n"
        "    Ci = color(xcomp(Vw), ycomp(Vw), zcomp(Vw));\n"
        "    Oi = Os;\n"
        "}\n";

    RhSLProgram* prog = compile_shader(src);
    if (!prog) return;

    RhSLShader* shader = rh_sl_shader_create(prog);

    RhTransformContext tc;
    setup_transform_context(&tc, NULL, 0);

    RhShaderContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.P = rh_vec3_create(1.0f, 2.0f, 3.0f);
    ctx.N = rh_vec3_create(0.0f, 0.0f, -1.0f);
    ctx.I = rh_vec3_create(0.5f, 0.3f, 0.8f);
    ctx.Cs = (RhColor){1, 1, 1};
    ctx.Os = (RhColor){1, 1, 1};
    ctx.transform_ctx = &tc;

    rh_sl_vm_shader_exec(&ctx, shader);

    /* vtransform("camera", "world", I):
     * Step 1: INV "camera": inv(identity) * I = I (still camera)
     * Step 2: FWD "world": camera_to_world * I (as vector)
     * Result = camera_to_world * I (w=0, direction only)
     */
    RhVec3 expected = rh_mat4_mul_dir(tc.camera_to_world, ctx.I);
    RhVec3 got = {ctx.Ci.r, ctx.Ci.g, ctx.Ci.b};
    check_vec3("vtransform(camera,world,I)", got, expected, 0.001f);

    rh_sl_shader_free(shader);
    rh_sl_program_free(prog);
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== Shading Language Transform Tests ===\n\n");

    test_transform_camera();
    test_transform_world();
    test_transform_object();
    test_ntransform_world();
    test_vtransform_world();
    test_transform_3arg();
    test_transform_named();
    test_transform_current_shader();
    test_coordinate_system_storage();
    test_vtransform_3arg();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
