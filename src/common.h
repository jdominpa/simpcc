#ifndef COMMON_H_
#define COMMON_H_

#define UNUSED(x) (void) (x)
#define UNREACHABLE(message)                                            \
    do {                                                                \
        fprintf(stderr, "%s:%d: UNREACHABLE: %s\n", __FILE__, __LINE__, \
                message);                                               \
        abort();                                                        \
    } while (0)
#define TODO(message)                                                      \
    do {                                                                   \
        fprintf(stderr, "%s:%d: TODO: %s\n", __FILE__, __LINE__, message); \
        abort();                                                           \
    } while (0)

#define MAX(a, b) (a) > (b) ? (a) : (b)
#define MIN(a, b) (a) > (b) ? (b) : (a)

#define DA_INIT_CAPACITY 4
// Dynamic array append: `da` is a pointer to a struct with `items`, `count`,
// and `capacity` members. Grows geometrically (starting at `DA_INIT_CAPACITY`),
// realloc failure is fatal with a caller-supplied message. Self-contained (only
// <stdio.h> and <stdlib.h>) so common.h stays dependency-free from the rest of
// the project.
#define da_append(da, item, err_msg)                                         \
    do {                                                                     \
        if ((da)->count + 1 >= (da)->capacity) {                             \
            (da)->capacity =                                                 \
                (da)->capacity == 0 ? DA_INIT_CAPACITY : (da)->capacity * 2; \
            (da)->items =                                                    \
                realloc((da)->items, (da)->capacity * sizeof(*(da)->items)); \
            if ((da)->items == NULL) {                                       \
                fprintf(stderr, "simpcc: fatal error: %s\n", (err_msg));     \
                exit(1);                                                     \
            }                                                                \
        }                                                                    \
        (da)->items[(da)->count++] = (item);                                 \
    } while (0)

#endif  // COMMON_H_
