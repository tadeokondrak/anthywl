#include "keymap.h"

#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <sys/mman.h>
#include <unistd.h>

#include <wayland-util.h>
#include <xkbcommon/xkbcommon.h>

static unsigned char const *utf8_simple(unsigned char const *s, long *c) {
    unsigned char const *next;
    if (s[0] < 0x80) {
        *c = s[0];
        next = s + 1;
    } else if ((s[0] & 0xe0) == 0xc0) {
        *c = ((long)(s[0] & 0x1f) <<  6) |
             ((long)(s[1] & 0x3f) <<  0);
        next = s + 2;
    } else if ((s[0] & 0xf0) == 0xe0) {
        *c = ((long)(s[0] & 0x0f) << 12) |
             ((long)(s[1] & 0x3f) <<  6) |
             ((long)(s[2] & 0x3f) <<  0);
        next = s + 3;
    } else if ((s[0] & 0xf8) == 0xf0 && (s[0] <= 0xf4)) {
        *c = ((long)(s[0] & 0x07) << 18) |
             ((long)(s[1] & 0x3f) << 12) |
             ((long)(s[2] & 0x3f) <<  6) |
             ((long)(s[3] & 0x3f) <<  0);
        next = s + 4;
    } else {
        *c = -1; // invalid
        next = s + 1; // skip this byte
    }
    if (*c >= 0xd800 && *c <= 0xdfff)
        *c = -1; // surrogate half
    return next;
}


static int compare_keysym(void const *a, void const *b) {
    return *(xkb_keysym_t *)a - *(xkb_keysym_t *)b;
}

bool anthywl_make_keymap(char const *string,
    int *out_keymap_fd, size_t *out_keymap_size,
    uint32_t **out_keys_seq, size_t *out_keys_size)
{
    struct wl_array keys;
    wl_array_init(&keys);

    xkb_keysym_t keysyms[247] = {0};
    size_t keysyms_len = 0;

    {
        unsigned char const *s = (unsigned char const *)string;
        long c;
        while (*s) {
            s = utf8_simple(s, &c);
            if (c < 0) continue;
            xkb_keysym_t keysym = xkb_utf32_to_keysym((uint32_t)c);
            uint32_t *found = bsearch(
                &keysym, keysyms, keysyms_len, sizeof(uint32_t), compare_keysym);
            if (!found) {
                found = &keysyms[keysyms_len++];
                *found = keysym;
                qsort(keysyms, keysyms_len, sizeof(uint32_t), compare_keysym);
            }
        }
    }

    {
        unsigned char const *s = (unsigned char const *)string;
        long c;
        while (*s) {
            s = utf8_simple(s, &c);
            if (c < 0) continue;
            xkb_keysym_t keysym = xkb_utf32_to_keysym((uint32_t)c);
            uint32_t *found = bsearch(
                &keysym, keysyms, keysyms_len, sizeof(uint32_t), compare_keysym);
            assert(found);
            uint32_t found_index = found - keysyms;
            *(uint32_t *)wl_array_add(&keys, sizeof(uint32_t)) =
                found_index + 2;
        }
    }

    int fd = memfd_create("anthywl-keymap", MFD_CLOEXEC);
    if (fd == -1) {
        perror("memfd_create");
        return false;
    }

    FILE *f = fdopen(fd, "w");
    if (f == NULL) {
        perror("fdopen");
        close(fd);
        return false;
    }

    fputs("xkb_keymap {\n", f);
    fputs("\txkb_keycodes {\n", f);
    fputs("\t\tminimum = 8;\n", f);
    fputs("\t\tmaximum = 255;\n", f);
    for (size_t i = 0; i < keysyms_len; i++) {
        fprintf(f, "\t\t<C%zu> = %zu;\n", i + 2, i + 8 + 2);
    }
    fputs("\t};\n", f);
    fputs("\txkb_types {\n", f);
    fputs("\t\tvirtual_modifiers _;\n", f);
    fputs("\t};\n", f);
    fputs("\txkb_compatibility {\n", f);
    fputs("\t\tinterpret None { action = NoAction(); };\n", f);
    fputs("\t};\n", f);
    fputs("\txkb_symbols {\n", f);
    for (size_t i = 0; i < keysyms_len; i++) {
        char sym_name[256];
        xkb_keysym_get_name(keysyms[i], sym_name, sizeof(sym_name));
        fprintf(f, "\t\tkey <C%zu> { [ %s ] };\n", i + 2, sym_name);
    }
    fputs("\t};\n", f);
    fputs("};\n", f);
    fputc(0, f);

    if (fflush(f) != 0) {
        perror("fflush");
        fclose(f);
        return false;
    }

    long keymap_size = ftell(f);

    int duplicated_fd = dup(fd);
    if (duplicated_fd == -1) {
        perror("dup");
        fclose(f);
        return false;
    }

    if (fclose(f) != 0) {
        perror("fclose");
        close(duplicated_fd);
        close(fd);
        return false;
    }


    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    char *map = mmap(NULL, keymap_size, PROT_READ, MAP_PRIVATE, duplicated_fd, 0);
    assert(map);
    struct xkb_keymap *keymap = xkb_keymap_new_from_string(
        context, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, keymap_size);
    assert(keymap);


    *out_keymap_fd = duplicated_fd;
    *out_keymap_size = keymap_size;
    *out_keys_seq = keys.data;
    *out_keys_size = keys.size / sizeof(uint32_t);

    return true;
}
