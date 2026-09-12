#include "ast_print.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ast.h"
#include "common.h"

#define INDENT_WIDTH 2

static const char *unop_kind_to_str(UnopKind kind)
{
    static_assert(UNOP_COUNT == 10, "unop_kind_to_str: `UNOP_COUNT` value has changed");
    switch (kind) {
    case UNOP_POS:      return "+";
    case UNOP_NEG:      return "-";
    case UNOP_NOT:      return "!";
    case UNOP_BIT_NOT:  return "~";
    case UNOP_DEREF:    return "*";
    case UNOP_ADDR:     return "&";
    case UNOP_PRE_INC:  return "++ (pre)";
    case UNOP_PRE_DEC:  return "-- (pre)";
    case UNOP_POST_INC: return "++ (post)";
    case UNOP_POST_DEC: return "-- (post)";
    default: UNREACHABLE("unop_kind_to_str");
    }
}

static const char *binop_kind_to_str(BinopKind kind)
{
    static_assert(BINOP_COUNT == 19, "binop_kind_to_str: `BINOP_COUNT` value has changed");
    switch (kind) {
    case BINOP_COMMA:   return ",";
    case BINOP_OR:      return "||";
    case BINOP_AND:     return "&&";
    case BINOP_BIT_OR:  return "|";
    case BINOP_BIT_XOR: return "^";
    case BINOP_BIT_AND: return "&";
    case BINOP_EQ:      return "==";
    case BINOP_NOT_EQ:  return "!=";
    case BINOP_LT:      return "<";
    case BINOP_LT_EQ:   return "<=";
    case BINOP_GT:      return ">";
    case BINOP_GT_EQ:   return ">=";
    case BINOP_LSFT:    return "<<";
    case BINOP_RSFT:    return ">>";
    case BINOP_PLUS:    return "+";
    case BINOP_MINUS:   return "-";
    case BINOP_MULT:    return "*";
    case BINOP_DIV:     return "/";
    case BINOP_MOD:     return "%";
    default: UNREACHABLE("binop_kind_to_str");
    }
}

static const char *assign_kind_to_str(AssignKind kind)
{
    static_assert(ASSIGN_COUNT == 11, "assign_kind_to_str: `ASSIGN_COUNT` value has changed");
    switch (kind) {
    case ASSIGN_AND:   return "&=";
    case ASSIGN_XOR:   return "^=";
    case ASSIGN_OR:    return "|=";
    case ASSIGN_LSFT:  return "<<=";
    case ASSIGN_RSFT:  return ">>=";
    case ASSIGN_MULT:  return "*=";
    case ASSIGN_DIV:   return "/=";
    case ASSIGN_MOD:   return "%=";
    case ASSIGN_PLUS:  return "+=";
    case ASSIGN_MINUS: return "-=";
    case ASSIGN_EQ:    return "=";
    default: UNREACHABLE("assign_kind_to_str");
    }
}

