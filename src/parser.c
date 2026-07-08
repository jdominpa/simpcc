#include "parser.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arena.h"
#include "ast.h"
#include "ast_print.h"
#include "common.h"
#include "diag.h"
#include "lexer.h"

// TODO: maybe this should be moved somewhere else
static const char *token_kind_to_str[] = {
    [TK_INVALID] = "invalid",
    [TK_EOF] = "EOF",
    [TK_IDENT] = "ident",
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

// Checks if the current token is of TokenKind `kind`, and returns `true` if so.
static inline bool parser_check(Parser *p, TokenKind kind)
{
    assert(p->pos < p->token_count);
    return p->tokens[p->pos].kind == kind;
}

// Returns whether the parser is at EOF or not.
static inline bool parser_at_eof(Parser *p)
{
    return parser_check(p, TK_EOF);
}

// Advance the parser by one token.
static inline void parser_bump(Parser *p)
{
    if (!parser_at_eof(p))
        p->pos++;
}

// Consume token of TokenKind `kind` if present.
// Return whether the given token was present.
static bool parser_eat(Parser *p, TokenKind kind)
{
    if (parser_check(p, kind)) {
        parser_bump(p);
        return true;
    }
    return false;
}

// Expects and consumes a token of TokenKind `kind`.
// Raises an error if the current token is not of type `kind`.
static bool parser_expect(Parser *p, TokenKind kind)
{
    if (parser_check(p, kind)) {
        parser_bump(p);
        return true;
    } else {
        Token t = p->tokens[p->pos];
        // TODO: try to recover from unexpected tokens instead of crashing
        diag_fatal_at(t.loc, "unexpected token, expected `%s` but found `%s`",
                      token_kind_to_str[kind], token_kind_to_str[t.kind]);
    }
}

// Returns whether the content of a given token `t` matches a string `str`.
static inline bool token_equal(Token t, const char *str)
{
    return t.len == strlen(str) && strncmp(t.start, str, t.len) == 0;
}

//
// Type parser
//

static bool is_type(Token t)
{
    if (t.kind == TK_IDENT)
        TODO("named types not implemented yet");
    else if (t.kind != TK_KW)
        return false;

    const char *types[] = {
        "void", "bool", "_Bool", "char", "short", "int", "long",
        "float", "double", "signed", "unsigned", "struct", "union", "enum",
        "const", "volatile", "restrict"
    };
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); ++i)
        if (token_equal(t, types[i]))
            return true;
    return false;
}

static bool is_castable_type(Type ty)
{
    switch (ty.kind) {
    case TYPE_VOID:
    case TYPE_CHAR:
    case TYPE_SHORT:
    case TYPE_INT:
    case TYPE_LONG:
    case TYPE_FLOAT:
    case TYPE_DOUBLE:
    case TYPE_LDOUBLE:
    case TYPE_PTR:
        return true;
    default:
        return false;
    }
}

