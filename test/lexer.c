#include "../src/lexer.h"

#include <string.h>

#include "test.h"

//
// Helpers
//

// Takes the next token of the lexer `l` and checks whether its TokenKind is
// `kind` and its content is `token_text`. A wrong kind desyncs the token
// stream, so it aborts the test; a wrong text does not, so it does not.
static void check_next_token(Lexer *l, TokenKind kind, const char *token_text)
{
    Token t = lexer_next_token(l);
    ASSERT(t.kind == kind,
           "expected TokenKind `%s` but got `%s` (%.*s at <%zu:%zu>)",
           token_kind_to_str[kind], token_kind_to_str[t.kind], (int) t.len,
           t.start, t.loc.line, t.loc.col);
    if (token_text != NULL)
        EXPECT(t.len == strlen(token_text) && strncmp(t.start, token_text, t.len) == 0,
               "expected token content `%s` but got `%.*s`", token_text,
               (int) t.len, t.start);
}

// Like check_next_token(), but also checks where the token starts.
static void check_next_token_at(Lexer *l, TokenKind kind, const char *token_text,
                                size_t line, size_t col)
{
    Token t = lexer_next_token(l);
    ASSERT(t.kind == kind, "expected TokenKind `%s` but got `%s`",
           token_kind_to_str[kind], token_kind_to_str[t.kind]);
    if (token_text != NULL)
        EXPECT(t.len == strlen(token_text) && strncmp(t.start, token_text, t.len) == 0,
               "expected token content `%s` but got `%.*s`", token_text,
               (int) t.len, t.start);
    EXPECT(t.loc.line == line && t.loc.col == col,
           "expected `%.*s` at <%zu:%zu> but got <%zu:%zu>", (int) t.len,
           t.start, line, col, t.loc.line, t.loc.col);
}

// Lexes `src` on its own and checks that it produces exactly one token of
// TokenKind `kind` spanning the whole input. Each call uses a fresh lexer, so
// a failure does not invalidate the calls that follow: report them all instead
// of stopping at the first.
static void expect_lexes_as(const char *src, TokenKind kind)
{
    Lexer l = lexer_init_from_src(src);
    Token t = lexer_next_token(&l);
    EXPECT(t.kind == kind, "`%s`: expected TokenKind `%s` but got `%s`", src,
           token_kind_to_str[kind], token_kind_to_str[t.kind]);
    EXPECT(t.len == strlen(src), "`%s`: expected the whole input to be one token of length %zu but got %zu",
           src, strlen(src), t.len);
    Token after = lexer_next_token(&l);
    EXPECT(after.kind == TK_EOF, "`%s`: expected EOF after the token but got `%s`",
           src, token_kind_to_str[after.kind]);
}

//
// End of input and whitespace
//

DEFINE_TEST(test_empty_input)
{
    Lexer l = lexer_init_from_src("");
    check_next_token(&l, TK_EOF, NULL);
}

DEFINE_TEST(test_whitespace_only)
{
    Lexer l = lexer_init_from_src("   \n\n   ");
    check_next_token(&l, TK_EOF, NULL);
}

DEFINE_TEST(test_all_whitespace_kinds)
{
    Lexer l = lexer_init_from_src(" \t\v\f\r\n x");
    check_next_token(&l, TK_IDENT, "x");
    check_next_token(&l, TK_EOF, NULL);
}

DEFINE_TEST(test_eof_is_repeatable)
{
    Lexer l = lexer_init_from_src("x");
    check_next_token(&l, TK_IDENT, "x");
    for (int i = 0; i < 5; ++i)
        check_next_token(&l, TK_EOF, NULL);
}

//
// Comments
//

DEFINE_TEST(test_line_comment)
{
    Lexer l = lexer_init_from_src("// a comment\nx");
    check_next_token(&l, TK_IDENT, "x");
    check_next_token(&l, TK_EOF, NULL);
}

DEFINE_TEST(test_line_comment_at_end_of_input)
{
    Lexer l = lexer_init_from_src("x // a comment with no trailing newline");
    check_next_token(&l, TK_IDENT, "x");
    check_next_token(&l, TK_EOF, NULL);
}

DEFINE_TEST(test_block_comment)
{
    Lexer l = lexer_init_from_src("/* Test\nblock\n*comment */\n\n// Test inline comment\n// Test inline /* nested comment */");
    check_next_token(&l, TK_EOF, NULL);
}

