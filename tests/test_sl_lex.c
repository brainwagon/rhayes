#include "rh_sl_lex.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

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
/*  Test: keywords                                                     */
/* ------------------------------------------------------------------ */

static void test_keywords(void) {
    printf("Test: keywords...\n");
    RhSLLexer lex;
    rh_sl_lex_init(&lex, "surface light displacement float color point vector normal "
                         "matrix string void uniform varying output if else while for "
                         "return break continue illuminance illuminate solar");

    RhSLTokenType expected[] = {
        TOK_SURFACE, TOK_LIGHT, TOK_DISPLACEMENT, TOK_FLOAT, TOK_COLOR,
        TOK_POINT, TOK_VECTOR, TOK_NORMAL, TOK_MATRIX, TOK_STRING, TOK_VOID,
        TOK_UNIFORM, TOK_VARYING, TOK_OUTPUT, TOK_IF, TOK_ELSE, TOK_WHILE,
        TOK_FOR, TOK_RETURN, TOK_BREAK, TOK_CONTINUE, TOK_ILLUMINANCE,
        TOK_ILLUMINATE, TOK_SOLAR, TOK_EOF
    };

    for (int i = 0; expected[i] != TOK_EOF; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "keyword[%d] = %s", i, rh_sl_token_name(expected[i]));
        check(buf, lex.current.type == expected[i]);
        rh_sl_lex_next(&lex);
    }
    check("keywords EOF", lex.current.type == TOK_EOF);
}

/* ------------------------------------------------------------------ */
/*  Test: numeric literals                                             */
/* ------------------------------------------------------------------ */

static void test_numbers(void) {
    printf("Test: numeric literals...\n");
    RhSLLexer lex;
    rh_sl_lex_init(&lex, "42 3.14 0.5 .25 1e3 2.5e-2 0");

    check("42 is INT_LIT", lex.current.type == TOK_INT_LIT);
    check("42 value", fabsf(lex.current.float_val - 42.0f) < 1e-6f);
    rh_sl_lex_next(&lex);

    check("3.14 is FLOAT_LIT", lex.current.type == TOK_FLOAT_LIT);
    check("3.14 value", fabsf(lex.current.float_val - 3.14f) < 1e-4f);
    rh_sl_lex_next(&lex);

    check("0.5 is FLOAT_LIT", lex.current.type == TOK_FLOAT_LIT);
    rh_sl_lex_next(&lex);

    check(".25 is FLOAT_LIT", lex.current.type == TOK_FLOAT_LIT);
    check(".25 value", fabsf(lex.current.float_val - 0.25f) < 1e-6f);
    rh_sl_lex_next(&lex);

    check("1e3 is FLOAT_LIT", lex.current.type == TOK_FLOAT_LIT);
    check("1e3 value", fabsf(lex.current.float_val - 1000.0f) < 1e-2f);
    rh_sl_lex_next(&lex);

    check("2.5e-2 is FLOAT_LIT", lex.current.type == TOK_FLOAT_LIT);
    check("2.5e-2 value", fabsf(lex.current.float_val - 0.025f) < 1e-6f);
    rh_sl_lex_next(&lex);

    check("0 is INT_LIT", lex.current.type == TOK_INT_LIT);
    rh_sl_lex_next(&lex);

    check("numbers EOF", lex.current.type == TOK_EOF);
}

/* ------------------------------------------------------------------ */
/*  Test: string literals                                              */
/* ------------------------------------------------------------------ */

static void test_strings(void) {
    printf("Test: string literals...\n");
    RhSLLexer lex;
    rh_sl_lex_init(&lex, "\"hello\" \"world\\n\" \"with \\\"quotes\\\"\"");

    check("hello type", lex.current.type == TOK_STRING_LIT);
    check("hello text", strcmp(lex.current.text, "hello") == 0);
    rh_sl_lex_next(&lex);

    check("world\\n type", lex.current.type == TOK_STRING_LIT);
    check("world\\n text", strcmp(lex.current.text, "world\n") == 0);
    rh_sl_lex_next(&lex);

    check("quotes type", lex.current.type == TOK_STRING_LIT);
    check("quotes text", strcmp(lex.current.text, "with \"quotes\"") == 0);
    rh_sl_lex_next(&lex);

    check("strings EOF", lex.current.type == TOK_EOF);
}

