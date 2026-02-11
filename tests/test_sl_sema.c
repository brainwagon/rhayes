/*
 * Semantic analysis test suite.
 *
 * Build:
 *   gcc -std=c99 -Wall -Wextra -Werror -pedantic -Iinclude -O2 \
 *       tests/test_sl_sema.c -Llib -lrh -lri -lm -o tests/test_sl_sema
 *
 * Run:
 *   ./tests/test_sl_sema
 */

#include "rh_sl_parse.h"
#include "rh_sl_sema.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

static void check(const char* name, int cond) {
    tests_run++;
    if (cond) {
        tests_passed++;
    } else {
        printf("  FAIL: %s\n", name);
    }
}

/* Helper: parse source and run semantic analysis.
 * Returns the AST (caller must free). sema is populated. */
static RhSLNode* parse_and_analyze(const char* src, RhSLSema* sema) {
    RhSLParser parser;
    rh_sl_parse_init(&parser, src);
    RhSLNode* ast = rh_sl_parse(&parser);
    if (!ast) return NULL;
    rh_sl_sema_analyze(sema, ast);
    return ast;
}

/* ------------------------------------------------------------------ */
/*  Type resolution tests                                              */
/* ------------------------------------------------------------------ */

static void test_type_resolution(void) {
    printf("Test: type resolution...\n");

    /* Float literal */
    {
        const char* src =
            "surface test() { float x = 1.0; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("float lit: no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
        check("float lit: var_decl resolved", s1->resolved_type == SL_TYPE_FLOAT);
        rh_sl_node_free(ast);
    }

    /* Builtin globals resolve to correct type */
    {
        const char* src =
            "surface test() { Ci = Cs; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("builtins: no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
        check("builtins: assign stmt", s1->node_type == SL_NODE_ASSIGN);
        check("builtins: target is color",
              s1->u.assign.target->resolved_type == SL_TYPE_COLOR);
        check("builtins: value is color",
              s1->u.assign.value->resolved_type == SL_TYPE_COLOR);
        rh_sl_node_free(ast);
    }

    /* Shader parameters resolve to declared type */
    {
        const char* src =
            "surface test(float Ka = 1; color spec = 1) { float x = Ka; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("params: no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
        check("params: init type float",
              s1->u.var_decl.init->resolved_type == SL_TYPE_FLOAT);
        rh_sl_node_free(ast);
    }

    /* Variable declaration with type */
    {
        const char* src =
            "surface test() { vector V = I; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("var_decl vector: no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
        check("var_decl vector: resolved", s1->resolved_type == SL_TYPE_VECTOR);
        rh_sl_node_free(ast);
    }
}

/* ------------------------------------------------------------------ */
/*  Binary operator tests                                              */
/* ------------------------------------------------------------------ */

