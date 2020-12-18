#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/mman.h>
#include <poll.h>
#include <unistd.h>

#include <anthy/anthy.h>
#include <cairo.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "input-method-unstable-v2-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"

struct anthywl_state {
    bool running;
    struct wl_display *wl_display;
    struct wl_registry *wl_registry;
    struct wl_compositor *wl_compositor;
    struct wl_shm *wl_shm;
    struct zwp_input_method_manager_v2 *zwp_input_method_manager_v2;
    struct zwp_virtual_keyboard_manager_v1 *zwp_virtual_keyboard_manager_v1;
    struct wl_list seats;
    struct wl_list buffers;
    struct wl_list timers;
};

struct anthywl_timer {
    struct wl_list link;
    struct timespec time;
    void (*callback)(struct anthywl_timer *timer);
};

struct anthywl_buffer {
    char *text;
    size_t len;
    size_t pos;
};

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

struct anthywl_seat {
    struct wl_list link;
    struct anthywl_state *state;
    struct wl_seat *wl_seat;

    bool are_protocols_initted;
    struct zwp_input_method_v2 *zwp_input_method_v2;
    struct zwp_input_method_keyboard_grab_v2 *zwp_input_method_keyboard_grab_v2;
    struct zwp_virtual_keyboard_v1 *zwp_virtual_keyboard_v1;

    struct xkb_context *xkb_context;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;

    // wl_seat
    char *name;

    // zwp_input_method_v2
    bool pending_activate, active;
    char *pending_surrounding_text, *surrounding_text;
    uint32_t pending_surrounding_text_cursor, surrounding_text_cursor;
    uint32_t pending_surrounding_text_anchor, surrounding_text_anchor;
    uint32_t pending_text_change_cause, text_change_cause;
    uint32_t pending_content_type_hint, content_type_hint;
    uint32_t pending_content_type_purpose, content_type_purpose;
    uint32_t done_events_received;

    // zwp_input_method_keyboard_grab_v2 
    uint32_t repeat_rate;
    uint32_t repeat_delay;
    xkb_keycode_t pressed[64];
    xkb_keycode_t repeating_keycode;
    struct anthywl_timer repeat_timer;

    // composing
    bool is_composing;
    struct anthywl_buffer buffer;

    // selecting 
    bool is_selecting;
    int current_segment;
    int segment_count;
    int *selected_candidates;
    struct anthy_context *anthy_context;

    // popup
    struct wl_surface *wl_surface;
    struct zwp_input_popup_surface_v2 *zwp_input_popup_surface_v2;
    bool show_preedit;
};

static void wl_buffer_release(void *data, struct wl_buffer *wl_buffer) {
    struct anthywl_graphics_buffer *buffer = data;
    buffer->in_use = false;
}

static struct wl_buffer_listener const wl_buffer_listener = {
    .release = wl_buffer_release,
};

static struct anthywl_graphics_buffer *anthywl_graphics_buffer_create(
    struct anthywl_state *state, int width, int height)
{
    struct anthywl_graphics_buffer *buffer = calloc(1, sizeof(*buffer));

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

    struct wl_shm_pool *pool =
        wl_shm_create_pool(state->wl_shm, fd, buffer->size);

    buffer->wl_buffer = wl_shm_pool_create_buffer(
        pool, 0, buffer->width, buffer->height,
        buffer->stride, WL_SHM_FORMAT_ARGB8888);

    close(fd);
    wl_shm_pool_destroy(pool);

    wl_buffer_add_listener(buffer->wl_buffer, &wl_buffer_listener, buffer);
    wl_list_insert(&state->buffers, &buffer->link);

    buffer->cairo_surface = cairo_image_surface_create_for_data(
        buffer->data, CAIRO_FORMAT_ARGB32,
        buffer->width, buffer->height, buffer->stride);
    buffer->cairo = cairo_create(buffer->cairo_surface);

    return buffer;
}

static void anthywl_graphics_buffer_destroy(
    struct anthywl_graphics_buffer *buffer)
{
    cairo_destroy(buffer->cairo);
    cairo_surface_destroy(buffer->cairo_surface);
    munmap(buffer->data, buffer->size);
    wl_buffer_destroy(buffer->wl_buffer);
    wl_list_remove(&buffer->link);
    free(buffer);
}

static struct anthywl_graphics_buffer *anthywl_get_graphics_buffer(
    struct anthywl_state *state, int width, int height)
{
    bool found = false;

    struct anthywl_graphics_buffer *buffer, *tmp;
    wl_list_for_each_safe (buffer, tmp, &state->buffers, link) {
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
        buffer = anthywl_graphics_buffer_create(state, width, height);

    buffer->in_use = true;

    return buffer;
}

static void zwp_input_popup_surface_v2_text_input_rectangle(void *data,
    struct zwp_input_popup_surface_v2 *zwp_input_popup_surface_v2,
    int32_t x, int32_t y, int32_t width, int32_t height)
{
}

static struct zwp_input_popup_surface_v2_listener const
    zwp_input_popup_surface_v2_listener =
{
    .text_input_rectangle = zwp_input_popup_surface_v2_text_input_rectangle,
};

#define BORDER (1.0)
#define PADDING (5.0)

static struct anthywl_graphics_buffer *anthywl_seat_composing_draw_popup(
    struct anthywl_seat *seat)
{
    cairo_surface_t *recording_cairo_surface =
        cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA, NULL);
    cairo_t *recording_cairo = cairo_create(recording_cairo_surface);

    PangoLayout *layout = pango_cairo_create_layout(recording_cairo);

    double x = BORDER + PADDING, y = BORDER + PADDING, max_x = 0;
    cairo_move_to(recording_cairo, x, y);

    {
        pango_layout_set_text(layout, seat->buffer.text, -1);
        PangoRectangle rect;
        pango_layout_get_extents(layout, NULL, &rect);
        max_x = (double)rect.width / PANGO_SCALE > max_x
            ? (double)rect.width / PANGO_SCALE
            : max_x;
        y += (double)rect.height / PANGO_SCALE;
        cairo_set_source_rgba(recording_cairo, 1.0, 1.0, 1.0, 1.0);
        pango_cairo_update_layout(recording_cairo, layout);
        pango_cairo_show_layout(recording_cairo, layout);
        cairo_move_to(recording_cairo, x, y);
    }

