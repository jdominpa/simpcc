#include "../src/parser.h"

#include <assert.h>
#include <string.h>

#include "../src/ast_print.h"
#include "../src/io.h"
#include "test.h"

// Formats `fmt` into `buf` and reports a failure if it did not fit. Returns
// whether the result is complete (a silently truncated path would make the test
// read or write the wrong file).
static bool format_path(char *buf, size_t size, const char *fmt, const char *arg)
{
    int n = snprintf(buf, size, fmt, arg);
    if (n < 0) {
        TEST_FAIL("could not format a path for '%s'", arg);
        return false;
    }
    if ((size_t) n >= size) {
        TEST_FAIL("path for '%s' needs %d bytes but only %zu are available",
                  arg, n, size);
        return false;
    }
    return true;
}

// Checks that all tokens in the parser have been consumed.
static void expect_input_consumed(Parser *p, const char *src)
{
    assert(p->pos < p->token_count);
    Token left = p->tokens[p->pos];
    EXPECT(left.kind == TK_EOF,
           "expected the whole input `%s` to be parsed but %s was left over",
           src, token_to_str(left));
}

static void expect_type(const char *src, const char *expected)
{
    Parser p = parser_init_from_src(&g_test_ctx.test_arena, src);
    char *got = NULL;
    size_t len = 0;
    FILE *f = open_memstream(&got, &len);
    print_type_compact(f, parse_type(&p));
    fclose(f);

    EXPECT(len == strlen(expected) && strcmp(got, expected) == 0,
           "expected type `%s` but got `%s`", expected, got);
    expect_input_consumed(&p, src);

    free(got);
    parser_free(&p);
}

static void expect_expr(const char *src, const char *expected)
{
    Parser p = parser_init_from_src(&g_test_ctx.test_arena, src);
    char *got = NULL;
    size_t len = 0;
    FILE *f = open_memstream(&got, &len);
    print_expr_compact(f, parse_expr(&p));
    fclose(f);

    EXPECT(len == strlen(expected) && strcmp(got, expected) == 0,
           "expected expression `%s` but got `%s`", expected, got);
    expect_input_consumed(&p, src);

    free(got);
    parser_free(&p);
}

static void expect_stmt_from_file(const char *src, const char *file_name)
{
    char path[512];
    // `TEST_DATA_DIR` is passed through `-DTEST_DATA_DIR` at compile time of
    // the unit tests (see nob.c)
    if (!format_path(path, sizeof path, TEST_DATA_DIR"%s", file_name)) return;
    char *expected;
    bool pass =
        read_entire_file(&g_test_ctx.test_arena, path, &expected, NULL);
    EXPECT(pass, "could not read expected output file '%s'", path);

    Parser p = parser_init_from_src(&g_test_ctx.test_arena, src);
    char *got = NULL;
    size_t len = 0;
    FILE *f = open_memstream(&got, &len);
    print_stmt(f, parse_stmt(&p), 0);
    fclose(f);
    expect_input_consumed(&p, src);
    if (pass) {
        pass = len == strlen(expected) && strcmp(got, expected) == 0;
        EXPECT(pass,
               "mismatch between expected and actual output (run: diff -u %s %s.actual)",
               path, path);
    }

    // Write .actual file with test output in case of test failure (mismatch or
    // missing golden file)
    if (!pass) {
        char output[1024];
        if (format_path(output, sizeof output, "%s.actual", path)) {
            FILE *out = fopen(output, "w");
            if (out == NULL) {
                TEST_FAIL("could not open test output file '%s'", output);
            } else {
                fwrite(got, 1, len, out);
                fclose(out);
            }
        }
    }

    free(got);
    parser_free(&p);
}

//
// Type tests
//

DEFINE_TEST(test_arith_types)
{
    expect_type("char", "(type char)");
    expect_type("signed char", "(type signed char)");
    expect_type("unsigned char", "(type unsigned char)");
    expect_type("int", "(type int)");
    expect_type("signed", "(type int)");
    expect_type("unsigned", "(type unsigned int)");
    expect_type("int *", "(type int *)");
    expect_type("int **", "(type int **)");
    expect_type("int[3][4]", "(type int[3][4])");
    expect_type("int[]", "(type int[])");
    expect_type("int *[3]", "(type int *[3])");
}

