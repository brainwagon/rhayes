#include "rh_sl_parse.h"
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

/* ------------------------------------------------------------------ */
/*  Test: parse matte shader                                           */
/* ------------------------------------------------------------------ */

static void test_matte(void) {
    printf("Test: parse matte shader...\n");
    const char* src =
        "surface matte(float Ka = 1; float Kd = 1)\n"
        "{\n"
        "    normal Nf = faceforward(normalize(N), I);\n"
        "    Oi = Os;\n"
        "    Ci = Os * Cs * (Ka * ambient() + Kd * diffuse(Nf));\n"
        "}\n";

    RhSLParser parser;
    rh_sl_parse_init(&parser, src);
    RhSLNode* ast = rh_sl_parse(&parser);

    check("parse succeeded", ast != NULL);
    check("no errors", parser.num_errors == 0);
    if (parser.num_errors > 0)
        printf("    error: %s\n", parser.errors[0]);

    check("is shader", ast->node_type == SL_NODE_SHADER);
    check("shader type surface", ast->u.shader.shader_type == SL_SHADER_SURFACE);
    check("shader name", strcmp(ast->u.shader.name, "matte") == 0);

    /* Parameters: Ka, Kd */
    check("has params", ast->u.shader.params != NULL);
    check("2 params", rh_sl_node_count(ast->u.shader.params) == 2);

    RhSLNode* ka = ast->u.shader.params;
    check("Ka name", strcmp(ka->u.param.name, "Ka") == 0);
    check("Ka type float", ka->u.param.type == SL_TYPE_FLOAT);
    check("Ka has default", ka->u.param.default_val != NULL);
    check("Ka default is lit", ka->u.param.default_val->node_type == SL_NODE_FLOAT_LIT);
    check("Ka default = 1", ka->u.param.default_val->u.float_lit.value == 1.0f);

    RhSLNode* kd = ka->next;
    check("Kd name", strcmp(kd->u.param.name, "Kd") == 0);

    /* Body */
    check("has body", ast->u.shader.body != NULL);
    check("body is block", ast->u.shader.body->node_type == SL_NODE_BLOCK);

    /* Statements: var_decl, assign, assign = 3 statements */
    int stmt_count = rh_sl_node_count(ast->u.shader.body->u.block.stmts);
    check("3 statements", stmt_count == 3);

    /* First statement: normal Nf = faceforward(...) */
    RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
    check("stmt1 is var_decl", s1->node_type == SL_NODE_VAR_DECL);
    check("stmt1 name Nf", strcmp(s1->u.var_decl.name, "Nf") == 0);
    check("stmt1 type normal", s1->u.var_decl.type == SL_TYPE_NORMAL);
    check("stmt1 has init", s1->u.var_decl.init != NULL);
    check("stmt1 init is call", s1->u.var_decl.init->node_type == SL_NODE_CALL);
    check("stmt1 init is faceforward",
          strcmp(s1->u.var_decl.init->u.call.name, "faceforward") == 0);

    rh_sl_node_free(ast);
}

/* ------------------------------------------------------------------ */
/*  Test: parse plastic shader                                         */
/* ------------------------------------------------------------------ */

static void test_plastic(void) {
    printf("Test: parse plastic shader...\n");
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

    RhSLParser parser;
    rh_sl_parse_init(&parser, src);
    RhSLNode* ast = rh_sl_parse(&parser);

    check("parse succeeded", ast != NULL);
    check("no errors", parser.num_errors == 0);
    if (parser.num_errors > 0)
        printf("    error: %s\n", parser.errors[0]);

    check("shader name plastic", strcmp(ast->u.shader.name, "plastic") == 0);
    check("5 params", rh_sl_node_count(ast->u.shader.params) == 5);

    /* Check specularcolor is color type */
    RhSLNode* p = ast->u.shader.params;
    while (p && strcmp(p->u.param.name, "specularcolor") != 0) p = p->next;
    check("specularcolor found", p != NULL);
    if (p) check("specularcolor type color", p->u.param.type == SL_TYPE_COLOR);

    rh_sl_node_free(ast);
}

/* ------------------------------------------------------------------ */
/*  Test: parse pointlight shader                                      */
/* ------------------------------------------------------------------ */