// TODO: implement types TYPE_ENUM, TYPE_FUNC, TYPE_ARRAY, TYPE_VLA,
// TYPE_STRUCT, TYPE_UNION, TYPE_NAMED
static Type parse_type(Parser *p)
{
    if (parser_at_eof(p))
        diag_fatal_at(p->tokens[p->pos].loc, "unexpected end of file encountered while parsing type");

    Type ty = { 0 };
    enum {
        VOID     = 1 << 0,
        BOOL     = 1 << 2,
        CHAR     = 1 << 4,
        SHORT    = 1 << 6,
        INT      = 1 << 8,
        LONG     = 1 << 10,
        FLOAT    = 1 << 12,
        DOUBLE   = 1 << 14,
        OTHER    = 1 << 16,
        SIGNED   = 1 << 17,
        UNSIGNED = 1 << 18,
    };

    // Built-in types
    uint32_t counter = 0;
    Token t = p->tokens[p->pos];
    ty.loc = t.loc;
    while (is_type(t)) {
        if (token_equal(t, "void"))
            counter += VOID;
        else if (token_equal(t, "bool"))
            counter += BOOL;
        else if (token_equal(t, "char"))
            counter += CHAR;
        else if (token_equal(t, "short"))
            counter += SHORT;
        else if (token_equal(t, "int"))
            counter += INT;
        else if (token_equal(t, "long"))
            counter += LONG;
        else if (token_equal(t, "float"))
            counter += FLOAT;
        else if (token_equal(t, "double"))
            counter += DOUBLE;
        else if (token_equal(t, "signed"))
            counter |= SIGNED;
        else if (token_equal(t, "unsigned"))
            counter |= UNSIGNED;
        else
            UNREACHABLE("parse_type");

        parser_bump(p);
        t = p->tokens[p->pos];
    }

    switch (counter) {
    case VOID:
        ty.kind = TYPE_VOID;
        break;
    case VOID + SIGNED:
        diag_fatal_at(ty.loc, "type `void` is incompatible with type modifier `signed`");
    case VOID + UNSIGNED:
        diag_fatal_at(ty.loc, "type `void` is incompatible with type modifier `unsigned`");
    case BOOL:
        ty.kind = TYPE_BOOL;
        break;
    case BOOL + SIGNED:
        diag_fatal_at(ty.loc, "type `bool` is incompatible with type modifier `unsigned`");
    case BOOL + UNSIGNED:
        diag_fatal_at(ty.loc, "type `bool` is incompatible with type modifier `unsigned`");
    case CHAR:
    case CHAR + SIGNED:
        ty.kind = TYPE_CHAR;
        ty.is_signed = true;
        break;
    case CHAR + UNSIGNED:
        ty.kind = TYPE_CHAR;
        ty.is_signed = false;
        break;
    case SHORT:
    case SHORT + INT:
    case SHORT + SIGNED:
    case SHORT + INT + SIGNED:
        ty.kind = TYPE_SHORT;
        ty.is_signed = true;
        break;
    case SHORT + UNSIGNED:
    case SHORT + INT + UNSIGNED:
        ty.kind = TYPE_SHORT;
        ty.is_signed = false;
        break;
    case INT:
    case INT + SIGNED:
    case SIGNED:
        ty.kind = TYPE_INT;
        ty.is_signed = true;
        break;
    case UNSIGNED:
    case UNSIGNED + INT:
        ty.kind = TYPE_INT;
        ty.is_signed = false;
        break;
    case LONG:
    case LONG + INT:
    case LONG + LONG:
    case LONG + LONG + INT:
    case LONG + SIGNED:
    case LONG + INT + SIGNED:
    case LONG + LONG + SIGNED:
    case LONG + LONG + INT + SIGNED:
        ty.kind = TYPE_LONG;
        ty.is_signed = true;
        break;
    case LONG + UNSIGNED:
    case LONG + INT + UNSIGNED:
    case LONG + LONG + UNSIGNED:
    case LONG + LONG + INT + UNSIGNED:
        ty.kind = TYPE_LONG;
        ty.is_signed = false;
        break;
    case FLOAT:
        ty.kind = TYPE_FLOAT;
        break;
    case DOUBLE:
        ty.kind = TYPE_DOUBLE;
        break;
    case LONG + DOUBLE:
        ty.kind = TYPE_LDOUBLE;
        break;
    default:
        diag_fatal_at(ty.loc, "invalid type");
    }

    // Check for pointer suffixes
    while (parser_eat(p, TK_STAR)) {
        Type *base = arena_alloc(p->a, Type);
        *base = ty;
        ty.kind = TYPE_PTR;
        ty.ptr.base = base;
    }

    return ty;
}

//
// Expression parser
//

static Expr *new_unop_expr(Arena *a, Loc loc, UnopKind kind, Expr *operand)
{
    Expr *e = arena_alloc(a, Expr);
    e->kind = EXPR_UNOP;
    e->loc = loc;
    e->unop.kind = kind;
    e->unop.operand = operand;
    return e;
}

static Expr *new_binop_expr(Arena *a, Loc loc, BinopKind kind, Expr *lhs, Expr *rhs)
{
    Expr *e = arena_alloc(a, Expr);
    e->kind = EXPR_BINOP;
    e->loc = loc;
    e->binop.kind = kind;
    e->binop.lhs = lhs;
    e->binop.rhs = rhs;
    return e;
}

static Expr *new_assign_expr(Arena *a, Loc loc, AssignKind kind, Expr *var, Expr *value)
{
    Expr *e = arena_alloc(a, Expr);
    e->kind = EXPR_ASSIGN;
    e->loc = loc;
    e->assign.kind = kind;
    e->assign.var = var;
    e->assign.value = value;
    return e;
}

