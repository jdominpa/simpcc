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
#include "scope.h"

// Returns the current token without consuming it.
static inline Token parser_peek(const Parser *p)
{
    assert(p->pos < p->token_count);
    return p->tokens[p->pos];
}

// Returns the most recently consumed token. Panics if called without having
// consumed at least one token.
static inline Token parser_prev(const Parser *p)
{
    assert(p->pos > 0 && p->pos <= p->token_count);
    return p->tokens[p->pos - 1];
}

// Checks if the current token is of TokenKind `kind`, and returns `true` if so.
static inline bool parser_check(const Parser *p, TokenKind kind)
{
    return parser_peek(p).kind == kind;
}

// Returns whether the parser is at EOF or not.
static inline bool parser_at_eof(const Parser *p)
{
    return parser_check(p, TK_EOF);
}

// Advance the parser by one token.
static inline void parser_bump(Parser *p)
{
    if (!parser_at_eof(p)) p->pos++;
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
        Token t = parser_peek(p);
        // TODO: try to recover from unexpected tokens instead of crashing
        diag_fatal_at(t.loc, "unexpected token, expected `%s` but found %s",
                      token_kind_to_str[kind], token_to_str(t));
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

// Returns `true` if the current token in the parser `p` is a type.
static bool is_type(const Scope *sc, Token t)
{
    if (t.kind == TK_IDENT) {
        Symbol *found = scope_lookup_var_n(sc, t.start, t.len);
        return found != NULL && found->kind == SYMBOL_TYPEDEF;
    }
    if (t.kind != TK_KW) return false;

    const char *types[] = {
        "void", "bool", "_Bool", "char", "short", "int", "long",
        "float", "double", "signed", "unsigned", "struct", "union", "enum",
        "const", "volatile", "restrict"
    };
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); ++i)
        if (token_equal(t, types[i])) return true;
    return false;
}

// Returns `true` if the given Token `t` is a type qualifier.
static bool is_type_qual(Token t)
{
    if (t.kind != TK_KW) return false;
    return token_equal(t, "const") || token_equal(t, "volatile") || token_equal(t, "restrict");
}

// Parses a list of consecutive type qualifiers. Returns an bitmap specifying
// the qualifiers present according to the `TypeQual` enum.
static uint8_t parse_type_quals(Parser *p)
{
    uint8_t quals = 0;
    Token t = parser_peek(p);
    while (is_type_qual(t)) {
        if (token_equal(t, "const"))
            quals |= QUAL_CONST;
        else if (token_equal(t, "volatile"))
            quals |= QUAL_VOLATILE;
        else
            quals |= QUAL_RESTRICT;
        parser_bump(p);
        t = parser_peek(p);
    }
    return quals;
}

// Temporary struct to hold the declaration-specifiers. Only used transitively
// to store the information.
typedef struct {
    Loc loc;
    StorageClass storage;
    Type *base;
    bool is_inline;
    bool is_noreturn;
    bool has_base_type;
    bool saw_arith;  // void/char/int/... present?
} DeclSpec;

// Type of C declaration to be parsed. They share the type specifiers and
// qualifiers and only differ in what else is permitted.
typedef enum {
    DECL_SPEC_DECLARATION,  // declaration-specifiers: everything allowed
    DECL_SPEC_TYPE_NAME,    // specifier-qualifier-list: no storage class or
                            // function specifiers
    DECL_SPEC_PARAM,        // parameter-declaration: `register` and nothing else
} DeclSpecMode;