static void test_binops(void) {
    printf("Test: binary operators...\n");

    /* float + float = float */
    {
        const char* src = "surface test() { float x = 1.0 + 2.0; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("f+f: no errors", sema.num_errors == 0);
        RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
        check("f+f: init is binop", s1->u.var_decl.init->node_type == SL_NODE_BINOP);
        check("f+f: result float", s1->u.var_decl.init->resolved_type == SL_TYPE_FLOAT);
        rh_sl_node_free(ast);
    }

    /* color * color = color */
    {
        const char* src = "surface test() { color c = Cs * Cs; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("c*c: no errors", sema.num_errors == 0);
        RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
        check("c*c: result color", s1->u.var_decl.init->resolved_type == SL_TYPE_COLOR);
        rh_sl_node_free(ast);
    }

    /* float * color = color (promotion) */
    {
        const char* src = "surface test() { color c = 0.5 * Cs; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("f*c: no errors", sema.num_errors == 0);
        RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
        check("f*c: result color", s1->u.var_decl.init->resolved_type == SL_TYPE_COLOR);
        rh_sl_node_free(ast);
    }

    /* color * float = color */
    {
        const char* src = "surface test() { color c = Cs * 0.5; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("c*f: no errors", sema.num_errors == 0);
        RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
        check("c*f: result color", s1->u.var_decl.init->resolved_type == SL_TYPE_COLOR);
        rh_sl_node_free(ast);
    }

    /* dot product: vector . vector = float */
    {
        const char* src = "surface test() { float d = N . I; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("dot: no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
        check("dot: result float", s1->u.var_decl.init->resolved_type == SL_TYPE_FLOAT);
        rh_sl_node_free(ast);
    }

    /* cross product: vector ^ vector = vector */
    {
        const char* src = "surface test() { vector c = I ^ I; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("cross: no errors", sema.num_errors == 0);
        RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
        check("cross: result vector", s1->u.var_decl.init->resolved_type == SL_TYPE_VECTOR);
        rh_sl_node_free(ast);
    }

    /* comparison: float < float = float */
    {
        const char* src = "surface test() { float b = 1.0 < 2.0; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("cmp: no errors", sema.num_errors == 0);
        RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
        check("cmp: result float", s1->u.var_decl.init->resolved_type == SL_TYPE_FLOAT);
        rh_sl_node_free(ast);
    }

    /* logic: a && b = float */
    {
        const char* src = "surface test() { float b = 1.0 && 0.0; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("logic: no errors", sema.num_errors == 0);
        RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
        check("logic: result float", s1->u.var_decl.init->resolved_type == SL_TYPE_FLOAT);
        rh_sl_node_free(ast);
    }

    /* point - point = vector */
    {
        const char* src = "surface test() { vector v = P - E; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("p-p: no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
        check("p-p: result vector", s1->u.var_decl.init->resolved_type == SL_TYPE_VECTOR);
        rh_sl_node_free(ast);
    }
}

/* ------------------------------------------------------------------ */
/*  Unary operator tests                                               */
/* ------------------------------------------------------------------ */

static void test_unops(void) {
    printf("Test: unary operators...\n");

    /* negation of float */
    {
        const char* src = "surface test() { float x = -1.0; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("neg float: no errors", sema.num_errors == 0);
        rh_sl_node_free(ast);
    }

    /* negation of vector */
    {
        const char* src = "surface test() { vector v = -I; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("neg vector: no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
        check("neg vector: type preserved",
              s1->u.var_decl.init->resolved_type == SL_TYPE_VECTOR);
        rh_sl_node_free(ast);
    }

    /* logical not -> float */
    {
        const char* src = "surface test() { float b = !1.0; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("not: no errors", sema.num_errors == 0);
        RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
        check("not: result float", s1->u.var_decl.init->resolved_type == SL_TYPE_FLOAT);
        rh_sl_node_free(ast);
    }
}

/* ------------------------------------------------------------------ */
/*  Ternary tests                                                      */
/* ------------------------------------------------------------------ */

static void test_ternary(void) {
    printf("Test: ternary operator...\n");

    /* same types */
    {
        const char* src = "surface test() { float x = 1.0 > 0.0 ? 1.0 : 0.0; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("ternary same: no errors", sema.num_errors == 0);
        RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
        check("ternary same: float", s1->u.var_decl.init->resolved_type == SL_TYPE_FLOAT);
        rh_sl_node_free(ast);
    }

    /* promotion: float / color */
    {
        const char* src = "surface test() { color c = 1.0 > 0.0 ? 1.0 : Cs; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("ternary promo: no errors", sema.num_errors == 0);
        RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
        check("ternary promo: color", s1->u.var_decl.init->resolved_type == SL_TYPE_COLOR);
        rh_sl_node_free(ast);
    }

    /* both tuple */
    {
        const char* src = "surface test() { color c = 1.0 > 0.0 ? Cs : Os; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("ternary tuple: no errors", sema.num_errors == 0);
        RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
        check("ternary tuple: color", s1->u.var_decl.init->resolved_type == SL_TYPE_COLOR);
        rh_sl_node_free(ast);
    }
}