DEFINE_TEST(test_qualified_types)
{
    expect_type("const int", "(type const int)");
    expect_type("volatile int", "(type volatile int)");
    expect_type("int *restrict", "(type int *restrict)");
    expect_type("const volatile int", "(type const volatile int)");
    expect_type("volatile const int", "(type const volatile int)");
    expect_type("const int volatile", "(type const volatile int)");
    expect_type("const const int", "(type const int)");
    expect_type("const int *", "(type const int *)");
    expect_type("int *const", "(type int *const)");
}

//
// Expression tests
//
// Literals and identifiers
//

DEFINE_TEST(test_literals)
{
    expect_expr("0", "0");
    expect_expr("42", "42");
    expect_expr("1234567890", "1234567890");
    // TODO: leading zeros are not treated as octal yet, `007` is just 7
    expect_expr("007", "7");
    expect_expr("\"abc\"", "\"abc\"");
    expect_expr("\"\"", "\"\"");
    expect_expr("x", "x");
    expect_expr("foo_bar1", "foo_bar1");
}

//
// Precedence
//

DEFINE_TEST(test_precedence_arithmetic)
{
    expect_expr("a + b * c", "(binop + a (binop * b c))");
    expect_expr("a * b + c", "(binop + (binop * a b) c)");
    expect_expr("a - b / c", "(binop - a (binop / b c))");
    expect_expr("a / b - c", "(binop - (binop / a b) c)");
    expect_expr("a + b % c", "(binop + a (binop % b c))");
    expect_expr("a % b - c", "(binop - (binop % a b) c)");
    expect_expr("a + b - c", "(binop - (binop + a b) c)");
    expect_expr("a * b / c % d", "(binop % (binop / (binop * a b) c) d)");
}

DEFINE_TEST(test_precedence_shift)
{
    expect_expr("a << b * c", "(binop << a (binop * b c))");
    expect_expr("a >> b * c", "(binop >> a (binop * b c))");
    expect_expr("a + b << c", "(binop << (binop + a b) c)");
    expect_expr("a - b >> c", "(binop >> (binop - a b) c)");
    expect_expr("a << b + c", "(binop << a (binop + b c))");
    expect_expr("a >> b - c", "(binop >> a (binop - b c))");
}

DEFINE_TEST(test_precedence_relational_and_equality)
{
    expect_expr("a < b << c", "(binop < a (binop << b c))");
    expect_expr("a << b < c", "(binop < (binop << a b) c)");
    expect_expr("a > b >> c", "(binop > a (binop >> b c))");
    expect_expr("a == b < c", "(binop == a (binop < b c))");
    expect_expr("a < b == c", "(binop == (binop < a b) c)");
    expect_expr("a != b > c", "(binop != a (binop > b c))");
    expect_expr("a <= b >= c", "(binop >= (binop <= a b) c)");
}

DEFINE_TEST(test_precedence_bitwise)
{
    expect_expr("a & b == c", "(binop & a (binop == b c))");
    expect_expr("a == b & c", "(binop & (binop == a b) c)");
    expect_expr("a ^ b & c", "(binop ^ a (binop & b c))");
    expect_expr("a & b ^ c", "(binop ^ (binop & a b) c)");
    expect_expr("a | b ^ c", "(binop | a (binop ^ b c))");
    expect_expr("a ^ b | c", "(binop | (binop ^ a b) c)");
    expect_expr("a & b | c", "(binop | (binop & a b) c)");
}

DEFINE_TEST(test_precedence_logical)
{
    expect_expr("a && b | c", "(binop && a (binop | b c))");
    expect_expr("a | b && c", "(binop && (binop | a b) c)");
    expect_expr("a || b && c", "(binop || a (binop && b c))");
    expect_expr("a && b || c", "(binop || (binop && a b) c)");
}

DEFINE_TEST(test_precedence_ladder)
{
    expect_expr("a || b && c | d ^ e & f == g < h + i * j",
                "(binop || a (binop && b (binop | c (binop ^ d (binop & e "
                "(binop == f (binop < g (binop + h (binop * i j)))))))))");
}

DEFINE_TEST(test_precedence_ternary)
{
    expect_expr("a ? b : c", "(ternop a b c)");
    expect_expr("a || b ? c : d", "(ternop (binop || a b) c d)");
    expect_expr("a ? b || c : d", "(ternop a (binop || b c) d)");
    expect_expr("a ? b : c || d", "(ternop a b (binop || c d))");
}

