#include "arena.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

#define CHUNK_MIN_SIZE (4 * 1024)        /* 4 KB */
#define CHUNK_MAX_SIZE (2 * 1024 * 1024) /* 2 MB */

static Chunk *arena_new_chunk(Arena *a, size_t size)
{
    Chunk *c = (Chunk *) malloc(sizeof(Chunk));
    if (c == NULL) {
        fprintf(stderr, "arena: error: could not allocate new chunk\n");
        abort();
    }

    size_t chunk_size = MIN(MAX(a->next_chunk_size, CHUNK_MIN_SIZE), CHUNK_MAX_SIZE);
    if (size > chunk_size)
        chunk_size = size;
    c->start = malloc(chunk_size);
    if (c->start == NULL) {
        fprintf(stderr, "arena: error: could not allocate memory for new chunk\n");
        abort();
    }
    c->prev = NULL;
    c->size = chunk_size;
    c->offset = 0;

    a->next_chunk_size = MIN(a->next_chunk_size * 2, CHUNK_MAX_SIZE);
    return c;
}

/* Initialize the arena `a` with a single chunk of size CHUNK_MIN_SIZE */
Arena arena_init(void)
{
    Arena a;
    a.next_chunk_size = CHUNK_MIN_SIZE;
    a.current = arena_new_chunk(&a, CHUNK_MIN_SIZE);
    return a;
}

/* Allocate `size` bytes on the arena `a` with alignment `align` and return a
   pointer to the start of the allocated bytes */
void *arena_alloc_aligned(Arena *a, size_t size, size_t align)
{
    if (a == NULL || a->current == NULL) {
        fprintf(stderr, "arena: error: null pointer to arena or chunk found during allocation\n");
        abort();
    }

    size_t aligned_offset = (a->current->offset + align - 1) & ~(align - 1);
    if (aligned_offset + size > a->current->size) {
        Chunk *c = arena_new_chunk(a, size);
        c->prev = a->current;
        a->current = c;
        aligned_offset = 0;
    }

    void *ptr = a->current->start + aligned_offset;
    a->current->offset = aligned_offset + size;
    return ptr;
}

const char *arena_strndup(Arena *a, const char *str, size_t len)
{
    char *copy = arena_alloc_many(a, char, len + 1);
    memcpy(copy, str, len);
    copy[len] = '\0';
    return copy;
}

const char *arena_strdup(Arena *a, const char *str)
{
    return arena_strndup(a, str, strlen(str));
}

static void arena_free_chunk(Chunk *c)
{
    free(c->start);
    free(c);
}

/* Reset the arena `a` by freeing all chunks except the current one, setting the
   offset to 0 and resetting the chunk size to CHUNK_SIZE_MIN */
void arena_reset(Arena *a)
{
    if (a == NULL || a->current == NULL) {
        fprintf(stderr, "arena: error: could not reset arena due to null pointer arena or chunk\n");
        abort();
    }
    Chunk *c = a->current->prev;
    while (c) {
        Chunk *prev = c->prev;
        arena_free_chunk(c);
        c = prev;
    }
    a->current->prev = NULL;
    a->current->offset = 0;
    a->next_chunk_size = CHUNK_MIN_SIZE;
}

/* Free all chunks of `a` and set `a->current` to NULL */
void arena_free(Arena *a)
{
    if (a == NULL || a->current == NULL) {
        fprintf(stderr, "arena: error: could not free arena due to null pointer arena or chunk\n");
        abort();
    }
    while (a->current) {
        Chunk *prev = a->current->prev;
        arena_free_chunk(a->current);
        a->current = prev;
    }
    a->next_chunk_size = 0;
}
