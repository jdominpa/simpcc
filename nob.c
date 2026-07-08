#define NOB_IMPLEMENTATION
#include "nob.h"

#define CFLAGS "-Wall", "-Wextra", "-ggdb"

#define BUILD_DIR "build/"
#define SRC_DIR "src/"

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    if (!nob_mkdir_if_not_exists(BUILD_DIR)) return 1;
    Nob_Cmd cmd = { 0 };
    const char *src_files[] = {
        "arena",
        "ast_print",
        "simpcc",
        "diag",
        "lexer",
        "parser",
    };

    nob_cmd_append(&cmd, "cc", CFLAGS);
    nob_cmd_append(&cmd, "-o", BUILD_DIR"simpcc");
    for (size_t i = 0; i < NOB_ARRAY_LEN(src_files); ++i)
        nob_cmd_append(&cmd, nob_temp_sprintf(SRC_DIR"%s.c", src_files[i]));

    if (!nob_cmd_run(&cmd)) return 1;
    return 0;
}