DEFINE_TEST(test_precedence_assignment)
{
    expect_expr("a = b + c", "(assign = a (binop + b c))");
    expect_expr("a += b * c", "(assign += a (binop * b c))");
    expect_expr("a = b ? c : d", "(assign = a (ternop b c d))");
    // The third operand of `?:` is an assignment expression, so the `=` binds
    // inside the else branch rather than around the whole conditional.
    expect_expr("a ? b : c = d", "(ternop a b (assign = c d))");
    // The parser is purely syntactic: it does not reject a non-lvalue target.
    expect_expr("a + b = c", "(assign = (binop + a b) c)");
}

DEFINE_TEST(test_precedence_comma)
{
    expect_expr("a, b &= c", "(binop , a (assign &= b c))");
    expect_expr("a ^= b, c", "(binop , (assign ^= a b) c)");
}

//
// Prefix operators
//

DEFINE_TEST(test_prefix_operators)
{
    expect_expr("+a", "(unop + a)");
    expect_expr("-a", "(unop - a)");
    expect_expr("!a", "(unop ! a)");
    expect_expr("~a", "(unop ~ a)");
    expect_expr("*a", "(unop * a)");
    expect_expr("&a", "(unop & a)");
    expect_expr("++a", "(unop ++ (pre) a)");
    expect_expr("--a", "(unop -- (pre) a)");
}

DEFINE_TEST(test_repeated_prefix_operators)
{
    expect_expr("- -a", "(unop - (unop - a))");
    expect_expr("!!a", "(unop ! (unop ! a))");
    expect_expr("**a", "(unop * (unop * a))");
    expect_expr("~~a", "(unop ~ (unop ~ a))");
}

DEFINE_TEST(test_prefix_binds_tighter_than_binary)
{
    expect_expr("-a * b", "(binop * (unop - a) b)");
    expect_expr("-a + b", "(binop + (unop - a) b)");
    expect_expr("!a && b", "(binop && (unop ! a) b)");
    expect_expr("~a | b", "(binop | (unop ~ a) b)");
    expect_expr("*a + b", "(binop + (unop * a) b)");
    expect_expr("&a == b", "(binop == (unop & a) b)");
    expect_expr("++a * b", "(binop * (unop ++ (pre) a) b)");
    expect_expr("--a + b", "(binop + (unop -- (pre) a) b)");
}

//
// Postfix operators
//

DEFINE_TEST(test_postfix_operators)
{
    expect_expr("a++", "(unop ++ (post) a)");
    expect_expr("a--", "(unop -- (post) a)");
    expect_expr("a++ + b", "(binop + (unop ++ (post) a) b)");
}

// Postfix binds tighter than prefix: `*p++` is `*(p++)`, not `(*p)++`.
DEFINE_TEST(test_postfix_binds_tighter_than_prefix)
{
    expect_expr("*p++", "(unop * (unop ++ (post) p))");
    expect_expr("-a++", "(unop - (unop ++ (post) a))");
    expect_expr("++a++", "(unop ++ (pre) (unop ++ (post) a))");
    expect_expr("-a.b", "(unop - (field b a))");
    expect_expr("*a[0]", "(unop * (subscript a 0))");
    expect_expr("*f(x)", "(unop * (fn_call f x))");
}

//
// Function calls
//

DEFINE_TEST(test_function_calls)
{
    expect_expr("f()", "(fn_call f)");
    expect_expr("f(a)", "(fn_call f a)");
    expect_expr("f(a, b)", "(fn_call f a b)");
    expect_expr("f(a, b, c)", "(fn_call f a b c)");
    expect_expr("f(g(x))", "(fn_call f (fn_call g x))");
}

DEFINE_TEST(test_function_call_arguments)
{
    expect_expr("f(a + b)", "(fn_call f (binop + a b))");
    expect_expr("f(a, b + c)", "(fn_call f a (binop + b c))");
    expect_expr("f(a = b)", "(fn_call f (assign = a b))");
    expect_expr("f(a ? b : c)", "(fn_call f (ternop a b c))");
}

//
// Array subscript
//

DEFINE_TEST(test_array_subscript)
{
    expect_expr("a[0]", "(subscript a 0)");
    expect_expr("a[i + 1]", "(subscript a (binop + i 1))");
    expect_expr("a[b][c]", "(subscript (subscript a b) c)");
    expect_expr("a[f(x)]", "(subscript a (fn_call f x))");
    expect_expr("a[0] + b", "(binop + (subscript a 0) b)");
}

//
// Member access
//