DEFINE_TEST(test_comments_between_tokens)
{
    Lexer l = lexer_init_from_src("a/* one */b// two\nc");
    check_next_token(&l, TK_IDENT, "a");
    check_next_token(&l, TK_IDENT, "b");
    check_next_token(&l, TK_IDENT, "c");
    check_next_token(&l, TK_EOF, NULL);
}

DEFINE_TEST(test_adjacent_comments)
{
    Lexer l = lexer_init_from_src("/*one*//*two*/ // three\n/*four*/x");
    check_next_token(&l, TK_IDENT, "x");
    check_next_token(&l, TK_EOF, NULL);
}

DEFINE_TEST(test_block_comments_do_not_nest)
{
    Lexer l = lexer_init_from_src("/* /* still one comment */ x");
    check_next_token(&l, TK_IDENT, "x");
    check_next_token(&l, TK_EOF, NULL);
}

DEFINE_TEST(test_empty_block_comment)
{
    Lexer l = lexer_init_from_src("/**/x");
    check_next_token(&l, TK_IDENT, "x");
    check_next_token(&l, TK_EOF, NULL);
}

DEFINE_TEST(test_slash_outside_a_comment)
{
    Lexer l = lexer_init_from_src("a / b /= c");
    check_next_token(&l, TK_IDENT, "a");
    check_next_token(&l, TK_SLASH, "/");
    check_next_token(&l, TK_IDENT, "b");
    check_next_token(&l, TK_SLASH_EQ, "/=");
    check_next_token(&l, TK_IDENT, "c");
    check_next_token(&l, TK_EOF, NULL);
}

//
// Identifiers and keywords
//

DEFINE_TEST(test_identifiers)
{
    Lexer l = lexer_init_from_src("x foo _bar baz123 _ _9 CamelCase");
    check_next_token(&l, TK_IDENT, "x");
    check_next_token(&l, TK_IDENT, "foo");
    check_next_token(&l, TK_IDENT, "_bar");
    check_next_token(&l, TK_IDENT, "baz123");
    check_next_token(&l, TK_IDENT, "_");
    check_next_token(&l, TK_IDENT, "_9");
    check_next_token(&l, TK_IDENT, "CamelCase");
    check_next_token(&l, TK_EOF, NULL);
}

DEFINE_TEST(test_keywords)
{
    Lexer l = lexer_init_from_src("int void return if else while struct const _Bool static_assert");
    check_next_token(&l, TK_KW, "int");
    check_next_token(&l, TK_KW, "void");
    check_next_token(&l, TK_KW, "return");
    check_next_token(&l, TK_KW, "if");
    check_next_token(&l, TK_KW, "else");
    check_next_token(&l, TK_KW, "while");
    check_next_token(&l, TK_KW, "struct");
    check_next_token(&l, TK_KW, "const");
    check_next_token(&l, TK_KW, "_Bool");
    check_next_token(&l, TK_KW, "static_assert");
    check_next_token(&l, TK_EOF, NULL);
}

DEFINE_TEST(test_identifiers_starting_with_a_keyword)
{
    expect_lexes_as("integer", TK_IDENT);
    expect_lexes_as("iffy", TK_IDENT);
    expect_lexes_as("forx", TK_IDENT);
    expect_lexes_as("voidptr", TK_IDENT);
    expect_lexes_as("charlie", TK_IDENT);
    expect_lexes_as("doubled", TK_IDENT);
    expect_lexes_as("returns", TK_IDENT);
    expect_lexes_as("structure", TK_IDENT);
}

DEFINE_TEST(test_identifiers_ending_with_a_keyword)
{
    expect_lexes_as("xint", TK_IDENT);
    expect_lexes_as("myfor", TK_IDENT);
    expect_lexes_as("_if", TK_IDENT);
}

DEFINE_TEST(test_identifier_stops_at_non_identifier_char)
{
    Lexer l = lexer_init_from_src("foo+bar");
    check_next_token(&l, TK_IDENT, "foo");
    check_next_token(&l, TK_PLUS, "+");
    check_next_token(&l, TK_IDENT, "bar");
    check_next_token(&l, TK_EOF, NULL);
}

//
// Numbers
//

DEFINE_TEST(test_numbers)
{
    Lexer l = lexer_init_from_src("0 7 42 1234567890 007");
    check_next_token(&l, TK_NUM, "0");
    check_next_token(&l, TK_NUM, "7");
    check_next_token(&l, TK_NUM, "42");
    check_next_token(&l, TK_NUM, "1234567890");
    check_next_token(&l, TK_NUM, "007");
    check_next_token(&l, TK_EOF, NULL);
}

