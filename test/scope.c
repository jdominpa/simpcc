#include "../src/scope.h"

#include "../src/ast_print.h"
#include "test.h"

static Type primitive_type(TypeKind kind)
{
    return (Type) { .kind = kind, .sign = SIGN_SIGNED };
}

static Symbol *new_symbol(SymbolKind kind, Namespace ns, const char *name,
                          Type ty)
{
    Symbol *sym = arena_alloc(&g_test_ctx.test_arena, Symbol);
    sym->ty = arena_alloc(&g_test_ctx.test_arena, Type);
    sym->kind = kind;
    sym->ns = ns;
    sym->loc = (Loc) { .file_path = "<test>", .line = 0, .col = 0 };
    sym->name = name;
    *sym->ty = ty;
    sym->depth = 0;
    return sym;
}

DEFINE_TEST(test_lookup_unknown_name)
{
    Scope sc = { 0 };
    EXPECT(scope_lookup_var(&sc, "a") == NULL,
           "expected variable `a` to be unknown");
    EXPECT(scope_lookup_tag(&sc, "a") == NULL,
           "expected tag `a` to be unknown");
    scope_free(&sc);
}

DEFINE_TEST(test_lookup_existing_symbol)
{
    Scope sc = { 0 };
    const char *name = "a";

    Symbol *sym = new_symbol(SYMBOL_VAR, NS_VAR, name, primitive_type(TYPE_INT));
    scope_add_sym(&sc, sym);

    Symbol *found = scope_lookup_var(&sc, name);
    EXPECT(found == sym, "expected to find variable `%s` in scope", name);
    EXPECT(scope_lookup_var(&sc, "b") == NULL,
           "expected variable `b` to be unknown");
    scope_free(&sc);
}

DEFINE_TEST(test_namespaces_do_not_collide)
{
    Scope sc = { 0 };
    const char *name = "a";

    Symbol *var = new_symbol(SYMBOL_VAR, NS_VAR, name, primitive_type(TYPE_INT));
    Symbol *tag = new_symbol(SYMBOL_ENUM, NS_TAG, name, primitive_type(TYPE_INT));
    scope_add_sym(&sc, var);
    scope_add_sym(&sc, tag);

    EXPECT(scope_lookup_var(&sc, name) == var,
           "expected `%s` to resolve to the variable in the ordinary namespace",
           name);
    EXPECT(scope_lookup_tag(&sc, name) == tag,
           "expected `%s` to resolve to the tag in the tag namespace", name);
    scope_free(&sc);
}

DEFINE_TEST(test_shadowing_symbol)
{
    Scope sc = { 0 };
    const char *name = "a";

    scope_add_sym(&sc, new_symbol(SYMBOL_VAR, NS_VAR, name, primitive_type(TYPE_INT)));
    scope_enter(&sc);
    scope_add_sym(&sc, new_symbol(SYMBOL_VAR, NS_VAR, name, primitive_type(TYPE_CHAR)));

    Symbol *found = scope_lookup_var(&sc, name);
    ASSERT(found != NULL, "expected variable `%s` to be visible in the inner scope",
           name);
    EXPECT(found->ty->kind == TYPE_CHAR,
           "expected inner variable `%s` of type `char` but got type `%s`", name,
           TYPE_TO_STR(found->ty));

    scope_exit(&sc);
    found = scope_lookup_var(&sc, name);
    ASSERT(found != NULL,
           "expected variable `%s` to be visible again after exiting the inner scope",
           name);
    EXPECT(found->ty->kind == TYPE_INT,
           "expected outer variable `%s` of type `int` but got type `%s`", name,
           TYPE_TO_STR(found->ty));
    scope_free(&sc);
}

DEFINE_TEST(test_unbind_symbol_on_scope_exit)
{
    Scope sc = { 0 };
    scope_add_sym(&sc, new_symbol(SYMBOL_VAR, NS_VAR, "outer", primitive_type(TYPE_INT)));
    scope_enter(&sc);
    scope_add_sym(&sc, new_symbol(SYMBOL_VAR, NS_VAR, "inner", primitive_type(TYPE_INT)));
    EXPECT(scope_lookup_var(&sc, "inner") != NULL,
           "expected `inner` to be visible in the scope that declares it");
    EXPECT(scope_lookup_var(&sc, "outer") != NULL,
           "expected `outer` to be visible from the inner scope");

    scope_exit(&sc);
    EXPECT(scope_lookup_var(&sc, "inner") == NULL,
           "expected `inner` to be unbound after exiting its scope");
    EXPECT(scope_lookup_var(&sc, "outer") != NULL,
           "expected `outer` to survive the exit of the inner scope");
    scope_free(&sc);
}