    max_x += BORDER * 2.0 + PADDING * 2.0;
    y += BORDER + PADDING;

    double half_border = BORDER / 2.0;
    cairo_move_to(recording_cairo, half_border, half_border);
    cairo_line_to(recording_cairo, max_x - half_border, half_border);
    cairo_line_to(recording_cairo, max_x - half_border, y - half_border);
    cairo_line_to(recording_cairo, half_border, y - half_border);
    cairo_line_to(recording_cairo, half_border, half_border);
    cairo_set_line_width(recording_cairo, BORDER);
    cairo_set_source_rgba(recording_cairo, 1.0, 1.0, 1.0, 1.0);
    cairo_stroke(recording_cairo);

    double surface_x, surface_y, surface_width, surface_height;
    cairo_recording_surface_ink_extents(recording_cairo_surface,
        &surface_x, &surface_y, &surface_width, &surface_height);

    struct anthywl_graphics_buffer *buffer =
        anthywl_get_graphics_buffer(seat->state, surface_width, surface_height);
    cairo_set_source_rgba(buffer->cairo, 0.0, 0.0, 0.0, 1.0);
    cairo_paint(buffer->cairo);
    cairo_set_source_surface(buffer->cairo, recording_cairo_surface, 0.0, 0.0);
    cairo_paint(buffer->cairo);

    return buffer;
}

static struct anthywl_graphics_buffer *anthywl_seat_selecting_draw_popup(
    struct anthywl_seat *seat)
{
    cairo_surface_t *recording_cairo_surface =
        cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA, NULL);
    cairo_t *recording_cairo = cairo_create(recording_cairo_surface);

    PangoLayout *layout = pango_cairo_create_layout(recording_cairo);

    double x = BORDER + PADDING, y = BORDER + PADDING;
    double line_y = 0;
    double max_x = 0;
    cairo_move_to(recording_cairo, x, y);

    if (seat->show_preedit) {
        GString *markup = g_string_new(NULL);
        for (int i = 0; i < seat->segment_count; i++) {
            struct anthy_segment_stat segment_stat;
            anthy_get_segment_stat(seat->anthy_context, i, &segment_stat);
            char buf[64];
            anthy_get_segment(seat->anthy_context, i, seat->selected_candidates[i],
                buf, sizeof buf);
            char *markup_fragment;
            if (i == seat->current_segment)
                markup_fragment = g_markup_printf_escaped("<b>%s</b>", buf);
            else 
                markup_fragment = g_markup_escape_text(buf, -1);
            g_string_append(markup, markup_fragment);
            g_free(markup_fragment);
        }
        pango_layout_set_markup(layout, markup->str, -1);
        g_string_free(markup, TRUE);

        PangoRectangle rect;
        pango_layout_get_extents(layout, NULL, &rect);
        max_x = (double)rect.width / PANGO_SCALE > max_x
            ? (double)rect.width / PANGO_SCALE
            : max_x;
        y += (double)rect.height / PANGO_SCALE;
        cairo_set_source_rgba(recording_cairo, 1.0, 1.0, 1.0, 1.0);
        pango_cairo_update_layout(recording_cairo, layout);
        pango_cairo_show_layout(recording_cairo, layout);
        line_y = y + PADDING + BORDER / 2.0;
        y += BORDER + PADDING * 2.0;
        cairo_move_to(recording_cairo, x, y);
    }

    {
        struct anthy_segment_stat segment_stat;
        anthy_get_segment_stat(seat->anthy_context, seat->current_segment, &segment_stat);
        char buf[64];
        for (int i = 0; i < segment_stat.nr_candidate; i++) {
            anthy_get_segment(
                seat->anthy_context, seat->current_segment, i, buf, sizeof buf);
            PangoAttrList *attrs = pango_attr_list_new();
            if (i == seat->selected_candidates[seat->current_segment]) {
                PangoAttribute *attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
                pango_attr_list_insert(attrs, attr);
            }
            pango_layout_set_text(layout, buf, -1);
            pango_layout_set_attributes(layout, attrs);

            PangoRectangle rect;
            pango_layout_get_extents(layout, NULL, &rect);
            max_x = (double)rect.width / PANGO_SCALE > max_x
                ? (double)rect.width / PANGO_SCALE
                : max_x;
            y += (double)rect.height / PANGO_SCALE;
            cairo_set_source_rgba(recording_cairo, 1.0, 1.0, 1.0, 1.0);
            pango_cairo_update_layout(recording_cairo, layout);
            pango_cairo_show_layout(recording_cairo, layout);
            cairo_move_to(recording_cairo, x, y);
        }
    }

    max_x += BORDER * 2.0 + PADDING * 2.0;
    y += BORDER + PADDING;

    double half_border = BORDER / 2.0;

    if (seat->show_preedit) {
        cairo_move_to(recording_cairo, half_border, line_y);
        cairo_line_to(recording_cairo, max_x, line_y);
        cairo_set_line_width(recording_cairo, BORDER);
        cairo_set_source_rgba(recording_cairo, 1.0, 1.0, 1.0, 1.0);
        cairo_stroke(recording_cairo);
    }

    cairo_move_to(recording_cairo, half_border, half_border);
    cairo_line_to(recording_cairo, max_x - half_border, half_border);
    cairo_line_to(recording_cairo, max_x - half_border, y - half_border);
    cairo_line_to(recording_cairo, half_border, y - half_border);
    cairo_line_to(recording_cairo, half_border, half_border);
    cairo_set_line_width(recording_cairo, BORDER);
    cairo_set_source_rgba(recording_cairo, 1.0, 1.0, 1.0, 1.0);
    cairo_stroke(recording_cairo);

    double surface_x, surface_y, surface_width, surface_height;
    cairo_recording_surface_ink_extents(recording_cairo_surface,
        &surface_x, &surface_y, &surface_width, &surface_height);

    struct anthywl_graphics_buffer *buffer =
        anthywl_get_graphics_buffer(seat->state, surface_width, surface_height);
    cairo_set_source_rgba(buffer->cairo, 0.0, 0.0, 0.0, 1.0);
    cairo_paint(buffer->cairo);
    cairo_set_source_surface(buffer->cairo, recording_cairo_surface, 0.0, 0.0);
    cairo_paint(buffer->cairo);

    return buffer;
}