// Parses a run of declaration specifiers. The `mode` specifies which type of
// specifiers are allowed in the declaration.
static DeclSpec parse_decl_spec(Parser *p, DeclSpecMode mode)
{
    DeclSpec spec = { 0 };
    spec.base = arena_alloc(p->a, Type);
    *spec.base = (Type) { 0 };
    uint32_t counter = 0;
    uint8_t quals = 0;
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

    spec.loc = parser_peek(p).loc;
    const size_t start_pos = p->pos;
    for (;;) {
        Token t = parser_peek(p);

        // Storage class specifiers
        if (token_equal(t, "typedef") || token_equal(t, "extern") || token_equal(t, "static") ||
            token_equal(t, "auto") || token_equal(t, "register")) {
            switch (mode) {
            case DECL_SPEC_DECLARATION:
                break;
            case DECL_SPEC_TYPE_NAME:
                diag_fatal_at(t.loc,
                              "storage class specifier `%.*s` is not allowed in a type name",
                              (int) t.len, t.start);
            case DECL_SPEC_PARAM:
                if (!token_equal(t, "register"))
                    diag_fatal_at(t.loc,
                                  "storage class specifier `%.*s` is not allowed in a parameter declaration",
                                  (int) t.len, t.start);
                break;
            }

            if (spec.storage != STORAGE_NONE)
                // TODO: print the original and current specifiers
                diag_fatal_at(t.loc, "multiple storage specifiers found in declaration");

            if (token_equal(t, "typedef"))
                spec.storage = STORAGE_TYPEDEF;
            else if (token_equal(t, "extern"))
                spec.storage = STORAGE_EXTERN;
            else if (token_equal(t, "static"))
                spec.storage = STORAGE_STATIC;
            else if (token_equal(t, "auto"))
                spec.storage = STORAGE_AUTO;
            else
                spec.storage = STORAGE_REGISTER;

            parser_bump(p);
            continue;
        }

        // Function specifiers
        if (token_equal(t, "inline") || token_equal(t, "_Noreturn")) {
            switch (mode) {
            case DECL_SPEC_DECLARATION:
                break;
            case DECL_SPEC_TYPE_NAME:
                diag_fatal_at(t.loc,
                              "function specifier `%.*s` is not allowed in a type name",
                              (int) t.len, t.start);
            case DECL_SPEC_PARAM:
                diag_fatal_at(t.loc,
                              "function specifier `%.*s` is not allowed in a parameter declaration",
                              (int) t.len, t.start);
            }

            if (token_equal(t, "inline"))
                spec.is_inline = true;
            else
                spec.is_noreturn = true;

            parser_bump(p);
            continue;
        }

        // Type qualifiers
        if (is_type_qual(t)) {
            quals |= parse_type_quals(p);
            continue;
        }

        // Arithmetic types
        if (token_equal(t, "void") || token_equal(t, "bool") || token_equal(t, "char") ||
            token_equal(t, "short") || token_equal(t, "int") || token_equal(t, "long") ||
            token_equal(t, "float") || token_equal(t, "double") || token_equal(t, "signed") ||
            token_equal(t, "unsigned")) {
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
            else
                counter |= UNSIGNED;

            spec.saw_arith = true;
            spec.has_base_type = true;
            parser_bump(p);
            continue;
        }

        // Struct / union / enum
        if (token_equal(t, "struct") || token_equal(t, "union") || token_equal(t, "enum")) {
            TODO("parse_decl_spec: struct/union/enum");
            // TODO: consult the tag namespace; may also *define* a type inline.
            //       Sets ds.base directly and ds.has_base_type.
        }

        // Typedef types
        if (t.kind == TK_IDENT && !spec.has_base_type) {
            Symbol *found = scope_lookup_var_n(&p->sc, t.start, t.len);
            if (found == NULL || found->kind != SYMBOL_TYPEDEF) break;
            spec.base->kind = TYPE_NAMED;
            spec.base->named.name = found->name;
            spec.base->named.ty = found->ty;
            spec.has_base_type = true;
            quals |= found->ty->quals;
            parser_bump(p);
            continue;
        }

        break;
    }

    if (spec.saw_arith) {
        switch (counter) {
        case VOID:
            spec.base->kind = TYPE_VOID;
            break;
        case VOID + SIGNED:
            diag_fatal_at(spec.loc, "type `void` is incompatible with type modifier `signed`");
        case VOID + UNSIGNED:
            diag_fatal_at(spec.loc, "type `void` is incompatible with type modifier `unsigned`");
        case BOOL:
            spec.base->kind = TYPE_BOOL;
            break;
        case BOOL + SIGNED:
            diag_fatal_at(spec.loc, "type `bool` is incompatible with type modifier `unsigned`");
        case BOOL + UNSIGNED:
            diag_fatal_at(spec.loc, "type `bool` is incompatible with type modifier `unsigned`");
        case CHAR:
            spec.base->kind = TYPE_CHAR;
            spec.base->sign = SIGN_UNSPECIFIED;
            break;
        case CHAR + SIGNED:
            spec.base->kind = TYPE_CHAR;
            spec.base->sign = SIGN_SIGNED;
            break;
        case CHAR + UNSIGNED:
            spec.base->kind = TYPE_CHAR;
            spec.base->sign = SIGN_UNSIGNED;
            break;
        case SHORT:
        case SHORT + INT:
        case SHORT + SIGNED:
        case SHORT + INT + SIGNED:
            spec.base->kind = TYPE_SHORT;
            spec.base->sign = SIGN_SIGNED;
            break;
        case SHORT + UNSIGNED:
        case SHORT + INT + UNSIGNED:
            spec.base->kind = TYPE_SHORT;
            spec.base->sign = SIGN_UNSIGNED;
            break;
        case INT:
        case INT + SIGNED:
        case SIGNED:
            spec.base->kind = TYPE_INT;
            spec.base->sign = SIGN_SIGNED;
            break;
        case UNSIGNED:
        case UNSIGNED + INT:
            spec.base->kind = TYPE_INT;
            spec.base->sign = SIGN_UNSIGNED;
            break;
        case LONG:
        case LONG + INT:
        case LONG + SIGNED:
        case LONG + INT + SIGNED:
        case LONG + LONG:
        case LONG + LONG + INT:
        case LONG + LONG + SIGNED:
        case LONG + LONG + INT + SIGNED:
            spec.base->kind = TYPE_LONG;
            spec.base->sign = SIGN_SIGNED;
            break;
        case LONG + UNSIGNED:
        case LONG + INT + UNSIGNED:
        case LONG + LONG + UNSIGNED:
        case LONG + LONG + INT + UNSIGNED:
            spec.base->kind = TYPE_LONG;
            spec.base->sign = SIGN_UNSIGNED;
            break;
        case FLOAT:
            spec.base->kind = TYPE_FLOAT;
            break;
        case DOUBLE:
            spec.base->kind = TYPE_DOUBLE;
            break;
        case LONG + DOUBLE:
            spec.base->kind = TYPE_LDOUBLE;
            break;
        default:
            diag_fatal_at(spec.loc, "invalid type");
        }
    } else if (!spec.has_base_type) {
        // A DeclSpec with no type specifier names no type at all
        if (p->pos == start_pos)
            diag_fatal_at(spec.loc, "expected a type but found %s",
                          token_to_str(parser_peek(p)));
        diag_fatal_at(spec.loc, "declaration specifiers name no type");
    }

    spec.base->quals = quals;
    spec.base->loc = spec.loc;
    return spec;
}