const char *type_to_str(Type ty)
{
    switch (ty.kind) {
    case TYPE_VOID: return "void";
    case TYPE_BOOL: return "bool";
    case TYPE_CHAR:
        switch (ty.sign) {
        case SIGN_UNSPECIFIED: return "char";
        case SIGN_SIGNED:      return "signed_char";
        case SIGN_UNSIGNED:    return "unsigned_char";
        }
    case TYPE_SHORT:
        if (ty.sign == SIGN_UNSPECIFIED || ty.sign == SIGN_SIGNED)
            return "short";
        else
            return "unsigned_short";
    case TYPE_INT:
        if (ty.sign == SIGN_UNSPECIFIED || ty.sign == SIGN_SIGNED)
            return "int";
        else
            return "unsigned_int";
    case TYPE_LONG:
        if (ty.sign == SIGN_UNSPECIFIED || ty.sign == SIGN_SIGNED)
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

typedef struct {
    FILE *out;
    uint32_t depth;
    bool compact;
    bool print_locs;
} PrintCtx;

static inline void print_loc(PrintCtx *ctx, Loc loc)
{
    if (ctx->print_locs)
        fprintf(ctx->out, " <%zu:%zu>", loc.line, loc.col);
}

static void print_type_ctx(PrintCtx *ctx, const Type ty);
static void print_expr_ctx(PrintCtx *ctx, const Expr *e);
static void print_stmt_ctx(PrintCtx *ctx, const Stmt *s);

static void print_type_field(PrintCtx *ctx, const char *label, const Type ty)
{
    if (ctx->compact) {
        fprintf(ctx->out, " ");
    } else {
        fprintf(ctx->out, "\n%*s", (ctx->depth + 1) * INDENT_WIDTH, "");
        if (label != NULL)
            fprintf(ctx->out, "%s: ", label);
    }
    ctx->depth++;
    print_type_ctx(ctx, ty);
    ctx->depth--;
}

static void print_expr_field(PrintCtx *ctx, const char *label, const Expr *e)
{
    if (ctx->compact) {
        fprintf(ctx->out, " ");
    } else {
        fprintf(ctx->out, "\n%*s", (ctx->depth + 1) * INDENT_WIDTH, "");
        if (label != NULL)
            fprintf(ctx->out, "%s: ", label);
    }
    ctx->depth++;
    print_expr_ctx(ctx, e);
    ctx->depth--;
}

static void print_stmt_field(PrintCtx *ctx, const char *label, const Stmt *s)
{
    if (ctx->compact) {
        fprintf(ctx->out, " ");
    } else {
        fprintf(ctx->out, "\n%*s", (ctx->depth + 1) * INDENT_WIDTH, "");
        if (label != NULL)
            fprintf(ctx->out, "%s: ", label);
    }
    ctx->depth++;
    print_stmt_ctx(ctx, s);
    ctx->depth--;
}

// TODO: implement types TYPE_ENUM, TYPE_FUNC, TYPE_VLA, TYPE_STRUCT,
// TYPE_UNION, TYPE_NAMED
static void print_type_ctx(PrintCtx *ctx, const Type ty)
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
        fprintf(ctx->out, "(type");
        print_loc(ctx, ty.loc);
        fprintf(ctx->out, " %s)", type_to_str(ty));
        break;
    case TYPE_ENUM:
        TODO("print_type_ctx: implement `TYPE_ENUM`");
    case TYPE_PTR:
        fprintf(ctx->out, "(ptr_type");
        print_loc(ctx, ty.loc);
        print_type_field(ctx, "base", *ty.ptr.base);
        fprintf(ctx->out, ")");
        break;
    case TYPE_FUNC:
        TODO("print_type_ctx: implement `TYPE_FUNC`");
    case TYPE_ARRAY:
        fprintf(ctx->out, "(array_type[%zu]", ty.array.size);
        print_loc(ctx, ty.loc);
        print_type_field(ctx, "base", *ty.array.base);
        fprintf(ctx->out, ")");
        break;
    case TYPE_VLA:
        TODO("print_type_ctx: implement `TYPE_VLA`");
    case TYPE_STRUCT:
        TODO("print_type_ctx: implement `TYPE_STRUCT`");
    case TYPE_UNION:
        TODO("print_type_ctx: implement `TYPE_UNION`");
    case TYPE_NAMED:
        TODO("print_type_ctx: implement `TYPE_NAMED`");
    case TYPE_COUNT:
        UNREACHABLE("print_type_ctx");
    }
}