static void anthywl_seat_draw_popup(struct anthywl_seat *seat) {
    struct anthywl_graphics_buffer *buffer = NULL;
    if (seat->is_selecting)
        buffer = anthywl_seat_selecting_draw_popup(seat);
    else if (seat->is_composing && seat->buffer.len != 0 && seat->show_preedit)
        buffer = anthywl_seat_composing_draw_popup(seat);

    if (buffer) {
        wl_surface_attach(seat->wl_surface, buffer->wl_buffer, 0, 0);
        wl_surface_damage_buffer(seat->wl_surface, 0, 0,
            buffer->width, buffer->height);
    } else {
        wl_surface_attach(seat->wl_surface, NULL, 0, 0);
    }

    wl_surface_commit(seat->wl_surface);
}

static void anthywl_buffer_init(struct anthywl_buffer *buffer) {
    buffer->text = calloc(1, 1);
    buffer->len = 0;
    buffer->pos = 0;
}

static void anthywl_buffer_destroy(struct anthywl_buffer *buffer) {
    free(buffer->text);
}

static void anthywl_buffer_clear(struct anthywl_buffer *buffer) {
    buffer->text[0] = '\0';
    buffer->len = 0;
    buffer->pos = 0;
}

static void anthywl_buffer_append(struct anthywl_buffer *buffer, char const *text) {
    size_t text_len = strlen(text);
    buffer->text = realloc(buffer->text, buffer->len + text_len + 1);
    if (buffer->pos == 0) {
        memmove(
            buffer->text + text_len,
            buffer->text,
            buffer->len + 1);
        memcpy(
            buffer->text,
            text,
            text_len);
    } else if (buffer->pos == buffer->len) {
        memcpy(
            buffer->text + buffer->len,
            text,
            text_len + 1);
    } else {
        memmove(
            buffer->text + buffer->pos + text_len,
            buffer->text + buffer->pos,
            buffer->len - buffer->pos + 1);
        memcpy(
            buffer->text + buffer->pos,
            text,
            text_len);
    }
    buffer->len += text_len;
    buffer->pos += text_len;
}

static void anthywl_buffer_delete_backwards(struct anthywl_buffer *buffer,
    size_t amt)
{
    if (buffer->pos == 0)
        return;
    size_t end = buffer->pos;
    size_t start = buffer->pos;
    for (size_t i = 0; i < amt; i++) {
        start -= 1;
        for (; start != 0; start--) {
             if ((buffer->text[start] & 0x80) == 0
                || (buffer->text[start] & 0xC0) == 0xC0)
            {
                break;
            }
        }
    }
    memmove(
        buffer->text + start,
        buffer->text + end,
        buffer->len - end + 1);
    buffer->len -= end - start;
    buffer->pos -= end - start;
}

static void anthywl_buffer_delete_forwards(struct anthywl_buffer *buffer,
    size_t amt)
{
    if (buffer->pos == buffer->len)
        return;
    size_t start = buffer->pos;
    size_t end = start + 1;
    for (; end != buffer->len; end++) {
         if ((buffer->text[end] & 0x80) == 0
            || (buffer->text[end] & 0xC0) == 0xC0)
        {
            break;
        }
    }
    memmove(
        buffer->text + start,
        buffer->text + end,
        buffer->len - end + 1);
    buffer->len -= end - start;
}

static void anthywl_buffer_move_left(struct anthywl_buffer *buffer) {
    if (buffer->pos == 0)
        return;
    buffer->pos -= 1;
    while ((buffer->text[buffer->pos] & 0x80) != 0
        && (buffer->text[buffer->pos] & 0xC0) != 0xC0)
    {
        buffer->pos -= 1;
    }
}

static void anthywl_buffer_move_right(struct anthywl_buffer *buffer) {
    if (buffer->pos == buffer->len)
        return;
    buffer->pos += 1;
    while ((buffer->text[buffer->pos] & 0x80) != 0
        && (buffer->text[buffer->pos] & 0xC0) != 0xC0)
    {
        buffer->pos += 1;
    }
}