static BinopKind get_binop_kind(TokenKind kind)
{
    switch (kind) {
    case TK_PIPE_PIPE: return BINOP_OR;
    case TK_AMP_AMP:   return BINOP_AND;
    case TK_PIPE:      return BINOP_BIT_OR;
    case TK_CARET:     return BINOP_BIT_XOR;
    case TK_AMP:       return BINOP_BIT_AND;
    case TK_EQ_EQ:     return BINOP_EQ;
    case TK_BANG_EQ:   return BINOP_NOT_EQ;
    case TK_LT:        return BINOP_LT;
    case TK_LT_EQ:     return BINOP_LT_EQ;
    case TK_GT:        return BINOP_GT;
    case TK_GT_EQ:     return BINOP_GT_EQ;
    case TK_LT_LT:     return BINOP_LSFT;
    case TK_GT_GT:     return BINOP_RSFT;
    case TK_PLUS:      return BINOP_PLUS;
    case TK_MINUS:     return BINOP_MINUS;
    case TK_STAR:      return BINOP_MULT;
    case TK_SLASH:     return BINOP_DIV;
    case TK_PERCENT:   return BINOP_MOD;
    default:
        UNREACHABLE("get_binop_kind called with non-binop token");
    }
}

static AssignKind get_assign_kind(TokenKind kind)
{
    switch (kind) {
    case TK_AMP_EQ:     return ASSIGN_AND;
    case TK_CARET_EQ:   return ASSIGN_XOR;
    case TK_PIPE_EQ:    return ASSIGN_OR;
    case TK_LT_LT_EQ:   return ASSIGN_LSFT;
    case TK_GT_GT_EQ:   return ASSIGN_RSFT;
    case TK_STAR_EQ:    return ASSIGN_MULT;
    case TK_SLASH_EQ:   return ASSIGN_DIV;
    case TK_PERCENT_EQ: return ASSIGN_MOD;
    case TK_PLUS_EQ:    return ASSIGN_PLUS;
    case TK_MINUS_EQ:   return ASSIGN_MINUS;
    case TK_EQ:         return ASSIGN_EQ;
    default:
        UNREACHABLE("get_assign_kind called with non-assingment token");
    }
}

static bool is_assign_op(TokenKind kind)
{
    switch (kind) {
    case TK_AMP_EQ:   case TK_CARET_EQ: case TK_PIPE_EQ:
    case TK_LT_LT_EQ: case TK_GT_GT_EQ:
    case TK_STAR_EQ:  case TK_SLASH_EQ: case TK_PERCENT_EQ:
    case TK_PLUS_EQ:  case TK_MINUS_EQ:
    case TK_EQ:
        return true;
    default:
        return false;
    }
}

// Returns the binding power (left,right) of an operation with TokenKind `kind`.
// If `kind` doesn't correspond to any operation the binding power is (0,0).
static BindPower get_op_bp(TokenKind kind)
{
    switch (kind) {
    case TK_COMMA:                                          // ","
        return (BindPower) { .left = 1, .right = 2 };
    case TK_AMP_EQ:   case TK_CARET_EQ: case TK_PIPE_EQ:    // "&=", "^=", "|="
    case TK_LT_LT_EQ: case TK_GT_GT_EQ:                     // "<<=", ">>="
    case TK_STAR_EQ:  case TK_SLASH_EQ: case TK_PERCENT_EQ: // "*=", "/=", "%="
    case TK_PLUS_EQ:  case TK_MINUS_EQ:                     // "+=", "-="
    case TK_EQ:                                             // "="
        return (BindPower) { .left = 3, .right = 2 };
    case TK_QUESTION: case TK_COLON:                        // "?" ":"
        return (BindPower) { .left = 4, .right = 3 };
    case TK_PIPE_PIPE:                                      // "||"
        return (BindPower) { .left = 4, .right = 5 };
    case TK_AMP_AMP:                                        // "&&"
        return (BindPower) { .left = 5, .right = 6 };
    case TK_PIPE:                                           // "|"
        return (BindPower) { .left = 6, .right = 7 };
    case TK_CARET:                                          // "^"
        return (BindPower) { .left = 7, .right = 8 };
    case TK_AMP:                                            // "&"
        return (BindPower) { .left = 8, .right = 9 };
    case TK_EQ_EQ: case TK_BANG_EQ:                         // "==", "!="
        return (BindPower) { .left = 9, .right = 10 };
    case TK_LT: case TK_LT_EQ:                              // "<", "<="
    case TK_GT: case TK_GT_EQ:                              // ">", ">="
        return (BindPower) { .left = 10, .right = 11 };
    case TK_LT_LT: case TK_GT_GT:                           // "<<", ">>"
        return (BindPower) { .left = 11, .right = 12 };
    case TK_PLUS: case TK_MINUS:                            // "+", "-"
        return (BindPower) { .left = 11, .right = 12 };
    case TK_STAR: case TK_SLASH: case TK_PERCENT:           // "*", "/", "%"
        return (BindPower) { .left = 12, .right = 13 };
    // NOTE: `get_prefix_op` needs to be updated if the highest postfix operator
    // changes
    case TK_DOT: case TK_MINUS_GT:                          // ".", "->"
    case TK_OPAREN: case TK_OBRACK:                         // "(", "["
    case TK_PLUS_PLUS: case TK_MINUS_MINUS:                 // "++", "--"
        return (BindPower) { .left = 13, .right = 14 };
    default:                                                // non-operation
        return (BindPower) { .left = 0, .right = 0 };
    }
}

