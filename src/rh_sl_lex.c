#include "rh_sl_lex.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/*  Keyword table                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    const char* word;
    RhSLTokenType type;
} Keyword;

static const Keyword keywords[] = {
    {"surface",       TOK_SURFACE},
    {"light",         TOK_LIGHT},
    {"displacement",  TOK_DISPLACEMENT},
    {"volume",        TOK_VOLUME},
    {"float",         TOK_FLOAT},
    {"color",         TOK_COLOR},
    {"point",         TOK_POINT},
    {"vector",        TOK_VECTOR},
    {"normal",        TOK_NORMAL},
    {"matrix",        TOK_MATRIX},
    {"string",        TOK_STRING},
    {"void",          TOK_VOID},
    {"uniform",       TOK_UNIFORM},
    {"varying",       TOK_VARYING},
    {"output",        TOK_OUTPUT},
    {"extern",        TOK_EXTERN},
    {"if",            TOK_IF},
    {"else",          TOK_ELSE},
    {"while",         TOK_WHILE},
    {"for",           TOK_FOR},
    {"return",        TOK_RETURN},
    {"break",         TOK_BREAK},
    {"continue",      TOK_CONTINUE},
    {"illuminance",   TOK_ILLUMINANCE},
    {"illuminate",    TOK_ILLUMINATE},
    {"solar",         TOK_SOLAR},
    {NULL, TOK_EOF}
};

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static int lex_at_end(const RhSLLexer* lex) {
    return lex->pos >= lex->len;
}

static char lex_peek_char(const RhSLLexer* lex) {
    if (lex->pos >= lex->len) return '\0';
    return lex->src[lex->pos];
}

static char lex_peek_char2(const RhSLLexer* lex) {
    if (lex->pos + 1 >= lex->len) return '\0';
    return lex->src[lex->pos + 1];
}

static char lex_advance(RhSLLexer* lex) {
    if (lex->pos >= lex->len) return '\0';
    char c = lex->src[lex->pos++];
    if (c == '\n') {
        lex->line++;
        lex->col = 1;
    } else {
        lex->col++;
    }
    return c;
}

static void skip_whitespace_and_comments(RhSLLexer* lex) {
    while (!lex_at_end(lex)) {
        char c = lex_peek_char(lex);

        /* Whitespace */
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            lex_advance(lex);
            continue;
        }

        /* Block comment: / * ... * / */
        if (c == '/' && lex_peek_char2(lex) == '*') {
            lex_advance(lex); /* consume / */
            lex_advance(lex); /* consume * */
            while (!lex_at_end(lex)) {
                if (lex_peek_char(lex) == '*' && lex_peek_char2(lex) == '/') {
                    lex_advance(lex); /* consume * */
                    lex_advance(lex); /* consume / */
                    break;
                }
                lex_advance(lex);
            }
            continue;
        }

        /* Line comment: // ... */
        if (c == '/' && lex_peek_char2(lex) == '/') {
            lex_advance(lex); /* consume / */
            lex_advance(lex); /* consume / */
            while (!lex_at_end(lex) && lex_peek_char(lex) != '\n')
                lex_advance(lex);
            continue;
        }

        /* Preprocessor lines: # ... (skip entire line) */
        if (c == '#') {
            while (!lex_at_end(lex) && lex_peek_char(lex) != '\n')
                lex_advance(lex);
            continue;
        }

        break; /* Not whitespace or comment */
    }
}

static void set_error(RhSLLexer* lex, const char* msg) {
    snprintf(lex->error, sizeof(lex->error), "line %d col %d: %s",
             lex->line, lex->col, msg);
    lex->current.type = TOK_ERROR;
    memcpy(lex->current.text, lex->error, RH_SL_MAX_TOKEN_LEN);
    lex->current.text[RH_SL_MAX_TOKEN_LEN - 1] = '\0';
}

/* ------------------------------------------------------------------ */
/*  Token scanning                                                     */
/* ------------------------------------------------------------------ */