// Result of parsing a declarator: the type it builds around the base type it
// was given, plus the identifier it declares. `name` is NULL for an abstract
// declarator.
typedef struct {
    Type *ty;
    const char *name;
    Loc name_loc;
} Declarator;

// Whether a declarator declares an identifier. C's grammar has both declarator
// and abstract-declarator and they differ only in the name.
typedef enum {
    DECLARATOR_NAMED,
    DECLARATOR_ABSTRACT,
    DECLARATOR_OPTIONAL,
} DeclaratorMode;

static Type *parse_declarator_suffix(Parser *p, Type *base);

// Parses the [...] array suffixes of a declarator. Must be called after
// consuming the opening `[`.
static Type *parse_declarator_array_suffix(Parser *p, Type *base)
{
    Type *array = arena_alloc(p->a, Type);
    *array = (Type) { .kind = TYPE_ARRAY, .loc = parser_prev(p).loc };

    // TODO: array size can be any constant expression, but we only accept
    // integer literals currently. `[*]`, `[static n]` and qualifiers are only
    // legal in parameter lists and are not handled either.
    if (parser_eat(p, TK_CBRACK)) {
        array->array.has_size = false;
        array->array.size_loc = parser_prev(p).loc;
    } else if (parser_eat(p, TK_NUM)) {
        Token num = parser_prev(p);

        NumericLiteral size = token_numeric_value(num);
        if (!size.valid)
            diag_fatal_at(num.loc,
                          "invalid array size `%.*s`", (int) num.len, num.start);
        if (size.overflow)
            diag_fatal_at(num.loc,
                          "array size `%.*s` is too large", (int) num.len, num.start);
        if (size.kind != NUMLIT_INT)
            diag_fatal_at(num.loc,
                          "array size `%.*s` must be an integer", (int) num.len, num.start);

        array->array.has_size = true;
        array->array.size_loc = num.loc;
        array->array.size = size.i;
        if (!parser_expect(p, TK_CBRACK))
            UNREACHABLE("parser_expect is currently nonreturnable");
    } else {
        diag_fatal_at(parser_peek(p).loc,
                      "incorrect array size found while parsing array declaration");
    }

    Type *elem = parse_declarator_suffix(p, base);
    if (elem->kind == TYPE_FUNC)
        diag_fatal_at(array->loc,
                      "array cannot have a function type as its element type");
    array->array.base = elem;
    return array;
}

