/*
 * shader -- RenderMan Shading Language compiler.
 *
 * Reads a .sl source file, compiles it through lex -> parse -> sema -> codegen,
 * and writes the result as a .slo bytecode file.
 *
 * Usage: shader [-o output.slo] input.sl
 */

#include "rh_sl_slo.h"
#include "rh_sl_codegen.h"
#include "rh_sl_parse.h"
#include "rh_sl_sema.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Read an entire file into a malloc'd buffer. Returns NULL on error. */
static char* read_file(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "shader: cannot open '%s'\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* buf = malloc(len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t nread = fread(buf, 1, (size_t)len, f);
    buf[nread] = '\0';
    fclose(f);
    return buf;
}

/* Build default output path: replace .sl extension with .slo. */
static char* default_output(const char* input) {
    size_t len = strlen(input);
    char* out = malloc(len + 2);  /* room for extra 'o' */
    if (!out) return NULL;
    memcpy(out, input, len + 1);

    /* If ends with .sl, append 'o' */
    if (len >= 3 && strcmp(out + len - 3, ".sl") == 0) {
        out[len] = 'o';
        out[len + 1] = '\0';
    } else {
        /* Otherwise just append .slo */
        free(out);
        out = malloc(len + 5);
        if (!out) return NULL;
        memcpy(out, input, len);
        memcpy(out + len, ".slo", 5);
    }
    return out;
}

static void usage(void) {
    fprintf(stderr, "Usage: shader [-o output.slo] input.sl\n");
}

int main(int argc, char** argv) {
    const char* input_path = NULL;
    const char* output_path = NULL;
    char* output_alloc = NULL;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                usage();
                return 1;
            }
            output_path = argv[++i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "shader: unknown option '%s'\n", argv[i]);
            usage();
            return 1;
        } else {
            if (input_path) {
                fprintf(stderr, "shader: multiple input files not supported\n");
                usage();
                return 1;
            }
            input_path = argv[i];
        }
    }

    if (!input_path) {
        usage();
        return 1;
    }

    /* Default output path */
    if (!output_path) {
        output_alloc = default_output(input_path);
        output_path = output_alloc;
    }

    /* Read source */
    char* source = read_file(input_path);
    if (!source) return 1;

    /* Lex + Parse */
    RhSLParser parser;
    rh_sl_parse_init(&parser, source);
    RhSLNode* ast = rh_sl_parse(&parser);
    if (!ast || parser.num_errors > 0) {
        for (int i = 0; i < parser.num_errors; i++)
            fprintf(stderr, "%s: %s\n", input_path, parser.errors[i]);
        if (!ast) fprintf(stderr, "%s: parse failed\n", input_path);
        if (ast) rh_sl_node_free(ast);
        free(source);
        free(output_alloc);
        return 1;
    }

    /* Semantic analysis */
    RhSLSema sema;
    rh_sl_sema_init(&sema);
    if (rh_sl_sema_analyze(&sema, ast) != 0) {
        for (int i = 0; i < sema.num_errors; i++)
            fprintf(stderr, "%s: %s\n", input_path, sema.errors[i]);
        rh_sl_node_free(ast);
        free(source);
        free(output_alloc);
        return 1;
    }

    /* Code generation */
    RhSLCodegenErrors errs;
    RhSLProgram* prog = rh_sl_codegen(ast, &errs);
    if (!prog) {
        for (int i = 0; i < errs.num_errors; i++)
            fprintf(stderr, "%s: %s\n", input_path, errs.errors[i]);
        rh_sl_node_free(ast);
        free(source);
        free(output_alloc);
        return 1;
    }

    /* Write .slo */
    if (rh_sl_slo_write(output_path, prog) != 0) {
        fprintf(stderr, "shader: failed to write '%s'\n", output_path);
        rh_sl_program_free(prog);
        rh_sl_node_free(ast);
        free(source);
        free(output_alloc);
        return 1;
    }

    rh_sl_program_free(prog);
    rh_sl_node_free(ast);
    free(source);
    free(output_alloc);
    return 0;
}
