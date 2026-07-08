#ifndef AST_PRINT_H_
#define AST_PRINT_H_

#include "ast.h"
#include "common.h"

#include <stdio.h>

const char *type_to_str(Type ty);
void print_expr(FILE *out, const Expr *e, uint32_t depth);
void print_type(FILE *out, const Type ty, uint32_t depth);

#endif  // AST_PRINT_H_