DEFINE_TEST(test_number_followed_by_identifier)
{
    Lexer l = lexer_init_from_src("123abc");
    check_next_token(&l, TK_NUM, "123");
    check_next_token(&l, TK_IDENT, "abc");
    check_next_token(&l, TK_EOF, NULL);
}

DEFINE_TEST(test_float_lexes_as_three_tokens_for_now)
{
    Lexer l = lexer_init_from_src("1.5");
    check_next_token(&l, TK_NUM, "1");
    check_next_token(&l, TK_DOT, ".");
    check_next_token(&l, TK_NUM, "5");
    check_next_token(&l, TK_EOF, NULL);
}

//
// Character literals
//

DEFINE_TEST(test_char_literals)
{
    Lexer l = lexer_init_from_src("'a' 'Z' '0' ' '");
    check_next_token(&l, TK_CHAR, "'a'");
    check_next_token(&l, TK_CHAR, "'Z'");
    check_next_token(&l, TK_CHAR, "'0'");
    check_next_token(&l, TK_CHAR, "' '");
    check_next_token(&l, TK_EOF, NULL);
}

DEFINE_TEST(test_char_literal_escapes)
{
    // Source text: '\n' '\'' '\\' '\0'
    Lexer l = lexer_init_from_src("'\\n' '\\'' '\\\\' '\\0'");
    check_next_token(&l, TK_CHAR, "'\\n'");
    check_next_token(&l, TK_CHAR, "'\\''");
    check_next_token(&l, TK_CHAR, "'\\\\'");
    check_next_token(&l, TK_CHAR, "'\\0'");
    check_next_token(&l, TK_EOF, NULL);
}

DEFINE_TEST(test_char_literal_next_to_other_tokens)
{
    Lexer l = lexer_init_from_src("c='x';");
    check_next_token(&l, TK_IDENT, "c");
    check_next_token(&l, TK_EQ, "=");
    check_next_token(&l, TK_CHAR, "'x'");
    check_next_token(&l, TK_SEMI, ";");
    check_next_token(&l, TK_EOF, NULL);
}

//
// String literals
//

DEFINE_TEST(test_string_literals)
{
    Lexer l = lexer_init_from_src("\"\" \"a\" \"hello world\"");
    check_next_token(&l, TK_STR, "\"\"");
    check_next_token(&l, TK_STR, "\"a\"");
    check_next_token(&l, TK_STR, "\"hello world\"");
    check_next_token(&l, TK_EOF, NULL);
}

DEFINE_TEST(test_string_literal_escapes)
{
    // Source text: "a\"b" "a\\" "tab\there"
    Lexer l = lexer_init_from_src("\"a\\\"b\" \"a\\\\\" \"tab\\there\"");
    check_next_token(&l, TK_STR, "\"a\\\"b\"");
    check_next_token(&l, TK_STR, "\"a\\\\\"");
    check_next_token(&l, TK_STR, "\"tab\\there\"");
    check_next_token(&l, TK_EOF, NULL);
}

DEFINE_TEST(test_comment_markers_inside_string)
{
    Lexer l = lexer_init_from_src("\"/* not a comment */\" x");
    check_next_token(&l, TK_STR, "\"/* not a comment */\"");
    check_next_token(&l, TK_IDENT, "x");
    check_next_token(&l, TK_EOF, NULL);
}

DEFINE_TEST(test_adjacent_strings)
{
    Lexer l = lexer_init_from_src("\"a\"\"b\"");
    check_next_token(&l, TK_STR, "\"a\"");
    check_next_token(&l, TK_STR, "\"b\"");
    check_next_token(&l, TK_EOF, NULL);
}

//
// Punctuators
//

DEFINE_TEST(test_single_char_adjecent_punctuators)
{
    Lexer l = lexer_init_from_src("(){}[];:.,...?~");
    check_next_token(&l, TK_OPAREN, "(");
    check_next_token(&l, TK_CPAREN, ")");
    check_next_token(&l, TK_OBRACE, "{");
    check_next_token(&l, TK_CBRACE, "}");
    check_next_token(&l, TK_OBRACK, "[");
    check_next_token(&l, TK_CBRACK, "]");
    check_next_token(&l, TK_SEMI, ";");
    check_next_token(&l, TK_COLON, ":");
    check_next_token(&l, TK_DOT, ".");
    check_next_token(&l, TK_COMMA, ",");
    check_next_token(&l, TK_ELLIPSIS, "...");
    check_next_token(&l, TK_QUESTION, "?");
    check_next_token(&l, TK_TILDE, "~");
    check_next_token(&l, TK_EOF, NULL);
}