static Declarator parse_declarator(Parser *p, const Type *base, DeclaratorMode mode);

static Type *parse_declarator_func_suffix(Parser *p, Type *ret)
{
    scope_enter(&p->sc);
    Type *func = arena_alloc(p->a, Type);
    *func = (Type) {
        .kind = TYPE_FUNC,
        .loc = parser_prev(p).loc,
        .func = { .ret = ret, .argc = 0, .args = NULL, .is_variadic = false }
    };

    struct {
        Type **items;
        size_t count;
        size_t capacity;
    } args = { 0 };

    while (!parser_at_eof(p) && !parser_check(p, TK_CPAREN)) {
        // Variadic arguments
        if (parser_eat(p, TK_ELLIPSIS)) {
            func->func.is_variadic = true;
            if (!parser_check(p, TK_CPAREN))
                diag_fatal_at(parser_peek(p).loc,
                              "`%s` must be the only/last parameter in a function declaration",
                              token_kind_to_str[TK_ELLIPSIS]);
            break;
        }

        DeclSpec spec = parse_decl_spec(p, DECL_SPEC_PARAM);
        Declarator dec = parse_declarator(p, spec.base, DECLARATOR_OPTIONAL);

        // `void` must be the only parameter in a function declaration and
        // cannot be a named parameter.
        if (dec.ty->kind == TYPE_VOID) {
            if (args.count > 0 || parser_check(p, TK_COMMA)) {
                diag_fatal_at(dec.ty->loc,
                              "type %s must be the first and only parameter in a function declaration",
                              TYPE_TO_STR(dec.ty));
            } else if (dec.name != NULL) {
                diag_fatal_at(dec.name_loc,
                              "type %s cannot have named parameters in function declaration",
                              TYPE_TO_STR(dec.ty));
            }
            break;
        }

        // A parameter of array type is adjusted to a pointer to its element
        // type, and a parameter of function type to a pointer to the function.
        Type *param = dec.ty;
        if (param->kind == TYPE_ARRAY || param->kind == TYPE_FUNC) {
            Type *ptr = arena_alloc(p->a, Type);
            *ptr = (Type) {
                .kind = TYPE_PTR,
                .loc = param->loc,
                // Qualifiers written inside the brackets belong to the pointer
                // the parameter becomes, not to the element type.
                .quals = param->quals,
                .ptr.base = param->kind == TYPE_ARRAY ? param->array.base : param,
            };
            param = ptr;
        }

        // Add declarator name if present to the function's parameter list
        // scope.
        if (dec.name != NULL) {
            Symbol *sym = arena_alloc(p->a, Symbol);
            *sym = (Symbol) {
                .kind = SYMBOL_VAR,
                .ns = NS_VAR,
                .loc = dec.name_loc,
                .name = dec.name,
                .ty = param,
            };
            scope_add_sym(&p->sc, sym);
        }

        da_append(&args, param,
                  "could not allocate temporary memory to parse function declaration");
        if (!parser_eat(p, TK_COMMA)) break;
        if (parser_check(p, TK_CPAREN))
            diag_fatal_at(parser_peek(p).loc,
                          "trailing comma in function declaration parameter list");
    }
    if (!parser_eat(p, TK_CPAREN)) {
        free(args.items);
        // TODO: handle error instead of crashing
        if (parser_at_eof(p))
            diag_fatal_at(func->loc,
                          "unclosed function declaration parameter list");
        else
            diag_fatal_at(parser_peek(p).loc,
                          "expected `,` or `)` in function declaration parameter list, but found %s",
                          token_to_str(parser_peek(p)));
    }

    // Parameter list parsing finished; exit its scope.
    scope_exit(&p->sc);

    if (args.count > 0) {
        func->func.argc = args.count;
        func->func.args = arena_alloc_many(p->a, Type *, args.count);
        memcpy(func->func.args, args.items, args.count * sizeof(Type *));
    }
    free(args.items);
    // Suffixes chain left to right with the leftmost outermost, so whatever
    // follows the parameter list is what the function returns.
    Type *ret_ty = parse_declarator_suffix(p, ret);
    if (ret_ty->kind == TYPE_ARRAY)
        diag_fatal_at(func->loc, "function cannot return an array type");
    if (ret_ty->kind == TYPE_FUNC)
        diag_fatal_at(func->loc, "function cannot return a function type");
    func->func.ret = ret_ty;
    return func;
}