DEFINE_TEST(test_member_access)
{
    expect_expr("a.b", "(field b a)");
    expect_expr("a->b", "(arrow b a)");
    expect_expr("a->b.c", "(field c (arrow b a))");
    expect_expr("a.b->c", "(arrow c (field b a))");
    expect_expr("a.b + c", "(binop + (field b a) c)");
}

DEFINE_TEST(test_postfix_chains)
{
    expect_expr("a[0].b", "(field b (subscript a 0))");
    expect_expr("a.b[0]", "(subscript (field b a) 0)");
    expect_expr("f(x)[0]", "(subscript (fn_call f x) 0)");
}

//
// Casts
//

DEFINE_TEST(test_casts)
{
    expect_expr("(char) x", "(cast (type char) x)");
    expect_expr("(signed char) x", "(cast (type signed char) x)");
    expect_expr("(unsigned char) x", "(cast (type unsigned char) x)");
    expect_expr("(int) x", "(cast (type int) x)");
    expect_expr("(long) x", "(cast (type long) x)");
    expect_expr("(void) x", "(cast (type void) x)");
    expect_expr("(unsigned) x", "(cast (type unsigned int) x)");
    expect_expr("(int *) x", "(cast (type int *) x)");
    expect_expr("(int) (char) x", "(cast (type int) (cast (type char) x))");
}

DEFINE_TEST(test_cast_binding)
{
    expect_expr("(char) a + b", "(binop + (cast (type char) a) b)");
    expect_expr("(double) x * y", "(binop * (cast (type double) x) y)");
    expect_expr("(int) -a", "(cast (type int) (unop - a))");
    expect_expr("(int) a.b", "(cast (type int) (field b a))");
}

//
// sizeof and _Alignof
//

DEFINE_TEST(test_sizeof)
{
    expect_expr("sizeof(int)", "(sizeof_type (type int))");
    expect_expr("sizeof(int *)", "(sizeof_type (type int *))");
    expect_expr("sizeof x", "(sizeof_value x)");
    expect_expr("sizeof a.b", "(sizeof_value (field b a))");
    expect_expr("sizeof -a", "(sizeof_value (unop - a))");
    expect_expr("sizeof x + y", "(binop + (sizeof_value x) y)");
    expect_expr("sizeof(int) + 1", "(binop + (sizeof_type (type int)) 1)");
}

DEFINE_TEST(test_alignof)
{
    expect_expr("_Alignof(int)", "(alignof (type int))");
    expect_expr("alignof(int)", "(alignof (type int))");
    expect_expr("_Alignof(double)", "(alignof (type double))");
    expect_expr("_Alignof(char *)", "(alignof (type char *))");
}

//
// Parentheses
//

DEFINE_TEST(test_parenthesized_expressions)
{
    expect_expr("(a)", "a");
    expect_expr("(a + b) * c", "(binop * (binop + a b) c)");
    expect_expr("a * (b + c)", "(binop * a (binop + b c))");
    expect_expr("(a + b) * (c + d)", "(binop * (binop + a b) (binop + c d))");
    expect_expr("-(a + b)", "(unop - (binop + a b))");
    expect_expr("f((a + b))", "(fn_call f (binop + a b))");
}