static void anthywl_buffer_convert_romaji(struct anthywl_buffer *buffer) {
#include "kana.inc"
    if (buffer->pos == 0)
        return;

    unsigned char c0 = '\0', c1 = '\0', c2 = '\0', c3 = '\0';

    if (buffer->pos >= 1) c3 = buffer->text[buffer->pos - 1];
    if (buffer->pos >= 2) c2 = buffer->text[buffer->pos - 2];
    if (buffer->pos >= 3) c1 = buffer->text[buffer->pos - 3];
    if (buffer->pos >= 4) c0 = buffer->text[buffer->pos - 4];

    if (c3 >= 0x80) c3 = '\0';
    if (c2 >= 0x80) c2 = '\0';
    if (c1 >= 0x80) c1 = '\0';
    if (c0 >= 0x80) c0 = '\0';

    if (c2 == 'n' && c3 == '\'') {
        anthywl_buffer_delete_backwards(buffer, 2);
        anthywl_buffer_append(buffer, "ん");
        return;
    }

    if (!c3)
        return;

    if (c3 == '.') {
        anthywl_buffer_delete_backwards(buffer, 1);
        anthywl_buffer_append(buffer, "。");
    }

    if (c3 == '.') {
        anthywl_buffer_delete_backwards(buffer, 1);
        anthywl_buffer_append(buffer, "。");
    }

    if (c2 == c3 && twos[c3]) {
        anthywl_buffer_delete_backwards(buffer, 2);
        anthywl_buffer_append(buffer, "っ");
        anthywl_buffer_append(buffer, (char[]){c3, 0});
    }

    // shi -> si
    // chi -> ti
    // thi -> texi
    // t'i -> texi
    // dzu -> du

    if (c1 == 's' && c2 == 'h' && c3 == 'i') {
        anthywl_buffer_delete_backwards(buffer, 3);
        anthywl_buffer_append(buffer, "し");
        return;
    }

    if (c1 == 'c' && c2 == 'h' && c3 == 'i') {
        anthywl_buffer_delete_backwards(buffer, 3);
        anthywl_buffer_append(buffer, "ち");
        return;
    }

    if ((c1 == 'x' || c1 == 'l') && c2 == 't' && c3 == 'u') {
        anthywl_buffer_delete_backwards(buffer, 3);
        anthywl_buffer_append(buffer, "っ");
        return;
    }

    if (c1 == 't' && c2 == 's' && c3 == 'u') {
        if (c0 == 'x' || c0 == 'l') {
            anthywl_buffer_delete_backwards(buffer, 4);
            anthywl_buffer_append(buffer, "っ");
            return;
        }
        anthywl_buffer_delete_backwards(buffer, 3);
        anthywl_buffer_append(buffer, "つ");
        return;
    }

    if (c1 == 's' && c2 == 'h' && youon[c3])
        c2 = 'y';

    if (c1 == 'c' && c2 == 'h' && youon[c3])
        c1 = 't', c2 = 'y';

    if (c1 && c2 == 'y' && youon[c3]) {
        anthywl_buffer_delete_backwards(buffer, 3);
        if (c1 != 'x' && c1 != 'l')
            anthywl_buffer_append(buffer, twos[c1]['i']);
        anthywl_buffer_append(buffer, youon[c3]);
        return;
    }

    if (c2 && twos[c2] && twos[c2][c3]) {
        anthywl_buffer_delete_backwards(buffer, 2);
        anthywl_buffer_append(buffer, twos[c2][c3]);
        return;
    }

    if (ones[c3]) {
        anthywl_buffer_delete_backwards(buffer, 1);
        anthywl_buffer_append(buffer, ones[c3]);
        return;
    }
}

static struct wl_seat_listener const wl_seat_listener;
static void anthywl_seat_init_protocols(struct anthywl_seat *seat);

static void anthywl_seat_init(struct anthywl_seat *seat,
    struct anthywl_state *state, struct wl_seat *wl_seat)
{
    seat->state = state;
    seat->wl_seat = wl_seat;
    wl_seat_add_listener(wl_seat, &wl_seat_listener, seat);
    seat->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (state->running)
        anthywl_seat_init_protocols(seat);
    anthywl_buffer_init(&seat->buffer);
    seat->anthy_context = anthy_create_context();
    anthy_context_set_encoding(seat->anthy_context, ANTHY_UTF8_ENCODING);
    seat->is_composing = true;
}

static struct zwp_input_method_v2_listener const zwp_input_method_v2_listener;
static struct zwp_input_method_keyboard_grab_v2_listener const
    zwp_input_method_keyboard_grab_v2_listener;

static void anthywl_seat_init_protocols(struct anthywl_seat *seat) {
    seat->zwp_input_method_v2 =
        zwp_input_method_manager_v2_get_input_method(
            seat->state->zwp_input_method_manager_v2, seat->wl_seat);
    zwp_input_method_v2_add_listener(seat->zwp_input_method_v2,
        &zwp_input_method_v2_listener, seat);
    seat->zwp_virtual_keyboard_v1 =
        zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
            seat->state->zwp_virtual_keyboard_manager_v1, seat->wl_seat);
    seat->zwp_input_method_keyboard_grab_v2 =
        zwp_input_method_v2_grab_keyboard(seat->zwp_input_method_v2);
    zwp_input_method_keyboard_grab_v2_add_listener(
        seat->zwp_input_method_keyboard_grab_v2,
        &zwp_input_method_keyboard_grab_v2_listener, seat);
    seat->wl_surface = wl_compositor_create_surface(seat->state->wl_compositor);
    anthywl_seat_draw_popup(seat);
    seat->zwp_input_popup_surface_v2=
        zwp_input_method_v2_get_input_popup_surface(
            seat->zwp_input_method_v2, seat->wl_surface);
    zwp_input_popup_surface_v2_add_listener(seat->zwp_input_popup_surface_v2,
        &zwp_input_popup_surface_v2_listener, seat);
    seat->are_protocols_initted = true;
}

static void anthywl_seat_destroy(struct anthywl_seat *seat) {
    anthy_release_context(seat->anthy_context);
    free(seat->selected_candidates);
    anthywl_buffer_destroy(&seat->buffer);
    free(seat->pending_surrounding_text);
    free(seat->surrounding_text);
    free(seat->name);
    xkb_state_unref(seat->xkb_state);
    xkb_keymap_unref(seat->xkb_keymap);
    xkb_context_unref(seat->xkb_context);
    if (seat->are_protocols_initted) {
        zwp_input_popup_surface_v2_destroy(seat->zwp_input_popup_surface_v2);
        wl_surface_destroy(seat->wl_surface);
        zwp_virtual_keyboard_v1_destroy(seat->zwp_virtual_keyboard_v1);
        zwp_input_method_keyboard_grab_v2_destroy(
            seat->zwp_input_method_keyboard_grab_v2);
        zwp_input_method_v2_destroy(seat->zwp_input_method_v2);
    }
    wl_seat_destroy(seat->wl_seat);
    wl_list_remove(&seat->link);
    free(seat);
}

static void anthywl_seat_composing_update(struct anthywl_seat *seat) {
    zwp_input_method_v2_set_preedit_string(
        seat->zwp_input_method_v2, seat->buffer.text,
        seat->buffer.pos, seat->buffer.pos);
    zwp_input_method_v2_commit(
        seat->zwp_input_method_v2, seat->done_events_received);
    anthywl_seat_draw_popup(seat);
}

static void anthywl_seat_composing_commit(struct anthywl_seat *seat) {
    zwp_input_method_v2_commit_string(
        seat->zwp_input_method_v2, seat->buffer.text);
    zwp_input_method_v2_commit(
        seat->zwp_input_method_v2, seat->done_events_received);
    anthywl_buffer_clear(&seat->buffer);
    anthywl_seat_draw_popup(seat);
}