// Returns the left binding power for prefix operations. Used when parsing the
// operand of prefix unary operations.
static inline uint8_t get_prefix_op_bp(void)
{
    // The prefix left binding power equals the right binding power of the
    // tightest postfix operator (i.e. *p++ == *(p++) and not *p++ != (*p)++).
    return get_op_bp(TK_MINUS_MINUS).left;
}

static Expr *parse_expr(Parser *p);
static Expr *parse_expr_bp(Parser *p, uint8_t min_bp);

// Parses and returns the arguments in a function call while storing the
// argument count in `argc`. Must be called after consuming the open paren of
// the function call. After returning, the parser will be at the closing paren.
static Expr **parse_fn_call_args(Parser *p, size_t *argc)
{
    size_t capacity = 4;
    Expr **tmp = malloc(capacity * sizeof(Expr *));
    if (tmp == NULL)
        diag_fatal("could not allocate temporary memory to parse function arguments");

    *argc = 0;
    uint8_t min_arg_bp = get_op_bp(TK_COMMA).right;
    while (!parser_at_eof(p) && !parser_check(p, TK_CPAREN)) {
        if (*argc >= capacity) {
            capacity *= 2;
            tmp = realloc(tmp, capacity * sizeof(Expr *));
            if (tmp == NULL)
                diag_fatal("could not allocate temporary memory to parse function arguments");
        }
        tmp[(*argc)++] = parse_expr_bp(p, min_arg_bp);
        if (!parser_eat(p, TK_COMMA))
            break;
        if (parser_check(p, TK_CPAREN))
            diag_fatal_at(p->tokens[p->pos].loc, "trailing comma in function call argument list");
    }

    if (*argc == 0) {
        free(tmp);
        return NULL;
    }
    Expr **args = arena_alloc_many(p->a, Expr *, *argc);
    memcpy(args, tmp, *argc * sizeof(Expr *));
    free(tmp);
    return args;
}

