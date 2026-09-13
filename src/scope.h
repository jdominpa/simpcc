#ifndef SCOPE_H_
#define SCOPE_H_

#include <stdint.h>

#include "ast.h"

typedef enum {
    SYMBOL_TYPEDEF,
    SYMBOL_VAR,
    SYMBOL_FUNC,
    SYMBOL_ENUM,
    SYMBOL_CONST,
    SYMBOL_COUNT,
} SymbolKind;

typedef enum {
    NS_VAR,
    NS_TAG,
} Namespace;

// NOTE: `Symbol` is declared in ast.h to avoid an inclusion cycle
struct Symbol {
    SymbolKind kind;
    Namespace ns;
    Loc loc;
    const char *name;
    Type *ty;
    uint32_t depth;
};

typedef struct {
    Symbol **items;
    size_t count;
    size_t capacity;
    uint32_t depth;
} Scope;

void scope_enter(Scope *sc);
void scope_exit(Scope *sc);
void scope_free(Scope *sc);
void scope_add_sym(Scope *sc, Symbol *sym);
Symbol *scope_lookup_var_n(const Scope *sc, const char *name, size_t len);
Symbol *scope_lookup_tag_n(const Scope *sc, const char *name, size_t len);
Symbol *scope_lookup_var(const Scope *sc, const char *name);
Symbol *scope_lookup_tag(const Scope *sc, const char *name);

#endif  // SCOPE_H_
