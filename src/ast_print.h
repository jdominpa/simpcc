#ifndef AST_PRINT_H_
#define AST_PRINT_H_

#include <stdio.h>

#include "ast.h"
#include "common.h"

#define TYPE_STR_CAP 256

// Enough for `const volatile restrict` (longest qualifier)
#define QUALS_STR_CAP 32

#define TYPE_TO_STR(ty) type_to_str((char[TYPE_STR_CAP]) { 0 }, TYPE_STR_CAP, (ty))

const char *type_to_str(char *buf, size_t size, const Type *ty);
void print_type(FILE *out, const Type *ty, uint32_t depth);
void print_type_compact(FILE *out, const Type *ty);
void print_expr(FILE *out, const Expr *e, uint32_t depth);
void print_expr_compact(FILE *out, const Expr *e);
void print_stmt(FILE *out, const Stmt *s, uint32_t depth);
void print_stmt_compact(FILE *out, const Stmt *s);

#endif  // AST_PRINT_H_