/* ------------------------------------------------------------------ */
/*  Test: operators                                                    */
/* ------------------------------------------------------------------ */

static void test_operators(void) {
    printf("Test: operators...\n");
    RhSLLexer lex;
    rh_sl_lex_init(&lex, "+ - * / . ^ = == != < > <= >= && || ! ? : += -= *= /=");

    RhSLTokenType expected[] = {
        TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_DOT, TOK_CARET,
        TOK_ASSIGN, TOK_EQ, TOK_NE, TOK_LT, TOK_GT, TOK_LE, TOK_GE,
        TOK_AND, TOK_OR, TOK_NOT, TOK_QUESTION, TOK_COLON,
        TOK_PLUS_ASSIGN, TOK_MINUS_ASSIGN, TOK_STAR_ASSIGN, TOK_SLASH_ASSIGN,
        TOK_EOF
    };

    for (int i = 0; expected[i] != TOK_EOF; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "op[%d] = %s", i, rh_sl_token_name(expected[i]));
        check(buf, lex.current.type == expected[i]);
        rh_sl_lex_next(&lex);
    }
    check("operators EOF", lex.current.type == TOK_EOF);
}

/* ------------------------------------------------------------------ */
/*  Test: comments                                                     */
/* ------------------------------------------------------------------ */

static void test_comments(void) {
    printf("Test: comments...\n");
    RhSLLexer lex;
    rh_sl_lex_init(&lex, "a /* block comment */ b // line comment\nc");

    check("a", lex.current.type == TOK_IDENT && strcmp(lex.current.text, "a") == 0);
    rh_sl_lex_next(&lex);
    check("b after block", lex.current.type == TOK_IDENT && strcmp(lex.current.text, "b") == 0);
    rh_sl_lex_next(&lex);
    check("c after line", lex.current.type == TOK_IDENT && strcmp(lex.current.text, "c") == 0);
    rh_sl_lex_next(&lex);
    check("comments EOF", lex.current.type == TOK_EOF);
}

/* ------------------------------------------------------------------ */
/*  Test: matte shader tokenization                                    */
/* ------------------------------------------------------------------ */

static void test_matte_shader(void) {
    printf("Test: matte shader tokenization...\n");
    const char* src =
        "surface matte(float Ka = 1; float Kd = 1)\n"
        "{\n"
        "    normal Nf = faceforward(normalize(N), I);\n"
        "    Oi = Os;\n"
        "    Ci = Os * Cs * (Ka * ambient() + Kd * diffuse(Nf));\n"
        "}\n";

    RhSLLexer lex;
    rh_sl_lex_init(&lex, src);

    /* Just verify first few tokens and count total */
    check("surface", lex.current.type == TOK_SURFACE);
    rh_sl_lex_next(&lex);
    check("matte ident", lex.current.type == TOK_IDENT && strcmp(lex.current.text, "matte") == 0);
    rh_sl_lex_next(&lex);
    check("(", lex.current.type == TOK_LPAREN);

    /* Count remaining tokens */
    int count = 3;
    while (lex.current.type != TOK_EOF && lex.current.type != TOK_ERROR) {
        rh_sl_lex_next(&lex);
        count++;
    }
    check("no errors", lex.current.type == TOK_EOF);
    check("token count > 30", count > 30);
}

/* ------------------------------------------------------------------ */
/*  Test: line numbers                                                 */
/* ------------------------------------------------------------------ */

static void test_line_numbers(void) {
    printf("Test: line numbers...\n");
    RhSLLexer lex;
    rh_sl_lex_init(&lex, "a\nb\n\nc");

    check("a line 1", lex.current.line == 1);
    rh_sl_lex_next(&lex);
    check("b line 2", lex.current.line == 2);
    rh_sl_lex_next(&lex);
    check("c line 4", lex.current.line == 4);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== RSL Lexer Tests ===\n\n");

    test_keywords();
    test_numbers();
    test_strings();
    test_operators();
    test_comments();
    test_matte_shader();
    test_line_numbers();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