DEFINE_TEST(test_tag_scoping)
{
    Scope sc = { 0 };

    scope_enter(&sc);
    scope_add_sym(&sc, new_symbol(SYMBOL_ENUM, NS_TAG, "T", primitive_type(TYPE_INT)));
    EXPECT(scope_lookup_tag(&sc, "T") != NULL,
           "expected tag `T` to be visible in the scope that declares it");

    scope_exit(&sc);
    EXPECT(scope_lookup_tag(&sc, "T") == NULL,
           "expected tag `T` to be unbound after exiting its scope");
    scope_free(&sc);
}

DEFINE_TEST(test_sibling_scopes)
{
    Scope sc = { 0 };
    const char *name = "a";

    scope_enter(&sc);
    scope_add_sym(&sc, new_symbol(SYMBOL_VAR, NS_VAR, name, primitive_type(TYPE_INT)));
    scope_exit(&sc);

    scope_enter(&sc);
    scope_add_sym(&sc, new_symbol(SYMBOL_VAR, NS_VAR, name, primitive_type(TYPE_CHAR)));
    Symbol *found = scope_lookup_var(&sc, name);
    ASSERT(found != NULL, "expected `%s` to be visible in the second scope", name);
    EXPECT(found->ty->kind == TYPE_CHAR,
           "expected the second declaration of `%s` with type `char` but got type `%s`",
           name, TYPE_TO_STR(found->ty));
    scope_exit(&sc);

    EXPECT(sc.count == 0,
           "expected the scope to be empty but it still holds %zu symbol(s)",
           sc.count);
    scope_free(&sc);
}

//
// Fatal diagnostics
//

#ifndef _WIN32

DEFINE_TEST(test_redefinition_in_same_scope_is_fatal)
{
    Scope sc = { 0 };
    scope_add_sym(&sc, new_symbol(SYMBOL_VAR, NS_VAR, "a", primitive_type(TYPE_INT)));
    EXPECT_EXIT(1, {
        scope_add_sym(&sc,
                      new_symbol(SYMBOL_VAR, NS_VAR, "a", primitive_type(TYPE_INT)));
    });
    scope_free(&sc);
}

DEFINE_TEST(test_redefinition_as_different_kind_is_fatal)
{
    Scope sc = { 0 };
    scope_add_sym(&sc, new_symbol(SYMBOL_VAR, NS_VAR, "a", primitive_type(TYPE_INT)));
    EXPECT_EXIT(1, {
        scope_add_sym(
            &sc, new_symbol(SYMBOL_TYPEDEF, NS_VAR, "a", primitive_type(TYPE_INT)));
    });
    scope_free(&sc);
}

DEFINE_TEST(test_legal_redeclarations_are_not_fatal)
{
    Scope sc = { 0 };
    scope_add_sym(&sc, new_symbol(SYMBOL_VAR, NS_VAR, "a", primitive_type(TYPE_INT)));

    // Shadowing in a nested scope.
    EXPECT_EXIT(0, {
        scope_enter(&sc);
        scope_add_sym(&sc,
                      new_symbol(SYMBOL_VAR, NS_VAR, "a", primitive_type(TYPE_CHAR)));
    });

    // The same name in two scopes that do not nest.
    EXPECT_EXIT(0, {
        scope_enter(&sc);
        scope_add_sym(&sc,
                      new_symbol(SYMBOL_VAR, NS_VAR, "b", primitive_type(TYPE_INT)));
        scope_exit(&sc);
        scope_enter(&sc);
        scope_add_sym(&sc,
                      new_symbol(SYMBOL_VAR, NS_VAR, "b", primitive_type(TYPE_CHAR)));
    });
    scope_free(&sc);
}

#endif  // _WIN32

int main(void)
{
    g_test_ctx.test_arena = arena_init();

    RUN_TEST(test_lookup_unknown_name);
    RUN_TEST(test_lookup_existing_symbol);
    RUN_TEST(test_namespaces_do_not_collide);
    RUN_TEST(test_shadowing_symbol);
    RUN_TEST(test_unbind_symbol_on_scope_exit);
    RUN_TEST(test_tag_scoping);
    RUN_TEST(test_sibling_scopes);

#ifndef _WIN32
    RUN_TEST(test_redefinition_in_same_scope_is_fatal);
    RUN_TEST(test_redefinition_as_different_kind_is_fatal);
    RUN_TEST(test_legal_redeclarations_are_not_fatal);
#endif

    TEST_SUMMARY();
    return 0;
}
