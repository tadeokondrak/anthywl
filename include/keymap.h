#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool anthywl_make_keymap(char const *string,
    int *out_keymap_fd, size_t *out_keymap_size,
    uint32_t **out_keys, size_t *out_keys_size);
