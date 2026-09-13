#ifndef PARSER_H_
#define PARSER_H_

#include <stdbool.h>
#include <stddef.h>

#include "arena.h"
#include "ast.h"
#include "lexer.h"
#include "scope.h"

typedef struct {
    Arena *a;
    Token *tokens;
    size_t token_count;
    size_t pos;
    Scope sc;
    bool panic_mode;
    size_t err_count;
} Parser;

typedef struct {
    uint8_t left;
    uint8_t right;
} BindPower;

Type *parse_type(Parser *p);
Expr *parse_expr(Parser *p);
Stmt *parse_stmt(Parser *p);
TranslUnit parse_transl_unit(Parser *p);
Parser parser_init_from_lexer(Arena *a, Lexer l);
Parser parser_init_from_src(Arena *a, const char *src);
Parser parser_init_from_file_path(Arena *a, const char *path);
void parser_free(Parser *p);

#endif  // PARSER_H_
