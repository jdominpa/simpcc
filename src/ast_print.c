#include "ast_print.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ast.h"
#include "common.h"

#define INDENT_WIDTH 2

typedef struct {
    FILE *out;
    uint32_t depth;
    bool compact;
    bool print_locs;
} PrintCtx;

static void print_type_ctx(PrintCtx *ctx, const Type *ty);
static void print_expr_ctx(PrintCtx *ctx, const Expr *e);
static void print_stmt_ctx(PrintCtx *ctx, const Stmt *s);

// A write cursor over a fixed-capacity buffer. `len` is kept strictly below
// `size`, so the buffer stays NUL-terminated and a build that runs out of room
// stops growing instead of running past the end.
typedef struct {
    char *buf;
    size_t size;
    size_t used;
} StrCursor;

static void cursor_printf(StrCursor *c, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void cursor_printf(StrCursor *c, const char *fmt, ...)
{
    if (c->used >= c->size) return;  // also covers size == 0
    size_t avail = c->size - c->used;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(c->buf + c->used, avail, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    c->used += (size_t) n < avail ? (size_t) n : avail - 1;
}

static const char *unop_kind_to_str(UnopKind kind)
{
    static_assert(UNOP_COUNT == 10,
                  "unop_kind_to_str: `UNOP_COUNT` value has changed");
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
    case UNOP_COUNT:    break;
    }
    UNREACHABLE("unop_kind_to_str");
}

static const char *binop_kind_to_str(BinopKind kind)
{
    static_assert(BINOP_COUNT == 19,
                  "binop_kind_to_str: `BINOP_COUNT` value has changed");
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
    case BINOP_COUNT:   break;
    }
    UNREACHABLE("binop_kind_to_str");
}

static const char *assign_kind_to_str(AssignKind kind)
{
    static_assert(ASSIGN_COUNT == 11,
                  "assign_kind_to_str: `ASSIGN_COUNT` value has changed");
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
    case ASSIGN_COUNT: break;
    }
    UNREACHABLE("assign_kind_to_str");
}

// Writes the qualifier keywords held in `quals` to `buf`, separated by single
// spaces, or the empty string when there are none.
static const char *type_quals_to_str(char *buf, size_t size, uint8_t quals)
{
    const struct {
        TypeQual qual;
        const char *name;
    } qual_names[] = {
        { QUAL_CONST, "const" },
        { QUAL_VOLATILE, "volatile" },
        { QUAL_RESTRICT, "restrict" },
    };
    const char *sep = "";
    size_t used = 0;
    buf[0] = '\0';
    for (size_t i = 0; i < sizeof qual_names / sizeof *qual_names; ++i) {
        if (!(quals & qual_names[i].qual)) continue;
        int n = snprintf(buf + used, size - used, "%s%s", sep,
                         qual_names[i].name);
        if (n < 0 || (size_t) n >= size - used) break;
        used += (size_t) n;
        sep = " ";
    }
    return buf;
}

static const char *type_sign_and_kind_to_str(const Type *ty)
{
    static_assert(TYPE_COUNT == 17,
                  "type_sign_and_kind_to_str: `TYPE_COUNT` value has changed");
    switch (ty->kind) {
    case TYPE_VOID:
        return "void";
    case TYPE_BOOL:
        return "bool";
    case TYPE_CHAR:
        switch (ty->sign) {
        case SIGN_UNSPECIFIED: return "char";
        case SIGN_SIGNED:      return "signed char";
        case SIGN_UNSIGNED:    return "unsigned char";
        }
        break;
    case TYPE_SHORT:
        return ty->sign == SIGN_UNSIGNED ? "unsigned short" : "short";
    case TYPE_INT:
        return ty->sign == SIGN_UNSIGNED ? "unsigned int" : "int";
    case TYPE_LONG:
        return ty->sign == SIGN_UNSIGNED ? "unsigned long" : "long";
    case TYPE_FLOAT:
        return "float";
    case TYPE_DOUBLE:
        return "double";
    case TYPE_LDOUBLE:
        return "long double";
    case TYPE_NAMED:
        return ty->named.name;
    case TYPE_ENUM:
        TODO("type_sign_and_kind_to_str: implement `TYPE_ENUM`");
    case TYPE_STRUCT:
        TODO("type_sign_and_kind_to_str: implement `TYPE_STRUCT`");
    case TYPE_UNION:
        TODO("type_sign_and_kind_to_str: implement `TYPE_UNION`");
    // Derived types are spelled by `type_to_str_rec`, never here.
    case TYPE_PTR:
    case TYPE_FUNC:
    case TYPE_ARRAY:
    case TYPE_VLA:
    case TYPE_COUNT:
        break;
    }
    UNREACHABLE("type_sign_and_kind_to_str");
}

