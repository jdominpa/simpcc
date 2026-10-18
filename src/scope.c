#include "scope.h"

#include <assert.h>
#include <string.h>

#include "ast_print.h"
#include "common.h"

// TODO: move this function to a more appropriate module.
// Checks whether the two types `ty_1` and `ty_2` are the same type.
static bool type_equal(const Type *ty_1, const Type *ty_2)
{
    if (ty_1->quals != ty_2->quals) return false;
    // Unwrap typedef aliases
    while (ty_1->kind == TYPE_NAMED) ty_1 = ty_1->named.ty;
    while (ty_2->kind == TYPE_NAMED) ty_2 = ty_2->named.ty;
    if (ty_1->kind != ty_2->kind) return false;

    static_assert(TYPE_COUNT == 17, "type_equal: `TYPE_COUNT` value has changed");
    switch (ty_1->kind) {
    case TYPE_VOID:
    case TYPE_BOOL:
    case TYPE_CHAR:
    case TYPE_SHORT:
    case TYPE_INT:
    case TYPE_LONG:
    case TYPE_FLOAT:
    case TYPE_DOUBLE:
    case TYPE_LDOUBLE:
        return ty_1->sign == ty_2->sign;
    case TYPE_ENUM:
        TODO("type_equal: implement `TYPE_ENUM` type comparison");
    case TYPE_PTR:
        return type_equal(ty_1->ptr.base, ty_2->ptr.base);
    case TYPE_ARRAY:
        if (ty_1->array.has_size != ty_2->array.has_size ||
            (ty_1->array.has_size && ty_1->array.size != ty_2->array.size))
            return false;
        return type_equal(ty_1->array.base, ty_2->array.base);
    case TYPE_FUNC:
        if (ty_1->func.arg_count != ty_2->func.arg_count ||
            ty_1->func.is_variadic != ty_2->func.is_variadic ||
            !type_equal(ty_1->func.ret, ty_2->func.ret))
            return false;
        for (size_t i = 0; i < ty_1->func.arg_count; ++i)
            if (!type_equal(ty_1->func.args[i], ty_2->func.args[i]))
                return false;
        return true;
    case TYPE_VLA:
        TODO("type_equal: implement `TYPE_VLA` type comparison");
    case TYPE_STRUCT:
        TODO("type_equal: implement `TYPE_STRUCT` type comparison");
    case TYPE_UNION:
        TODO("type_equal: implement `TYPE_UNION` type comparison");
    case TYPE_NAMED:
        UNREACHABLE("type_equal: typedef aliases are unwrapped above");
    case TYPE_COUNT:
        break;
    }
    UNREACHABLE("type_equal");
}

// Enters a new scope inside the current one.
void scope_enter(Scope *sc)
{
    sc->depth++;
}

// Exits the inner most scope.
void scope_exit(Scope *sc)
{
    assert(sc->depth > 0 && "Attempted to exit top level scope");
    while (sc->count > 0 &&
           sc->items[sc->count - 1]->depth == sc->depth)
        sc->count--;
    sc->depth--;
}

// Releases the symbol table of `sc` and leaves it in the same state as a
// zero-initialised scope. The symbols themselves are owned by the arena they
// were allocated from and are not freed here.
void scope_free(Scope *sc)
{
    free(sc->items);
    sc->items = NULL;
    sc->count = 0;
    sc->capacity = 0;
    sc->depth = 0;
}

// Adds the symbol `sym` to the scope `sc`. The depth of `sym` gets set to the
// current depth of `sc`.
Symbol *scope_add_sym(Arena *a, Scope *sc, Symbol sym)
{
    sym.depth = sc->depth;
    for (size_t i = sc->count; i-- > 0;) {
        if (sc->items[i]->depth < sc->depth) break;
        if (sc->items[i]->ns == sym.ns &&
            strcmp(sc->items[i]->name, sym.name) == 0) {
            if (sc->items[i]->kind != sym.kind) {
                diag_fatal_at(sym.loc,
                              "redefinition of symbol `%s` as a different kind of symbol",
                              sym.name);
            } else if (!type_equal(sc->items[i]->ty, sym.ty)) {
                diag_fatal_at(sym.loc,
                              "redefinition of symbol `%s` with a different type: `%s` vs `%s`",
                              sym.name, TYPE_TO_STR(sc->items[i]->ty), TYPE_TO_STR(sym.ty));
            } else {
                if (sym.kind == SYMBOL_TYPEDEF) return sc->items[i];
                if (sym.kind == SYMBOL_FUNC &&
                    !(sc->items[i]->is_def && sym.is_def)) {
                    sc->items[i]->is_def |= sym.is_def;
                    return sc->items[i];
                }
                diag_fatal_at(sym.loc, "redefinition of symbol `%s`",
                              sym.name);
            }
        }
    }
    Symbol *arena_sym = arena_alloc(a, Symbol);
    *arena_sym = sym;
    da_append(sc, arena_sym, "could not append symbol to current scope");
    return arena_sym;
}

// Looks up a symbol `name` with namespace `ns`. Returns NULL if no such symbol
// exists.
static Symbol *scope_lookup_sym(const Scope *sc, Namespace ns, const char *name, size_t len)
{
    for (size_t i = sc->count; i-- > 0;) {
        Symbol *sym = sc->items[i];
        if (sym->ns == ns &&
            strlen(sym->name) == len &&
            strncmp(sym->name, name, len) == 0)
            return sym;
    }
    return NULL;
}

// Looks up a variable symbol `name` of length `len` or returns NULL if no such
// symbol exists.
Symbol *scope_lookup_var_n(const Scope *sc, const char *name, size_t len)
{
    return scope_lookup_sym(sc, NS_VAR, name, len);
}

// Looks up a tag symbol `name` of length `len` or returns NULL if no such
// symbol exists.
Symbol *scope_lookup_tag_n(const Scope *sc, const char *name, size_t len)
{
    return scope_lookup_sym(sc, NS_TAG, name, len);
}

// Looks up a variable symbol `name` or returns NULL if no such symbol exists.
Symbol *scope_lookup_var(const Scope *sc, const char *name)
{
    return scope_lookup_var_n(sc, name, strlen(name));
}

// Looks up a tag symbol `name` or returns NULL if no such symbol exists.
Symbol *scope_lookup_tag(const Scope *sc, const char *name)
{
    return scope_lookup_tag_n(sc, name, strlen(name));
}
