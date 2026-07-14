#include "lexer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"

static Loc lexer_get_loc(Lexer *l)
{
    return (Loc) {
        .file_path = l->file_path,
        .line = l->line + 1,
        .col = l->pos - l->bol + 1,
    };
}

static bool lexer_bump(Lexer *l)
{
    if (l->pos >= l->size)
        return false;
    if (l->src[l->pos++] == '\n') {
        l->bol = l->pos;
        l->line++;
    }
    return true;
}

static bool lexer_bump_bytes(Lexer *l, size_t n)
{
    while (n--)
        if (!lexer_bump(l))
            return false;
    return true;
}

static inline char lexer_peek_first(Lexer *l)
{
    return l->pos + 1 < l->size ? l->src[l->pos + 1] : '\0';
}

static inline char lexer_peek_second(Lexer *l)
{
    return l->pos + 2 < l->size ? l->src[l->pos + 2] : '\0';
}

static bool lexer_starts_with(Lexer *l, const char *prefix)
{
    size_t len = strlen(prefix);
    if (l->pos + len <= l->size) {
        for (size_t i = 0; i < len; ++i)
            if (l->src[l->pos + i] != prefix[i])
                return false;
        return true;
    }
    return false;
}

static inline bool is_ident_start(char c)
{
    return isalpha(c) || c == '_';
}

static inline bool is_ident_cont(char c)
{
    return isalnum(c) || c == '_';
}

static bool is_keyword(const char *symbol)
{
    const char *keywords[] = {
        "alignas", "alignof", "auto", "bool", "break", "case", "char",
        "const", "constexpr", "continue", "default", "do", "double", "else",
        "enum", "extern", "false", "float", "for", "goto", "if",
        "inline", "int", "long", "nullptr", "register", "restrict", "return",
        "short", "signed", "sizeof", "static", "static_assert", "struct", "switch",
        "thread_local", "true", "typedef", "typeof", "typeof_unqual", "union", "unsigned",
        "void", "volatile", "while", "_Alignas", "_Alignof", "_Atomic",
        "_BitInt", "_Bool", "_Complex", "_Decimal128", "_Decimal32", "_Decimal64",
        "_Generic", "_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local",
    };
    size_t keyword_count = sizeof(keywords) / sizeof(*keywords);
    for (size_t i = 0; i < keyword_count; ++i)
        if (strncmp(symbol, keywords[i], strlen(keywords[i])) == 0)
            return true;
    return false;
}

// Returns a human-readable description of the Token `t`. Identifiers, keywords,
// numbers, strings and characters include their own source text. Every other
// token kind fallsback to the result of the array `token_kind_to_str`.
const char *token_to_str(Token t)
{
    static char buf[128];
    switch (t.kind) {
    case TK_IDENT:
    case TK_KW:
    case TK_NUM:
    case TK_STR:
    case TK_CHAR:
        snprintf(buf, sizeof(buf), "%s `%.*s`",
                 token_kind_to_str[t.kind], (int) t.len, t.start);
        return buf;
    default:
        snprintf(buf, sizeof(buf), "`%s`",
                 token_kind_to_str[t.kind]);
        return buf;
    }
}