// Parses the suffix after a declarator.
static Type *parse_declarator_suffix(Parser *p, Type *base)
{
    if (parser_eat(p, TK_OBRACK)) return parse_declarator_array_suffix(p, base);
    if (parser_eat(p, TK_OPAREN)) return parse_declarator_func_suffix(p, base);
    return base;
}

// Parses a single declarator with base type `base`.
static Declarator parse_declarator(Parser *p, const Type *base, DeclaratorMode mode)
{
    Declarator dec = { 0 };
    Type *ty = arena_alloc(p->a, Type);
    *ty = *base;

    // Pointer stars
    while (parser_check(p, TK_STAR)) {
        Type *inner = arena_alloc(p->a, Type);
        *inner = *ty;
        *ty = (Type) { .kind = TYPE_PTR,
                       .loc = parser_peek(p).loc,
                       .ptr.base = inner };
        parser_bump(p);
        ty->quals |= parse_type_quals(p);
    }
    dec.ty = ty;  // base type of declarator

    // Declarator name
    switch (mode) {
    case DECLARATOR_NAMED: {
        if (!parser_expect(p, TK_IDENT))
            UNREACHABLE("parser_expect is currently nonreturnable");
        Token name = parser_prev(p);
        dec.name = arena_strndup(p->a, name.start, name.len);
        dec.name_loc = name.loc;
        break;
    }
    case DECLARATOR_ABSTRACT:
        break;
    case DECLARATOR_OPTIONAL:
        if (parser_eat(p, TK_IDENT)) {
            Token name = parser_prev(p);
            dec.name = arena_strndup(p->a, name.start, name.len);
            dec.name_loc = name.loc;
        }
        break;
    }

    dec.ty = parse_declarator_suffix(p, dec.ty);
    return dec;
}