static void anthywl_seat_selecting_update(struct anthywl_seat *seat);

static bool anthywl_seat_composing_handle_key_event(
    struct anthywl_seat *seat, xkb_keycode_t keycode)
{
    xkb_keysym_t keysym = xkb_state_key_get_one_sym(seat->xkb_state, keycode);

    switch (keysym) {
    case XKB_KEY_Super_R:
        seat->is_composing = false;
        anthywl_buffer_clear(&seat->buffer);
        anthywl_seat_composing_update(seat);
        return true;
    case XKB_KEY_Menu:
        seat->show_preedit = !seat->show_preedit;
        anthywl_seat_composing_update(seat);
        return true;
    case XKB_KEY_Escape:
        if (seat->buffer.len == 0)
            return false;
        anthywl_buffer_clear(&seat->buffer);
        anthywl_seat_composing_update(seat);
        return true;
    case XKB_KEY_BackSpace:
        if (seat->buffer.len == 0)
            return false;
        anthywl_buffer_delete_backwards(&seat->buffer, 1);
        anthywl_seat_composing_update(seat);
        return true;
    case XKB_KEY_Delete:
        if (seat->buffer.len == 0)
            return false;
        anthywl_buffer_delete_forwards(&seat->buffer, 1);
        anthywl_seat_composing_update(seat);
        return true;
    case XKB_KEY_Left:
        if (seat->buffer.len == 0)
            return false;
        anthywl_buffer_move_left(&seat->buffer);
        anthywl_seat_composing_update(seat);
        return true;
    case XKB_KEY_Right:
        if (seat->buffer.len == 0)
            return false;
        anthywl_buffer_move_right(&seat->buffer);
        anthywl_seat_composing_update(seat);
        return true;
    case XKB_KEY_space:
        if (seat->buffer.len == 0)
            return false;
        seat->is_selecting = true;
        anthy_reset_context(seat->anthy_context);
        anthy_set_string(seat->anthy_context, seat->buffer.text);
        struct anthy_conv_stat conv_stat;
        anthy_get_stat(seat->anthy_context, &conv_stat);
        free(seat->selected_candidates);
        seat->selected_candidates = calloc(conv_stat.nr_segment, sizeof(int));
        seat->segment_count = conv_stat.nr_segment;
        seat->current_segment = 0;
        anthywl_seat_selecting_update(seat);
        return true;
    case XKB_KEY_Return:
        if (seat->buffer.len == 0)
            return false;
        anthywl_seat_composing_commit(seat);
        return true;
    }

    int utf8_len = xkb_state_key_get_utf8(seat->xkb_state, keycode, NULL, 0);
    if (utf8_len == 0)
        return false;

    char *utf8 = malloc(utf8_len + 1);
    xkb_state_key_get_utf8(seat->xkb_state, keycode, utf8, utf8_len + 1);
    anthywl_buffer_append(&seat->buffer, utf8);
    anthywl_buffer_convert_romaji(&seat->buffer);
    anthywl_seat_composing_update(seat);
    free(utf8);

    return true;
}

static void anthywl_seat_selecting_update(struct anthywl_seat *seat) {
    size_t cursor_begin = 0, cursor_end = 0;
    struct wl_array buffer;
    wl_array_init(&buffer);

    for (int i = 0; i < seat->segment_count; i++) {
        struct anthy_segment_stat segment_stat;
        anthy_get_segment_stat(seat->anthy_context, i, &segment_stat);
        char buf[64];
        anthy_get_segment(seat->anthy_context, i, seat->selected_candidates[i],
            buf, sizeof buf);
        if (i == seat->current_segment)
            cursor_begin = buffer.size;
        size_t buf_len = strlen(buf);
        wl_array_add(&buffer, buf_len);
        memcpy(buffer.data + buffer.size - buf_len, buf, buf_len);
        if (i == seat->current_segment)
            cursor_end = buffer.size;
    }

    wl_array_add(&buffer, 1);
    ((char *)buffer.data)[buffer.size - 1] = 0;

    zwp_input_method_v2_set_preedit_string(
        seat->zwp_input_method_v2, buffer.data, cursor_begin, cursor_end);
    zwp_input_method_v2_commit(
        seat->zwp_input_method_v2, seat->done_events_received);

    anthywl_seat_draw_popup(seat);

    wl_array_release(&buffer);
}

static void anthywl_seat_selecting_commit(struct anthywl_seat *seat) {
    struct wl_array buffer;
    wl_array_init(&buffer);

    char buf[64];
    for (int i = 0; i < seat->segment_count; i++) {
        struct anthy_segment_stat segment_stat;
        anthy_get_segment_stat(seat->anthy_context, i, &segment_stat);
        anthy_get_segment(seat->anthy_context,
            i, seat->selected_candidates[i], buf, sizeof buf);
        size_t buf_len = strlen(buf);
        wl_array_add(&buffer, buf_len);
        memcpy(buffer.data + buffer.size - buf_len, buf, buf_len);
    }

    wl_array_add(&buffer, 1);
    ((char *)buffer.data)[buffer.size - 1] = 0;

    zwp_input_method_v2_commit_string(seat->zwp_input_method_v2, buffer.data);
    zwp_input_method_v2_commit(
        seat->zwp_input_method_v2, seat->done_events_received);
    seat->is_selecting = false;
    anthywl_buffer_clear(&seat->buffer);

    anthywl_seat_draw_popup(seat);

    wl_array_release(&buffer);
}