// Base type a declarator is applied to, qualifiers included: the `const int` of
// `const int *`.
static void base_type_to_str(char *buf, size_t size, const Type *ty)
{
    char quals[QUALS_STR_CAP];
    type_quals_to_str(quals, sizeof quals, ty->quals);
    snprintf(buf, size, "%s%s%s", quals, quals[0] != '\0' ? " " : "",
             type_sign_and_kind_to_str(ty));
}

// A qualifier keyword needs a space after it only when the next character would
// otherwise run into the keyword: `int *const *p` needs one, `int *const[5]`
// and `int (*const)[5]` do not.
static inline bool quals_need_space(const char *decl)
{
    return decl[0] != '\0' && decl[0] != '(' && decl[0] != '[';
}

// Builds the C spelling of `ty` by wrapping `decl`, the declarator text
// accumulated by the levels already visited. C declarators are read from the
// name outwards, so this walks from the outermost type inwards and prefixes or
// suffixes `decl` at each step, parenthesising when a prefix `*` would
// otherwise bind looser than a following `[]` or `()`.
static void type_to_str_rec(char *buf, size_t size, const Type *ty,
                            const char *decl)
{
    switch (ty->kind) {
    case TYPE_PTR: {
        char quals[QUALS_STR_CAP];
        char inner[TYPE_STR_CAP];
        type_quals_to_str(quals, sizeof quals, ty->quals);

        // `*` binds looser than the postfix `[]` and `()`, so a pointer to an
        // array or a function needs parentheses: `int (*)[5]`, not `int *[5]`.
        const TypeKind base_kind = ty->ptr.base->kind;
        bool parens = base_kind == TYPE_ARRAY || base_kind == TYPE_VLA ||
                      base_kind == TYPE_FUNC;
        snprintf(inner, sizeof inner, "%s*%s%s%s%s", parens ? "(" : "", quals,
                 quals[0] != '\0' && quals_need_space(decl) ? " " : "", decl,
                 parens ? ")" : "");
        type_to_str_rec(buf, size, ty->ptr.base, inner);
        return;
    }
    case TYPE_ARRAY: {
        char inner[TYPE_STR_CAP];
        if (ty->array.has_size)
            snprintf(inner, sizeof inner, "%s[%zu]", decl, ty->array.size);
        else
            snprintf(inner, sizeof inner, "%s[]", decl);
        type_to_str_rec(buf, size, ty->array.base, inner);
        return;
    }
    case TYPE_VLA:
        TODO("type_to_str_rec: implement `TYPE_VLA`");
    case TYPE_FUNC: {
        char args[TYPE_STR_CAP];
        StrCursor c = { args, sizeof args, 0 };
        cursor_printf(&c, "%s(", decl);
        for (size_t i = 0; i < ty->func.arg_count; ++i)
            cursor_printf(&c, "%s%s",
                          i > 0 ? ", " : "", TYPE_TO_STR(ty->func.args[i]));
        if (ty->func.is_variadic)
            cursor_printf(&c, "%s...", ty->func.arg_count > 0 ? ", " : "");
        else if (ty->func.arg_count == 0)
            cursor_printf(&c, "void");
        cursor_printf(&c, ")");
        type_to_str_rec(buf, size, ty->func.ret, args);
        return;
    }
    default: {
        char base[TYPE_STR_CAP];
        base_type_to_str(base, sizeof base, ty);
        // A `(` opening a parameter list binds tight like `[`, whereas a `(`
        // opening a parenthesised declarator binds like the `*` inside it and
        // takes a space. `decl[1]` is always safe since `decl[0] == '('` means
        // that, at least, `decl[1]` must be a closing `)`.
        bool tight = decl[0] == '[' || (decl[0] == '(' && decl[1] != '*');
        bool space = decl[0] != '\0' && !tight;
        snprintf(buf, size, "%s%s%s", base, space ? " " : "", decl);
        return;
    }
    }
}