/*
  Parses the head of an expression and returns it. The head of an expression is
  constructed as follows:

  expr_head = ident
            | str
            | num
            | "(" expr ")"
            | unary_op expr

  unary_op  = "+" | "-" | "!" | "~" | "*" | "&" | "++" | "--"
*/
static Expr *parse_expr_head(Parser *p)
{
    Token t = p->tokens[p->pos];
    parser_bump(p);
    switch (t.kind) {
    case TK_IDENT: {
        Expr *e = arena_alloc(p->a, Expr);
        e->kind = EXPR_IDENT;
        e->loc = t.loc;
        e->ident = arena_strndup(p->a, t.start, t.len);
        return e;
    }
    case TK_KW:
        if (token_equal(t, "sizeof")) {
            Expr *e = arena_alloc(p->a, Expr);
            e->loc = t.loc;
            if (parser_eat(p, TK_OPAREN)) {
                e->kind = EXPR_SIZEOF_TY;
                e->sizeof_ty = parse_type(p);
                if (!parser_expect(p, TK_CPAREN))
                    UNREACHABLE("parser_expect is currently nonreturnable");
            } else {
                e->kind = EXPR_SIZEOF_EX;
                e->sizeof_expr = parse_expr_bp(p, get_prefix_op_bp());
            }
            return e;
        } else if (token_equal(t, "_Alignof") || token_equal(t, "alignof")) {
            if (!parser_expect(p, TK_OPAREN))
                UNREACHABLE("parser_expect is currently nonreturnable");
            Expr *e = arena_alloc(p->a, Expr);
            e->kind = EXPR_ALIGNOF;
            e->loc = t.loc;
            e->alignof_ty = parse_type(p);
            if (!parser_expect(p, TK_CPAREN))
                UNREACHABLE("parser_expect is currently nonreturnable");
            return e;
        } else
            diag_fatal_at(t.loc, "unexpected keyword found while parsing expression");
    case TK_CHAR:
        TODO("implement parsing of character literals");
    case TK_STR: {
        Expr *e = arena_alloc(p->a, Expr);
        e->kind = EXPR_STR;
        e->loc = t.loc;
        e->str = arena_strndup(p->a, t.start, t.len);
        return e;
    }
    case TK_NUM: {
        Expr *e = arena_alloc(p->a, Expr);
        e->kind = EXPR_NUM;
        e->loc = t.loc;
        e->val = 0;
        for (size_t i = 0; i < t.len; ++i)
            e->val = e->val * 10 + (t.start[i] - '0');
        return e;
    }
    case TK_OPAREN:
        if (is_type(p->tokens[p->pos])) {
            // Cast
            // "(" Type ")" expr
            Expr *e = arena_alloc(p->a, Expr);
            e->kind = EXPR_CAST;
            e->loc = t.loc;
            e->cast.type = parse_type(p);
            if (!is_castable_type(e->cast.type))
                diag_fatal_at(e->cast.type.loc,
                              "could not cast to type `%s`, only arithmetic "
                              "and pointer types are castable",
                              type_to_str(e->cast.type));
            if (!parser_expect(p, TK_CPAREN))
                UNREACHABLE("parser_expect is currently nonreturnable");
            e->cast.expr = parse_expr_bp(p, get_prefix_op_bp());
            return e;
        } else {
            // Parenthesized expression
            // "(" expr ")"
            Expr *e = parse_expr(p);
            if (!parser_expect(p, TK_CPAREN))
                UNREACHABLE("parser_expect is currently nonreturnable");
            return e;
        }
    // "+" expr
    case TK_PLUS:
        return new_unop_expr(p->a, t.loc, UNOP_POS,
                             parse_expr_bp(p, get_prefix_op_bp()));
    // "-" expr
    case TK_MINUS:
        return new_unop_expr(p->a, t.loc, UNOP_NEG,
                             parse_expr_bp(p, get_prefix_op_bp()));
    // "!" expr
    case TK_BANG:
        return new_unop_expr(p->a, t.loc, UNOP_NOT,
                             parse_expr_bp(p, get_prefix_op_bp()));
    // "~" expr
    case TK_TILDE:
        return new_unop_expr(p->a, t.loc, UNOP_BIT_NOT,
                             parse_expr_bp(p, get_prefix_op_bp()));
    // "*" expr
    case TK_STAR:
        return new_unop_expr(p->a, t.loc, UNOP_DEREF,
                             parse_expr_bp(p, get_prefix_op_bp()));
    // "&" expr
    case TK_AMP:
        return new_unop_expr(p->a, t.loc, UNOP_ADDR,
                             parse_expr_bp(p, get_prefix_op_bp()));
    // "++" expr
    case TK_PLUS_PLUS:
        return new_unop_expr(p->a, t.loc, UNOP_PRE_INC,
                             parse_expr_bp(p, get_prefix_op_bp()));
    // "--" expr
    case TK_MINUS_MINUS:
        return new_unop_expr(p->a, t.loc, UNOP_PRE_DEC,
                             parse_expr_bp(p, get_prefix_op_bp()));
    default:
        // TODO: use `diag_report_at` and try to recover from unexpected
        // expression
        diag_fatal_at(t.loc, "unexpected expression `%.*s`", t.len, t.start);
    }
}