static bool anthywl_seat_selecting_handle_key_event(
    struct anthywl_seat *seat, xkb_keycode_t keycode)
{
    xkb_keysym_t keysym = xkb_state_key_get_one_sym(seat->xkb_state, keycode);
    if (xkb_state_mod_name_is_active(
        seat->xkb_state, XKB_MOD_NAME_SHIFT, XKB_STATE_EFFECTIVE))
    {
        if (keysym == XKB_KEY_Left || keysym == XKB_KEY_Right) {
            int amount = keysym == XKB_KEY_Left ? -1 : 1;
            anthy_resize_segment(
                seat->anthy_context, seat->current_segment, amount);
            struct anthy_conv_stat conv_stat;
            anthy_get_stat(seat->anthy_context, &conv_stat);
            seat->selected_candidates = realloc(
                seat->selected_candidates, conv_stat.nr_segment * sizeof(int));
            int difference = conv_stat.nr_segment - seat->segment_count;
            if (difference > 0) {
                memset(seat->selected_candidates + seat->segment_count,
                    0, difference * sizeof(int));
            }
            seat->segment_count = conv_stat.nr_segment;
            anthywl_seat_selecting_update(seat);
            return true;
        }
    }

    struct anthy_conv_stat conv_stat;
    anthy_get_stat(seat->anthy_context, &conv_stat);
    assert(conv_stat.nr_segment == seat->segment_count);

    struct anthy_segment_stat segment_stat;
    anthy_get_segment_stat(seat->anthy_context, seat->current_segment, &segment_stat);

    switch (keysym) {
    case XKB_KEY_Shift_L:
    case XKB_KEY_Shift_R:
    case XKB_KEY_Control_L:
    case XKB_KEY_Control_R:
    case XKB_KEY_Alt_L:
    case XKB_KEY_Alt_R:
        return true;
    case XKB_KEY_F6:
    case XKB_KEY_F7:
    case XKB_KEY_F8:
    case XKB_KEY_F9:
    case XKB_KEY_F10:
        return true;
    case XKB_KEY_Menu:
        seat->show_preedit = !seat->show_preedit;
        anthywl_seat_selecting_update(seat);
        return true;
    case XKB_KEY_space:
        if (seat->selected_candidates[seat->current_segment]
            != segment_stat.nr_candidate - 1)
        {
            seat->selected_candidates[seat->current_segment] += 1;
        }
        anthywl_seat_selecting_update(seat);
        return true;
    case XKB_KEY_Escape:
    case XKB_KEY_BackSpace:
        seat->is_selecting = false;
        anthywl_seat_composing_update(seat);
        return true;
    case XKB_KEY_Return:
        anthywl_seat_selecting_commit(seat);
        return true;
    case XKB_KEY_Left:
        anthy_commit_segment(
            seat->anthy_context,
            seat->current_segment,
            seat->selected_candidates[seat->current_segment]);
        if (seat->current_segment != 0)
            seat->current_segment -= 1;
        anthywl_seat_selecting_update(seat);
        return true;
    case XKB_KEY_Right:
        anthy_commit_segment(
            seat->anthy_context,
            seat->current_segment,
            seat->selected_candidates[seat->current_segment]);
        if (seat->current_segment != seat->segment_count - 1)
            seat->current_segment += 1;
        anthywl_seat_selecting_update(seat);
        return true;
    case XKB_KEY_Up:
        if (seat->selected_candidates[seat->current_segment] != 0)
            seat->selected_candidates[seat->current_segment] -= 1;
        anthywl_seat_selecting_update(seat);
        return true;
    case XKB_KEY_Down:
        if (seat->selected_candidates[seat->current_segment]
            != segment_stat.nr_candidate - 1)
        {
            seat->selected_candidates[seat->current_segment] += 1;
        }
        anthywl_seat_selecting_update(seat);
        return true;
    default:
        anthywl_seat_selecting_commit(seat);
        return false;
    }
}

static bool anthywl_seat_handle_key(struct anthywl_seat *seat,
    xkb_keycode_t keycode)
{
    xkb_keysym_t keysym = xkb_state_key_get_one_sym(seat->xkb_state, keycode);
    if (seat->is_selecting) {
        return anthywl_seat_selecting_handle_key_event(seat, keycode);
    } else if (seat->is_composing) {
        return anthywl_seat_composing_handle_key_event(seat, keycode);
    } else if (keysym == XKB_KEY_Super_R) {
        seat->is_composing = true;
        return true;
    }
    return false;
}

static void anthywl_seat_repeat_timer_callback(struct anthywl_timer *timer) {
    struct anthywl_seat *seat = wl_container_of(timer, seat, repeat_timer);
    clock_gettime(CLOCK_MONOTONIC, &seat->repeat_timer.time);
    seat->repeat_timer.time.tv_nsec += 1000000000 / seat->repeat_rate;
    seat->repeat_timer.callback = anthywl_seat_repeat_timer_callback;
    anthywl_seat_handle_key(seat, seat->repeating_keycode);
}

