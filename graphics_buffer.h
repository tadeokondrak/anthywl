#pragma once

#include <stdbool.h>

#include <cairo.h>
#include <wayland-client.h>

struct anthywl_graphics_buffer {
    struct wl_list link;
    struct anthywl_state *state;
    struct wl_buffer *wl_buffer;
    int width, height, stride;
    unsigned char *data;
    size_t size;
    bool in_use;
    cairo_t *cairo;
    cairo_surface_t *cairo_surface;
};

struct anthywl_graphics_buffer *anthywl_graphics_buffer_get(
    struct wl_shm *wl_shm, struct wl_list *buffers, int width, int height);
void anthywl_graphics_buffer_destroy(struct anthywl_graphics_buffer *buffer);