static void test_pointlight(void) {
    printf("Test: parse pointlight shader...\n");
    const char* src =
        "light pointlight(float intensity = 1;\n"
        "                 color lightcolor = 1;\n"
        "                 point from = point \"shader\" (0, 0, 0))\n"
        "{\n"
        "    illuminate(from) {\n"
        "        Cl = intensity * lightcolor;\n"
        "        L = Ps - from;\n"
        "    }\n"
        "}\n";

    RhSLParser parser;
    rh_sl_parse_init(&parser, src);
    RhSLNode* ast = rh_sl_parse(&parser);

    check("parse succeeded", ast != NULL);
    check("no errors", parser.num_errors == 0);
    if (parser.num_errors > 0)
        printf("    error: %s\n", parser.errors[0]);

    check("shader type light", ast->u.shader.shader_type == SL_SHADER_LIGHT);
    check("shader name pointlight", strcmp(ast->u.shader.name, "pointlight") == 0);
    check("3 params", rh_sl_node_count(ast->u.shader.params) == 3);

    /* Check 'from' parameter has space "shader" */
    RhSLNode* p = ast->u.shader.params;
    while (p && strcmp(p->u.param.name, "from") != 0) p = p->next;
    check("from found", p != NULL);
    if (p) {
        check("from type point", p->u.param.type == SL_TYPE_POINT);
        check("from default is typecast", p->u.param.default_val->node_type == SL_NODE_TYPECAST);
        check("from space shader",
              strcmp(p->u.param.default_val->u.typecast.space, "shader") == 0);
    }

    /* Body has illuminate statement */
    RhSLNode* s = ast->u.shader.body->u.block.stmts;
    check("body has illuminate", s != NULL && s->node_type == SL_NODE_ILLUMINATE);

    rh_sl_node_free(ast);
}

/* ------------------------------------------------------------------ */
/*  Test: control flow (if/else, while, for)                           */
/* ------------------------------------------------------------------ */

static void test_control_flow(void) {
    printf("Test: parse control flow...\n");
    const char* src =
        "surface test_cf()\n"
        "{\n"
        "    float x = 0.5;\n"
        "    if (x > 0) {\n"
        "        x = 1;\n"
        "    } else {\n"
        "        x = 0;\n"
        "    }\n"
        "    float i;\n"
        "    for (i = 0; i < 10; i += 1) {\n"
        "        x += 0.1;\n"
        "    }\n"
        "    while (x > 0) {\n"
        "        x -= 0.1;\n"
        "        if (x < 0.5)\n"
        "            break;\n"
        "    }\n"
        "    Ci = color(x, x, x);\n"
        "}\n";

    RhSLParser parser;
    rh_sl_parse_init(&parser, src);
    RhSLNode* ast = rh_sl_parse(&parser);

    check("parse succeeded", ast != NULL);
    check("no errors", parser.num_errors == 0);
    if (parser.num_errors > 0)
        printf("    error: %s\n", parser.errors[0]);

    /* Count statements */
    int stmt_count = rh_sl_node_count(ast->u.shader.body->u.block.stmts);
    check("6 statements", stmt_count == 6);

    RhSLNode* s = ast->u.shader.body->u.block.stmts;

    /* stmt 1: var decl */
    check("s1 var_decl", s->node_type == SL_NODE_VAR_DECL);
    s = s->next;

    /* stmt 2: if */
    check("s2 if", s->node_type == SL_NODE_IF);
    check("s2 has else", s->u.if_stmt.else_body != NULL);
    s = s->next;

    /* stmt 3: var decl for i */
    check("s3 var_decl", s->node_type == SL_NODE_VAR_DECL);
    s = s->next;

    /* stmt 4: for */
    check("s4 for", s->node_type == SL_NODE_FOR);
    s = s->next;

    /* stmt 5: while */
    check("s5 while", s->node_type == SL_NODE_WHILE);
    s = s->next;

    /* stmt 6: Ci = color(x, x, x) */
    check("s6 assign", s->node_type == SL_NODE_ASSIGN);

    rh_sl_node_free(ast);
}

/* ------------------------------------------------------------------ */
/*  Test: user-defined function                                        */
/* ------------------------------------------------------------------ */