static void zwp_input_method_keyboard_grab_v2_keymap(void *data,
    struct zwp_input_method_keyboard_grab_v2 *zwp_input_method_keyboard_grab_v2,
    uint32_t format, int32_t fd, uint32_t size)
{
    struct anthywl_seat *seat = data;
    zwp_virtual_keyboard_v1_keymap(
        seat->zwp_virtual_keyboard_v1, format, fd, size);

    char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    xkb_keymap_unref(seat->xkb_keymap);
    xkb_state_unref(seat->xkb_state);
    seat->xkb_keymap = xkb_keymap_new_from_string(seat->xkb_context, map,
        XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    seat->xkb_state = xkb_state_new(seat->xkb_keymap);

    close(fd);
    munmap(map, size);
}

static void zwp_input_method_keyboard_grab_v2_key(void *data,
    struct zwp_input_method_keyboard_grab_v2 *zwp_input_method_keyboard_grab_v2,
    uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
    struct anthywl_seat *seat = data;
    xkb_keycode_t keycode = key + 8;
    bool handled = false;

    if (state == WL_KEYBOARD_KEY_STATE_PRESSED
        && seat->repeating_keycode != 0
        && seat->repeating_keycode != keycode)
    {
        if (!anthywl_seat_handle_key(seat, keycode)) {
            seat->repeating_keycode = 0;
            wl_list_remove(&seat->repeat_timer.link);
            goto forward;
        }
        if (xkb_key_repeats(seat->xkb_keymap, keycode)) {
            seat->repeating_keycode = keycode;
            clock_gettime(CLOCK_MONOTONIC, &seat->repeat_timer.time);
            seat->repeat_timer.time.tv_nsec += seat->repeat_delay * 1000000;
            seat->repeat_timer.callback = anthywl_seat_repeat_timer_callback;
        } else {
            seat->repeating_keycode = 0;
        }
        return;
    }

    if (state == WL_KEYBOARD_KEY_STATE_RELEASED
        && seat->repeating_keycode == keycode)
    {
        seat->repeating_keycode = 0;
        wl_list_remove(&seat->repeat_timer.link);
        return;
    }

    if (seat->active && state == WL_KEYBOARD_KEY_STATE_PRESSED)
        handled |= anthywl_seat_handle_key(seat, keycode);

    if (state == WL_KEYBOARD_KEY_STATE_PRESSED
        && xkb_key_repeats(seat->xkb_keymap, keycode)
        && handled)
    {
        seat->repeating_keycode = keycode;
        clock_gettime(CLOCK_MONOTONIC, &seat->repeat_timer.time);
        seat->repeat_timer.time.tv_nsec += seat->repeat_delay * 1000000;
        seat->repeat_timer.callback = anthywl_seat_repeat_timer_callback;
        wl_list_insert(&seat->state->timers, &seat->repeat_timer.link);
        return;
    }

    if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        if (handled) {
            for (size_t i = 0;
                i < sizeof(seat->pressed) / sizeof(seat->pressed[0]); i++)
            {
                if (seat->pressed[i] == 0) {
                    seat->pressed[i] = keycode;
                    goto forward;
                }
            }
        }
    }

    if (state == WL_KEYBOARD_KEY_STATE_RELEASED) {
        for (size_t i = 0;
            i < sizeof(seat->pressed) / sizeof(seat->pressed[0]); i++)
        {
            if (seat->pressed[i] == keycode) {
                seat->pressed[i] = 0;
                return;
            }
        }
    }

    if (handled)
        return;

forward:
    zwp_virtual_keyboard_v1_key(
        seat->zwp_virtual_keyboard_v1, time, key, state);
}

static void zwp_input_method_keyboard_grab_v2_modifiers(void *data,
    struct zwp_input_method_keyboard_grab_v2 *zwp_input_method_keyboard_grab_v2,
    uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
    uint32_t mods_locked, uint32_t group)
{
    struct anthywl_seat *seat = data;
    xkb_state_update_mask(seat->xkb_state,
        mods_depressed, mods_latched, mods_locked, 0, 0, group);
    zwp_virtual_keyboard_v1_modifiers(seat->zwp_virtual_keyboard_v1,
        mods_depressed, mods_latched, mods_locked, group);
}

static void zwp_input_method_keyboard_grab_v2_repeat_info(void *data,
    struct zwp_input_method_keyboard_grab_v2 *zwp_input_method_keyboard_grab_v2,
    int32_t rate, int32_t delay)
{
    struct anthywl_seat *seat = data;
    seat->repeat_rate = rate;
    seat->repeat_delay = delay;
}

static struct zwp_input_method_keyboard_grab_v2_listener const
    zwp_input_method_keyboard_grab_v2_listener =
{
    .keymap = zwp_input_method_keyboard_grab_v2_keymap,
    .key = zwp_input_method_keyboard_grab_v2_key,
    .modifiers = zwp_input_method_keyboard_grab_v2_modifiers,
    .repeat_info = zwp_input_method_keyboard_grab_v2_repeat_info,
};

static void zwp_input_method_v2_activate(void *data,
    struct zwp_input_method_v2 *zwp_input_method_v2)
{
    struct anthywl_seat *seat = data;
    seat->pending_activate = true;
}

static void zwp_input_method_v2_deactivate(void *data,
    struct zwp_input_method_v2 *zwp_input_method_v2)
{
    struct anthywl_seat *seat = data;
    seat->pending_activate = false;
}

static void zwp_input_method_v2_surrounding_text(void *data,
    struct zwp_input_method_v2 *zwp_input_method_v2, char const *text,
    uint32_t cursor, uint32_t anchor)
{
    struct anthywl_seat *seat = data;
    free(seat->pending_surrounding_text);
    seat->pending_surrounding_text = strdup(text);
    seat->pending_surrounding_text_cursor = cursor;
    seat->pending_surrounding_text_anchor = anchor;
}

static void zwp_input_method_v2_text_change_cause(void *data,
    struct zwp_input_method_v2 *zwp_input_method_v2, uint32_t cause)
{
    struct anthywl_seat *seat = data;
    seat->pending_text_change_cause = cause;
}

static void zwp_input_method_v2_content_type(void *data,
    struct zwp_input_method_v2 *zwp_input_method_v2, uint32_t hint,
    uint32_t purpose)
{
    struct anthywl_seat *seat = data;
    seat->pending_content_type_hint = hint;
    seat->pending_content_type_purpose = purpose;
}

static void zwp_input_method_v2_done(void *data,
    struct zwp_input_method_v2 *zwp_input_method_v2)
{
    struct anthywl_seat *seat = data;
    bool was_active = seat->active;
    seat->active = seat->pending_activate;
    free(seat->surrounding_text);
    seat->surrounding_text = seat->pending_surrounding_text;
    seat->pending_surrounding_text = NULL;
    seat->surrounding_text_cursor = seat->pending_surrounding_text_cursor;
    seat->surrounding_text_anchor = seat->pending_surrounding_text_anchor;
    seat->text_change_cause = seat->pending_text_change_cause;
    seat->content_type_hint = seat->pending_content_type_hint;
    seat->content_type_purpose = seat->pending_content_type_purpose;
    seat->done_events_received++;
    if (!was_active && seat->active) {
        seat->is_selecting = false;
        anthywl_buffer_clear(&seat->buffer);
        anthywl_seat_draw_popup(seat);
    }
}

static void zwp_input_method_v2_unavailable(void *data,
    struct zwp_input_method_v2 *zwp_input_method_v2)
{
    struct anthywl_seat *seat = data;
    fprintf(stderr, "Input method unavailable on seat \"%s\".\n", seat->name);
    anthywl_seat_destroy(seat);
}