Token lexer_next_token(Lexer *l)
{
    Token t = { 0 };

    // Skip whitespace and comments
    for (;;) {
        while (l->pos < l->size && isspace(l->src[l->pos]))
            lexer_bump(l);
        if (l->pos < l->size && l->src[l->pos] == '/') {
            switch (lexer_peek_first(l)) {
            case '/':
                lexer_bump_bytes(l, 2);
                while (l->pos < l->size && l->src[l->pos] != '\r' &&
                       l->src[l->pos] != '\n')
                    lexer_bump(l);
                continue;
            case '*': {
                Loc comment_beg = lexer_get_loc(l);
                lexer_bump_bytes(l, 2);
                while (l->pos < l->size && !lexer_starts_with(l, "*/"))
                    lexer_bump(l);
                if (l->pos >= l->size)
                    diag_fatal_at(comment_beg, "unclosed comment block");
                lexer_bump_bytes(l, 2);
                continue;
            }
            default:
                break;
            }
        }
        break;
    }

    t.loc = lexer_get_loc(l);

    // EOF
    if (l->pos >= l->size) {
        t.kind = TK_EOF;
        t.start = l->src + l->size;
        return t;
    }

    t.start = l->src + l->pos;

    // Identifier/Keyword
    if (is_ident_start(l->src[l->pos])) {
        t.kind = TK_IDENT;
        while (l->pos < l->size && is_ident_cont(l->src[l->pos])) {
            t.len++;
            lexer_bump(l);
        }
        if (is_keyword(t.start))
            t.kind = TK_KW;
        return t;
    }

    // TODO: the code to lex string and character tokens is almost the same. It
    // should be combined
    // Character
    if (l->src[l->pos] == '\'') {
        t.kind = TK_CHAR;
        lexer_bump(l);
        while (l->pos < l->size && l->src[l->pos] != '\'') {
            if (l->src[l->pos] == '\n' || l->src[l->pos] == '\0')
                diag_fatal_at(t.loc, "unclosed character literal");
            if (l->src[l->pos] == '\\')
                lexer_bump(l);
            lexer_bump(l);
        }
        if (l->pos >= l->size)
            diag_fatal_at(t.loc, "unclosed character literal");
        lexer_bump(l);
        t.len = l->pos - (t.start - l->src);
        return t;
    }

    // String
    if (l->src[l->pos] == '"') {
        t.kind = TK_STR;
        lexer_bump(l);
        while (l->pos < l->size && l->src[l->pos] != '"') {
            if (l->src[l->pos] == '\n' || l->src[l->pos] == '\0')
                diag_fatal_at(t.loc, "unclosed string literal");
            if (l->src[l->pos] == '\\')
                lexer_bump(l);
            lexer_bump(l);
        }
        if (l->pos >= l->size)
            diag_fatal_at(t.loc, "unclosed string literal");
        lexer_bump(l);
        t.len = l->pos - (t.start - l->src);
        return t;
    }

    // Number
    if (isdigit(l->src[l->pos])) {
        t.kind = TK_NUM;
        // TODO: handle non-integer numbers
        while (l->pos < l->size && isdigit(l->src[l->pos])) {
            t.len++;
            lexer_bump(l);
        }
        return t;
    }

#define MAKE_TOKEN(k, length)          \
    do {                               \
        t.kind = (k);                  \
        t.len = (length);              \
        lexer_bump_bytes(l, (length)); \
        return t;                      \
    } while (0)

    char c = l->src[l->pos];
    char first = lexer_peek_first(l);
    char second = lexer_peek_second(l);

    switch (c) {
    case '(':
        MAKE_TOKEN(TK_OPAREN, 1);
        break;
    case ')':
        MAKE_TOKEN(TK_CPAREN, 1);
        break;
    case '{':
        MAKE_TOKEN(TK_OBRACE, 1);
        break;
    case '}':
        MAKE_TOKEN(TK_CBRACE, 1);
        break;
    case '[':
        MAKE_TOKEN(TK_OBRACK, 1);
        break;
    case ']':
        MAKE_TOKEN(TK_CBRACK, 1);
        break;
    case ';':
        MAKE_TOKEN(TK_SEMI, 1);
        break;
    case ':':
        MAKE_TOKEN(TK_COLON, 1);
        break;
    case '.':
        MAKE_TOKEN(TK_DOT, 1);
        break;
    case ',':
        MAKE_TOKEN(TK_COMMA, 1);
        break;
    case '?':
        MAKE_TOKEN(TK_QUESTION, 1);
        break;
    case '+':
        if (first == '=')
            MAKE_TOKEN(TK_PLUS_EQ, 2);
        else if (first == '+')
            MAKE_TOKEN(TK_PLUS_PLUS, 2);
        else
            MAKE_TOKEN(TK_PLUS, 1);
        break;
    case '-':
        if (first == '=')
            MAKE_TOKEN(TK_MINUS_EQ, 2);
        else if (first == '-')
            MAKE_TOKEN(TK_MINUS_MINUS, 2);
        else if (first == '>')
            MAKE_TOKEN(TK_MINUS_GT, 2);
        else
            MAKE_TOKEN(TK_MINUS, 1);
        break;
    case '*':
        if (first == '=')
            MAKE_TOKEN(TK_STAR_EQ, 2);
        else
            MAKE_TOKEN(TK_STAR, 1);
        break;
    case '/':
        if (first == '=')
            MAKE_TOKEN(TK_SLASH_EQ, 2);
        else
            MAKE_TOKEN(TK_SLASH, 1);
        break;
    case '%':
        if (first == '=')
            MAKE_TOKEN(TK_PERCENT_EQ, 2);
        else
            MAKE_TOKEN(TK_PERCENT, 1);
        break;
    case '~':
        MAKE_TOKEN(TK_TILDE, 1);
        break;
    case '&':
        if (first == '=')
            MAKE_TOKEN(TK_AMP_EQ, 2);
        else if (first == '&')
            MAKE_TOKEN(TK_AMP_AMP, 2);
        else
            MAKE_TOKEN(TK_AMP, 1);
        break;
    case '|':
        if (first == '=')
            MAKE_TOKEN(TK_PIPE_EQ, 2);
        else if (first == '|')
            MAKE_TOKEN(TK_PIPE_PIPE, 2);
        else
            MAKE_TOKEN(TK_PIPE, 1);
        break;
    case '^':
        if (first == '=')
            MAKE_TOKEN(TK_CARET_EQ, 2);
        else
            MAKE_TOKEN(TK_CARET, 1);
        break;
    case '!':
        if (first == '=')
            MAKE_TOKEN(TK_BANG_EQ, 2);
        else
            MAKE_TOKEN(TK_BANG, 1);
        break;
    case '=':
        if (first == '=')
            MAKE_TOKEN(TK_EQ_EQ, 2);
        else
            MAKE_TOKEN(TK_EQ, 1);
        break;
    case '<':
        if (first == '<')
            if (second == '=')
                MAKE_TOKEN(TK_LT_LT_EQ, 3);
            else
                MAKE_TOKEN(TK_LT_LT, 2);
        else if (first == '=')
            MAKE_TOKEN(TK_LT_EQ, 2);
        else
            MAKE_TOKEN(TK_LT, 1);
        break;
    case '>':
        if (first == '>')
            if (second == '=')
                MAKE_TOKEN(TK_GT_GT_EQ, 3);
            else
                MAKE_TOKEN(TK_GT_GT, 2);
        else if (first == '=')
            MAKE_TOKEN(TK_GT_EQ, 2);
        else
            MAKE_TOKEN(TK_GT, 1);
        break;
    }

#undef MAKE_TOKEN

    t.len = 1;
    lexer_bump(l);
    return t;
}

// Accepts a file path and the source code of the file. Returns an initialized
// lexer.
Lexer lexer_init_from_src(const char *src)
{
    return (Lexer) {
        .file_path = NULL,
        .src = src,
        .size = strlen(src),
    };
}

// Accepts an arena where the source code of a file will be stored and the file
// path. Returns the initialized lexer.
Lexer lexer_init_from_file_path(Arena *a, const char *file_path)
{
    /* Read file */
    FILE *f = fopen(file_path, "rb");
    if (f == NULL)
        diag_fatal("could not open file '%s' for parsing", file_path);
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *src = arena_alloc_many(a, char, size + 1);
    if (src == NULL)
        diag_fatal("could not allocate memory to read file '%s'", file_path);
    size_t size_read = fread(src, 1, size, f);
    if (size_read != size)
        diag_fatal("expected %zu bytes from file '%s' but got %zu", size,
                   file_path, size_read);
    fclose(f);
    src[size] = '\0';

    Lexer l = lexer_init_from_src(src);
    l.file_path = file_path;
    return l;
}