static void test_user_function(void) {
    printf("Test: parse user-defined function...\n");
    const char* src =
        "color my_blend(color a, b; float t)\n"
        "{\n"
        "    return mix(a, b, t);\n"
        "}\n"
        "\n"
        "surface test_func()\n"
        "{\n"
        "    color c = my_blend(Cs, color(1, 0, 0), 0.5);\n"
        "    Ci = c;\n"
        "}\n";

    RhSLParser parser;
    rh_sl_parse_init(&parser, src);
    RhSLNode* ast = rh_sl_parse(&parser);

    check("parse succeeded", ast != NULL);
    check("no errors", parser.num_errors == 0);
    if (parser.num_errors > 0)
        printf("    error: %s\n", parser.errors[0]);

    /* Should have one function definition */
    check("has functions", ast->u.shader.functions != NULL);
    check("1 function", rh_sl_node_count(ast->u.shader.functions) == 1);

    RhSLNode* fn = ast->u.shader.functions;
    check("fn name my_blend", strcmp(fn->u.function.name, "my_blend") == 0);
    check("fn return type color", fn->u.function.return_type == SL_TYPE_COLOR);
    check("fn 3 formals", rh_sl_node_count(fn->u.function.formals) == 3);

    /* Check formals: a (color), b (color), t (float) */
    RhSLNode* f1 = fn->u.function.formals;
    check("f1 name a", strcmp(f1->u.formal.name, "a") == 0);
    check("f1 type color", f1->u.formal.type == SL_TYPE_COLOR);

    RhSLNode* f2 = f1->next;
    check("f2 name b", strcmp(f2->u.formal.name, "b") == 0);
    check("f2 type color", f2->u.formal.type == SL_TYPE_COLOR);

    RhSLNode* f3 = f2->next;
    check("f3 name t", strcmp(f3->u.formal.name, "t") == 0);
    check("f3 type float", f3->u.formal.type == SL_TYPE_FLOAT);

    /* Function body has return statement */
    RhSLNode* body_stmt = fn->u.function.body->u.block.stmts;
    check("fn body has return", body_stmt->node_type == SL_NODE_RETURN);
    check("return has value", body_stmt->u.ret.value != NULL);
    check("return value is call", body_stmt->u.ret.value->node_type == SL_NODE_CALL);
    check("return calls mix", strcmp(body_stmt->u.ret.value->u.call.name, "mix") == 0);

    rh_sl_node_free(ast);
}

/* ------------------------------------------------------------------ */
/*  Test: expressions and operator precedence                          */
/* ------------------------------------------------------------------ */

static void test_expressions(void) {
    printf("Test: parse expressions...\n");
    const char* src =
        "surface test_expr()\n"
        "{\n"
        "    float a = 1 + 2 * 3;\n"
        "    float b = (1 + 2) * 3;\n"
        "    float c = a > 0 ? 1 : 0;\n"
        "    float d = -a;\n"
        "    float e = !0;\n"
        "    Ci = Cs * Os;\n"
        "}\n";

    RhSLParser parser;
    rh_sl_parse_init(&parser, src);
    RhSLNode* ast = rh_sl_parse(&parser);

    check("parse succeeded", ast != NULL);
    check("no errors", parser.num_errors == 0);
    if (parser.num_errors > 0)
        printf("    error: %s\n", parser.errors[0]);

    /* 1 + 2 * 3: should parse as 1 + (2 * 3), i.e., top is ADD */
    RhSLNode* s1 = ast->u.shader.body->u.block.stmts;
    check("s1 var_decl", s1->node_type == SL_NODE_VAR_DECL);
    RhSLNode* init = s1->u.var_decl.init;
    check("s1 init is binop", init->node_type == SL_NODE_BINOP);
    check("s1 top is ADD", init->u.binop.op == SL_OP_ADD);
    check("s1 right is MUL", init->u.binop.right->node_type == SL_NODE_BINOP &&
                              init->u.binop.right->u.binop.op == SL_OP_MUL);

    /* Ternary */
    RhSLNode* s3 = s1->next->next;
    check("s3 var_decl", s3->node_type == SL_NODE_VAR_DECL);
    check("s3 init is ternary", s3->u.var_decl.init->node_type == SL_NODE_TERNARY);

    /* Unary neg */
    RhSLNode* s4 = s3->next;
    check("s4 var_decl", s4->node_type == SL_NODE_VAR_DECL);
    check("s4 init is unop", s4->u.var_decl.init->node_type == SL_NODE_UNOP);
    check("s4 unop is NEG", s4->u.var_decl.init->u.unop.op == SL_UOP_NEG);

    rh_sl_node_free(ast);
}

/* ------------------------------------------------------------------ */
/*  Test: illuminance in surface shader                                */
/* ------------------------------------------------------------------ */