DEFINE_TEST(test_every_punctuator_in_isolation)
{
    static const struct {
        const char *src;
        TokenKind kind;
    } punctuators[] = {
        { "(", TK_OPAREN },     { ")", TK_CPAREN },
        { "{", TK_OBRACE },     { "}", TK_CBRACE },
        { "[", TK_OBRACK },     { "]", TK_CBRACK },
        { ";", TK_SEMI },       { ":", TK_COLON },
        { ".", TK_DOT },        { ",", TK_COMMA },
        { "...", TK_ELLIPSIS }, { "?", TK_QUESTION },
        { "~", TK_TILDE },      { "+", TK_PLUS },
        { "++", TK_PLUS_PLUS }, { "+=", TK_PLUS_EQ },
        { "-", TK_MINUS },      { "--", TK_MINUS_MINUS },
        { "-=", TK_MINUS_EQ },  { "->", TK_MINUS_GT },
        { "*", TK_STAR },       { "*=", TK_STAR_EQ },
        { "/", TK_SLASH },      { "/=", TK_SLASH_EQ },
        { "%", TK_PERCENT },    { "%=", TK_PERCENT_EQ },
        { "&", TK_AMP },        { "&&", TK_AMP_AMP },
        { "&=", TK_AMP_EQ },    { "|", TK_PIPE },
        { "||", TK_PIPE_PIPE }, { "|=", TK_PIPE_EQ },
        { "^", TK_CARET },      { "^=", TK_CARET_EQ },
        { "!", TK_BANG },       { "!=", TK_BANG_EQ },
        { "=", TK_EQ },         { "==", TK_EQ_EQ },
        { "<", TK_LT },         { "<=", TK_LT_EQ },
        { "<<", TK_LT_LT },     { "<<=", TK_LT_LT_EQ },
        { ">", TK_GT },         { ">=", TK_GT_EQ },
        { ">>", TK_GT_GT },     { ">>=", TK_GT_GT_EQ },
    };

    for (size_t i = 0; i < sizeof(punctuators) / sizeof(*punctuators); ++i)
        expect_lexes_as(punctuators[i].src, punctuators[i].kind);
}

DEFINE_TEST(test_maximal_munch)
{
    Lexer l = lexer_init_from_src("+++ --- <<<= >>>= ==== !== ->- &&& |||");
    check_next_token(&l, TK_PLUS_PLUS, "++");
    check_next_token(&l, TK_PLUS, "+");
    check_next_token(&l, TK_MINUS_MINUS, "--");
    check_next_token(&l, TK_MINUS, "-");
    check_next_token(&l, TK_LT_LT, "<<");
    check_next_token(&l, TK_LT_EQ, "<=");
    check_next_token(&l, TK_GT_GT, ">>");
    check_next_token(&l, TK_GT_EQ, ">=");
    check_next_token(&l, TK_EQ_EQ, "==");
    check_next_token(&l, TK_EQ_EQ, "==");
    check_next_token(&l, TK_BANG_EQ, "!=");
    check_next_token(&l, TK_EQ, "=");
    check_next_token(&l, TK_MINUS_GT, "->");
    check_next_token(&l, TK_MINUS, "-");
    check_next_token(&l, TK_AMP_AMP, "&&");
    check_next_token(&l, TK_AMP, "&");
    check_next_token(&l, TK_PIPE_PIPE, "||");
    check_next_token(&l, TK_PIPE, "|");
    check_next_token(&l, TK_EOF, NULL);
}

// A three-character punctuator that is cut short by the end of input must fall
// back to the two-character one, not read past the buffer.
DEFINE_TEST(test_punctuator_truncated_by_end_of_input)
{
    expect_lexes_as("<<", TK_LT_LT);
    expect_lexes_as(">>", TK_GT_GT);
    expect_lexes_as("<", TK_LT);
    expect_lexes_as(">", TK_GT);
    expect_lexes_as("+", TK_PLUS);
    expect_lexes_as("-", TK_MINUS);
}

DEFINE_TEST(test_punctuators_without_separators)
{
    Lexer l = lexer_init_from_src("a>=b&&c!=d");
    check_next_token(&l, TK_IDENT, "a");
    check_next_token(&l, TK_GT_EQ, ">=");
    check_next_token(&l, TK_IDENT, "b");
    check_next_token(&l, TK_AMP_AMP, "&&");
    check_next_token(&l, TK_IDENT, "c");
    check_next_token(&l, TK_BANG_EQ, "!=");
    check_next_token(&l, TK_IDENT, "d");
    check_next_token(&l, TK_EOF, NULL);
}