Type *parse_type(Parser *p)
{
    if (parser_at_eof(p))
        diag_fatal_at(parser_peek(p).loc,
                      "unexpected %s encountered while parsing type",
                      token_kind_to_str[TK_EOF]);

    DeclSpec spec = parse_decl_spec(p, DECL_SPEC_TYPE_NAME);
    return parse_declarator(p, spec.base, DECLARATOR_ABSTRACT).ty;
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

// Returns the BinopKind associated to a TokenKind `kind`.
static BinopKind get_binop_kind(TokenKind kind)
{
    static_assert(BINOP_COUNT == 19, "get_binop_kind: `BINOP_COUNT` value has changed");
    switch (kind) {
    case TK_COMMA:     return BINOP_COMMA;
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

// Returns the AssignKind associated to a TokenKind `kind`.
static AssignKind get_assign_kind(TokenKind kind)
{
    static_assert(ASSIGN_COUNT == 11, "get_assign_kind: `ASSIGN_COUNT` value has changed");
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

// Returns whether the TokenKind `kind` corresponds to an assignment operation.
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

// Returns the binding power (left,right) of an operation with TokenKind `kind`,
// with left < right meaning left associativity, and right > left meaning right
// associativity. If `kind` doesn't correspond to any operation the binding
// power is (0,0).
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
        return (BindPower) { .left = 12, .right = 13 };
    case TK_STAR: case TK_SLASH: case TK_PERCENT:           // "*", "/", "%"
        return (BindPower) { .left = 13, .right = 14 };
    // NOTE: `get_prefix_op` needs to be updated if the highest postfix operator
    // changes
    case TK_DOT: case TK_MINUS_GT:                          // ".", "->"
    case TK_OPAREN: case TK_OBRACK:                         // "(", "["
    case TK_PLUS_PLUS: case TK_MINUS_MINUS:                 // "++", "--"
        return (BindPower) { .left = 14, .right = 15 };
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

static Expr *parse_expr_bp(Parser *p, uint8_t min_bp);

// Parses and returns the arguments in a function call while storing the
// argument count in `argc`. Must be called after consuming the open paren of
// the function call. After returning, the parser will be at the closing paren.
static Expr **parse_func_call_args(Parser *p, size_t *argc)
{
    // Location of the '(' already consumed by the caller, used for diagnostics
    Loc open_loc = parser_prev(p).loc;

    struct {
        Expr **items;
        size_t count;
        size_t capacity;
    } args = { 0 };

    uint8_t min_arg_bp = get_op_bp(TK_COMMA).right;
    while (!parser_at_eof(p) && !parser_check(p, TK_CPAREN)) {
        da_append(&args, parse_expr_bp(p, min_arg_bp),
                  "could not allocate temporary memory to parse function call arguments");
        if (!parser_eat(p, TK_COMMA))
            break;
        if (parser_check(p, TK_CPAREN))
            diag_fatal_at(parser_peek(p).loc, "trailing comma in function call argument list");
    }
    if (!parser_eat(p, TK_CPAREN)) {
        free(args.items);
        // TODO: handle error instead of crashing
        if (parser_at_eof(p))
            diag_fatal_at(open_loc, "unclosed function call argument list");
        else
            diag_fatal_at(parser_peek(p).loc,
                          "expected `,` or `)` in function call argument list, but found %s",
                          token_to_str(parser_peek(p)));
    }

    *argc = args.count;
    if (args.count == 0) {
        free(args.items);
        return NULL;
    }
    Expr **args_ptr = arena_alloc_many(p->a, Expr *, args.count);
    memcpy(args_ptr, args.items, args.count * sizeof(Expr *));
    free(args.items);
    return args_ptr;
}

// TODO: parse compound literals
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
    Token t = parser_peek(p);
    parser_bump(p);
    switch (t.kind) {
    case TK_IDENT: {
        Expr *e = arena_alloc(p->a, Expr);
        e->kind = EXPR_IDENT;
        e->loc = t.loc;
        e->ident.name = arena_strndup(p->a, t.start, t.len);
        e->ident.sym = NULL;
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

        NumericLiteral val = token_numeric_value(t);
        if (!val.valid)
            diag_fatal_at(t.loc,
                          "invalid numeric literal `%.*s`", (int) t.len, t.start);
        if (val.overflow)
            diag_fatal_at(t.loc,
                          "numeric literal `%.*s` is too large", (int) t.len, t.start);

        // TODO: `Expr.val` is an int. It should carry the literal's own type.
        e->val = (int) val.i;
        return e;
    }
    case TK_OPAREN:
        if (is_type(&p->sc, parser_peek(p))) {
            // Cast
            // "(" Type ")" expr
            Expr *e = arena_alloc(p->a, Expr);
            e->kind = EXPR_CAST;
            e->loc = t.loc;
            e->cast.type = parse_type(p);
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
        diag_fatal_at(t.loc, "unexpected expression `%.*s`", (int) t.len, t.start);
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
        Token op = parser_peek(p);

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
            Expr *callee = e;
            e = arena_alloc(p->a, Expr);
            e->kind = EXPR_FUNC_CALL;
            e->loc = callee->loc;
            e->func_call.callee = callee;
            e->func_call.args = parse_func_call_args(p, &e->func_call.argc);
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
            Token field = parser_peek(p);
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
inline Expr *parse_expr(Parser *p)
{
    // NOTE: minimum binding power of any operation is 1. The binding power of
    // any other token kind is 0.
    return parse_expr_bp(p, 1);
}

//
// Statement parser
//

// Parses a block statement. The closing `}` is consumed by the function.
static Stmt *parse_block_stmts(Parser *p)
{
    Stmt *s = arena_alloc(p->a, Stmt);
    s->kind = STMT_BLOCK;
    s->loc = parser_prev(p).loc;

    struct {
        Stmt **items;
        size_t count;
        size_t capacity;
    } stmts = { 0 };

    // Parse statements
    while (!parser_at_eof(p) && !parser_check(p, TK_CBRACE))
        da_append(&stmts, parse_stmt(p),
                  "could not allocate temporary memory to parse block statement");
    if (!parser_eat(p, TK_CBRACE)) {
        free(stmts.items);
        // TODO: handle error instead of crashing
        diag_fatal_at(s->loc, "unclosed block statement");
    }

    if (stmts.count > 0) {
        s->block.stmts = arena_alloc_many(p->a, Stmt *, stmts.count);
        memcpy(s->block.stmts, stmts.items, stmts.count * sizeof(Stmt *));
    } else {
        s->block.stmts = NULL;
    }
    s->block.count = stmts.count;
    free(stmts.items);
    return s;
}

Stmt *parse_stmt(Parser *p)
{
    Token t = parser_peek(p);
    parser_bump(p);
    switch (t.kind) {
    case TK_EOF:
        diag_fatal_at(t.loc, "unexpected %s encountered while parsing statement", token_kind_to_str[TK_EOF]);
    case TK_IDENT:
        if (parser_eat(p, TK_COLON)) {
            Stmt *s = arena_alloc(p->a, Stmt);
            s->kind = STMT_LABEL;
            s->loc = t.loc;
            s->label.name = arena_strndup(p->a, t.start, t.len);
            if (parser_check(p, TK_CBRACE)) {
                Stmt *next = arena_alloc(p->a, Stmt);
                next->kind = STMT_NULL;
                next->loc = parser_peek(p).loc;
                s->label.next = next;
            } else {
                s->label.next = parse_stmt(p);
            }
            return s;
        } else
            diag_fatal_at(t.loc, "implement the rest of stmts beginning with `TK_IDENT`");
    case TK_KW: {
        Stmt *s = arena_alloc(p->a, Stmt);
        if (token_equal(t, "while")) {
            s->kind = STMT_WHILE;
            s->loc = t.loc;
            if (!parser_expect(p, TK_OPAREN))
                UNREACHABLE("parser_expect is currently nonreturnable");
            s->_while.cond = parse_expr(p);
            if (!parser_expect(p, TK_CPAREN))
                UNREACHABLE("parser_expect is currently nonreturnable");
            s->_while.body = parse_stmt(p);
        } else if (token_equal(t, "do")) {
            s->kind = STMT_DO;
            s->loc = t.loc;
            s->_while.body = parse_stmt(p);
            t = parser_peek(p);
            if (!parser_expect(p, TK_KW) || !token_equal(t, "while"))
                UNREACHABLE("parser_expect is currently nonreturnable");
            if (!parser_expect(p, TK_OPAREN))
                UNREACHABLE("parser_expect is currently nonreturnable");
            s->_while.cond = parse_expr(p);
            if (!parser_expect(p, TK_CPAREN) || !parser_expect(p, TK_SEMI))
                UNREACHABLE("parser_expect is currently nonreturnable");
        } else if (token_equal(t, "if")) {
            s->kind = STMT_IF;
            s->loc = t.loc;
            if (!parser_expect(p, TK_OPAREN))
                UNREACHABLE("parser_expect is currently nonreturnable");
            s->_if.cond = parse_expr(p);
            if (!parser_expect(p, TK_CPAREN))
                UNREACHABLE("parser_expect is currently nonreturnable");
            s->_if.then = parse_stmt(p);
            s->_if._else = NULL;
            if (token_equal(parser_peek(p), "else")) {
                parser_bump(p);
                s->_if._else = parse_stmt(p);
            }
        } else if (token_equal(t, "break")) {
            s->kind = STMT_BREAK;
            s->loc = t.loc;
            if (!parser_expect(p, TK_SEMI))
                UNREACHABLE("parser_expect is currently nonreturnable");
        } else if (token_equal(t, "continue")) {
            s->kind = STMT_CONT;
            s->loc = t.loc;
            if (!parser_expect(p, TK_SEMI))
                UNREACHABLE("parser_expect is currently nonreturnable");
        } else if (token_equal(t, "goto")) {
            s->kind = STMT_GOTO;
            s->loc = t.loc;
            t = parser_peek(p);
            if (!parser_expect(p, TK_IDENT))
                UNREACHABLE("parser_expect is currently nonreturnable");
            s->goto_label = arena_strndup(p->a, t.start, t.len);
            if (!parser_expect(p, TK_SEMI))
                UNREACHABLE("parser_expect is currently nonreturnable");
        } else if (token_equal(t, "return")) {
            s->kind = STMT_RET;
            s->loc = t.loc;
            s->_return = parser_check(p, TK_SEMI) ? NULL : parse_expr(p);
            if (!parser_expect(p, TK_SEMI))
                UNREACHABLE("parser_expect is currently nonreturnable");
        } else
            diag_fatal_at(t.loc, "implemenet the rest of stmts beginning with `TK_KW`");
        return s;
    }
    case TK_OBRACE:
        return parse_block_stmts(p);
    case TK_SEMI: {
        Stmt *s = arena_alloc(p->a, Stmt);
        s->kind = STMT_NULL;
        s->loc = t.loc;
        return s;
    }
    default:
        // TODO: use `diag_report_at` and try to recover from the error
        diag_fatal_at(t.loc, "invalid statement `%.*s` encountered",
                      (int) t.len, t.start);
    }
}

//
// Translation unit parser
//

TranslUnit parse_transl_unit(Parser *p)
{
    TranslUnit _tl = { 0 };
    print_stmt(stdout, parse_stmt(p), 0);
    return _tl;
}

//
// Parser initialization and freeing
//

Parser parser_init_from_lexer(Arena *a, Lexer l)
{
    Parser p = { 0 };
    p.a = a;

    // Count amount of tokens
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

inline Parser parser_init_from_src(Arena *a, const char *src)
{
    return parser_init_from_lexer(a, lexer_init_from_src(src));
}

inline Parser parser_init_from_file_path(Arena *a, const char *path)
{
    return parser_init_from_lexer(a, lexer_init_from_file_path(a, path));
}

void parser_free(Parser *p)
{
    scope_free(&p->sc);
}