static void scan_number(RhSLLexer* lex) {
    int start = lex->pos;
    int is_float = 0;

    /* Integer part */
    while (!lex_at_end(lex) && isdigit((unsigned char)lex_peek_char(lex)))
        lex_advance(lex);

    /* Fractional part */
    if (!lex_at_end(lex) && lex_peek_char(lex) == '.') {
        /* Check it's not a dot operator following a number with no fractional digits */
        char after_dot = lex_peek_char2(lex);
        if (isdigit((unsigned char)after_dot) || after_dot == 'e' || after_dot == 'E'
            || !isalpha((unsigned char)after_dot)) {
            is_float = 1;
            lex_advance(lex); /* consume . */
            while (!lex_at_end(lex) && isdigit((unsigned char)lex_peek_char(lex)))
                lex_advance(lex);
        }
    }

    /* Exponent */
    if (!lex_at_end(lex) && (lex_peek_char(lex) == 'e' || lex_peek_char(lex) == 'E')) {
        is_float = 1;
        lex_advance(lex);
        if (!lex_at_end(lex) && (lex_peek_char(lex) == '+' || lex_peek_char(lex) == '-'))
            lex_advance(lex);
        while (!lex_at_end(lex) && isdigit((unsigned char)lex_peek_char(lex)))
            lex_advance(lex);
    }

    int len = lex->pos - start;
    if (len >= RH_SL_MAX_TOKEN_LEN) len = RH_SL_MAX_TOKEN_LEN - 1;
    memcpy(lex->current.text, &lex->src[start], (size_t)len);
    lex->current.text[len] = '\0';
    lex->current.float_val = (float)atof(lex->current.text);
    lex->current.type = is_float ? TOK_FLOAT_LIT : TOK_INT_LIT;
}

static void scan_string(RhSLLexer* lex) {
    lex_advance(lex); /* consume opening " */
    int start = lex->pos;
    int out = 0;

    while (!lex_at_end(lex) && lex_peek_char(lex) != '"') {
        char c = lex_advance(lex);
        if (c == '\\' && !lex_at_end(lex)) {
            char esc = lex_advance(lex);
            switch (esc) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case '\\': c = '\\'; break;
                case '"': c = '"'; break;
                default: c = esc; break;
            }
        }
        if (out < RH_SL_MAX_TOKEN_LEN - 1)
            lex->current.text[out++] = c;
    }

    if (!lex_at_end(lex))
        lex_advance(lex); /* consume closing " */
    else
        set_error(lex, "unterminated string literal");

    lex->current.text[out] = '\0';
    lex->current.type = TOK_STRING_LIT;

    (void)start;
}