//
// Invalid characters
//

DEFINE_TEST(test_invalid_characters)
{
    Lexer l = lexer_init_from_src("@#$`\\");
    check_next_token(&l, TK_INVALID, "@");
    check_next_token(&l, TK_INVALID, "#");
    check_next_token(&l, TK_INVALID, "$");
    check_next_token(&l, TK_INVALID, "`");
    check_next_token(&l, TK_INVALID, "\\");
    check_next_token(&l, TK_EOF, NULL);
}

// An invalid character must not stop the lexer from making progress.
DEFINE_TEST(test_lexing_continues_after_invalid_character)
{
    Lexer l = lexer_init_from_src("a @ b");
    check_next_token(&l, TK_IDENT, "a");
    check_next_token(&l, TK_INVALID, "@");
    check_next_token(&l, TK_IDENT, "b");
    check_next_token(&l, TK_EOF, NULL);
}

//
// Source locations
//

DEFINE_TEST(test_locations_on_one_line)
{
    Lexer l = lexer_init_from_src("a bb  ccc");
    check_next_token_at(&l, TK_IDENT, "a", 1, 1);
    check_next_token_at(&l, TK_IDENT, "bb", 1, 3);
    check_next_token_at(&l, TK_IDENT, "ccc", 1, 7);
    check_next_token_at(&l, TK_EOF, NULL, 1, 10);
}

DEFINE_TEST(test_locations_across_lines)
{
    Lexer l = lexer_init_from_src("a\nb\n\n  c");
    check_next_token_at(&l, TK_IDENT, "a", 1, 1);
    check_next_token_at(&l, TK_IDENT, "b", 2, 1);
    check_next_token_at(&l, TK_IDENT, "c", 4, 3);
    check_next_token_at(&l, TK_EOF, NULL, 4, 4);
}

DEFINE_TEST(test_locations_after_comments)
{
    Lexer l = lexer_init_from_src("/* c */ x\n// line\ny");
    check_next_token_at(&l, TK_IDENT, "x", 1, 9);
    check_next_token_at(&l, TK_IDENT, "y", 3, 1);
    check_next_token_at(&l, TK_EOF, NULL, 3, 2);
}

DEFINE_TEST(test_locations_after_multiline_comment)
{
    Lexer l = lexer_init_from_src("/*\n*/x");
    check_next_token_at(&l, TK_IDENT, "x", 2, 3);
    check_next_token_at(&l, TK_EOF, NULL, 2, 4);
}

DEFINE_TEST(test_locations_after_literals)
{
    Lexer l = lexer_init_from_src("\"ab\" 'c' 12 d");
    check_next_token_at(&l, TK_STR, "\"ab\"", 1, 1);
    check_next_token_at(&l, TK_CHAR, "'c'", 1, 6);
    check_next_token_at(&l, TK_NUM, "12", 1, 10);
    check_next_token_at(&l, TK_IDENT, "d", 1, 13);
}

//
// Token descriptions
//

// token_kind_to_str is indexed by TokenKind everywhere, so a missing entry is
// a null dereference waiting to happen.
DEFINE_TEST(test_token_kind_to_str_has_every_kind)
{
    for (int kind = 0; kind < TK_COUNT; ++kind)
        EXPECT(token_kind_to_str[kind] != NULL,
               "token_kind_to_str has no entry for TokenKind %d", kind);
}

DEFINE_TEST(test_token_to_str)
{
    Lexer l = lexer_init_from_src("foo");
    Token ident = lexer_next_token(&l);
    EXPECT(strcmp(token_to_str(ident), "identifier `foo`") == 0,
           "expected \"identifier `foo`\" but got \"%s\"", token_to_str(ident));

    Lexer l2 = lexer_init_from_src("(");
    Token oparen = lexer_next_token(&l2);
    EXPECT(strcmp(token_to_str(oparen), "`(`") == 0,
           "expected \"`(`\" but got \"%s\"", token_to_str(oparen));
}

//
// Fatal error paths
//
// These call diag_fatal_at(), which exits the process, so each one runs in a
// forked child. Skipped on Windows, which has no fork().
//

#ifndef _WIN32