static void print_expr_ctx(PrintCtx *ctx, const Expr *e)
{
    if (e == NULL) {
        fprintf(ctx->out, "(null_error)");
        return;
    }

    switch (e->kind) {
    case EXPR_CHAR:
        if (ctx->compact) {
            fprintf(ctx->out, "'%c'", e->c);
        } else {
            fprintf(ctx->out, "(char");
            print_loc(ctx, e->loc);
            fprintf(ctx->out, " '%c')", e->c);
        }
        break;
    case EXPR_STR:
        if (ctx->compact) {
            // NOTE: `e->str` already contains the enclosing double quotes, we
            // don't need to add them here
            fprintf(ctx->out, "%s", e->str);
        } else {
            fprintf(ctx->out, "(str");
            print_loc(ctx, e->loc);
            fprintf(ctx->out, " %s)", e->str);
        }
        break;
    case EXPR_NUM:
        if (ctx->compact) {
            fprintf(ctx->out, "%d", e->val);
        } else {
            fprintf(ctx->out, "(num");
            print_loc(ctx, e->loc);
            fprintf(ctx->out, " %d)", e->val);
        }
        break;
    case EXPR_IDENT:
        if (ctx->compact) {
            fprintf(ctx->out, "%s", e->ident.name);
        } else {
            fprintf(ctx->out, "(ident");
            print_loc(ctx, e->loc);
            fprintf(ctx->out, " %s)", e->ident.name);
        }
        break;
    case EXPR_CLIT:
        TODO("print_expr_ctx: implement `EXPR_CLIT`");
    case EXPR_UNOP:
        fprintf(ctx->out, "(unop");
        print_loc(ctx, e->loc);
        fprintf(ctx->out, " %s", unop_kind_to_str(e->unop.kind));
        print_expr_field(ctx, "operand", e->unop.operand);
        fprintf(ctx->out, ")");
        break;
    case EXPR_BINOP:
        fprintf(ctx->out, "(binop");
        print_loc(ctx, e->loc);
        fprintf(ctx->out, " %s", binop_kind_to_str(e->binop.kind));
        print_expr_field(ctx, "lhs", e->binop.lhs);
        print_expr_field(ctx, "rhs", e->binop.rhs);
        fprintf(ctx->out, ")");
        break;
    case EXPR_TERNOP:
        fprintf(ctx->out, "(ternop");
        print_loc(ctx, e->loc);
        print_expr_field(ctx, "cond", e->ternop.cond);
        print_expr_field(ctx, "then", e->ternop.then);
        print_expr_field(ctx, "else", e->ternop._else);
        fprintf(ctx->out, ")");
        break;
    case EXPR_FN_CALL:
        fprintf(ctx->out, "(fn_call");
        print_loc(ctx, e->loc);
        print_expr_field(ctx, "callee", e->fn_call.callee);
        for (size_t i = 0; i < e->fn_call.argc; ++i) {
            char arg_label[50];
            sprintf(arg_label, "arg %zu", i);
            print_expr_field(ctx, arg_label, e->fn_call.args[i]);
        }
        fprintf(ctx->out, ")");
        break;
    case EXPR_ASSIGN:
        fprintf(ctx->out, "(assign");
        print_loc(ctx, e->loc);
        fprintf(ctx->out, " %s", assign_kind_to_str(e->assign.kind));
        print_expr_field(ctx, "var", e->assign.var);
        print_expr_field(ctx, "value", e->assign.value);
        fprintf(ctx->out, ")");
        break;
    case EXPR_INDEX:
        fprintf(ctx->out, "(subscript");
        print_loc(ctx, e->loc);
        print_expr_field(ctx, "array", e->index.array);
        print_expr_field(ctx, "index", e->index.index);
        fprintf(ctx->out, ")");
        break;
    case EXPR_FIELD:
        fprintf(ctx->out, "(field");
        print_loc(ctx, e->loc);
        fprintf(ctx->out, " %s", e->field.field);
        print_expr_field(ctx, "obj", e->field._struct);
        fprintf(ctx->out, ")");
        break;
    case EXPR_ARROW:
        fprintf(ctx->out, "(arrow");
        print_loc(ctx, e->loc);
        fprintf(ctx->out, " %s", e->field.field);
        print_expr_field(ctx, "obj", e->field._struct);
        fprintf(ctx->out, ")");
        break;
    case EXPR_CAST:
        fprintf(ctx->out, "(cast");
        print_loc(ctx, e->loc);
        print_type_field(ctx, "type", e->cast.type);
        print_expr_field(ctx, "expr", e->cast.expr);
        fprintf(ctx->out, ")");
        break;
    case EXPR_SIZEOF_TY:
        fprintf(ctx->out, "(sizeof_type");
        print_loc(ctx, e->loc);
        print_type_field(ctx, "type", e->sizeof_ty);
        fprintf(ctx->out, ")");
        break;
    case EXPR_SIZEOF_EX:
        fprintf(ctx->out, "(sizeof_value");
        print_loc(ctx, e->loc);
        print_expr_field(ctx, "expr", e->sizeof_expr);
        fprintf(ctx->out, ")");
        break;
    case EXPR_ALIGNOF:
        fprintf(ctx->out, "(alignof");
        print_loc(ctx, e->loc);
        print_type_field(ctx, "type", e->alignof_ty);
        fprintf(ctx->out, ")");
        break;
    case EXPR_COUNT:
        UNREACHABLE("print_expr_ctx");
    }
}