static void scan_ident_or_keyword(RhSLLexer* lex) {
    int start = lex->pos;

    while (!lex_at_end(lex)) {
        char c = lex_peek_char(lex);
        if (isalnum((unsigned char)c) || c == '_')
            lex_advance(lex);
        else
            break;
    }

    int len = lex->pos - start;
    if (len >= RH_SL_MAX_TOKEN_LEN) len = RH_SL_MAX_TOKEN_LEN - 1;
    memcpy(lex->current.text, &lex->src[start], (size_t)len);
    lex->current.text[len] = '\0';

    /* Check keywords */
    for (int i = 0; keywords[i].word; i++) {
        if (strcmp(lex->current.text, keywords[i].word) == 0) {
            lex->current.type = keywords[i].type;
            return;
        }
    }

    lex->current.type = TOK_IDENT;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void rh_sl_lex_init(RhSLLexer* lex, const char* source) {
    memset(lex, 0, sizeof(RhSLLexer));
    lex->src = source;
    lex->len = (int)strlen(source);
    lex->pos = 0;
    lex->line = 1;
    lex->col = 1;
    lex->current.type = TOK_EOF;
    /* Prime the lexer with the first token */
    rh_sl_lex_next(lex);
}

RhSLTokenType rh_sl_lex_next(RhSLLexer* lex) {
    skip_whitespace_and_comments(lex);

    lex->current.line = lex->line;
    lex->current.col = lex->col;
    lex->current.text[0] = '\0';
    lex->current.float_val = 0.0f;

    if (lex_at_end(lex)) {
        lex->current.type = TOK_EOF;
        return TOK_EOF;
    }

    char c = lex_peek_char(lex);

    /* Number */
    if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)lex_peek_char2(lex)))) {
        scan_number(lex);
        return lex->current.type;
    }

    /* String literal */
    if (c == '"') {
        scan_string(lex);
        return lex->current.type;
    }

    /* Identifier or keyword */
    if (isalpha((unsigned char)c) || c == '_') {
        scan_ident_or_keyword(lex);
        return lex->current.type;
    }

    /* Operators and punctuation */
    lex_advance(lex);
    char c2 = lex_peek_char(lex);

    switch (c) {
    case '+':
        if (c2 == '=') { lex_advance(lex); lex->current.type = TOK_PLUS_ASSIGN; strcpy(lex->current.text, "+="); }
        else { lex->current.type = TOK_PLUS; strcpy(lex->current.text, "+"); }
        break;
    case '-':
        if (c2 == '=') { lex_advance(lex); lex->current.type = TOK_MINUS_ASSIGN; strcpy(lex->current.text, "-="); }
        else { lex->current.type = TOK_MINUS; strcpy(lex->current.text, "-"); }
        break;
    case '*':
        if (c2 == '=') { lex_advance(lex); lex->current.type = TOK_STAR_ASSIGN; strcpy(lex->current.text, "*="); }
        else { lex->current.type = TOK_STAR; strcpy(lex->current.text, "*"); }
        break;
    case '/':
        if (c2 == '=') { lex_advance(lex); lex->current.type = TOK_SLASH_ASSIGN; strcpy(lex->current.text, "/="); }
        else { lex->current.type = TOK_SLASH; strcpy(lex->current.text, "/"); }
        break;
    case '.': lex->current.type = TOK_DOT; strcpy(lex->current.text, "."); break;
    case '^': lex->current.type = TOK_CARET; strcpy(lex->current.text, "^"); break;
    case '=':
        if (c2 == '=') { lex_advance(lex); lex->current.type = TOK_EQ; strcpy(lex->current.text, "=="); }
        else { lex->current.type = TOK_ASSIGN; strcpy(lex->current.text, "="); }
        break;
    case '!':
        if (c2 == '=') { lex_advance(lex); lex->current.type = TOK_NE; strcpy(lex->current.text, "!="); }
        else { lex->current.type = TOK_NOT; strcpy(lex->current.text, "!"); }
        break;
    case '<':
        if (c2 == '=') { lex_advance(lex); lex->current.type = TOK_LE; strcpy(lex->current.text, "<="); }
        else { lex->current.type = TOK_LT; strcpy(lex->current.text, "<"); }
        break;
    case '>':
        if (c2 == '=') { lex_advance(lex); lex->current.type = TOK_GE; strcpy(lex->current.text, ">="); }
        else { lex->current.type = TOK_GT; strcpy(lex->current.text, ">"); }
        break;
    case '&':
        if (c2 == '&') { lex_advance(lex); lex->current.type = TOK_AND; strcpy(lex->current.text, "&&"); }
        else { set_error(lex, "unexpected '&' (did you mean '&&'?)"); }
        break;
    case '|':
        if (c2 == '|') { lex_advance(lex); lex->current.type = TOK_OR; strcpy(lex->current.text, "||"); }
        else { set_error(lex, "unexpected '|' (did you mean '||'?)"); }
        break;
    case '?': lex->current.type = TOK_QUESTION; strcpy(lex->current.text, "?"); break;
    case ':': lex->current.type = TOK_COLON; strcpy(lex->current.text, ":"); break;
    case '(': lex->current.type = TOK_LPAREN; strcpy(lex->current.text, "("); break;
    case ')': lex->current.type = TOK_RPAREN; strcpy(lex->current.text, ")"); break;
    case '{': lex->current.type = TOK_LBRACE; strcpy(lex->current.text, "{"); break;
    case '}': lex->current.type = TOK_RBRACE; strcpy(lex->current.text, "}"); break;
    case '[': lex->current.type = TOK_LBRACKET; strcpy(lex->current.text, "["); break;
    case ']': lex->current.type = TOK_RBRACKET; strcpy(lex->current.text, "]"); break;
    case ';': lex->current.type = TOK_SEMICOLON; strcpy(lex->current.text, ";"); break;
    case ',': lex->current.type = TOK_COMMA; strcpy(lex->current.text, ","); break;
    default: {
        char msg[64];
        snprintf(msg, sizeof(msg), "unexpected character '%c' (0x%02x)", c, (unsigned char)c);
        set_error(lex, msg);
        break;
    }
    }

    return lex->current.type;
}