DEFINE_TEST(test_binop_assoc)
{
    // Right associative
    expect_expr("a &= b &= c", "(assign &= a (assign &= b c))");
    expect_expr("a ^= b ^= c", "(assign ^= a (assign ^= b c))");
    expect_expr("a |= b |= c", "(assign |= a (assign |= b c))");
    expect_expr("a <<= b <<= c", "(assign <<= a (assign <<= b c))");
    expect_expr("a >>= b >>= c", "(assign >>= a (assign >>= b c))");
    expect_expr("a *= b *= c", "(assign *= a (assign *= b c))");
    expect_expr("a /= b /= c", "(assign /= a (assign /= b c))");
    expect_expr("a %= b %= c", "(assign %= a (assign %= b c))");
    expect_expr("a += b += c", "(assign += a (assign += b c))");
    expect_expr("a -= b -= c", "(assign -= a (assign -= b c))");
    expect_expr("a = b = c", "(assign = a (assign = b c))");
    expect_expr("a ? b : c ? d : e", "(ternop a b (ternop c d e))");

    // Left associative
    expect_expr("a, b, c", "(binop , (binop , a b) c)");
    expect_expr("a || b || c", "(binop || (binop || a b) c)");
    expect_expr("a && b && c", "(binop && (binop && a b) c)");
    expect_expr("a | b | c", "(binop | (binop | a b) c)");
    expect_expr("a ^ b ^ c", "(binop ^ (binop ^ a b) c)");
    expect_expr("a & b & c", "(binop & (binop & a b) c)");
    expect_expr("a == b == c", "(binop == (binop == a b) c)");
    expect_expr("a != b != c", "(binop != (binop != a b) c)");
    expect_expr("a < b < c", "(binop < (binop < a b) c)");
    expect_expr("a <= b <= c", "(binop <= (binop <= a b) c)");
    expect_expr("a > b > c", "(binop > (binop > a b) c)");
    expect_expr("a >= b >= c", "(binop >= (binop >= a b) c)");
    expect_expr("a << b << c", "(binop << (binop << a b) c)");
    expect_expr("a >> b >> c", "(binop >> (binop >> a b) c)");
    expect_expr("a + b + c", "(binop + (binop + a b) c)");
    expect_expr("a - b - c", "(binop - (binop - a b) c)");
    expect_expr("a * b * c", "(binop * (binop * a b) c)");
    expect_expr("a / b / c", "(binop / (binop / a b) c)");
    expect_expr("a % b % c", "(binop % (binop % a b) c)");
    expect_expr("a.b.c", "(field c (field b a))");
    expect_expr("a->b->c", "(arrow c (arrow b a))");
    expect_expr("a++++", "(unop ++ (post) (unop ++ (post) a))");
    expect_expr("a----", "(unop -- (post) (unop -- (post) a))");
}

//
// Statement tests
//
// Empty statement
//

DEFINE_TEST(test_empty_statement)
{
    expect_stmt_from_file(";", "test_empty_stmt");
    expect_stmt_from_file("{ ;;; }", "test_multiple_empty_stmt");
}

//
// Block statements
//

DEFINE_TEST(test_block_statements)
{
    expect_stmt_from_file("{}", "test_empty_block_stmt");
    expect_stmt_from_file("{ break; }", "test_single_block_stmt");
    expect_stmt_from_file("{ break; continue; }", "test_multi_block_stmt");
    expect_stmt_from_file("{ { break; } }", "test_nested_block_stmt");
    expect_stmt_from_file("{ if (a) break; return 0; }", "test_mixed_block_stmt");
}

//
// Label statement
//

DEFINE_TEST(test_label_statements)
{
    expect_stmt_from_file("test_label: break;", "test_label_stmt");
    expect_stmt_from_file("{ test_label: }", "test_null_stmt_after_label");
}

//
// While statement
//

DEFINE_TEST(test_while_statement)
{
    expect_stmt_from_file("while (a) break;", "test_while_stmt");
    expect_stmt_from_file("while (a) { break; }", "test_while_block_stmt");
    expect_stmt_from_file("while (a || b) break;",
                          "test_while_complex_cond_stmt");
}

//
// Do/while statement
//

DEFINE_TEST(test_do_statement)
{
    expect_stmt_from_file("do break; while (a);", "test_do_stmt");
    expect_stmt_from_file("do { break; } while (a);", "test_do_block_stmt");
    expect_stmt_from_file("do break; while (a && b);", "test_do_complex_cond_stmt");
}

//
// If statements
//

DEFINE_TEST(test_if_statements)
{
    expect_stmt_from_file("if (a) break;", "test_if_stmt");
    expect_stmt_from_file("if (a) break; else continue;", "test_if_else_stmt");
    expect_stmt_from_file("if (a) { break; }", "test_if_block_stmt");
    expect_stmt_from_file("if (a) { break; } else { continue; }",
                          "test_if_else_block_stmt");
    expect_stmt_from_file("if (a && b) break;", "test_if_complex_cond_stmt");
}

DEFINE_TEST(test_if_statement_nesting)
{
    expect_stmt_from_file("if (a) if (b) break; else continue;",
                          "test_dangling_else_stmt");
    // `else if` is just an `if` statement in the else branch.
    expect_stmt_from_file("if (a) break; else if (b) continue; else return;",
                          "test_else_if_chain_stmt");
    expect_stmt_from_file("if (a) { if (b) break; else continue; }",
                          "test_if_in_block_stmt");
}

//
// Jump statements
//

DEFINE_TEST(test_jump_statements)
{
    expect_stmt_from_file("break;", "test_break_stmt");
    expect_stmt_from_file("continue;", "test_continue_stmt");
    expect_stmt_from_file("goto a;", "test_goto_stmt");
}

