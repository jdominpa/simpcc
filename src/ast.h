#ifndef AST_H_
#define AST_H_

#include <stdbool.h>
#include <stdint.h>

#include "arena.h"
#include "common.h"
#include "diag.h"

//
// Types
//

typedef enum {
    TYPE_VOID,
    TYPE_BOOL,
    TYPE_CHAR,
    TYPE_SHORT,
    TYPE_INT,
    TYPE_LONG,
    TYPE_FLOAT,
    TYPE_DOUBLE,
    TYPE_LDOUBLE,
    TYPE_ENUM,
    TYPE_PTR,
    TYPE_ARRAY,
    TYPE_FUNC,
    TYPE_VLA,     // variable length array
    TYPE_STRUCT,
    TYPE_UNION,
    TYPE_NAMED,   // typedef types
    TYPE_COUNT,
} TypeKind;

typedef enum {
    QUAL_CONST    = 1 << 0,
    QUAL_VOLATILE = 1 << 1,
    QUAL_RESTRICT = 1 << 2,
} TypeQual;

typedef enum {
    SIGN_UNSPECIFIED,
    SIGN_SIGNED,
    SIGN_UNSIGNED,
} TypeSign;

typedef struct Type Type;
struct Type {
    TypeKind kind;
    Loc loc;
    uint8_t quals;
    TypeSign sign;
    union {
        struct {
            Type *base;
        } ptr;
        struct {
            Type *base;
            bool has_size;
            size_t size;
            Loc size_loc;
        } array;
        struct {
            Type *ret;
            size_t argc;
            Type **args;
            bool is_variadic;
        } func;
        struct {
            const char *name;
            Type *ty;
        } named;
    };
};

//
// Expressions
//

typedef enum {
    UNOP_POS,       // +x
    UNOP_NEG,       // -x
    UNOP_NOT,       // !x
    UNOP_BIT_NOT,   // ~x
    UNOP_DEREF,     // *x
    UNOP_ADDR,      // &x
    UNOP_PRE_INC,   // ++x
    UNOP_PRE_DEC,   // --x
    UNOP_POST_INC,  // x++
    UNOP_POST_DEC,  // x--
    UNOP_COUNT,
} UnopKind;

typedef enum {
    BINOP_COMMA,    // `,`
    BINOP_OR,       // `||`
    BINOP_AND,      // `&&`
    BINOP_BIT_OR,   // `|`
    BINOP_BIT_XOR,  // `^`
    BINOP_BIT_AND,  // `&`
    BINOP_EQ,       // `==`
    BINOP_NOT_EQ,   // `!=`
    BINOP_LT,       // `<`
    BINOP_LT_EQ,    // `<=`
    BINOP_GT,       // `>`
    BINOP_GT_EQ,    // `>=`
    BINOP_LSFT,     // `<<`
    BINOP_RSFT,     // `>>`
    BINOP_PLUS,     // `+`
    BINOP_MINUS,    // `-`
    BINOP_MULT,     // `*`
    BINOP_DIV,      // `/`
    BINOP_MOD,      // `%`
    BINOP_COUNT,
} BinopKind;

typedef enum {
    ASSIGN_AND,     // `&=`
    ASSIGN_XOR,     // `^=`
    ASSIGN_OR,      // `|=`
    ASSIGN_LSFT,    // `<<=`
    ASSIGN_RSFT,    // `>>=`
    ASSIGN_MULT,    // `*=`
    ASSIGN_DIV,     // `/=`
    ASSIGN_MOD,     // `%=`
    ASSIGN_PLUS,    // `+=`
    ASSIGN_MINUS,   // `-=`
    ASSIGN_EQ,      // `=`
    ASSIGN_COUNT,
} AssignKind;

typedef enum {
    EXPR_CHAR,      // character literal
    EXPR_STR,       // string literal
    EXPR_NUM,       // numeric literal
    EXPR_IDENT,     // identifier
    EXPR_CLIT,      // compound literal
    EXPR_UNOP,      // -x, ~x, !x, *x, &x, ++x, x++, --x, x--
    EXPR_BINOP,     // +, -, *, /, %, etc.
    EXPR_TERNOP,    // expr "?" expr ":" expr
    EXPR_FUNC_CALL, // name "(" args ")"
    EXPR_ASSIGN,    // Type name "=" expr
    EXPR_INDEX,     // name "[" expr "]"
    EXPR_FIELD,     // name "." name
    EXPR_ARROW,     // name "->" name
    EXPR_CAST,      // "(" Type ")" expr
    EXPR_SIZEOF_TY, // "sizeof(" Type ")"
    EXPR_SIZEOF_EX, // "sizeof" expr
    EXPR_ALIGNOF,   // "_Alignof(" Type ")"
    EXPR_COUNT,
} ExprKind;

