#include "../src/scope.h"

#include "../src/ast_print.h"
#include "test.h"

static Type *primitive_type(TypeKind kind)
{
    Type *ty = arena_alloc(&g_test_ctx.test_arena, Type);
    *ty = (Type) { 0 };
    ty->kind = kind;
    ty->sign = SIGN_SIGNED;
    return ty;
}

static Type *function_type(Type *ret)
{
    Type *ty = arena_alloc(&g_test_ctx.test_arena, Type);
    *ty = (Type) { .kind = TYPE_FUNC, .func = { .ret = ret } };
    return ty;
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

    Symbol *sym = scope_add_sym(&g_test_ctx.test_arena, &sc, (Symbol) {
        .kind = SYMBOL_VAR,
        .ns = NS_VAR,
        .name = name,
        .ty = primitive_type(TYPE_INT),
    });

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

    Symbol *var = scope_add_sym(&g_test_ctx.test_arena, &sc, (Symbol) {
        .kind = SYMBOL_VAR,
        .ns = NS_VAR,
        .name = name,
        .ty = primitive_type(TYPE_INT),
    });
    Symbol *tag = scope_add_sym(&g_test_ctx.test_arena, &sc, (Symbol) {
        .kind = SYMBOL_ENUM,
        .ns = NS_TAG,
        .name = name,
        .ty = primitive_type(TYPE_INT),
    });

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

    scope_add_sym(&g_test_ctx.test_arena, &sc, (Symbol) {
        .kind = SYMBOL_VAR,
        .ns = NS_VAR,
        .name = name,
        .ty = primitive_type(TYPE_INT),
    });
    scope_enter(&sc);
    scope_add_sym(&g_test_ctx.test_arena, &sc, (Symbol) {
        .kind = SYMBOL_VAR,
        .ns = NS_VAR,
        .name = name,
        .ty = primitive_type(TYPE_CHAR),
    });

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
    scope_add_sym(&g_test_ctx.test_arena, &sc, (Symbol) {
        .kind = SYMBOL_VAR,
        .ns = NS_VAR,
        .name = "outer",
        .ty = primitive_type(TYPE_INT),
    });
    scope_enter(&sc);
    scope_add_sym(&g_test_ctx.test_arena, &sc, (Symbol) {
        .kind = SYMBOL_VAR,
        .ns = NS_VAR,
        .name = "inner",
        .ty = primitive_type(TYPE_INT),
    });
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
    scope_add_sym(&g_test_ctx.test_arena, &sc, (Symbol) {
        .kind = SYMBOL_ENUM,
        .ns = NS_TAG,
        .name = "T",
        .ty = primitive_type(TYPE_INT),
    });
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
    scope_add_sym(&g_test_ctx.test_arena, &sc, (Symbol) {
        .kind = SYMBOL_VAR,
        .ns = NS_VAR,
        .name = name,
        .ty = primitive_type(TYPE_INT),
    });
    scope_exit(&sc);

    scope_enter(&sc);
    scope_add_sym(&g_test_ctx.test_arena, &sc, (Symbol) {
        .kind = SYMBOL_VAR,
        .ns = NS_VAR,
        .name = name,
        .ty = primitive_type(TYPE_CHAR),
    });
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

// Every declaration of one entity has to end up sharing a single `Symbol`, or
// a later pass resolving a call cannot reach the definition's symbol. This is
// invisible to the parser's output tests, which only compare printed trees.
DEFINE_TEST(test_redeclaration_reuses_one_symbol)
{
    Scope sc = { 0 };
    Type *ty = function_type(primitive_type(TYPE_INT));

    Symbol *proto = scope_add_sym(&g_test_ctx.test_arena, &sc, (Symbol) {
        .kind = SYMBOL_FUNC,
        .ns = NS_VAR,
        .name = "f",
        .ty = ty,
        .is_def = false,
    });
    Symbol *def = scope_add_sym(&g_test_ctx.test_arena, &sc, (Symbol) {
        .kind = SYMBOL_FUNC,
        .ns = NS_VAR,
        .name = "f",
        .ty = ty,
        .is_def = true,
    });
    Symbol *again = scope_add_sym(&g_test_ctx.test_arena, &sc, (Symbol) {
        .kind = SYMBOL_FUNC,
        .ns = NS_VAR,
        .name = "f",
        .ty = ty,
        .is_def = false,
    });

    EXPECT(proto == def && def == again,
           "expected every redeclaration of `f` to return the same symbol");
    EXPECT(sc.count == 1,
           "expected one entry in the scope for `f` but found %zu", sc.count);
    EXPECT(scope_lookup_var(&sc, "f") == proto,
           "expected the looked up `f` to be the symbol returned when declaring it");
    // A declaration after the definition must not clear the definition flag.
    EXPECT(proto->is_def,
           "expected `f` to still be marked as defined");
    scope_free(&sc);
}

//
// Fatal diagnostics
//

#ifndef _WIN32

DEFINE_TEST(test_redefinition_as_different_kind_is_fatal)
{
    Scope sc = { 0 };
    scope_add_sym(&g_test_ctx.test_arena, &sc, (Symbol) {
        .kind = SYMBOL_VAR,
        .ns = NS_VAR,
        .name = "a",
        .ty = primitive_type(TYPE_INT),
    });
    EXPECT_EXIT(1, {
        scope_add_sym(&g_test_ctx.test_arena, &sc, (Symbol) {
            .kind = SYMBOL_TYPEDEF,
            .ns = NS_VAR,
            .name = "a",
            .ty = primitive_type(TYPE_INT),
        });
    });
    scope_free(&sc);
}

DEFINE_TEST(test_second_function_definition_is_fatal)
{
    Scope sc = { 0 };
    Type *ty = function_type(primitive_type(TYPE_INT));
    scope_add_sym(&g_test_ctx.test_arena, &sc, (Symbol) {
        .kind = SYMBOL_FUNC,
        .ns = NS_VAR,
        .name = "f",
        .ty = ty,
        .is_def = true,
    });
    EXPECT_EXIT(1, {
        scope_add_sym(&g_test_ctx.test_arena, &sc, (Symbol) {
            .kind = SYMBOL_FUNC,
            .ns = NS_VAR,
            .name = "f",
            .ty = ty,
            .is_def = true,
        });
    });
    scope_free(&sc);
}

DEFINE_TEST(test_legal_redeclarations_are_not_fatal)
{
    Scope sc = { 0 };
    scope_add_sym(&g_test_ctx.test_arena, &sc, (Symbol) {
        .kind = SYMBOL_VAR,
        .ns = NS_VAR,
        .name = "a",
        .ty = primitive_type(TYPE_INT),
    });

    // Shadowing in a nested scope.
    EXPECT_EXIT(0, {
        scope_enter(&sc);
        scope_add_sym(&g_test_ctx.test_arena, &sc, (Symbol) {
            .kind = SYMBOL_VAR,
            .ns = NS_VAR,
            .name = "a",
            .ty = primitive_type(TYPE_CHAR),
        });
    });

    // The same name in two scopes that do not nest.
    EXPECT_EXIT(0, {
        scope_enter(&sc);
        scope_add_sym(&g_test_ctx.test_arena, &sc, (Symbol) {
            .kind = SYMBOL_VAR,
            .ns = NS_VAR,
            .name = "b",
            .ty = primitive_type(TYPE_INT),
        });
        scope_exit(&sc);
        scope_enter(&sc);
        scope_add_sym(&g_test_ctx.test_arena, &sc, (Symbol) {
            .kind = SYMBOL_VAR,
            .ns = NS_VAR,
            .name = "b",
            .ty = primitive_type(TYPE_CHAR),
        });
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
    RUN_TEST(test_redeclaration_reuses_one_symbol);

#ifndef _WIN32
    RUN_TEST(test_redefinition_as_different_kind_is_fatal);
    RUN_TEST(test_legal_redeclarations_are_not_fatal);
    RUN_TEST(test_second_function_definition_is_fatal);
#endif

    TEST_SUMMARY();
    return 0;
}