/*
  Pratt parser algorithm implementation for expression parsing. An expression is
  constructed in the following way:

  expr      = expr_head expr_tail*

  expr_head = ident
            | str
            | num
            | "(" expr ")"
            | unary_op expr

  expr_tail = "?" expr ":" expr
            | "(" expr1 "," expr2 "," ... ")"
            | "[" expr "]"
            | ("." | "->") field_name
            | tail_op expr

  unary_op  = "+" | "-" | "!" | "~" | "*" | "&" | "++" | "--"

  tail_op   = "++" | "--" | assign_op | bin_op

  assign_op = "&=" | "^=" | "|=" | "<<=" | ">>="
            | "*=" | "/=" | "%=" | "+=" | "-=" | "="

  bin_op    = "||" | "&&" | "|" | "^" | "==" | "!=" | "<" | "<="
            | ">" | ">=" | "<<" | ">>" | "+" | "-" | "*" | "/" | "%"
*/
static Expr *parse_expr_bp(Parser *p, uint8_t min_bp)
{
    Expr *e = parse_expr_head(p);
    while (!parser_at_eof(p)) {
        Token op = p->tokens[p->pos];

        // Operation precedence check
        BindPower op_bp = get_op_bp(op.kind);
        if (op_bp.left < min_bp)
            break;

        // Postfix increment/decrement
        if (parser_eat(p, TK_PLUS_PLUS) || parser_eat(p, TK_MINUS_MINUS)) {
            e = new_unop_expr(p->a, op.loc,
                              op.kind == TK_PLUS_PLUS ? UNOP_POST_INC : UNOP_POST_DEC, e);
            continue;
        }

        // Ternary operator: expr "?" expr : expr
        if (parser_eat(p, TK_QUESTION)) {
            Expr *cond = e;
            e = arena_alloc(p->a, Expr);
            e->kind = EXPR_TERNOP;
            e->loc = op.loc;
            e->ternop.cond = cond;
            e->ternop.then = parse_expr_bp(p, op_bp.right);
            if (!parser_expect(p, TK_COLON))
                UNREACHABLE("parser_expect is currently nonreturnable");
            e->ternop._else = parse_expr_bp(p, get_op_bp(TK_COLON).right);
            continue;
        }
        if (parser_check(p, TK_COLON))
            // The else branch after `:` is parsed in the above if statement
            break;

        // Function call
        if (parser_eat(p, TK_OPAREN)) {
            if (e->kind != EXPR_IDENT)
                diag_fatal_at(e->loc, "invalid identifier used as function name");
            const char *fn_name = e->ident;
            e->kind = EXPR_FN_CALL;
            e->fn_call.fn_name = fn_name;
            e->fn_call.args = parse_fn_call_args(p, &e->fn_call.argc);
            if (!parser_expect(p, TK_CPAREN))
                UNREACHABLE("parser_expect is currently nonreturnable");
            continue;
        }

        // Array element access
        if (parser_eat(p, TK_OBRACK)) {
            Expr *array = e;
            e = arena_alloc(p->a, Expr);
            e->kind = EXPR_INDEX;
            e->loc = op.loc;
            e->index.array = array;
            e->index.index = parse_expr(p);
            if (!parser_expect(p, TK_CBRACK))
                UNREACHABLE("parse_expect is nonreturnable");
            continue;
        }

        // Struct or struct pointer field access
        if (parser_eat(p, TK_DOT) || parser_eat(p, TK_MINUS_GT)) {
            Token field = p->tokens[p->pos];
            if (!parser_expect(p, TK_IDENT))
                UNREACHABLE("parser_expect is currently nonreturnable");
            Expr *obj = e;
            e = arena_alloc(p->a, Expr);
            e->kind = op.kind == TK_DOT ? EXPR_FIELD : EXPR_ARROW;
            e->loc = op.loc;
            e->field._struct = obj;
            e->field.field = arena_strndup(p->a, field.start, field.len);
            continue;
        }

        parser_bump(p);
        // Assignement operation
        if (is_assign_op(op.kind))
            e = new_assign_expr(p->a, op.loc, get_assign_kind(op.kind),
                                e, parse_expr_bp(p, op_bp.right));
        // Binary operation
        else
            e = new_binop_expr(p->a, op.loc, get_binop_kind(op.kind),
                               e, parse_expr_bp(p, op_bp.right));
    }
    return e;
}

// Wrapper function for `parse_expr_bp` with the minimum binding power set to
// the lowest binding power of any operation.
static inline Expr *parse_expr(Parser *p)
{
    // NOTE: minimum binding power of any operation is 1. The binding power of
    // any other token kind is 0.
    return parse_expr_bp(p, 1);
}