// Writes the C spelling of `ty` into `buf` and returns it. `buf` should be at
// least `TYPE_STR_CAP` bytes; longer spellings are truncated rather than
// overflowing. Takes a caller-supplied buffer so that two types can be
// formatted in a single call (`"cannot assign %s to %s"`).
const char *type_to_str(char *buf, size_t size, const Type *ty)
{
    type_to_str_rec(buf, size, ty, "");
    return buf;
}

static inline void print_loc(const PrintCtx *ctx, Loc loc)
{
    if (ctx->print_locs)
        fprintf(ctx->out, " <%zu:%zu>", loc.line, loc.col);
}

static void print_type_quals_field(const PrintCtx *ctx, const uint8_t quals)
{
    if (quals == 0) return;
    char buf[QUALS_STR_CAP];
    fprintf(ctx->out, "\n%*s", (ctx->depth + 1) * INDENT_WIDTH, "");
    fprintf(ctx->out, "quals: %s", type_quals_to_str(buf, sizeof buf, quals));
}

static void print_type_field(PrintCtx *ctx, const char *label, const Type *ty)
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

static void print_type_ctx(PrintCtx *ctx, const Type *ty)
{
    static_assert(TYPE_COUNT == 17,
                  "print_type_ctx: `TYPE_COUNT` value has changed");

    if (ctx->compact) {
        fprintf(ctx->out, "(type %s)", TYPE_TO_STR(ty));
        return;
    }

    switch (ty->kind) {
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
        print_loc(ctx, ty->loc);
        fprintf(ctx->out, " %s", type_sign_and_kind_to_str(ty));
        print_type_quals_field(ctx, ty->quals);
        fprintf(ctx->out, ")");
        break;
    case TYPE_ENUM:
        TODO("print_type_ctx: implement `TYPE_ENUM`");
    case TYPE_PTR:
        fprintf(ctx->out, "(ptr_type");
        print_loc(ctx, ty->loc);
        print_type_quals_field(ctx, ty->quals);
        print_type_field(ctx, "base", ty->ptr.base);
        fprintf(ctx->out, ")");
        break;
    case TYPE_FUNC:
        fprintf(ctx->out, "(func_type");
        print_loc(ctx, ty->loc);
        print_type_field(ctx, "ret_type", ty->func.ret);
        if (ty->func.arg_count == 0 && !ty->func.is_variadic) {
            fprintf(ctx->out, "\n%*s", (ctx->depth + 1) * INDENT_WIDTH, "");
            fprintf(ctx->out, "args: void");
        } else {
            for (size_t i = 0; i < ty->func.arg_count; ++i) {
                char arg_label[50];
                sprintf(arg_label, "arg %zu", i);
                print_type_field(ctx, arg_label, ty->func.args[i]);
            }
            if (ty->func.is_variadic) {
                fprintf(ctx->out, "\n%*s", (ctx->depth + 1) * INDENT_WIDTH, "");
                fprintf(ctx->out, "...");
            }
        }
        fprintf(ctx->out, ")");
        break;
    case TYPE_ARRAY:
        if (ty->array.has_size)
            fprintf(ctx->out, "(array_type[%zu]", ty->array.size);
        else
            fprintf(ctx->out, "(array_type[]");
        print_loc(ctx, ty->loc);
        print_type_quals_field(ctx, ty->quals);
        print_type_field(ctx, "base", ty->array.base);
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

    static_assert(EXPR_COUNT == 17,
                  "print_expr_ctx: `EXPR_COUNT` value has changed");
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
    case EXPR_FUNC_CALL:
        fprintf(ctx->out, "(fn_call");
        print_loc(ctx, e->loc);
        print_expr_field(ctx, "callee", e->func_call.callee);
        for (size_t i = 0; i < e->func_call.arg_count; ++i) {
            char arg_label[50];
            sprintf(arg_label, "arg %zu", i);
            print_expr_field(ctx, arg_label, e->func_call.args[i]);
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

    static_assert(STMT_COUNT == 16,
                  "print_stmt_ctx: `STMT_COUNT` value has changed");
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
        for (size_t i = 0; i < s->block.stmt_count; ++i) {
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

void print_type(FILE *out, const Type *ty, uint32_t depth)
{
    PrintCtx ctx = {
        .out = out,
        .depth = depth,
        .compact = false,
        .print_locs = true,
    };
    print_type_ctx(&ctx, ty);
}

void print_type_compact(FILE *out, const Type *ty)
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