static void test_illuminance(void) {
    printf("Test: parse illuminance...\n");
    const char* src =
        "surface test_illum()\n"
        "{\n"
        "    normal Nf = faceforward(normalize(N), I);\n"
        "    color C = 0;\n"
        "    illuminance(P, Nf, 1.5707963) {\n"
        "        C += Cl * normalize(L) . Nf;\n"
        "    }\n"
        "    Ci = C;\n"
        "}\n";

    RhSLParser parser;
    rh_sl_parse_init(&parser, src);
    RhSLNode* ast = rh_sl_parse(&parser);

    check("parse succeeded", ast != NULL);
    check("no errors", parser.num_errors == 0);
    if (parser.num_errors > 0)
        printf("    error: %s\n", parser.errors[0]);

    /* Third statement should be illuminance */
    RhSLNode* s = ast->u.shader.body->u.block.stmts;
    s = s->next->next; /* skip var_decl, var_decl */
    check("s3 is illuminance", s->node_type == SL_NODE_ILLUMINANCE);
    check("illuminance has position", s->u.illuminance.position != NULL);
    check("illuminance has normal", s->u.illuminance.normal != NULL);
    check("illuminance has angle", s->u.illuminance.angle != NULL);
    check("illuminance has body", s->u.illuminance.body != NULL);

    rh_sl_node_free(ast);
}

/* ------------------------------------------------------------------ */
/*  Test: error recovery — lexer error in block                        */
/* ------------------------------------------------------------------ */

static void test_error_lexer_error_in_block(void) {
    printf("Test: error recovery — lexer error in block...\n");
    const char* src = "surface foo() { @ }";

    RhSLParser parser;
    rh_sl_parse_init(&parser, src);
    RhSLNode* ast = rh_sl_parse(&parser);

    check("terminates", 1);
    check("has errors", parser.num_errors > 0);
    (void)ast;
    rh_sl_node_free(ast);
}

/* ------------------------------------------------------------------ */
/*  Test: error recovery — missing semicolon                           */
/* ------------------------------------------------------------------ */

static void test_error_missing_semicolon(void) {
    printf("Test: error recovery — missing semicolon...\n");
    const char* src = "surface foo() { x = 1 y = 2; }";

    RhSLParser parser;
    rh_sl_parse_init(&parser, src);
    RhSLNode* ast = rh_sl_parse(&parser);

    check("terminates", 1);
    check("has errors", parser.num_errors > 0);
    (void)ast;
    rh_sl_node_free(ast);
}

/* ------------------------------------------------------------------ */
/*  Test: error recovery — unexpected closing paren                    */
/* ------------------------------------------------------------------ */

static void test_error_unexpected_paren(void) {
    printf("Test: error recovery — unexpected closing paren...\n");
    const char* src = "surface foo() { ) }";

    RhSLParser parser;
    rh_sl_parse_init(&parser, src);
    RhSLNode* ast = rh_sl_parse(&parser);

    check("terminates", 1);
    check("has errors", parser.num_errors > 0);
    (void)ast;
    rh_sl_node_free(ast);
}

/* ------------------------------------------------------------------ */
/*  Test: error recovery — extra comma in args                         */
/* ------------------------------------------------------------------ */

static void test_error_extra_comma(void) {
    printf("Test: error recovery — extra comma in args...\n");
    const char* src = "surface foo() { float x = color(1, , 3); }";

    RhSLParser parser;
    rh_sl_parse_init(&parser, src);
    RhSLNode* ast = rh_sl_parse(&parser);

    check("terminates", 1);
    check("has errors", parser.num_errors > 0);
    (void)ast;
    rh_sl_node_free(ast);
}

/* ------------------------------------------------------------------ */
/*  Test: error recovery — truncated input (EOF mid-parse)             */
/* ------------------------------------------------------------------ */

static void test_error_truncated_input(void) {
    printf("Test: error recovery — truncated input...\n");
    const char* src = "surface foo(";

    RhSLParser parser;
    rh_sl_parse_init(&parser, src);
    RhSLNode* ast = rh_sl_parse(&parser);

    check("terminates", 1);
    check("has errors", parser.num_errors > 0);
    (void)ast;
    rh_sl_node_free(ast);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== RSL Parser Tests ===\n\n");

    test_matte();
    test_plastic();
    test_pointlight();
    test_control_flow();
    test_user_function();
    test_expressions();
    test_illuminance();
    test_error_lexer_error_in_block();
    test_error_missing_semicolon();
    test_error_unexpected_paren();
    test_error_extra_comma();
    test_error_truncated_input();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
