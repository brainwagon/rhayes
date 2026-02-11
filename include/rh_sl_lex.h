#ifndef RH_SL_LEX_H
#define RH_SL_LEX_H

/*
 * RSL Lexer -- hand-written tokenizer for the RenderMan Shading Language.
 *
 * Produces a stream of tokens from a source string.  Supports keywords,
 * identifiers, numeric literals, string literals, operators, punctuation,
 * and both C-style block comments and C++ line comments.
 */

typedef enum {
    /* End / error */
    TOK_EOF = 0,
    TOK_ERROR,

    /* Literals */
    TOK_INT_LIT,        /* integer literal (stored as float) */
    TOK_FLOAT_LIT,      /* floating-point literal */
    TOK_STRING_LIT,     /* "string" */

    /* Identifier */
    TOK_IDENT,

    /* Shader types */
    TOK_SURFACE,
    TOK_LIGHT,
    TOK_DISPLACEMENT,
    TOK_VOLUME,

    /* Data types */
    TOK_FLOAT,
    TOK_COLOR,
    TOK_POINT,
    TOK_VECTOR,
    TOK_NORMAL,
    TOK_MATRIX,
    TOK_STRING,
    TOK_VOID,

    /* Storage qualifiers */
    TOK_UNIFORM,
    TOK_VARYING,
    TOK_OUTPUT,
    TOK_EXTERN,

    /* Control flow */
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_FOR,
    TOK_RETURN,
    TOK_BREAK,
    TOK_CONTINUE,

    /* Lighting constructs */
    TOK_ILLUMINANCE,
    TOK_ILLUMINATE,
    TOK_SOLAR,

    /* Operators */
    TOK_PLUS,           /* + */
    TOK_MINUS,          /* - */
    TOK_STAR,           /* * */
    TOK_SLASH,          /* / */
    TOK_DOT,            /* . */
    TOK_CARET,          /* ^ */
    TOK_ASSIGN,         /* = */
    TOK_EQ,             /* == */
    TOK_NE,             /* != */
    TOK_LT,             /* < */
    TOK_GT,             /* > */
    TOK_LE,             /* <= */
    TOK_GE,             /* >= */
    TOK_AND,            /* && */
    TOK_OR,             /* || */
    TOK_NOT,            /* ! */
    TOK_QUESTION,       /* ? */
    TOK_COLON,          /* : */
    TOK_PLUS_ASSIGN,    /* += */
    TOK_MINUS_ASSIGN,   /* -= */
    TOK_STAR_ASSIGN,    /* *= */
    TOK_SLASH_ASSIGN,   /* /= */

    /* Punctuation */
    TOK_LPAREN,         /* ( */
    TOK_RPAREN,         /* ) */
    TOK_LBRACE,         /* { */
    TOK_RBRACE,         /* } */
    TOK_LBRACKET,       /* [ */
    TOK_RBRACKET,       /* ] */
    TOK_SEMICOLON,      /* ; */
    TOK_COMMA,          /* , */

    TOK_COUNT
} RhSLTokenType;

/* Maximum length for identifier/string token text */
#define RH_SL_MAX_TOKEN_LEN 256

typedef struct {
    RhSLTokenType type;
    char text[RH_SL_MAX_TOKEN_LEN];  /* Token text (identifier name, string content, etc.) */
    float float_val;                  /* Numeric value (for INT_LIT and FLOAT_LIT) */
    int line;                         /* Source line number (1-based) */
    int col;                          /* Source column number (1-based) */
} RhSLToken;

typedef struct {
    const char* src;        /* Source text (not owned) */
    int pos;                /* Current position in source */
    int len;                /* Total source length */
    int line;               /* Current line (1-based) */
    int col;                /* Current column (1-based) */
    RhSLToken current;      /* Most recently scanned token */
    char error[256];        /* Error message buffer */
} RhSLLexer;

/* Initialize lexer with source text */
void rh_sl_lex_init(RhSLLexer* lex, const char* source);

/* Advance to the next token.  Returns the token type. */
RhSLTokenType rh_sl_lex_next(RhSLLexer* lex);

/* Peek at the current token without consuming it */
const RhSLToken* rh_sl_lex_peek(const RhSLLexer* lex);

/* Return a human-readable name for a token type */
const char* rh_sl_token_name(RhSLTokenType type);

#endif /* RH_SL_LEX_H */