/* ------------------------------------------------------------------ */
/*  Type constructor tests                                             */
/* ------------------------------------------------------------------ */

static void test_type_constructors(void) {
    printf("Test: type constructors...\n");

    /* color(r,g,b) */
    {
        const char* src = "surface test() { color c = color(1,0,0); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("color(r,g,b): no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
        check("color(r,g,b): type", s1->u.var_decl.init->resolved_type == SL_TYPE_COLOR);
        rh_sl_node_free(ast);
    }

    /* color(f) -- splat */
    {
        const char* src = "surface test() { color c = color(0.5); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("color(f): no errors", sema.num_errors == 0);
        rh_sl_node_free(ast);
    }

    /* point constructor */
    {
        const char* src = "surface test() { point p = point(1,2,3); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("point(x,y,z): no errors", sema.num_errors == 0);
        check("point(x,y,z): type",
              ast->u.shader.body->u.block.stmts->u.var_decl.init->resolved_type == SL_TYPE_POINT);
        rh_sl_node_free(ast);
    }

    /* vector constructor */
    {
        const char* src = "surface test() { vector v = vector(0,1,0); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("vector(x,y,z): no errors", sema.num_errors == 0);
        rh_sl_node_free(ast);
    }

    /* wrong arg count -> error */
    {
        const char* src = "surface test() { color c = color(1,2); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("color(a,b) error: has errors", sema.num_errors > 0);
        rh_sl_node_free(ast);
    }
}

/* ------------------------------------------------------------------ */
/*  Built-in function tests                                            */
/* ------------------------------------------------------------------ */