DEFINE_TEST(test_unclosed_block_comment_is_fatal)
{
    EXPECT_EXIT(1, {
        Lexer l = lexer_init_from_src("/* never closed");
        lexer_next_token(&l);
    });
    EXPECT_EXIT(1, {
        Lexer l = lexer_init_from_src("x /* trailing");
        lexer_next_token(&l);
        lexer_next_token(&l);
    });
    // `/*/` is not a closed comment: the `/` is part of the opening marker.
    EXPECT_EXIT(1, {
        Lexer l = lexer_init_from_src("/*/");
        lexer_next_token(&l);
    });
}

DEFINE_TEST(test_unclosed_char_literal_is_fatal)
{
    EXPECT_EXIT(1, {
        Lexer l = lexer_init_from_src("'a");
        lexer_next_token(&l);
    });
    EXPECT_EXIT(1, {
        Lexer l = lexer_init_from_src("'");
        lexer_next_token(&l);
    });
    // A newline ends the literal early.
    EXPECT_EXIT(1, {
        Lexer l = lexer_init_from_src("'a\n'");
        lexer_next_token(&l);
    });
    // The backslash escapes the closing quote, so this literal is unclosed.
    EXPECT_EXIT(1, {
        Lexer l = lexer_init_from_src("'\\'");
        lexer_next_token(&l);
    });
}

DEFINE_TEST(test_unclosed_string_literal_is_fatal)
{
    EXPECT_EXIT(1, {
        Lexer l = lexer_init_from_src("\"abc");
        lexer_next_token(&l);
    });
    EXPECT_EXIT(1, {
        Lexer l = lexer_init_from_src("\"");
        lexer_next_token(&l);
    });
    EXPECT_EXIT(1, {
        Lexer l = lexer_init_from_src("\"abc\ndef\"");
        lexer_next_token(&l);
    });
    EXPECT_EXIT(1, {
        Lexer l = lexer_init_from_src("\"abc\\\"");
        lexer_next_token(&l);
    });
}

#endif  // _WIN32

int main(void)
{
    RUN_TEST(test_empty_input);
    RUN_TEST(test_whitespace_only);
    RUN_TEST(test_all_whitespace_kinds);
    RUN_TEST(test_eof_is_repeatable);

    RUN_TEST(test_line_comment);
    RUN_TEST(test_line_comment_at_end_of_input);
    RUN_TEST(test_block_comment);
    RUN_TEST(test_comments_between_tokens);
    RUN_TEST(test_adjacent_comments);
    RUN_TEST(test_block_comments_do_not_nest);
    RUN_TEST(test_empty_block_comment);
    RUN_TEST(test_slash_outside_a_comment);

    RUN_TEST(test_identifiers);
    RUN_TEST(test_keywords);
    RUN_TEST(test_identifiers_starting_with_a_keyword);
    RUN_TEST(test_identifiers_ending_with_a_keyword);
    RUN_TEST(test_identifier_stops_at_non_identifier_char);

    RUN_TEST(test_numbers);
    RUN_TEST(test_number_followed_by_identifier);
    RUN_TEST(test_float_lexes_as_three_tokens_for_now);

    RUN_TEST(test_char_literals);
    RUN_TEST(test_char_literal_escapes);
    RUN_TEST(test_char_literal_next_to_other_tokens);

    RUN_TEST(test_string_literals);
    RUN_TEST(test_string_literal_escapes);
    RUN_TEST(test_comment_markers_inside_string);
    RUN_TEST(test_adjacent_strings);

    RUN_TEST(test_single_char_adjecent_punctuators);
    RUN_TEST(test_every_punctuator_in_isolation);
    RUN_TEST(test_maximal_munch);
    RUN_TEST(test_punctuator_truncated_by_end_of_input);
    RUN_TEST(test_punctuators_without_separators);

    RUN_TEST(test_invalid_characters);
    RUN_TEST(test_lexing_continues_after_invalid_character);

    RUN_TEST(test_locations_on_one_line);
    RUN_TEST(test_locations_across_lines);
    RUN_TEST(test_locations_after_comments);
    RUN_TEST(test_locations_after_multiline_comment);
    RUN_TEST(test_locations_after_literals);

    RUN_TEST(test_token_kind_to_str_has_every_kind);
    RUN_TEST(test_token_to_str);

#ifndef _WIN32
    RUN_TEST(test_unclosed_block_comment_is_fatal);
    RUN_TEST(test_unclosed_char_literal_is_fatal);
    RUN_TEST(test_unclosed_string_literal_is_fatal);
#endif

    TEST_SUMMARY();
    return 0;
}