static void print_stmt_ctx(PrintCtx *ctx, const Stmt *s)
{
    if (s == NULL) {
        fprintf(ctx->out, "(null_error)");
        return;
    }

    switch (s->kind) {
    case STMT_NULL:
        fprintf(ctx->out, "(null_stmt");
        print_loc(ctx, s->loc);
        fprintf(ctx->out, ")");
        break;
    case STMT_EXPR:
        TODO("print_stmt_ctx: implement `STMT_EXPR`");
    case STMT_BLOCK:
        fprintf(ctx->out, "(block");
        print_loc(ctx, s->loc);
        for (size_t i = 0; i < s->block.count; ++i) {
            char stmt_label[50];
            sprintf(stmt_label, "statement %zu", i);
            print_stmt_field(ctx, stmt_label, s->block.stmts[i]);
        }
        fprintf(ctx->out, ")");
        break;
    case STMT_LABEL:
        fprintf(ctx->out, "(label");
        print_loc(ctx, s->loc);
        fprintf(ctx->out, " %s", s->label.name);
        print_stmt_field(ctx, "next_stmt", s->label.next);
        fprintf(ctx->out, ")");
        break;
    case STMT_DECL:
        TODO("print_stmt_ctx: implement `STMT_DECL`");
    case STMT_WHILE:
        fprintf(ctx->out, "(while");
        print_loc(ctx, s->loc);
        print_expr_field(ctx, "condition", s->_while.cond);
        print_stmt_field(ctx, "body", s->_while.body);
        fprintf(ctx->out, ")");
        break;
    case STMT_FOR:
        TODO("print_stmt_ctx: implement `STMT_FOR`");
    case STMT_DO:
        fprintf(ctx->out, "(do/while");
        print_loc(ctx, s->loc);
        print_stmt_field(ctx, "body", s->_while.body);
        print_expr_field(ctx, "condition", s->_while.cond);
        fprintf(ctx->out, ")");
        break;
    case STMT_IF:
        fprintf(ctx->out, "(if");
        print_loc(ctx, s->loc);
        print_expr_field(ctx, "condition", s->_if.cond);
        print_stmt_field(ctx, "then", s->_if.then);
        if (s->_if._else != NULL)
            print_stmt_field(ctx, "else", s->_if._else);
        fprintf(ctx->out, ")");
        break;
    case STMT_SWITCH:
        TODO("print_stmt_ctx: implement `STMT_SWITCH`");
    case STMT_CASE:
        TODO("print_stmt_ctx: implement `STMT_CASE`");
    case STMT_DEFAULT:
        TODO("print_stmt_ctx: implement `STMT_DEFAULT`");
    // Jump statements
    case STMT_BREAK:
        fprintf(ctx->out, "(break");
        print_loc(ctx, s->loc);
        fprintf(ctx->out, ")");
        break;
    case STMT_CONT:
        fprintf(ctx->out, "(continue");
        print_loc(ctx, s->loc);
        fprintf(ctx->out, ")");
        break;
    case STMT_GOTO:
        fprintf(ctx->out, "(goto");
        print_loc(ctx, s->loc);
        fprintf(ctx->out, " %s)", s->goto_label);
        break;
    case STMT_RET:
        fprintf(ctx->out, "(return");
        print_loc(ctx, s->loc);
        if (s->_return != NULL)
            print_expr_field(ctx, "expr", s->_return);
        fprintf(ctx->out, ")");
        break;
    case STMT_COUNT:
        UNREACHABLE("print_stmt_ctx");
    }
}

void print_type(FILE *out, const Type ty, uint32_t depth)
{
    PrintCtx ctx = {
        .out = out,
        .depth = depth,
        .compact = false,
        .print_locs = true,
    };
    print_type_ctx(&ctx, ty);
}

void print_type_compact(FILE *out, const Type ty)
{
    PrintCtx ctx = {
        .out = out,
        .depth = 0,
        .compact = true,
        .print_locs = false,
    };
    print_type_ctx(&ctx, ty);
}

void print_expr(FILE *out, const Expr *e, uint32_t depth)
{
    PrintCtx ctx = {
        .out = out,
        .depth = depth,
        .compact = false,
        .print_locs = true,
    };
    print_expr_ctx(&ctx, e);
}

void print_expr_compact(FILE *out, const Expr *e)
{
    PrintCtx ctx = {
        .out = out,
        .depth = 0,
        .compact = true,
        .print_locs = false,
    };
    print_expr_ctx(&ctx, e);
}

void print_stmt_compact(FILE *out, const Stmt *s)
{
    PrintCtx ctx = {
        .out = out,
        .depth = 0,
        .compact = true,
        .print_locs = false,
    };
    print_stmt_ctx(&ctx, s);
}

void print_stmt(FILE *out, const Stmt *s, uint32_t depth)
{
    PrintCtx ctx = {
        .out = out,
        .depth = depth,
        .compact = false,
        .print_locs = true,
    };
    print_stmt_ctx(&ctx, s);
}
