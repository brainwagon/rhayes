#include "rh_sl_parse.h"
#include "rh_sl_ast.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Safe string copy that always null-terminates (no truncation warnings) */
static void sl_strcpy(char* dst, size_t dstsz, const char* src) {
    size_t len = strlen(src);
    if (len >= dstsz) len = dstsz - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* ------------------------------------------------------------------ */
/*  AST node lifecycle                                                 */
/* ------------------------------------------------------------------ */

RhSLNode* rh_sl_node_alloc(RhSLNodeType type, int line) {
    RhSLNode* n = calloc(1, sizeof(RhSLNode));
    if (n) {
        n->node_type = type;
        n->line = line;
    }
    return n;
}

void rh_sl_node_free(RhSLNode* node) {
    if (!node) return;

    /* Free linked-list siblings */
    rh_sl_node_free(node->next);

    /* Free children based on node type */
    switch (node->node_type) {
    case SL_NODE_SHADER:
        rh_sl_node_free(node->u.shader.params);
        rh_sl_node_free(node->u.shader.body);
        rh_sl_node_free(node->u.shader.functions);
        break;
    case SL_NODE_FUNCTION:
        rh_sl_node_free(node->u.function.formals);
        rh_sl_node_free(node->u.function.body);
        break;
    case SL_NODE_PARAM:
        rh_sl_node_free(node->u.param.default_val);
        break;
    case SL_NODE_FORMAL:
        break;
    case SL_NODE_VAR_DECL:
        rh_sl_node_free(node->u.var_decl.init);
        break;
    case SL_NODE_BLOCK:
        rh_sl_node_free(node->u.block.stmts);
        break;
    case SL_NODE_ASSIGN:
        rh_sl_node_free(node->u.assign.target);
        rh_sl_node_free(node->u.assign.value);
        break;
    case SL_NODE_COMPOUND_ASSIGN:
        rh_sl_node_free(node->u.compound_assign.target);
        rh_sl_node_free(node->u.compound_assign.value);
        break;
    case SL_NODE_BINOP:
        rh_sl_node_free(node->u.binop.left);
        rh_sl_node_free(node->u.binop.right);
        break;
    case SL_NODE_UNOP:
        rh_sl_node_free(node->u.unop.operand);
        break;
    case SL_NODE_TERNARY:
        rh_sl_node_free(node->u.ternary.cond);
        rh_sl_node_free(node->u.ternary.then_expr);
        rh_sl_node_free(node->u.ternary.else_expr);
        break;
    case SL_NODE_CALL:
        rh_sl_node_free(node->u.call.args);
        break;
    case SL_NODE_IF:
        rh_sl_node_free(node->u.if_stmt.cond);
        rh_sl_node_free(node->u.if_stmt.then_body);
        rh_sl_node_free(node->u.if_stmt.else_body);
        break;
    case SL_NODE_WHILE:
        rh_sl_node_free(node->u.while_stmt.cond);
        rh_sl_node_free(node->u.while_stmt.body);
        break;
    case SL_NODE_FOR:
        rh_sl_node_free(node->u.for_stmt.init);
        rh_sl_node_free(node->u.for_stmt.cond);
        rh_sl_node_free(node->u.for_stmt.inc);
        rh_sl_node_free(node->u.for_stmt.body);
        break;
    case SL_NODE_RETURN:
        rh_sl_node_free(node->u.ret.value);
        break;
    case SL_NODE_ILLUMINANCE:
        rh_sl_node_free(node->u.illuminance.position);
        rh_sl_node_free(node->u.illuminance.normal);
        rh_sl_node_free(node->u.illuminance.angle);
        rh_sl_node_free(node->u.illuminance.body);
        break;
    case SL_NODE_ILLUMINATE:
        rh_sl_node_free(node->u.illuminate.position);
        rh_sl_node_free(node->u.illuminate.axis);
        rh_sl_node_free(node->u.illuminate.angle);
        rh_sl_node_free(node->u.illuminate.body);
        break;
    case SL_NODE_SOLAR:
        rh_sl_node_free(node->u.solar.axis);
        rh_sl_node_free(node->u.solar.angle);
        rh_sl_node_free(node->u.solar.body);
        break;
    case SL_NODE_TYPECAST:
        rh_sl_node_free(node->u.typecast.args);
        break;
    case SL_NODE_ARRAY_ACCESS:
        rh_sl_node_free(node->u.array_access.array);
        rh_sl_node_free(node->u.array_access.index);
        break;
    case SL_NODE_COMP_ACCESS:
        rh_sl_node_free(node->u.comp_access.operand);
        break;
    case SL_NODE_IDENT:
    case SL_NODE_FLOAT_LIT:
    case SL_NODE_STRING_LIT:
    case SL_NODE_BREAK:
    case SL_NODE_CONTINUE:
        break;
    }

    free(node);
}

RhSLNode* rh_sl_node_append(RhSLNode* list, RhSLNode* node) {
    if (!list) return node;
    RhSLNode* tail = list;
    while (tail->next) tail = tail->next;
    tail->next = node;
    return list;
}

int rh_sl_node_count(const RhSLNode* list) {
    int n = 0;
    while (list) { n++; list = list->next; }
    return n;
}

const char* rh_sl_type_name(RhSLType type) {
    switch (type) {
    case SL_TYPE_VOID:   return "void";
    case SL_TYPE_FLOAT:  return "float";
    case SL_TYPE_COLOR:  return "color";
    case SL_TYPE_POINT:  return "point";
    case SL_TYPE_VECTOR: return "vector";
    case SL_TYPE_NORMAL: return "normal";
    case SL_TYPE_MATRIX: return "matrix";
    case SL_TYPE_STRING: return "string";
    default:             return "?type?";
    }
}

/* ================================================================== */
/*  Recursive-descent parser                                           */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/*  Parser helpers                                                     */
/* ------------------------------------------------------------------ */

static void parse_error(RhSLParser* p, const char* msg) {
    if (p->num_errors < RH_SL_MAX_ERRORS) {
        char tok[32];
        sl_strcpy(tok, sizeof(tok), p->lex.current.text);
        snprintf(p->errors[p->num_errors], 256,
                 "line %d: %s (got '%.30s')",
                 p->lex.current.line, msg, tok);
        p->num_errors++;
    }
}

static RhSLTokenType peek(RhSLParser* p) {
    return p->lex.current.type;
}

static int peek_line(RhSLParser* p) {
    return p->lex.current.line;
}

static void advance(RhSLParser* p) {
    rh_sl_lex_next(&p->lex);
}

static int match(RhSLParser* p, RhSLTokenType type) {
    if (p->lex.current.type == type) {
        advance(p);
        return 1;
    }
    return 0;
}

static int expect(RhSLParser* p, RhSLTokenType type) {
    if (p->lex.current.type == type) {
        advance(p);
        return 1;
    }
    char msg[128];
    snprintf(msg, sizeof(msg), "expected '%s'", rh_sl_token_name(type));
    parse_error(p, msg);
    return 0;
}

/* Check if current token is a type keyword */
static int is_type_token(RhSLTokenType t) {
    return t == TOK_FLOAT || t == TOK_COLOR || t == TOK_POINT ||
           t == TOK_VECTOR || t == TOK_NORMAL || t == TOK_MATRIX ||
           t == TOK_STRING || t == TOK_VOID;
}

static RhSLType token_to_type(RhSLTokenType t) {
    switch (t) {
    case TOK_FLOAT:  return SL_TYPE_FLOAT;
    case TOK_COLOR:  return SL_TYPE_COLOR;
    case TOK_POINT:  return SL_TYPE_POINT;
    case TOK_VECTOR: return SL_TYPE_VECTOR;
    case TOK_NORMAL: return SL_TYPE_NORMAL;
    case TOK_MATRIX: return SL_TYPE_MATRIX;
    case TOK_STRING: return SL_TYPE_STRING;
    case TOK_VOID:   return SL_TYPE_VOID;
    default:         return SL_TYPE_VOID;
    }
}

static int is_shader_type(RhSLTokenType t) {
    return t == TOK_SURFACE || t == TOK_LIGHT ||
           t == TOK_DISPLACEMENT || t == TOK_VOLUME;
}

/* Forward declarations */
static RhSLNode* parse_expression(RhSLParser* p);
static RhSLNode* parse_statement(RhSLParser* p);
static RhSLNode* parse_block(RhSLParser* p);

/* ------------------------------------------------------------------ */
/*  Expression parsing (precedence climbing)                           */
/* ------------------------------------------------------------------ */

static RhSLNode* parse_primary(RhSLParser* p) {
    int line = peek_line(p);

    /* Numeric literal */
    if (peek(p) == TOK_INT_LIT || peek(p) == TOK_FLOAT_LIT) {
        RhSLNode* n = rh_sl_node_alloc(SL_NODE_FLOAT_LIT, line);
        n->u.float_lit.value = p->lex.current.float_val;
        advance(p);
        return n;
    }

    /* String literal */
    if (peek(p) == TOK_STRING_LIT) {
        RhSLNode* n = rh_sl_node_alloc(SL_NODE_STRING_LIT, line);
        sl_strcpy(n->u.string_lit.value, sizeof(n->u.string_lit.value), p->lex.current.text);
        advance(p);
        return n;
    }

    /* Type constructor: color(...), point "space" (...) */
    if (is_type_token(peek(p))) {
        RhSLType type = token_to_type(peek(p));
        advance(p);

        /* Optional coordinate space string */
        char space[SL_MAX_NAME] = "";
        if (peek(p) == TOK_STRING_LIT) {
            sl_strcpy(space, SL_MAX_NAME, p->lex.current.text);
            advance(p);
        }

        if (peek(p) == TOK_LPAREN) {
            RhSLNode* n = rh_sl_node_alloc(SL_NODE_TYPECAST, line);
            n->u.typecast.type = type;
            sl_strcpy(n->u.typecast.space, sizeof(n->u.typecast.space), space);
            expect(p, TOK_LPAREN);
            RhSLNode* args = NULL;
            if (peek(p) != TOK_RPAREN) {
                args = parse_expression(p);
                RhSLNode* tail = args;
                while (peek(p) == TOK_COMMA) {
                    advance(p);
                    tail->next = parse_expression(p);
                    tail = tail->next;
                }
            }
            n->u.typecast.args = args;
            expect(p, TOK_RPAREN);
            return n;
        }

        /* type followed by expression (e.g., "color texture(...)") -- typecast */
        if (peek(p) == TOK_IDENT) {
            RhSLNode* inner = parse_primary(p);
            RhSLNode* n = rh_sl_node_alloc(SL_NODE_TYPECAST, line);
            n->u.typecast.type = type;
            sl_strcpy(n->u.typecast.space, sizeof(n->u.typecast.space), space);
            n->u.typecast.args = inner;
            return n;
        }

        /* Not followed by '(' or ident -- error */
        parse_error(p, "expected '(' after type in constructor");
        return rh_sl_node_alloc(SL_NODE_FLOAT_LIT, line);
    }

    /* Identifier or function call */
    if (peek(p) == TOK_IDENT) {
        char name[SL_MAX_NAME];
        sl_strcpy(name, SL_MAX_NAME, p->lex.current.text);
        advance(p);

        /* Function call: ident(...) */
        if (peek(p) == TOK_LPAREN) {
            RhSLNode* n = rh_sl_node_alloc(SL_NODE_CALL, line);
            sl_strcpy(n->u.call.name, sizeof(n->u.call.name), name);
            expect(p, TOK_LPAREN);
            RhSLNode* args = NULL;
            if (peek(p) != TOK_RPAREN) {
                args = parse_expression(p);
                RhSLNode* tail = args;
                while (peek(p) == TOK_COMMA) {
                    advance(p);
                    tail->next = parse_expression(p);
                    tail = tail->next;
                }
            }
            n->u.call.args = args;
            expect(p, TOK_RPAREN);
            return n;
        }

        /* Array access: ident[expr] */
        if (peek(p) == TOK_LBRACKET) {
            advance(p);
            RhSLNode* idx = parse_expression(p);
            expect(p, TOK_RBRACKET);
            RhSLNode* n = rh_sl_node_alloc(SL_NODE_ARRAY_ACCESS, line);
            RhSLNode* base = rh_sl_node_alloc(SL_NODE_IDENT, line);
            sl_strcpy(base->u.ident.name, sizeof(base->u.ident.name), name);
            n->u.array_access.array = base;
            n->u.array_access.index = idx;
            return n;
        }

        /* Simple identifier */
        RhSLNode* n = rh_sl_node_alloc(SL_NODE_IDENT, line);
        sl_strcpy(n->u.ident.name, sizeof(n->u.ident.name), name);
        return n;
    }

    /* Parenthesized expression */
    if (peek(p) == TOK_LPAREN) {
        advance(p);
        RhSLNode* e = parse_expression(p);
        expect(p, TOK_RPAREN);
        return e;
    }

    parse_error(p, "expected expression");
    return rh_sl_node_alloc(SL_NODE_FLOAT_LIT, line);
}

static RhSLNode* parse_unary(RhSLParser* p) {
    int line = peek_line(p);

    if (peek(p) == TOK_MINUS) {
        advance(p);
        RhSLNode* n = rh_sl_node_alloc(SL_NODE_UNOP, line);
        n->u.unop.op = SL_UOP_NEG;
        n->u.unop.operand = parse_unary(p);
        return n;
    }
    if (peek(p) == TOK_NOT) {
        advance(p);
        RhSLNode* n = rh_sl_node_alloc(SL_NODE_UNOP, line);
        n->u.unop.op = SL_UOP_NOT;
        n->u.unop.operand = parse_unary(p);
        return n;
    }

    return parse_primary(p);
}

/* Binary operator precedence levels (higher = tighter binding) */
static int binop_prec(RhSLTokenType t) {
    switch (t) {
    case TOK_OR:       return 1;
    case TOK_AND:      return 2;
    case TOK_EQ:
    case TOK_NE:       return 3;
    case TOK_LT:
    case TOK_GT:
    case TOK_LE:
    case TOK_GE:       return 4;
    case TOK_PLUS:
    case TOK_MINUS:    return 5;
    case TOK_STAR:
    case TOK_SLASH:    return 6;
    case TOK_DOT:
    case TOK_CARET:    return 7;
    default:           return -1;
    }
}

static RhSLBinOp token_to_binop(RhSLTokenType t) {
    switch (t) {
    case TOK_PLUS:  return SL_OP_ADD;
    case TOK_MINUS: return SL_OP_SUB;
    case TOK_STAR:  return SL_OP_MUL;
    case TOK_SLASH: return SL_OP_DIV;
    case TOK_DOT:   return SL_OP_DOT;
    case TOK_CARET: return SL_OP_CROSS;
    case TOK_EQ:    return SL_OP_EQ;
    case TOK_NE:    return SL_OP_NE;
    case TOK_LT:    return SL_OP_LT;
    case TOK_GT:    return SL_OP_GT;
    case TOK_LE:    return SL_OP_LE;
    case TOK_GE:    return SL_OP_GE;
    case TOK_AND:   return SL_OP_AND;
    case TOK_OR:    return SL_OP_OR;
    default:        return SL_OP_ADD;
    }
}

static RhSLNode* parse_binop(RhSLParser* p, int min_prec) {
    RhSLNode* left = parse_unary(p);

    while (1) {
        int prec = binop_prec(peek(p));
        if (prec < min_prec) break;

        int line = peek_line(p);
        RhSLBinOp op = token_to_binop(peek(p));
        advance(p);

        RhSLNode* right = parse_binop(p, prec + 1);

        RhSLNode* n = rh_sl_node_alloc(SL_NODE_BINOP, line);
        n->u.binop.op = op;
        n->u.binop.left = left;
        n->u.binop.right = right;
        left = n;
    }

    return left;
}

static RhSLNode* parse_ternary(RhSLParser* p) {
    RhSLNode* cond = parse_binop(p, 0);

    if (peek(p) == TOK_QUESTION) {
        int line = peek_line(p);
        advance(p);
        RhSLNode* then_e = parse_expression(p);
        expect(p, TOK_COLON);
        RhSLNode* else_e = parse_expression(p);

        RhSLNode* n = rh_sl_node_alloc(SL_NODE_TERNARY, line);
        n->u.ternary.cond = cond;
        n->u.ternary.then_expr = then_e;
        n->u.ternary.else_expr = else_e;
        return n;
    }

    return cond;
}

static RhSLNode* parse_expression(RhSLParser* p) {
    return parse_ternary(p);
}

/* ------------------------------------------------------------------ */
/*  Statement parsing                                                  */
/* ------------------------------------------------------------------ */

/* Parse storage qualifiers: uniform, varying, output, extern */
static void parse_storage_qualifiers(RhSLParser* p, RhSLStorage* storage, int* is_output) {
    *storage = SL_STORAGE_DEFAULT;
    *is_output = 0;
    while (1) {
        if (peek(p) == TOK_UNIFORM) {
            *storage = SL_STORAGE_UNIFORM;
            advance(p);
        } else if (peek(p) == TOK_VARYING) {
            *storage = SL_STORAGE_VARYING;
            advance(p);
        } else if (peek(p) == TOK_OUTPUT) {
            *is_output = 1;
            advance(p);
        } else if (peek(p) == TOK_EXTERN) {
            advance(p); /* absorb extern, no separate tracking needed */
        } else {
            break;
        }
    }
}

/* Try to parse a variable declaration (type name [= init]; ).
 * Returns NULL if current token isn't a type keyword. */
static RhSLNode* try_parse_var_decl(RhSLParser* p) {
    /* Check for optional storage qualifiers followed by type */
    int saved_pos = p->lex.pos;
    int saved_line = p->lex.line;
    int saved_col = p->lex.col;
    RhSLToken saved_tok = p->lex.current;

    RhSLStorage storage;
    int is_output;
    parse_storage_qualifiers(p, &storage, &is_output);

    if (!is_type_token(peek(p)) || peek(p) == TOK_VOID) {
        /* Not a type -- restore and return NULL */
        p->lex.pos = saved_pos;
        p->lex.line = saved_line;
        p->lex.col = saved_col;
        p->lex.current = saved_tok;
        return NULL;
    }

    RhSLType type = token_to_type(peek(p));
    int line = peek_line(p);
    advance(p);

    /* Check for coordinate space string before identifier */
    char space[SL_MAX_NAME] = "";

    /* Must be followed by identifier for declaration */
    if (peek(p) != TOK_IDENT) {
        /* Could be a type constructor -- restore */
        p->lex.pos = saved_pos;
        p->lex.line = saved_line;
        p->lex.col = saved_col;
        p->lex.current = saved_tok;
        return NULL;
    }

    /* It's a declaration: type name ... */
    RhSLNode* first = NULL;

    /* Parse comma-separated declarators: type a, b = expr, c; */
    do {
        if (peek(p) != TOK_IDENT) {
            parse_error(p, "expected variable name");
            break;
        }

        RhSLNode* n = rh_sl_node_alloc(SL_NODE_VAR_DECL, line);
        n->u.var_decl.type = type;
        n->u.var_decl.storage = storage;
        sl_strcpy(n->u.var_decl.space, sizeof(n->u.var_decl.space), space);
        sl_strcpy(n->u.var_decl.name, sizeof(n->u.var_decl.name), p->lex.current.text);
        advance(p);

        /* Array size */
        if (peek(p) == TOK_LBRACKET) {
            advance(p);
            if (peek(p) == TOK_INT_LIT || peek(p) == TOK_FLOAT_LIT) {
                n->u.var_decl.array_size = (int)p->lex.current.float_val;
                advance(p);
            }
            expect(p, TOK_RBRACKET);
        }

        /* Initializer */
        if (peek(p) == TOK_ASSIGN) {
            advance(p);
            n->u.var_decl.init = parse_expression(p);
        }

        first = rh_sl_node_append(first, n);
    } while (match(p, TOK_COMMA));

    expect(p, TOK_SEMICOLON);
    return first;
}

static RhSLNode* parse_block(RhSLParser* p) {
    int line = peek_line(p);
    expect(p, TOK_LBRACE);

    RhSLNode* n = rh_sl_node_alloc(SL_NODE_BLOCK, line);
    RhSLNode* stmts = NULL;

    while (peek(p) != TOK_RBRACE && peek(p) != TOK_EOF) {
        RhSLNode* s = parse_statement(p);
        if (s) stmts = rh_sl_node_append(stmts, s);
    }

    n->u.block.stmts = stmts;
    expect(p, TOK_RBRACE);
    return n;
}

static RhSLNode* parse_if_stmt(RhSLParser* p) {
    int line = peek_line(p);
    expect(p, TOK_IF);
    expect(p, TOK_LPAREN);
    RhSLNode* cond = parse_expression(p);
    expect(p, TOK_RPAREN);

    RhSLNode* then_body;
    if (peek(p) == TOK_LBRACE)
        then_body = parse_block(p);
    else
        then_body = parse_statement(p);

    RhSLNode* else_body = NULL;
    if (peek(p) == TOK_ELSE) {
        advance(p);
        if (peek(p) == TOK_LBRACE)
            else_body = parse_block(p);
        else
            else_body = parse_statement(p);
    }

    RhSLNode* n = rh_sl_node_alloc(SL_NODE_IF, line);
    n->u.if_stmt.cond = cond;
    n->u.if_stmt.then_body = then_body;
    n->u.if_stmt.else_body = else_body;
    return n;
}

static RhSLNode* parse_while_stmt(RhSLParser* p) {
    int line = peek_line(p);
    expect(p, TOK_WHILE);
    expect(p, TOK_LPAREN);
    RhSLNode* cond = parse_expression(p);
    expect(p, TOK_RPAREN);

    RhSLNode* body;
    if (peek(p) == TOK_LBRACE)
        body = parse_block(p);
    else
        body = parse_statement(p);

    RhSLNode* n = rh_sl_node_alloc(SL_NODE_WHILE, line);
    n->u.while_stmt.cond = cond;
    n->u.while_stmt.body = body;
    return n;
}

static RhSLNode* parse_for_stmt(RhSLParser* p) {
    int line = peek_line(p);
    expect(p, TOK_FOR);
    expect(p, TOK_LPAREN);

    /* Init */
    RhSLNode* init = try_parse_var_decl(p);
    if (!init) {
        init = parse_expression(p);
        /* Check for assignment */
        if (peek(p) == TOK_ASSIGN && init->node_type == SL_NODE_IDENT) {
            advance(p);
            RhSLNode* val = parse_expression(p);
            RhSLNode* a = rh_sl_node_alloc(SL_NODE_ASSIGN, line);
            a->u.assign.target = init;
            a->u.assign.value = val;
            init = a;
        }
        expect(p, TOK_SEMICOLON);
    }

    /* Condition */
    RhSLNode* cond = parse_expression(p);
    expect(p, TOK_SEMICOLON);

    /* Increment */
    RhSLNode* inc = parse_expression(p);
    /* Check for assignment in increment */
    if (peek(p) == TOK_ASSIGN && inc->node_type == SL_NODE_IDENT) {
        advance(p);
        RhSLNode* val = parse_expression(p);
        RhSLNode* a = rh_sl_node_alloc(SL_NODE_ASSIGN, line);
        a->u.assign.target = inc;
        a->u.assign.value = val;
        inc = a;
    } else if (peek(p) == TOK_PLUS_ASSIGN || peek(p) == TOK_MINUS_ASSIGN ||
               peek(p) == TOK_STAR_ASSIGN || peek(p) == TOK_SLASH_ASSIGN) {
        RhSLCompoundOp cop;
        switch (peek(p)) {
        case TOK_PLUS_ASSIGN:  cop = SL_COMPOUND_ADD; break;
        case TOK_MINUS_ASSIGN: cop = SL_COMPOUND_SUB; break;
        case TOK_STAR_ASSIGN:  cop = SL_COMPOUND_MUL; break;
        default:               cop = SL_COMPOUND_DIV; break;
        }
        advance(p);
        RhSLNode* val = parse_expression(p);
        RhSLNode* a = rh_sl_node_alloc(SL_NODE_COMPOUND_ASSIGN, line);
        a->u.compound_assign.op = cop;
        a->u.compound_assign.target = inc;
        a->u.compound_assign.value = val;
        inc = a;
    }

    expect(p, TOK_RPAREN);

    RhSLNode* body;
    if (peek(p) == TOK_LBRACE)
        body = parse_block(p);
    else
        body = parse_statement(p);

    RhSLNode* n = rh_sl_node_alloc(SL_NODE_FOR, line);
    n->u.for_stmt.init = init;
    n->u.for_stmt.cond = cond;
    n->u.for_stmt.inc = inc;
    n->u.for_stmt.body = body;
    return n;
}

static RhSLNode* parse_illuminance_stmt(RhSLParser* p) {
    int line = peek_line(p);
    expect(p, TOK_ILLUMINANCE);
    expect(p, TOK_LPAREN);

    RhSLNode* pos = parse_expression(p);
    RhSLNode* normal = NULL;
    RhSLNode* angle = NULL;

    if (peek(p) == TOK_COMMA) {
        advance(p);
        normal = parse_expression(p);
        if (peek(p) == TOK_COMMA) {
            advance(p);
            angle = parse_expression(p);
        }
    }

    expect(p, TOK_RPAREN);
    RhSLNode* body = parse_block(p);

    RhSLNode* n = rh_sl_node_alloc(SL_NODE_ILLUMINANCE, line);
    n->u.illuminance.position = pos;
    n->u.illuminance.normal = normal;
    n->u.illuminance.angle = angle;
    n->u.illuminance.body = body;
    return n;
}

static RhSLNode* parse_illuminate_stmt(RhSLParser* p) {
    int line = peek_line(p);
    expect(p, TOK_ILLUMINATE);
    expect(p, TOK_LPAREN);

    RhSLNode* pos = parse_expression(p);
    RhSLNode* axis = NULL;
    RhSLNode* angle = NULL;

    if (peek(p) == TOK_COMMA) {
        advance(p);
        axis = parse_expression(p);
        if (peek(p) == TOK_COMMA) {
            advance(p);
            angle = parse_expression(p);
        }
    }

    expect(p, TOK_RPAREN);
    RhSLNode* body = parse_block(p);

    RhSLNode* n = rh_sl_node_alloc(SL_NODE_ILLUMINATE, line);
    n->u.illuminate.position = pos;
    n->u.illuminate.axis = axis;
    n->u.illuminate.angle = angle;
    n->u.illuminate.body = body;
    return n;
}

static RhSLNode* parse_solar_stmt(RhSLParser* p) {
    int line = peek_line(p);
    expect(p, TOK_SOLAR);
    expect(p, TOK_LPAREN);

    RhSLNode* axis = NULL;
    RhSLNode* angle = NULL;

    if (peek(p) != TOK_RPAREN) {
        axis = parse_expression(p);
        if (peek(p) == TOK_COMMA) {
            advance(p);
            angle = parse_expression(p);
        }
    }

    expect(p, TOK_RPAREN);
    RhSLNode* body = parse_block(p);

    RhSLNode* n = rh_sl_node_alloc(SL_NODE_SOLAR, line);
    n->u.solar.axis = axis;
    n->u.solar.angle = angle;
    n->u.solar.body = body;
    return n;
}

static RhSLNode* parse_return_stmt(RhSLParser* p) {
    int line = peek_line(p);
    expect(p, TOK_RETURN);

    RhSLNode* n = rh_sl_node_alloc(SL_NODE_RETURN, line);
    if (peek(p) != TOK_SEMICOLON)
        n->u.ret.value = parse_expression(p);
    expect(p, TOK_SEMICOLON);
    return n;
}

/* Parse an expression statement (assignment or bare expression). */
static RhSLNode* parse_expr_statement(RhSLParser* p) {
    int line = peek_line(p);
    RhSLNode* expr = parse_expression(p);

    /* Assignment: expr = value */
    if (peek(p) == TOK_ASSIGN) {
        advance(p);
        RhSLNode* val = parse_expression(p);
        RhSLNode* n = rh_sl_node_alloc(SL_NODE_ASSIGN, line);
        n->u.assign.target = expr;
        n->u.assign.value = val;
        expect(p, TOK_SEMICOLON);
        return n;
    }

    /* Compound assignment: expr += value */
    if (peek(p) == TOK_PLUS_ASSIGN || peek(p) == TOK_MINUS_ASSIGN ||
        peek(p) == TOK_STAR_ASSIGN || peek(p) == TOK_SLASH_ASSIGN) {
        RhSLCompoundOp cop;
        switch (peek(p)) {
        case TOK_PLUS_ASSIGN:  cop = SL_COMPOUND_ADD; break;
        case TOK_MINUS_ASSIGN: cop = SL_COMPOUND_SUB; break;
        case TOK_STAR_ASSIGN:  cop = SL_COMPOUND_MUL; break;
        default:               cop = SL_COMPOUND_DIV; break;
        }
        advance(p);
        RhSLNode* val = parse_expression(p);
        RhSLNode* n = rh_sl_node_alloc(SL_NODE_COMPOUND_ASSIGN, line);
        n->u.compound_assign.op = cop;
        n->u.compound_assign.target = expr;
        n->u.compound_assign.value = val;
        expect(p, TOK_SEMICOLON);
        return n;
    }

    /* Bare expression statement (e.g., function call) */
    expect(p, TOK_SEMICOLON);
    return expr;
}

static RhSLNode* parse_statement(RhSLParser* p) {
    switch (peek(p)) {
    case TOK_IF:           return parse_if_stmt(p);
    case TOK_WHILE:        return parse_while_stmt(p);
    case TOK_FOR:          return parse_for_stmt(p);
    case TOK_RETURN:       return parse_return_stmt(p);
    case TOK_ILLUMINANCE:  return parse_illuminance_stmt(p);
    case TOK_ILLUMINATE:   return parse_illuminate_stmt(p);
    case TOK_SOLAR:        return parse_solar_stmt(p);
    case TOK_LBRACE:       return parse_block(p);

    case TOK_BREAK: {
        int line = peek_line(p);
        advance(p);
        expect(p, TOK_SEMICOLON);
        return rh_sl_node_alloc(SL_NODE_BREAK, line);
    }
    case TOK_CONTINUE: {
        int line = peek_line(p);
        advance(p);
        expect(p, TOK_SEMICOLON);
        return rh_sl_node_alloc(SL_NODE_CONTINUE, line);
    }

    default: {
        /* Try variable declaration first */
        RhSLNode* decl = try_parse_var_decl(p);
        if (decl) return decl;

        /* Expression statement (assignment, function call, etc.) */
        return parse_expr_statement(p);
    }
    }
}

/* ------------------------------------------------------------------ */
/*  Shader parameter parsing                                           */
/* ------------------------------------------------------------------ */

static RhSLNode* parse_shader_params(RhSLParser* p) {
    RhSLNode* params = NULL;

    expect(p, TOK_LPAREN);
    if (peek(p) == TOK_RPAREN) {
        advance(p);
        return NULL;
    }

    while (1) {
        int line = peek_line(p);

        /* Storage qualifiers */
        RhSLStorage storage;
        int is_output;
        parse_storage_qualifiers(p, &storage, &is_output);

        /* Type */
        if (!is_type_token(peek(p))) {
            parse_error(p, "expected parameter type");
            break;
        }
        RhSLType type = token_to_type(peek(p));
        advance(p);

        /* Parse one or more parameter names sharing the same type.
         * RSL uses semicolons between parameter groups and commas
         * between names within a group:
         *   float Ka = 1, Kd = 1; color Cs = 1
         */
        do {
            if (peek(p) != TOK_IDENT) {
                parse_error(p, "expected parameter name");
                break;
            }

            RhSLNode* param = rh_sl_node_alloc(SL_NODE_PARAM, line);
            param->u.param.type = type;
            param->u.param.storage = storage;
            param->u.param.is_output = is_output;
            sl_strcpy(param->u.param.name, sizeof(param->u.param.name), p->lex.current.text);
            advance(p);

            /* Array size */
            if (peek(p) == TOK_LBRACKET) {
                advance(p);
                if (peek(p) == TOK_INT_LIT || peek(p) == TOK_FLOAT_LIT) {
                    param->u.param.array_size = (int)p->lex.current.float_val;
                    advance(p);
                }
                expect(p, TOK_RBRACKET);
            }

            /* Default value */
            if (peek(p) == TOK_ASSIGN) {
                advance(p);
                param->u.param.default_val = parse_expression(p);
            }

            params = rh_sl_node_append(params, param);
        } while (match(p, TOK_COMMA));

        /* Semicolon separates parameter groups, rparen ends */
        if (peek(p) == TOK_RPAREN) break;
        if (!match(p, TOK_SEMICOLON)) {
            parse_error(p, "expected ';' or ')' in parameter list");
            break;
        }
        if (peek(p) == TOK_RPAREN) break;
    }

    expect(p, TOK_RPAREN);
    return params;
}

/* ------------------------------------------------------------------ */
/*  Function definition parsing                                        */
/* ------------------------------------------------------------------ */

static RhSLNode* parse_function_formals(RhSLParser* p) {
    RhSLNode* formals = NULL;

    expect(p, TOK_LPAREN);
    if (peek(p) == TOK_RPAREN) {
        advance(p);
        return NULL;
    }

    while (1) {
        int line = peek_line(p);

        RhSLStorage storage;
        int is_output;
        parse_storage_qualifiers(p, &storage, &is_output);

        if (!is_type_token(peek(p))) {
            parse_error(p, "expected parameter type");
            break;
        }
        RhSLType type = token_to_type(peek(p));
        advance(p);

        /* RSL function formals: type name1, name2; type name3 */
        do {
            if (peek(p) != TOK_IDENT) {
                parse_error(p, "expected parameter name");
                break;
            }

            RhSLNode* f = rh_sl_node_alloc(SL_NODE_FORMAL, line);
            f->u.formal.type = type;
            f->u.formal.storage = storage;
            f->u.formal.is_output = is_output;
            sl_strcpy(f->u.formal.name, sizeof(f->u.formal.name), p->lex.current.text);
            advance(p);

            formals = rh_sl_node_append(formals, f);
        } while (match(p, TOK_COMMA));

        if (peek(p) == TOK_RPAREN) break;
        if (!match(p, TOK_SEMICOLON)) {
            parse_error(p, "expected ';' or ')' in formal parameter list");
            break;
        }
        if (peek(p) == TOK_RPAREN) break;
    }

    expect(p, TOK_RPAREN);
    return formals;
}

/* ------------------------------------------------------------------ */
/*  Top-level parsing                                                  */
/* ------------------------------------------------------------------ */

void rh_sl_parse_init(RhSLParser* parser, const char* source) {
    memset(parser, 0, sizeof(RhSLParser));
    rh_sl_lex_init(&parser->lex, source);
}

RhSLNode* rh_sl_parse(RhSLParser* parser) {
    RhSLParser* p = parser;
    RhSLNode* functions = NULL;

    /* Parse any preceding function definitions */
    while (peek(p) != TOK_EOF && !is_shader_type(peek(p))) {
        /* Must be: return_type function_name(...) { ... } */
        int line = peek_line(p);

        /* Return type (could be void or a data type) */
        if (!is_type_token(peek(p))) {
            parse_error(p, "expected shader type or function return type");
            advance(p);
            continue;
        }
        RhSLType ret_type = token_to_type(peek(p));
        advance(p);

        if (peek(p) != TOK_IDENT) {
            parse_error(p, "expected function name");
            advance(p);
            continue;
        }

        RhSLNode* fn = rh_sl_node_alloc(SL_NODE_FUNCTION, line);
        fn->u.function.return_type = ret_type;
        sl_strcpy(fn->u.function.name, sizeof(fn->u.function.name), p->lex.current.text);
        advance(p);

        fn->u.function.formals = parse_function_formals(p);
        fn->u.function.body = parse_block(p);

        functions = rh_sl_node_append(functions, fn);
    }

    /* Parse the shader definition */
    if (!is_shader_type(peek(p))) {
        parse_error(p, "expected shader type (surface, light, displacement, volume)");
        return NULL;
    }

    int line = peek_line(p);
    RhSLShaderType shader_type;
    switch (peek(p)) {
    case TOK_SURFACE:       shader_type = SL_SHADER_SURFACE; break;
    case TOK_LIGHT:         shader_type = SL_SHADER_LIGHT; break;
    case TOK_DISPLACEMENT:  shader_type = SL_SHADER_DISPLACEMENT; break;
    default:                shader_type = SL_SHADER_VOLUME; break;
    }
    advance(p);

    /* Shader name */
    if (peek(p) != TOK_IDENT) {
        parse_error(p, "expected shader name");
        return NULL;
    }

    RhSLNode* shader = rh_sl_node_alloc(SL_NODE_SHADER, line);
    shader->u.shader.shader_type = shader_type;
    sl_strcpy(shader->u.shader.name, sizeof(shader->u.shader.name), p->lex.current.text);
    advance(p);

    /* Parameters */
    shader->u.shader.params = parse_shader_params(p);

    /* Body */
    shader->u.shader.body = parse_block(p);

    /* Attach preceding functions */
    shader->u.shader.functions = functions;

    return shader;
}