//
// Return statements
//

DEFINE_TEST(test_return_statements)
{
    expect_stmt_from_file("return;", "test_void_return_stmt");
    expect_stmt_from_file("return 0;", "test_nonvoid_return_stmt");
    expect_stmt_from_file("return a + b;", "test_return_expr_stmt");
    expect_stmt_from_file("return f(x);", "test_return_call_stmt");
}

//
// Fatal error paths
//
// These call diag_fatal_at(), which exits the process, so each one runs in a
// forked child. Skipped on Windows, which has no fork().
//

#ifndef _WIN32

DEFINE_TEST(test_void_cast_fatal_paths)
{
    EXPECT_EXIT(1, { expect_type("signed void", ""); });
    EXPECT_EXIT(1, { expect_type("unsigned void", ""); });
}

// A type name is a specifier-qualifier-list: it admits type specifiers and
// qualifiers, but not the storage class and function specifiers that a full
// declaration allows.
DEFINE_TEST(test_type_name_rejects_decl_specifiers)
{
    EXPECT_EXIT(1, { expect_type("typedef int", ""); });
    EXPECT_EXIT(1, { expect_type("extern int", ""); });
    EXPECT_EXIT(1, { expect_type("static int", ""); });
    EXPECT_EXIT(1, { expect_type("auto int", ""); });
    EXPECT_EXIT(1, { expect_type("register int", ""); });
    EXPECT_EXIT(1, { expect_type("inline int", ""); });
    EXPECT_EXIT(1, { expect_type("_Noreturn int", ""); });
    EXPECT_EXIT(1, { expect_expr("sizeof(inline int)", ""); });
    EXPECT_EXIT(1, { expect_expr("_Alignof(static int)", ""); });
}

DEFINE_TEST(test_missing_type_specifier_is_fatal)
{
    EXPECT_EXIT(1, { expect_type("const", ""); });
    EXPECT_EXIT(1, { expect_type("volatile", ""); });
    EXPECT_EXIT(1, { expect_type("const volatile", ""); });
    EXPECT_EXIT(1, { expect_expr("sizeof(const)", ""); });
    EXPECT_EXIT(1, { expect_expr("sizeof()", ""); });
}

#endif  // _WIN32

int main(void)
{
    g_test_ctx.test_arena = arena_init();

    // Type tests
    RUN_TEST(test_arith_types);
    RUN_TEST(test_qualified_types);

    // Expression tests
    RUN_TEST(test_literals);
    RUN_TEST(test_precedence_arithmetic);
    RUN_TEST(test_precedence_shift);
    RUN_TEST(test_precedence_relational_and_equality);
    RUN_TEST(test_precedence_bitwise);
    RUN_TEST(test_precedence_logical);
    RUN_TEST(test_precedence_ladder);
    RUN_TEST(test_precedence_ternary);
    RUN_TEST(test_precedence_assignment);
    RUN_TEST(test_precedence_comma);
    RUN_TEST(test_prefix_operators);
    RUN_TEST(test_repeated_prefix_operators);
    RUN_TEST(test_prefix_binds_tighter_than_binary);
    RUN_TEST(test_postfix_operators);
    RUN_TEST(test_postfix_binds_tighter_than_prefix);
    RUN_TEST(test_function_calls);
    RUN_TEST(test_function_call_arguments);
    RUN_TEST(test_array_subscript);
    RUN_TEST(test_member_access);
    RUN_TEST(test_postfix_chains);
    RUN_TEST(test_casts);
    RUN_TEST(test_cast_binding);
    RUN_TEST(test_sizeof);
    RUN_TEST(test_alignof);
    RUN_TEST(test_parenthesized_expressions);
    RUN_TEST(test_binop_assoc);

    // Statement tests
    RUN_TEST(test_empty_statement);
    RUN_TEST(test_block_statements);
    RUN_TEST(test_label_statements);
    RUN_TEST(test_while_statement);
    RUN_TEST(test_do_statement);
    RUN_TEST(test_if_statements);
    RUN_TEST(test_if_statement_nesting);
    RUN_TEST(test_jump_statements);
    RUN_TEST(test_return_statements);

    // Fatal path tests
#ifndef _WIN32
    RUN_TEST(test_void_cast_fatal_paths);
    RUN_TEST(test_type_name_rejects_decl_specifiers);
    RUN_TEST(test_missing_type_specifier_is_fatal);
#endif

    TEST_SUMMARY();
    return 0;
}
