#ifndef RH_SL_PARSE_H
#define RH_SL_PARSE_H

/*
 * RSL Parser -- recursive-descent parser for RenderMan Shading Language.
 *
 * Parses a source string into an AST (abstract syntax tree).
 * The top-level result is an SL_NODE_SHADER with optional preceding functions.
 */

#include "rh_sl_ast.h"
#include "rh_sl_lex.h"

/* Maximum number of errors before the parser gives up */
#define RH_SL_MAX_ERRORS 32

typedef struct {
    RhSLLexer lex;
    char errors[RH_SL_MAX_ERRORS][256];
    int num_errors;
} RhSLParser;

/* Initialize the parser with source text. */
void rh_sl_parse_init(RhSLParser* parser, const char* source);

/* Parse the full source into an AST.  Returns the shader node (with
 * any preceding function definitions linked via shader.functions).
 * Returns NULL on fatal parse error.  Non-fatal errors are accumulated
 * in parser->errors[]. */
RhSLNode* rh_sl_parse(RhSLParser* parser);

#endif /* RH_SL_PARSE_H */
