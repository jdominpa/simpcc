#include "diag.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

const char *diag_level_as_str[] = {
    [DIAG_INFO] = "info",
    [DIAG_WARNING] = "warning",
    [DIAG_ERROR] = "error",
    [DIAG_FATAL] = "fatal error",
};

static void vdiag_report_at(DiagLevel level, Loc loc, const char *fmt, va_list va_args)
{
    fprintf(stderr, "%s:%zu:%zu: %s: ",
            loc.file_path, loc.line, loc.col,
            diag_level_as_str[level]);
    vfprintf(stderr, fmt, va_args);
    fprintf(stderr, "\n");
}

void diag_report_at(DiagLevel level, Loc loc, const char *fmt, ...)
{
    va_list va_args;
    va_start(va_args, fmt);
    vdiag_report_at(level, loc, fmt, va_args);
    va_end(va_args);
}

noreturn void diag_fatal(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    fprintf(stderr, "simpcc: fatal error: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");

    va_end(args);
    exit(1);
}

noreturn void diag_fatal_at(Loc loc, const char *fmt, ...)
{
    va_list va_args;
    va_start(va_args, fmt);
    vdiag_report_at(DIAG_FATAL, loc, fmt, va_args);
    va_end(va_args);
    exit(1);
}