static void test_builtin_functions(void) {
    printf("Test: built-in functions...\n");

    /* 1-arg math: sqrt(float) -> float */
    {
        const char* src = "surface test() { float x = sqrt(2.0); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("sqrt: no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        rh_sl_node_free(ast);
    }

    /* 2-arg math: pow(float,float) -> float */
    {
        const char* src = "surface test() { float x = pow(2.0, 3.0); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("pow: no errors", sema.num_errors == 0);
        rh_sl_node_free(ast);
    }

    /* 3-arg math: clamp(float,float,float) -> float */
    {
        const char* src = "surface test() { float x = clamp(0.5, 0.0, 1.0); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("clamp: no errors", sema.num_errors == 0);
        rh_sl_node_free(ast);
    }

    /* smoothstep */
    {
        const char* src = "surface test() { float x = smoothstep(0.0, 1.0, 0.5); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("smoothstep: no errors", sema.num_errors == 0);
        rh_sl_node_free(ast);
    }

    /* normalize(normal) -> normal */
    {
        const char* src = "surface test() { normal Nf = normalize(N); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("normalize(N): no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
        check("normalize(N): type normal",
              s1->u.var_decl.init->resolved_type == SL_TYPE_NORMAL);
        rh_sl_node_free(ast);
    }

    /* normalize(vector) -> vector */
    {
        const char* src = "surface test() { vector v = normalize(I); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("normalize(V): no errors", sema.num_errors == 0);
        RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
        check("normalize(V): type vector",
              s1->u.var_decl.init->resolved_type == SL_TYPE_VECTOR);
        rh_sl_node_free(ast);
    }

    /* faceforward(normal, vector) -> normal */
    {
        const char* src = "surface test() { normal Nf = faceforward(N, I); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("faceforward: no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
        check("faceforward: type normal",
              s1->u.var_decl.init->resolved_type == SL_TYPE_NORMAL);
        rh_sl_node_free(ast);
    }

    /* dot product via function call */
    {
        const char* src = "surface test() { float d = dot(I, N); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("dot(): no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        rh_sl_node_free(ast);
    }

    /* ambient() -> color */
    {
        const char* src = "surface test() { color c = ambient(); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("ambient: no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
        check("ambient: type color", s1->u.var_decl.init->resolved_type == SL_TYPE_COLOR);
        rh_sl_node_free(ast);
    }

    /* diffuse(normal) -> color */
    {
        const char* src = "surface test() { color c = diffuse(N); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("diffuse: no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        rh_sl_node_free(ast);
    }

    /* specular(normal, vector, float) -> color */
    {
        const char* src =
            "surface test() {\n"
            "    color c = specular(N, -normalize(I), 0.1);\n"
            "}";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("specular: no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        rh_sl_node_free(ast);
    }

    /* length(vector) -> float */
    {
        const char* src = "surface test() { float l = length(I); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("length: no errors", sema.num_errors == 0);
        rh_sl_node_free(ast);
    }

    /* distance(point, point) -> float */
    {
        const char* src = "surface test() { float d = distance(P, E); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("distance: no errors", sema.num_errors == 0);
        rh_sl_node_free(ast);
    }

    /* mix(color, color, float) -> color */
    {
        const char* src = "surface test() { color c = mix(Cs, Os, 0.5); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("mix(color): no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
        check("mix(color): type color",
              s1->u.var_decl.init->resolved_type == SL_TYPE_COLOR);
        rh_sl_node_free(ast);
    }

    /* noise(float) -> float */
    {
        const char* src = "surface test() { float n = noise(s); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("noise(f): no errors", sema.num_errors == 0);
        rh_sl_node_free(ast);
    }

    /* noise(point) -> float */
    {
        const char* src = "surface test() { float n = noise(P); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("noise(P): no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        rh_sl_node_free(ast);
    }

    /* texture(string, float, float) -> color */
    {
        const char* src =
            "surface test() { color c = texture(\"foo.tex\", s, t); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("texture: no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        rh_sl_node_free(ast);
    }

    /* unknown function -> error */
    {
        const char* src = "surface test() { float x = bogus(1.0); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("unknown func: has errors", sema.num_errors > 0);
        rh_sl_node_free(ast);
    }
}

/* ------------------------------------------------------------------ */
/*  User-defined function tests                                        */
/* ------------------------------------------------------------------ */

static void test_user_functions(void) {
    printf("Test: user-defined functions...\n");

    /* Correct call to user function */
    {
        const char* src =
            "float square(float x) { return x * x; }\n"
            "surface test() { float y = square(3.0); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("user func call: no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
        check("user func: return type float",
              s1->u.var_decl.init->resolved_type == SL_TYPE_FLOAT);
        rh_sl_node_free(ast);
    }

    /* Wrong arg count -> error */
    {
        const char* src =
            "float square(float x) { return x * x; }\n"
            "surface test() { float y = square(1.0, 2.0); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("user func wrong args: has errors", sema.num_errors > 0);
        rh_sl_node_free(ast);
    }

    /* User function returning color */
    {
        const char* src =
            "color tint(color base; float amt) { return base * amt; }\n"
            "surface test() { color c = tint(Cs, 0.5); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("user func color: no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
        check("user func color: return type",
              s1->u.var_decl.init->resolved_type == SL_TYPE_COLOR);
        rh_sl_node_free(ast);
    }
}

/* ------------------------------------------------------------------ */
/*  Assignment tests                                                   */
/* ------------------------------------------------------------------ */

static void test_assignment(void) {
    printf("Test: assignment...\n");

    /* Compatible: color = color */
    {
        const char* src = "surface test() { Ci = Cs; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("assign c=c: no errors", sema.num_errors == 0);
        rh_sl_node_free(ast);
    }

    /* Promotion: color = float */
    {
        const char* src = "surface test() { Ci = 1.0; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("assign c=f: no errors", sema.num_errors == 0);
        rh_sl_node_free(ast);
    }

    /* Incompatible: float = string -> error */
    {
        const char* src = "surface test() { float x = 1.0; x = \"hello\"; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("assign f=s: has errors", sema.num_errors > 0);
        rh_sl_node_free(ast);
    }
}

/* ------------------------------------------------------------------ */
/*  Read/write tests                                                   */
/* ------------------------------------------------------------------ */

static void test_readwrite(void) {
    printf("Test: read/write checks...\n");

    /* Write to Ng (read-only) -> error */
    {
        const char* src = "surface test() { Ng = N; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("write Ng: has errors", sema.num_errors > 0);
        rh_sl_node_free(ast);
    }

    /* Write to u (read-only) -> error */
    {
        const char* src = "surface test() { u = 0.5; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("write u: has errors", sema.num_errors > 0);
        rh_sl_node_free(ast);
    }

    /* Write to Ci (writable) -> ok */
    {
        const char* src = "surface test() { Ci = Cs; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("write Ci: no errors", sema.num_errors == 0);
        rh_sl_node_free(ast);
    }

    /* Write to P (writable) -> ok */
    {
        const char* src = "surface test() { P = P; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("write P: no errors", sema.num_errors == 0);
        rh_sl_node_free(ast);
    }

    /* Write to I (read-only) -> error */
    {
        const char* src = "surface test() { I = I; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("write I: has errors", sema.num_errors > 0);
        rh_sl_node_free(ast);
    }
}

/* ------------------------------------------------------------------ */
/*  Compound assignment tests                                          */
/* ------------------------------------------------------------------ */

static void test_compound_assign(void) {
    printf("Test: compound assignment...\n");

    /* float += float */
    {
        const char* src = "surface test() { float x = 1.0; x += 2.0; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("f+=f: no errors", sema.num_errors == 0);
        rh_sl_node_free(ast);
    }

    /* color *= float */
    {
        const char* src = "surface test() { Ci *= 0.5; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("c*=f: no errors", sema.num_errors == 0);
        rh_sl_node_free(ast);
    }
}

/* ------------------------------------------------------------------ */
/*  Scoping tests                                                      */
/* ------------------------------------------------------------------ */

static void test_scoping(void) {
    printf("Test: scoping...\n");

    /* Nested shadowing: inner x hides outer */
    {
        const char* src =
            "surface test() {\n"
            "    float x = 1.0;\n"
            "    {\n"
            "        color x = Cs;\n"
            "        Ci = x;\n"  /* should be color */
            "    }\n"
            "}";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("shadow: no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        rh_sl_node_free(ast);
    }

    /* After scope pop, inner not visible */
    {
        const char* src =
            "surface test() {\n"
            "    {\n"
            "        float inner = 1.0;\n"
            "    }\n"
            "    float x = inner;\n"  /* inner not visible -> error */
            "}";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("pop visibility: has errors", sema.num_errors > 0);
        rh_sl_node_free(ast);
    }

    /* Same-scope redeclaration -> error */
    {
        const char* src =
            "surface test() {\n"
            "    float x = 1.0;\n"
            "    float x = 2.0;\n"
            "}";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("redecl: has errors", sema.num_errors > 0);
        rh_sl_node_free(ast);
    }
}

/* ------------------------------------------------------------------ */
/*  Construct validation tests                                         */
/* ------------------------------------------------------------------ */

static void test_construct_validation(void) {
    printf("Test: construct validation...\n");

    /* illuminance in surface -> ok */
    {
        const char* src =
            "surface test() {\n"
            "    illuminance(P, N, 1.57) { Ci += Cl; }\n"
            "}";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("illuminance in surface: no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        rh_sl_node_free(ast);
    }

    /* illuminance in light -> error */
    {
        const char* src =
            "light test() {\n"
            "    illuminance(P, N, 1.57) { Cl = 1; }\n"
            "}";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("illuminance in light: has errors", sema.num_errors > 0);
        rh_sl_node_free(ast);
    }

    /* illuminate in light -> ok */
    {
        const char* src =
            "light test(point from = point(0,0,0)) {\n"
            "    illuminate(from) { Cl = 1; }\n"
            "}";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("illuminate in light: no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        rh_sl_node_free(ast);
    }

    /* illuminate in surface -> error */
    {
        const char* src =
            "surface test() {\n"
            "    illuminate(P) { Ci = 1; }\n"
            "}";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("illuminate in surface: has errors", sema.num_errors > 0);
        rh_sl_node_free(ast);
    }

    /* solar in light -> ok */
    {
        const char* src =
            "light test() {\n"
            "    solar() { Cl = 1; }\n"
            "}";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("solar in light: no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        rh_sl_node_free(ast);
    }

    /* solar in surface -> error */
    {
        const char* src =
            "surface test() {\n"
            "    solar() { Ci = 1; }\n"
            "}";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("solar in surface: has errors", sema.num_errors > 0);
        rh_sl_node_free(ast);
    }
}

/* ------------------------------------------------------------------ */
/*  Control flow tests                                                 */
/* ------------------------------------------------------------------ */

static void test_control_flow(void) {
    printf("Test: control flow...\n");

    /* break in loop -> ok */
    {
        const char* src =
            "surface test() {\n"
            "    float i = 0;\n"
            "    while (i < 10) { break; i += 1; }\n"
            "}";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("break in loop: no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        rh_sl_node_free(ast);
    }

    /* continue in loop -> ok */
    {
        const char* src =
            "surface test() {\n"
            "    float i = 0;\n"
            "    while (i < 10) { continue; }\n"
            "}";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("continue in loop: no errors", sema.num_errors == 0);
        rh_sl_node_free(ast);
    }

    /* break outside loop -> error */
    {
        const char* src = "surface test() { break; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("break outside: has errors", sema.num_errors > 0);
        rh_sl_node_free(ast);
    }

    /* continue outside loop -> error */
    {
        const char* src = "surface test() { continue; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("continue outside: has errors", sema.num_errors > 0);
        rh_sl_node_free(ast);
    }

    /* return type match */
    {
        const char* src =
            "float square(float x) { return x * x; }\n"
            "surface test() { float y = square(2.0); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("return match: no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        rh_sl_node_free(ast);
    }

    /* return type mismatch -> error */
    {
        const char* src =
            "float bad() { return \"hello\"; }\n"
            "surface test() { float y = bad(); }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("return mismatch: has errors", sema.num_errors > 0);
        rh_sl_node_free(ast);
    }

    /* for loop with var decl */
    {
        const char* src =
            "surface test() {\n"
            "    float sum = 0;\n"
            "    for (float i = 0; i < 10; i += 1) {\n"
            "        sum += i;\n"
            "    }\n"
            "}";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("for loop: no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        rh_sl_node_free(ast);
    }
}

/* ------------------------------------------------------------------ */
/*  Undeclared variable test                                           */
/* ------------------------------------------------------------------ */

static void test_undeclared(void) {
    printf("Test: undeclared variables...\n");

    {
        const char* src = "surface test() { float x = bogus_var; }";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("undeclared: has errors", sema.num_errors > 0);
        rh_sl_node_free(ast);
    }
}

/* ------------------------------------------------------------------ */
/*  Full shader tests                                                  */
/* ------------------------------------------------------------------ */

static void test_full_matte(void) {
    printf("Test: full matte shader...\n");
    const char* src =
        "surface matte(float Ka = 1; float Kd = 1)\n"
        "{\n"
        "    normal Nf = faceforward(normalize(N), I);\n"
        "    Oi = Os;\n"
        "    Ci = Os * Cs * (Ka * ambient() + Kd * diffuse(Nf));\n"
        "}\n";
    RhSLSema sema;
    RhSLNode* ast = parse_and_analyze(src, &sema);
    check("matte: no errors", sema.num_errors == 0);
    if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
    rh_sl_node_free(ast);
}

static void test_full_plastic(void) {
    printf("Test: full plastic shader...\n");
    const char* src =
        "surface plastic(float Ka = 1; float Kd = 0.5; float Ks = 0.5;\n"
        "                float roughness = 0.1;\n"
        "                color specularcolor = 1)\n"
        "{\n"
        "    normal Nf = faceforward(normalize(N), I);\n"
        "    Oi = Os;\n"
        "    Ci = Os * (Cs * (Ka * ambient() + Kd * diffuse(Nf))\n"
        "         + specularcolor * Ks * specular(Nf, -normalize(I), roughness));\n"
        "}\n";
    RhSLSema sema;
    RhSLNode* ast = parse_and_analyze(src, &sema);
    check("plastic: no errors", sema.num_errors == 0);
    if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
    rh_sl_node_free(ast);
}

static void test_full_pointlight(void) {
    printf("Test: full pointlight shader...\n");
    const char* src =
        "light pointlight(float intensity = 1;\n"
        "                 color lightcolor = 1;\n"
        "                 point from = point(0,0,0))\n"
        "{\n"
        "    illuminate(from) {\n"
        "        Cl = intensity * lightcolor;\n"
        "    }\n"
        "}\n";
    RhSLSema sema;
    RhSLNode* ast = parse_and_analyze(src, &sema);
    check("pointlight: no errors", sema.num_errors == 0);
    if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
    rh_sl_node_free(ast);
}

static void test_full_displacement(void) {
    printf("Test: full displacement shader...\n");
    const char* src =
        "displacement bumpy(float Km = 1)\n"
        "{\n"
        "    float mag = Km * noise(s * 10);\n"
        "    P = P + mag * normalize(N);\n"
        "    N = calculatenormal(P);\n"
        "}\n";
    RhSLSema sema;
    RhSLNode* ast = parse_and_analyze(src, &sema);
    check("displacement: no errors", sema.num_errors == 0);
    if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
    rh_sl_node_free(ast);
}

/* ------------------------------------------------------------------ */
/*  Light shader L/Cl writability tests                                */
/* ------------------------------------------------------------------ */

static void test_light_globals(void) {
    printf("Test: light shader globals...\n");

    /* L is writable in light shader */
    {
        const char* src =
            "light test(point from = point(0,0,0)) {\n"
            "    illuminate(from) { L = L; Cl = 1; }\n"
            "}";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("light L writable: no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        rh_sl_node_free(ast);
    }

    /* L is read-only in surface shader */
    {
        const char* src =
            "surface test() {\n"
            "    illuminance(P, N, 1.57) { L = I; }\n"
            "}";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("surface L read-only: has errors", sema.num_errors > 0);
        rh_sl_node_free(ast);
    }
}

/* ------------------------------------------------------------------ */
/*  If/else test                                                       */
/* ------------------------------------------------------------------ */

static void test_if_else(void) {
    printf("Test: if/else...\n");

    {
        const char* src =
            "surface test() {\n"
            "    if (s > 0.5) {\n"
            "        Ci = Cs;\n"
            "    } else {\n"
            "        Ci = Os;\n"
            "    }\n"
            "}";
        RhSLSema sema;
        RhSLNode* ast = parse_and_analyze(src, &sema);
        check("if/else: no errors", sema.num_errors == 0);
        if (sema.num_errors > 0) printf("    err: %s\n", sema.errors[0]);
        rh_sl_node_free(ast);
    }
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== RSL Semantic Analysis Tests ===\n\n");

    test_type_resolution();
    test_binops();
    test_unops();
    test_ternary();
    test_type_constructors();
    test_builtin_functions();
    test_user_functions();
    test_assignment();
    test_readwrite();
    test_compound_assign();
    test_scoping();
    test_construct_validation();
    test_control_flow();
    test_undeclared();
    test_full_matte();
    test_full_plastic();
    test_full_pointlight();
    test_full_displacement();
    test_light_globals();
    test_if_else();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
