#include "ast_print.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ast.h"
#include "common.h"

#define INDENT_WIDTH 2

static inline void print_loc(FILE *out, Loc loc)
{
    fprintf(out, "<%zu:%zu>", loc.line, loc.col);
}

static const char *unop_kind_to_str(UnopKind kind)
{
    switch (kind) {
    case UNOP_POS: return "+";
    case UNOP_NEG: return "-";
    case UNOP_NOT: return "!";
    case UNOP_BIT_NOT: return "~";
    case UNOP_DEREF: return "*";
    case UNOP_ADDR: return "&";
    case UNOP_PRE_INC: return "++ (pre)";
    case UNOP_PRE_DEC: return "-- (pre)";
    case UNOP_POST_INC: return "++ (post)";
    case UNOP_POST_DEC: return "-- (post)";
    default: UNREACHABLE("unop_to_str");
    }
}

static const char *binop_kind_to_str(BinopKind kind)
{
    switch (kind) {
    case BINOP_OR: return "||";
    case BINOP_AND: return "&&";
    case BINOP_BIT_OR: return "|";
    case BINOP_BIT_XOR: return "^";
    case BINOP_BIT_AND: return "&";
    case BINOP_EQ: return "==";
    case BINOP_NOT_EQ: return "!=";
    case BINOP_LT: return "<";
    case BINOP_LT_EQ: return "<=";
    case BINOP_GT: return ">";
    case BINOP_GT_EQ: return ">=";
    case BINOP_LSFT: return "<<";
    case BINOP_RSFT: return ">>";
    case BINOP_PLUS: return "+";
    case BINOP_MINUS: return "-";
    case BINOP_MULT: return "*";
    case BINOP_DIV: return "/";
    case BINOP_MOD: return "%";
    default: UNREACHABLE("binop_kind_to_str");
    }
}

static const char *assign_kind_to_str(AssignKind kind)
{
    switch (kind) {
    case ASSIGN_AND: return "&=";
    case ASSIGN_XOR: return "^=";
    case ASSIGN_OR: return "|=";
    case ASSIGN_LSFT: return "<<=";
    case ASSIGN_RSFT: return ">>=";
    case ASSIGN_MULT: return "*=";
    case ASSIGN_DIV: return "/=";
    case ASSIGN_MOD: return "%=";
    case ASSIGN_PLUS: return "+=";
    case ASSIGN_MINUS: return "-=";
    case ASSIGN_EQ: return "=";
    default: UNREACHABLE("assign_kind_to_str");
    }
}

const char *type_to_str(Type ty)
{
    switch (ty.kind) {
    case TYPE_VOID: return "void";
    case TYPE_BOOL: return "bool";
    case TYPE_CHAR:
        if (ty.is_signed)
            return "char";
        else
            return "unsigned_char";
    case TYPE_SHORT:
        if (ty.is_signed)
            return "short";
        else
            return "unsigned_short";
    case TYPE_INT:
        if (ty.is_signed)
            return "int";
        else
            return "unsigned_int";
    case TYPE_LONG:
        if (ty.is_signed)
            return "long";
        else
            return "unsigned_long";
    case TYPE_FLOAT: return "float";
    case TYPE_DOUBLE: return "double";
    case TYPE_LDOUBLE: return "long_double";
    case TYPE_PTR: {
        // Count number of stars
        int ptr_count = 0;
        Type *base = &ty;
        while (base->kind == TYPE_PTR) {
            ptr_count++;
            base = base->ptr.base;
        }

        // Base type string
        const char *base_str = type_to_str(*base);

        // Stars string
        char stars[ptr_count + 1];
        memset(stars, '*', ptr_count);
        stars[ptr_count] = '\0';

        // Full type string
        static char buf[64];
        sprintf(buf, "%s%s", base_str, stars);
        return buf;
    }
    default:
        UNREACHABLE("type_kind_to_str");
    }
}

static void print_type_field(FILE *out, const char *label, const Type ty, uint32_t depth)
{
    fprintf(out, "\n");
    fprintf(out, "%*s", depth * INDENT_WIDTH, "");
    if (label != NULL)
        fprintf(out, "%s: ", label);
    print_type(out, ty, depth);
}

static void print_expr_field(FILE *out, const char *label, const Expr *e, uint32_t depth)
{
    fprintf(out, "\n");
    fprintf(out, "%*s", depth * INDENT_WIDTH, "");
    if (label != NULL)
        fprintf(out, "%s: ", label);
    print_expr(out, e, depth);
}

// TODO: implement types TYPE_ENUM, TYPE_FUNC, TYPE_VLA, TYPE_STRUCT,
// TYPE_UNION, TYPE_NAMED
void print_type(FILE *out, const Type ty, uint32_t depth)
{
    switch (ty.kind) {
    case TYPE_VOID:
    case TYPE_BOOL:
    case TYPE_CHAR:
    case TYPE_SHORT:
    case TYPE_INT:
    case TYPE_LONG:
    case TYPE_FLOAT:
    case TYPE_DOUBLE:
    case TYPE_LDOUBLE:
        fprintf(out, "(type ");
        print_loc(out, ty.loc);
        fprintf(out, " %s)", type_to_str(ty));
        break;
    case TYPE_PTR:
        fprintf(out, "(ptr_type ");
        print_loc(out, ty.loc);
        print_type_field(out, "base", *ty.ptr.base, depth + 1);
        fprintf(out, ")");
        break;
    case TYPE_ARRAY:
        fprintf(out, "(array_type[%zu] ", ty.array.size);
        print_loc(out, ty.loc);
        print_type_field(out, "base", *ty.array.base, depth + 1);
        fprintf(out, ")");
        break;
    default:
        UNREACHABLE("print_type");
    }
}