// NOTE: `Symbol` is defined in scope.h which creates an inclusion cycle with
// this file. Forward declare it here to be able to use it.
typedef struct Symbol Symbol;
typedef struct Expr Expr;
struct Expr {
    ExprKind kind;
    Loc loc;
    union {
        char c;
        const char *str;
        int val;
        struct {
            const char *name;
            Symbol *sym;
        } ident;
        struct {
            UnopKind kind;
            Expr *operand;
        } unop;
        struct {
            BinopKind kind;
            Expr *lhs;
            Expr *rhs;
        } binop;
        struct {
            Expr *cond;
            Expr *then;
            Expr *_else;
        } ternop;
        struct {
            Expr *callee;
            size_t argc;
            Expr **args;
        } func_call;
        struct {
            AssignKind kind;
            Expr *var;
            Expr *value;
        } assign;
        struct {
            Expr *array;
            Expr *index;
        } index;
        struct {
            Expr *_struct;
            const char *field;
        } field;
        struct {
            Type *type;
            Expr *expr;
        } cast;
        Type *sizeof_ty;
        Expr *sizeof_expr;
        Type *alignof_ty;
    };
};

//
// Statements
//

typedef enum {
    STORAGE_NONE,
    STORAGE_TYPEDEF,
    STORAGE_EXTERN,
    STORAGE_STATIC,
    STORAGE_AUTO,
    STORAGE_REGISTER,
} StorageClass;

typedef enum {
    STMT_NULL,     // ";"
    STMT_EXPR,     // expr-stmt
    STMT_BLOCK,    // "{" compound-stmt "}"
    STMT_LABEL,    // ident ":" stmt
    STMT_DECL,     // int x = expr;
    STMT_WHILE,    // "while" "(" expr ")" stmt
    STMT_FOR,      // "for" "(" expr-stmt expr? ";" expr? ")" stmt
    STMT_DO,       // "do" stmt "while" "(" expr ")" ";"
    STMT_IF,       // "if" "(" expr ")" stmt ("else" stmt)?
    STMT_SWITCH,   // "switch" "(" expr ")" stmt
    STMT_CASE,     // "case" const-expr ("..." const-expr)? ":" stmt
    STMT_DEFAULT,  // "default" ":" stmt
    STMT_BREAK,    // "break" ";"
    STMT_CONT,     // "continue" ";"
    STMT_GOTO,     // "goto" (ident | "*" expr) ";"
    STMT_RET,      // "return" expr? ";"
    STMT_COUNT,
} StmtKind;

typedef struct Stmt Stmt;
struct Stmt {
    StmtKind kind;
    Loc loc;
    union {
        Expr *expr;
        struct {
            Stmt **stmts;
            size_t count;
        } block;
        struct {
            const char *name;
            Stmt *next;
        } label;
        const char *goto_label;
        struct {
            StorageClass storage;
            Type *ty;
            Expr *var;
            Expr *value;
        } decl;
        struct {
            Expr *cond;
            Stmt *body;
        } _while;
        struct {
            Expr *init;
            Expr *cond;
            Expr *step;
            Stmt *body;
        } _for;
        struct {
            Expr *cond;
            Stmt *then;
            Stmt *_else;
        } _if;
        struct {
            Expr *ctrl;
            Stmt *body;
        } _switch;
        struct {
            Expr *name;
            Stmt *body;
        } _case;
        Stmt *default_body;
        Expr *_return;
    };
};

//
// Declarations and translation units
//

typedef enum {
    DECL_VAR,
    DECL_STRUCT,
    DECL_ENUM,
    DECL_UNION,
    DECL_TYPEDEF,
    DECL_FUNC,
    DECL_COUNT,
} DeclKind;

typedef struct {
    DeclKind kind;
    Loc loc;
    union {
        Stmt var_decl;
        struct {
            const char *name;
            Stmt body;
        } _struct;
        struct {
            const char *name;
            const char **const_names;
            int *const_values;
            size_t const_count;
        } _enum;
        struct {
            const char *name;
            Stmt body;
        } _union;
        struct {
            Type *orig;
            Type *new;
        } _typedef;
        struct {
            Type *ret_type;
            const char *name;
            Type **param_types;
            const char **param_names;
            size_t param_count;
            bool is_variadic;
            Stmt body;
        } func;
    };
} Decl;

typedef struct {
    Arena *a;
    Decl *decls;
    size_t decls_count;
} TranslUnit;

#endif  // AST_H_
