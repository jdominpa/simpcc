#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "arena.h"
#include "ast.h"
#include "lexer.h"
#include "parser.h"

#define DA_INIT_CAPACITY 256
#define da_append(da, item)                                                                       \
    do {                                                                                          \
        if ((da)->count + 1 >= (da)->capacity) {                                                  \
            (da)->capacity = (da)->capacity == 0 ? DA_INIT_CAPACITY : (da)->capacity * 2;         \
            (da)->items = realloc((da)->items, (da)->capacity * sizeof(*(da)->items));            \
            if ((da)->items == NULL) {                                                            \
                fprintf(stderr, "simpcc: error: could not allocate memory for dynamic array\n");  \
                abort();                                                                          \
            }                                                                                     \
        }                                                                                         \
        (da)->items[(da)->count++] = (item);                                                      \
    } while (0)

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} StringArray;

static void print_tokens(Lexer *l)
{
    Token t = { 0 };
    size_t cur_line = 1;
    do {
        t = lexer_next_token(l);
        if (cur_line != t.loc.line) {
            printf("\n");
            cur_line = t.loc.line;
        } else {
            printf(" ");
        }
        printf("`%.*s`", (int) t.len, t.start);
    } while (t.kind != TK_EOF);
}

static const char *shift(int *argc, char ***argv)
{
    assert(*argc > 0);
    const char *arg = *argv[0];
    *argc -= 1;
    *argv += 1;
    return arg;
}

static void usage(int exit_status)
{
    fprintf(stderr, "Usage: simpcc [options] <file>\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --help    Display this information.\n");
    fprintf(stderr, "  -E        Preprocesses and lex only; do not compile, "
                    "assemble or link.\n");
    exit(exit_status);
}

int main(int argc, char **argv)
{
    const char *prog_name = "simpcc";
    shift(&argc, &argv);  // skip program name

    StringArray input_files = { 0 };
    bool opt_E = false;

    while (argc > 0) {
        const char *arg = shift(&argc, &argv);
        if (!strcmp(arg, "--help")) {
            usage(0);
        } else if (!strcmp(arg, "-E")) {
            opt_E = true;
            continue;
        } else if (arg[0] == '-' && arg[1] != '\0') {
            fprintf(stderr,
                    "%s: error: unrecognized command-line option '%s'\n",
                    prog_name, arg);
            usage(1);
        } else {
            da_append(&input_files, arg);
            continue;
        }
    }

    if (input_files.count == 0) {
        fprintf(stderr, "%s: error: no input files provided\n", prog_name);
        usage(1);
    }

    for (size_t i = 0; i < input_files.count; ++i) {
        const char *input_file = input_files.items[i];

        if (opt_E) {
            Arena lexer_arena = arena_init();
            Lexer l = lexer_init_from_file_path(&lexer_arena, input_file);
            print_tokens(&l);
            arena_free(&lexer_arena);
            return 0;
        }

        // AST arena
        Arena ast_arena = arena_init();

        // Parse translation unit
        Parser p = parser_init_from_file_path(&ast_arena, input_file);
        parse_transl_unit(&p);

        // (possible IR)
        // Compile to assembly
        // Link

        arena_free(&ast_arena);
    }

    return 0;
}