void wl_seat_capabilities(void *data, struct wl_seat *wl_seat,
    uint32_t capabilities)
{
}

void wl_seat_name(void *data, struct wl_seat *wl_seat, char const *name) {
    struct anthywl_seat *seat = data;
    free(seat->name);
    seat->name = strdup(name);
}

static struct wl_seat_listener const wl_seat_listener = {
    .capabilities = wl_seat_capabilities,
    .name = wl_seat_name,
};

static struct zwp_input_method_v2_listener const zwp_input_method_v2_listener =
{
    .activate = zwp_input_method_v2_activate,
    .deactivate = zwp_input_method_v2_deactivate,
    .surrounding_text = zwp_input_method_v2_surrounding_text,
    .text_change_cause = zwp_input_method_v2_text_change_cause,
    .content_type = zwp_input_method_v2_content_type,
    .done = zwp_input_method_v2_done,
    .unavailable = zwp_input_method_v2_unavailable,
};

static void wl_registry_global(void *data, struct wl_registry *wl_registry,
    uint32_t name, char const *interface, uint32_t version)
{
    struct anthywl_state *state = data;
    if (strcmp(interface, wl_shm_interface.name) == 0) {
        state->wl_shm = wl_registry_bind(
            wl_registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_compositor_interface.name) == 0) {
        state->wl_compositor = wl_registry_bind(
            wl_registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        struct anthywl_seat *seat = calloc(1, sizeof(*seat));
        struct wl_seat *wl_seat = wl_registry_bind(
            wl_registry, name, &wl_seat_interface, 4);
        anthywl_seat_init(seat, state, wl_seat);
        wl_list_insert(&state->seats, &seat->link);
    } else if (strcmp(
        interface, zwp_input_method_manager_v2_interface.name) == 0)
    {
        state->zwp_input_method_manager_v2 = wl_registry_bind(
            wl_registry, name, &zwp_input_method_manager_v2_interface, 1);
    } else if (strcmp(
        interface, zwp_virtual_keyboard_manager_v1_interface.name) == 0)
    {
        state->zwp_virtual_keyboard_manager_v1 = wl_registry_bind(
            wl_registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
    }
}

static void wl_registry_global_remove(void *data,
    struct wl_registry *wl_registry, uint32_t name)
{
    // TODO: handle seat removal?
}

static struct wl_registry_listener const wl_registry_listener = {
    .global = wl_registry_global,
    .global_remove = wl_registry_global_remove,
};

static bool interrupted;
static void sigint(int signal) { interrupted = true; }

int main() {
    int rc = 1;

    if (anthy_init() != 0) {
        perror("anthy_init");
        goto end;
    }
    atexit(anthy_quit);

    struct anthywl_state state = { 0 };
    wl_list_init(&state.seats);
    wl_list_init(&state.buffers);
    wl_list_init(&state.timers);
 
    state.wl_display = wl_display_connect(NULL);
    if (state.wl_display == NULL) {
        perror("wl_display_connect");
        goto end;
    }

    state.wl_registry = wl_display_get_registry(state.wl_display);
    wl_registry_add_listener(state.wl_registry, &wl_registry_listener, &state);
    wl_display_roundtrip(state.wl_display);

    if (state.wl_compositor == NULL) {
        fprintf(stderr, "Missing protocol: wl_compositor\n");
        goto end;
    }

    if (state.wl_shm == NULL) {
        fprintf(stderr, "Missing protocol: wl_shm\n");
        goto end;
    }

    if (state.zwp_input_method_manager_v2 == NULL) {
        fprintf(stderr, "Missing protocol: zwp_input_method_manager_v2\n");
        goto end;
    }

    if (state.zwp_virtual_keyboard_manager_v1 == NULL) {
        fprintf(stderr, "Missing protocol: zwp_virtual_keyboard_manager_v1\n");
        goto end;
    }

    struct anthywl_seat *seat;
    wl_list_for_each(seat, &state.seats, link)
        anthywl_seat_init_protocols(seat);

    wl_display_flush(state.wl_display);

    state.running = true;
    signal(SIGINT, sigint);
    while (state.running && !interrupted) {
        int timeout = INT_MAX;

        if (!wl_list_empty(&state.timers)) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            struct anthywl_timer *timer;
            wl_list_for_each(timer, &state.timers, link) {
                int time =
                    (timer->time.tv_sec - now.tv_sec) * 1000 +
                    (timer->time.tv_nsec - now.tv_nsec) / 1000000;
                if (time < timeout)
                    timeout = time;
            }
        }

        if (timeout == INT_MAX) timeout = -1;

        struct pollfd pfd = {
            .fd = wl_display_get_fd(state.wl_display),
            .events = POLLIN,
        };

        int res = poll(&pfd, 1, timeout);
        if (res == -1) {
            if (errno == EINTR)
                continue;
            perror("poll");
            goto end;
        }

        while (wl_display_prepare_read(state.wl_display) != 0)
            wl_display_dispatch_pending(state.wl_display);

        if (pfd.events & POLLIN)
            wl_display_read_events(state.wl_display);
        else
            wl_display_cancel_read(state.wl_display);


        if (wl_display_dispatch_pending(state.wl_display) == -1) {
            perror("wl_display_dispatch");
            goto end;
        }

        if (!wl_list_empty(&state.timers)) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            struct anthywl_timer *timer, *tmp;
            wl_list_for_each_safe(timer, tmp, &state.timers, link) {
                bool expired = timer->time.tv_sec < now.tv_sec ||
                    (timer->time.tv_sec == now.tv_sec &&
                     timer->time.tv_nsec < now.tv_nsec);
                 if (expired) {
                     timer->callback(timer);
                 }
            }
        }

        wl_display_flush(state.wl_display);

        if (wl_list_empty(&state.seats)) {
            fprintf(stderr, "No seats with input method unavailable.\n");
            break;
        }
    }
    state.running = false;

    struct anthywl_seat *tmp;
    wl_list_for_each_safe(seat, tmp, &state.seats, link)
        anthywl_seat_destroy(seat);

    wl_display_disconnect(state.wl_display);

    rc = 0;
end:
    return rc;
}
