#ifndef LEXER_H_
#define LEXER_H_

#include <stdbool.h>
#include <stddef.h>

#include "arena.h"
#include "diag.h"

typedef enum {
    TK_INVALID,                 // invalid token
    TK_EOF,                     // end of file

    // Literals
    TK_IDENT,                   // identifier
    TK_KW,                      // keyword
    TK_CHAR,                    // character
    TK_STR,                     // string
    TK_NUM,                     // number

    // Punctuators
    TK_OPAREN,                  // (
    TK_CPAREN,                  // )
    TK_OBRACE,                  // {
    TK_CBRACE,                  // }
    TK_OBRACK,                  // [
    TK_CBRACK,                  // ]
    TK_SEMI,                    // ;
    TK_COLON,                   // :
    TK_DOT,                     // .
    TK_COMMA,                   // ,

    // Arithmetic operators
    TK_PLUS,                    // +
    TK_PLUS_PLUS,               // ++
    TK_MINUS,                   // -
    TK_MINUS_MINUS,             // --
    TK_STAR,                    // *
    TK_SLASH,                   // /
    TK_PERCENT,                 // %

    // Assignment operators
    TK_EQ,                      // =
    TK_PLUS_EQ,                 // +=
    TK_MINUS_EQ,                // -=
    TK_STAR_EQ,                 // *=
    TK_SLASH_EQ,                // /=
    TK_PERCENT_EQ,              // %=
    TK_AMP_EQ,                  // &=
    TK_PIPE_EQ,                 // |=
    TK_CARET_EQ,                // ^=
    TK_LT_LT_EQ,                // <<=
    TK_GT_GT_EQ,                // >>=

    // Bitwise operators
    TK_TILDE,                   // ~
    TK_AMP,                     // &
    TK_PIPE,                    // |
    TK_CARET,                   // ^
    TK_LT_LT,                   // <<
    TK_GT_GT,                   // >>

    // Logical operators
    TK_BANG,                    // !
    TK_AMP_AMP,                 // &&
    TK_PIPE_PIPE,               // ||

    // Relational operators
    TK_EQ_EQ,                   // ==
    TK_BANG_EQ,                 // !=
    TK_LT,                      // <
    TK_LT_EQ,                   // <=
    TK_GT,                      // >
    TK_GT_EQ,                   // >=

    // Structure dereference operator
    TK_MINUS_GT,                // ->

    // Ternary operator
    TK_QUESTION,                // ?

    // Number of token kinds
    TK_COUNT,
} TokenKind;

// Array of human-readable `TokenKind`.
const char *token_kind_to_str[] = {
    [TK_INVALID] = "invalid",
    [TK_EOF] = "EOF",
    [TK_IDENT] = "identifier",
    [TK_KW] = "keyword",
    [TK_CHAR] = "char",
    [TK_STR] = "str",
    [TK_NUM] = "number",
    [TK_OPAREN] = "(",
    [TK_CPAREN] = ")",
    [TK_OBRACE] = "{",
    [TK_CBRACE] = "}",
    [TK_OBRACK] = "[",
    [TK_CBRACK] = "]",
    [TK_SEMI] = ";",
    [TK_COLON] = ":",
    [TK_DOT] = ".",
    [TK_COMMA] = ",",
    [TK_PLUS] = "+",
    [TK_PLUS_PLUS] = "++",
    [TK_MINUS] = "-",
    [TK_MINUS_MINUS] = "--",
    [TK_STAR] = "*",
    [TK_SLASH] = "/",
    [TK_PERCENT] = "%",
    [TK_EQ] = "=",
    [TK_PLUS_EQ] = "+=",
    [TK_MINUS_EQ] = "-=",
    [TK_STAR_EQ] = "*=",
    [TK_SLASH_EQ] = "/=",
    [TK_PERCENT_EQ] = "%=",
    [TK_AMP_EQ] = "&=",
    [TK_PIPE_EQ] = "|=",
    [TK_CARET_EQ] = "^=",
    [TK_LT_LT_EQ] = "<<=",
    [TK_GT_GT_EQ] = ">>=",
    [TK_TILDE] = "~",
    [TK_AMP] = "&",
    [TK_PIPE] = "|",
    [TK_CARET] = "^",
    [TK_LT_LT] = "<<",
    [TK_GT_GT] = ">>",
    [TK_BANG] = "!",
    [TK_AMP_AMP] = "&&",
    [TK_PIPE_PIPE] = "||",
    [TK_EQ_EQ] = "==",
    [TK_BANG_EQ] = "!=",
    [TK_LT] = "<",
    [TK_LT_EQ] = "<=",
    [TK_GT] = ">",
    [TK_GT_EQ] = ">=",
    [TK_MINUS_GT] = "->",
    [TK_QUESTION] = "?",
};

typedef struct {
    TokenKind kind;
    const char *start;
    size_t len;
    Loc loc;
} Token;

typedef struct {
    const char *file_path;
    const char *src;
    size_t size;                // size of source
    size_t pos;                 // position
    size_t bol;                 // beginning of current line (zero indexed)
    size_t line;                // current line number (zero indexed)
} Lexer;

const char *token_to_str(Token t);
Token lexer_next_token(Lexer *l);
Lexer lexer_init_from_src(const char *source);
Lexer lexer_init_from_file_path(Arena *a, const char *file_path);

#endif  // LEXER_H_