void print_expr(FILE *out, const Expr *e, uint32_t depth)
{
    if (e == NULL) {
        fprintf(out, "(expr null)");
        return;
    }

    switch (e->kind) {
    case EXPR_CHAR:
        fprintf(out, "(char_literal ");
        print_loc(out, e->loc);
        fprintf(out, " '%c')", e->c);
        break;
    case EXPR_STR:
        fprintf(out, "(string_literal ");
        print_loc(out, e->loc);
        // NOTE: `e->str` already contains the enclosing double quotes, we don't
        // need to add them here
        fprintf(out, " %s)", e->str);
        break;
    case EXPR_NUM:
        fprintf(out, "(number_literal ");
        print_loc(out, e->loc);
        fprintf(out, " %d)", e->val);
        break;
    case EXPR_IDENT:
        fprintf(out, "(identifier ");
        print_loc(out, e->loc);
        fprintf(out, " %s)", e->ident);
        break;
    case EXPR_UNOP:
        fprintf(out, "(unary_expr ");
        print_loc(out, e->loc);
        fprintf(out, " %s", unop_kind_to_str(e->unop.kind));
        print_expr_field(out, "operand", e->unop.operand, depth + 1);
        fprintf(out, ")");
        break;
    case EXPR_BINOP:
        fprintf(out, "(binary_expr ");
        print_loc(out, e->loc);
        fprintf(out, " %s", binop_kind_to_str(e->binop.kind));
        print_expr_field(out, "lhs", e->binop.lhs, depth + 1);
        print_expr_field(out, "rhs", e->binop.rhs, depth + 1);
        fprintf(out, ")");
        break;
    case EXPR_TERNOP:
        fprintf(out, "(ternary_expr ");
        print_loc(out, e->loc);
        print_expr_field(out, "cond", e->ternop.cond, depth + 1);
        print_expr_field(out, "then", e->ternop.then, depth + 1);
        print_expr_field(out, "else", e->ternop._else, depth + 1);
        fprintf(out, ")");
        break;
    case EXPR_FN_CALL:
        fprintf(out, "(fn_call_expr ");
        print_loc(out, e->loc);
        fprintf(out, " %s", e->fn_call.fn_name);
        for (size_t i = 0; i < e->fn_call.argc; ++i) {
            char arg_label[50];
            sprintf(arg_label, "arg %zu", i);
            print_expr_field(out, arg_label, e->fn_call.args[i], depth + 1);
        }
        fprintf(out, ")");
        break;
    case EXPR_ASSIGN:
        fprintf(out, "(assign_expr ");
        print_loc(out, e->loc);
        fprintf(out, " %s", assign_kind_to_str(e->assign.kind));
        print_expr_field(out, "var", e->assign.var, depth + 1);
        print_expr_field(out, "value", e->assign.value, depth + 1);
        fprintf(out, ")");
        break;
    case EXPR_INDEX:
        fprintf(out, "(subscript_expr ");
        print_loc(out, e->loc);
        print_expr_field(out, "array", e->index.array, depth + 1);
        print_expr_field(out, "index", e->index.index, depth + 1);
        fprintf(out, ")");
        break;
    case EXPR_FIELD:
        fprintf(out, "(field_expr ");
        print_loc(out, e->loc);
        fprintf(out, " %s", e->field.field);
        print_expr_field(out, "obj", e->field._struct, depth + 1);
        fprintf(out, ")");
        break;
    case EXPR_ARROW:
        fprintf(out, "(arrow_expr ");
        print_loc(out, e->loc);
        fprintf(out, " %s", e->field.field);
        print_expr_field(out, "obj", e->field._struct, depth + 1);
        fprintf(out, ")");
        break;
    case EXPR_CAST:
        fprintf(out, "(cast_expr ");
        print_loc(out, e->loc);
        print_type_field(out, "type", e->cast.type, depth + 1);
        print_expr_field(out, "expr", e->cast.expr, depth + 1);
        fprintf(out, ")");
        break;
    case EXPR_SIZEOF_TY:
        fprintf(out, "(sizeof_type_expr ");
        print_loc(out, e->loc);
        print_type_field(out, "type", e->sizeof_ty, depth + 1);
        fprintf(out, ")");
        break;
    case EXPR_SIZEOF_EX:
        fprintf(out, "(sizeof_value_expr ");
        print_loc(out, e->loc);
        print_expr_field(out, "expr", e->sizeof_expr, depth + 1);
        fprintf(out, ")");
        break;
    case EXPR_ALIGNOF:
        fprintf(out, "(alignof_expr ");
        print_loc(out, e->loc);
        print_type_field(out, "type", e->alignof_ty, depth + 1);
        fprintf(out, ")");
        break;
    default:
        UNREACHABLE("print_expr_as_sexp");
    }
}
