#pragma once

#include <stddef.h>

struct anthywl_buffer {
    char *text;
    size_t len;
    size_t pos;
};

void anthywl_buffer_init(struct anthywl_buffer *);
void anthywl_buffer_destroy(struct anthywl_buffer *);
void anthywl_buffer_clear(struct anthywl_buffer *);
void anthywl_buffer_append(struct anthywl_buffer *, char const *);
void anthywl_buffer_delete_backwards(struct anthywl_buffer *, size_t);
void anthywl_buffer_delete_forwards(struct anthywl_buffer *, size_t);
void anthywl_buffer_move_left(struct anthywl_buffer *);
void anthywl_buffer_move_right(struct anthywl_buffer *);
void anthywl_buffer_convert_romaji(struct anthywl_buffer *);