const RhSLToken* rh_sl_lex_peek(const RhSLLexer* lex) {
    return &lex->current;
}

const char* rh_sl_token_name(RhSLTokenType type) {
    switch (type) {
    case TOK_EOF:           return "EOF";
    case TOK_ERROR:         return "ERROR";
    case TOK_INT_LIT:       return "INT_LIT";
    case TOK_FLOAT_LIT:     return "FLOAT_LIT";
    case TOK_STRING_LIT:    return "STRING_LIT";
    case TOK_IDENT:         return "IDENT";
    case TOK_SURFACE:       return "surface";
    case TOK_LIGHT:         return "light";
    case TOK_DISPLACEMENT:  return "displacement";
    case TOK_VOLUME:        return "volume";
    case TOK_FLOAT:         return "float";
    case TOK_COLOR:         return "color";
    case TOK_POINT:         return "point";
    case TOK_VECTOR:        return "vector";
    case TOK_NORMAL:        return "normal";
    case TOK_MATRIX:        return "matrix";
    case TOK_STRING:        return "string";
    case TOK_VOID:          return "void";
    case TOK_UNIFORM:       return "uniform";
    case TOK_VARYING:       return "varying";
    case TOK_OUTPUT:        return "output";
    case TOK_EXTERN:        return "extern";
    case TOK_IF:            return "if";
    case TOK_ELSE:          return "else";
    case TOK_WHILE:         return "while";
    case TOK_FOR:           return "for";
    case TOK_RETURN:        return "return";
    case TOK_BREAK:         return "break";
    case TOK_CONTINUE:      return "continue";
    case TOK_ILLUMINANCE:   return "illuminance";
    case TOK_ILLUMINATE:    return "illuminate";
    case TOK_SOLAR:         return "solar";
    case TOK_PLUS:          return "+";
    case TOK_MINUS:         return "-";
    case TOK_STAR:          return "*";
    case TOK_SLASH:         return "/";
    case TOK_DOT:           return ".";
    case TOK_CARET:         return "^";
    case TOK_ASSIGN:        return "=";
    case TOK_EQ:            return "==";
    case TOK_NE:            return "!=";
    case TOK_LT:            return "<";
    case TOK_GT:            return ">";
    case TOK_LE:            return "<=";
    case TOK_GE:            return ">=";
    case TOK_AND:           return "&&";
    case TOK_OR:            return "||";
    case TOK_NOT:           return "!";
    case TOK_QUESTION:      return "?";
    case TOK_COLON:         return ":";
    case TOK_PLUS_ASSIGN:   return "+=";
    case TOK_MINUS_ASSIGN:  return "-=";
    case TOK_STAR_ASSIGN:   return "*=";
    case TOK_SLASH_ASSIGN:  return "/=";
    case TOK_LPAREN:        return "(";
    case TOK_RPAREN:        return ")";
    case TOK_LBRACE:        return "{";
    case TOK_RBRACE:        return "}";
    case TOK_LBRACKET:      return "[";
    case TOK_RBRACKET:      return "]";
    case TOK_SEMICOLON:     return ";";
    case TOK_COMMA:         return ",";
    default:                return "?unknown?";
    }
}