static void print_expr_as_sexp(Expr *e, uint32_t indent)
{
    switch (e->kind) {
    case EXPR_CHAR:
        printf("%c", e->c);
        break;
    case EXPR_STR:
        printf("%s", e->str);
        break;
    case EXPR_NUM:
        printf("%d", e->val);
        break;
    case EXPR_IDENT:
        printf("%s", e->ident);
        break;
    case EXPR_UNOP:
        switch (e->unop.kind) {
        case UNOP_POS:
            printf("+");
            print_expr_as_sexp(e->unop.operand, indent);
            break;
        case UNOP_NEG:
            printf("-");
            print_expr_as_sexp(e->unop.operand, indent);
            break;
        case UNOP_NOT:
            printf("!");
            print_expr_as_sexp(e->unop.operand, indent);
            break;
        case UNOP_BIT_NOT:
            printf("~");
            print_expr_as_sexp(e->unop.operand, indent);
            break;
        case UNOP_DEREF:
            printf("*");
            print_expr_as_sexp(e->unop.operand, indent);
            break;
        case UNOP_ADDR:
            printf("&");
            print_expr_as_sexp(e->unop.operand, indent);
            break;
        case UNOP_PRE_INC:
            printf("++");
            print_expr_as_sexp(e->unop.operand, indent);
            break;
        case UNOP_PRE_DEC:
            printf("--");
            print_expr_as_sexp(e->unop.operand, indent);
            break;
        case UNOP_POST_INC:
            print_expr_as_sexp(e->unop.operand, indent);
            printf("++");
            break;
        case UNOP_POST_DEC:
            print_expr_as_sexp(e->unop.operand, indent);
            printf("--");
            break;
        default:
            UNREACHABLE("e->unop.kind at print_expr_as_sexp");
        }
        break;
    case EXPR_BINOP:
        switch (e->binop.kind) {
        case BINOP_OR:
            printf("(or ");
            indent += 4;
            break;
        case BINOP_AND:
            printf("(and ");
            indent += 5;
            break;
        case BINOP_BIT_OR:
            printf("(| ");
            indent += 3;
            break;
        case BINOP_BIT_XOR:
            printf("(^ ");
            indent += 3;
            break;
        case BINOP_BIT_AND:
            printf("(& ");
            indent += 3;
            break;
        case BINOP_EQ:
            printf("(== ");
            indent += 4;
            break;
        case BINOP_NOT_EQ:
            printf("(!= ");
            indent += 4;
            break;
        case BINOP_LT:
            printf("(< ");
            indent += 3;
            break;
        case BINOP_LT_EQ:
            printf("(<= ");
            indent += 4;
            break;
        case BINOP_GT:
            printf("(> ");
            indent += 3;
            break;
        case BINOP_GT_EQ:
            printf("(>= ");
            indent += 4;
            break;
        case BINOP_LSFT:
            printf("(<< ");
            indent += 4;
            break;
        case BINOP_RSFT:
            printf("(>> ");
            indent += 4;
            break;
        case BINOP_PLUS:
            printf("(+ ");
            indent += 3;
            break;
        case BINOP_MINUS:
            printf("(- ");
            indent += 3;
            break;
        case BINOP_MULT:
            printf("(* ");
            indent += 3;
            break;
        case BINOP_DIV:
            printf("(/ ");
            indent += 3;
            break;
        case BINOP_MOD:
            printf("(%% ");
            indent += 3;
            break;
        default:
            UNREACHABLE("e->binop.kind at print_expr_as_sexp");
        }
        print_expr_as_sexp(e->binop.lhs, indent);
        printf("\n%*s", indent, "");
        print_expr_as_sexp(e->binop.rhs, indent);
        printf(")");
        break;
    case EXPR_TERNOP:
        printf("(if ");
        indent += 4;
        print_expr_as_sexp(e->ternop.cond, indent);
        printf("\n%*s", indent, "");
        print_expr_as_sexp(e->ternop.then, indent);
        printf("\n%*s", indent - 1, "");
        print_expr_as_sexp(e->ternop._else, indent);
        printf(")");
        break;
    case EXPR_FN_CALL:
        printf("(%s", e->fn_call.fn_name);
        if (e->fn_call.argc > 0) {
            printf(" ");
            indent += 2 + strlen(e->fn_call.fn_name);
            print_expr_as_sexp(e->fn_call.args[0], indent);
            for (size_t i = 1; i < e->fn_call.argc; ++i) {
                printf("\n%*s", indent, "");
                print_expr_as_sexp(e->fn_call.args[i], indent);
            }
        }
        printf(")");
        break;
    case EXPR_ASSIGN:
        printf("(let ");
        indent += 5;
        print_expr_as_sexp(e->assign.var, indent);
        printf("\n%*s", indent, "");
        switch (e->assign.kind) {
        case ASSIGN_AND:
            printf("(& ");
            indent += 3;
            print_expr_as_sexp(e->assign.var, indent);
            printf("\n%*s", indent, "");
            print_expr_as_sexp(e->assign.value, indent);
            printf(")");
            break;
        case ASSIGN_XOR:
            printf("(^ ");
            print_expr_as_sexp(e->assign.var, indent);
            printf(" ");
            print_expr_as_sexp(e->assign.value, indent);
            printf(")");
            break;
        case ASSIGN_OR:
            printf("(| ");
            print_expr_as_sexp(e->assign.var, indent);
            printf(" ");
            print_expr_as_sexp(e->assign.value, indent);
            printf(")");
            break;
        case ASSIGN_LSFT:
            printf("(<< ");
            print_expr_as_sexp(e->assign.var, indent);
            printf(" ");
            print_expr_as_sexp(e->assign.value, indent);
            printf(")");
            break;
        case ASSIGN_RSFT:
            printf("(>> ");
            print_expr_as_sexp(e->assign.var, indent);
            printf(" ");
            print_expr_as_sexp(e->assign.value, indent);
            printf(")");
            break;
        case ASSIGN_MULT:
            printf("(* ");
            print_expr_as_sexp(e->assign.var, indent);
            printf(" ");
            print_expr_as_sexp(e->assign.value, indent);
            printf(")");
            break;
        case ASSIGN_DIV:
            printf("(/ ");
            print_expr_as_sexp(e->assign.var, indent);
            printf(" ");
            print_expr_as_sexp(e->assign.value, indent);
            printf(")");
            break;
        case ASSIGN_MOD:
            printf("(%% ");
            print_expr_as_sexp(e->assign.var, indent);
            printf(" ");
            print_expr_as_sexp(e->assign.value, indent);
            printf(")");
            break;
        case ASSIGN_PLUS:
            printf("(+ ");
            print_expr_as_sexp(e->assign.var, indent);
            printf(" ");
            print_expr_as_sexp(e->assign.value, indent);
            printf(")");
            break;
        case ASSIGN_MINUS:
            printf("(- ");
            print_expr_as_sexp(e->assign.var, indent);
            printf(" ");
            print_expr_as_sexp(e->assign.value, indent);
            printf(")");
            break;
        case ASSIGN_EQ:
            print_expr_as_sexp(e->assign.value, indent);
            break;
        default:
            UNREACHABLE("e->assign.kind at print_expr_as_sexp");
        }
        printf(")");
        break;
    case EXPR_INDEX:
        print_expr_as_sexp(e->index.array, indent);
        printf("\[");
        print_expr_as_sexp(e->index.index, indent);
        printf("]");
        break;
    case EXPR_FIELD:
        print_expr_as_sexp(e->field._struct, indent);
        printf(".%s", e->field.field);
        break;
    case EXPR_ARROW:
        print_expr_as_sexp(e->field._struct, indent);
        printf("->%s", e->field.field);
        break;
    case EXPR_CAST:
        indent += strlen(type_to_str(e->cast.type)) + 1;
        printf("(%s) ", type_to_str(e->cast.type));
        print_expr_as_sexp(e->cast.expr, indent);
        break;
    case EXPR_SIZEOF_TY:
        printf("sizeof(%s)", type_to_str(e->sizeof_ty));
        break;
    case EXPR_SIZEOF_EX:
        printf("sizeof ");
        indent += 7;
        print_expr_as_sexp(e->sizeof_expr, indent);
        break;
    case EXPR_ALIGNOF:
        printf("alignof(%s)", type_to_str(e->alignof_ty));
        break;
    default:
        UNREACHABLE("print_expr_as_sexp");
    }
}

Parser parser_init_from_file_path(Arena *a, const char *file_path)
{
    Parser p = { 0 };
    p.a = a;

    // Count amount of tokens
    Lexer l = lexer_init_from_file_path(a, file_path);
    Token t = { 0 };
    do {
        t = lexer_next_token(&l);
        p.token_count++;
    } while (t.kind != TK_EOF);

    // Store tokens in token array
    l.pos = l.bol = l.line = 0;
    p.tokens = (Token *) arena_alloc_many(p.a, Token, p.token_count);
    for (size_t i = 0; i < p.token_count; ++i)
        p.tokens[i] = lexer_next_token(&l);

    return p;
}

void parse_transl_unit(Parser *p)
{
    Expr *e = parse_expr(p);
    print_expr(stdout, e, 0);
    printf("\n");
}
