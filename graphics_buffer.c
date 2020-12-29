#include "graphics_buffer.h"

#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

static void wl_buffer_release(void *data, struct wl_buffer *wl_buffer) {
    struct anthywl_graphics_buffer *buffer = data;
    buffer->in_use = false;
}

static struct wl_buffer_listener const wl_buffer_listener = {
    .release = wl_buffer_release,
};

static struct anthywl_graphics_buffer *anthywl_graphics_buffer_create(
    struct wl_shm *wl_shm, struct wl_list *buffers, int width, int height)
{
    struct anthywl_graphics_buffer *buffer = calloc(1, sizeof *buffer);

    buffer->width = width;
    buffer->height = height;
    buffer->stride =
        cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, buffer->width);
    buffer->size = buffer->width * buffer->stride * buffer->height;
    buffer->in_use = true;
    int fd = memfd_create("anthywl", MFD_CLOEXEC);
    int rc = ftruncate(fd, buffer->size); (void)rc;
    
    buffer->data =
        mmap(NULL, buffer->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    struct wl_shm_pool *pool = wl_shm_create_pool(wl_shm, fd, buffer->size);

    buffer->wl_buffer = wl_shm_pool_create_buffer(
        pool, 0, buffer->width, buffer->height,
        buffer->stride, WL_SHM_FORMAT_ARGB8888);

    close(fd);
    wl_shm_pool_destroy(pool);

    wl_buffer_add_listener(buffer->wl_buffer, &wl_buffer_listener, buffer);
    wl_list_insert(buffers, &buffer->link);

    buffer->cairo_surface = cairo_image_surface_create_for_data(
        buffer->data, CAIRO_FORMAT_ARGB32,
        buffer->width, buffer->height, buffer->stride);
    buffer->cairo = cairo_create(buffer->cairo_surface);

    return buffer;
}

void anthywl_graphics_buffer_destroy(
    struct anthywl_graphics_buffer *buffer)
{
    cairo_destroy(buffer->cairo);
    cairo_surface_destroy(buffer->cairo_surface);
    munmap(buffer->data, buffer->size);
    wl_buffer_destroy(buffer->wl_buffer);
    wl_list_remove(&buffer->link);
    free(buffer);
}

struct anthywl_graphics_buffer *anthywl_graphics_buffer_get(
    struct wl_shm *wl_shm, struct wl_list *buffers,
    int width, int height)
{
    bool found = false;

    struct anthywl_graphics_buffer *buffer, *tmp;
    wl_list_for_each_safe (buffer, tmp, buffers, link) {
        if (buffer->in_use)
            continue;
        if (buffer->width != width || buffer->height != height) {
            anthywl_graphics_buffer_destroy(buffer);
            continue;
        }
        found = true;
        break;
    }

    if (!found)
        buffer = anthywl_graphics_buffer_create(wl_shm, buffers, width, height);

    buffer->in_use = true;

    return buffer;
}

