#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#include <anthy/anthy.h>
#include <cairo.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "input-method-unstable-v2-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"

#include "buffer.h"
#include "graphics_buffer.h"

struct anthywl_state {
    bool running;
    struct wl_display *wl_display;
    struct wl_registry *wl_registry;
    struct wl_compositor *wl_compositor;
    struct wl_shm *wl_shm;
    struct zwp_input_method_manager_v2 *zwp_input_method_manager_v2;
    struct zwp_virtual_keyboard_manager_v1 *zwp_virtual_keyboard_manager_v1;
    struct wl_list buffers;
    struct wl_list seats;
    struct wl_list timers;
};

struct anthywl_timer {
    struct wl_list link;
    struct timespec time;
    void (*callback)(struct anthywl_timer *timer);
};

struct anthywl_seat {
    struct wl_list link;
    struct anthywl_state *state;
    struct wl_seat *wl_seat;

    bool are_protocols_initted;
    struct zwp_input_method_v2 *zwp_input_method_v2;
    struct zwp_input_method_keyboard_grab_v2 *zwp_input_method_keyboard_grab_v2;
    struct zwp_virtual_keyboard_v1 *zwp_virtual_keyboard_v1;

    char *xkb_keymap_string;
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
    bool is_initial_selection;
    int current_segment;
    int segment_count;
    int *selected_candidates;
    anthy_context_t anthy_context;

    // popup
    struct wl_surface *wl_surface;
    struct zwp_input_popup_surface_v2 *zwp_input_popup_surface_v2;
};

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

    double x = BORDER + PADDING, y = BORDER + PADDING;
    double max_text_width = 0.0;
    cairo_move_to(recording_cairo, x, y);

    pango_layout_set_text(layout, seat->buffer.text, -1);
    PangoRectangle rect;
    pango_layout_get_extents(layout, NULL, &rect);
    double text_width = (double)rect.width / PANGO_SCALE;
    double text_height = (double)rect.height / PANGO_SCALE;
    if (text_width > max_text_width) {
        max_text_width = text_width;
    }
    y += text_height;
    cairo_set_source_rgba(recording_cairo, 1.0, 1.0, 1.0, 1.0);
    pango_cairo_update_layout(recording_cairo, layout);
    pango_cairo_show_layout(recording_cairo, layout);
    cairo_move_to(recording_cairo, x, y);

    x = max_text_width + BORDER * 2.0 + PADDING * 2.0;
    y += BORDER + PADDING;

    double half_border = BORDER / 2.0;
    cairo_move_to(recording_cairo, half_border, half_border);
    cairo_line_to(recording_cairo, x - half_border, half_border);
    cairo_line_to(recording_cairo, x - half_border, y - half_border);
    cairo_line_to(recording_cairo, half_border, y - half_border);
    cairo_line_to(recording_cairo, half_border, half_border);
    cairo_set_line_width(recording_cairo, BORDER);
    cairo_set_source_rgba(recording_cairo, 1.0, 1.0, 1.0, 1.0);
    cairo_stroke(recording_cairo);

    double surface_x, surface_y, surface_width, surface_height;
    cairo_recording_surface_ink_extents(recording_cairo_surface,
        &surface_x, &surface_y, &surface_width, &surface_height);

    struct anthywl_graphics_buffer *buffer = anthywl_graphics_buffer_get(
        seat->state->wl_shm, &seat->state->buffers,
        surface_width, surface_height);
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

    if (seat->surrounding_text == NULL) {
        GString *markup = g_string_new(NULL);
        for (int i = 0; i < seat->segment_count; i++) {
            struct anthy_segment_stat segment_stat;
            anthy_get_segment_stat(seat->anthy_context, i, &segment_stat);
            char buf[64];
            anthy_get_segment(seat->anthy_context,
                i, seat->selected_candidates[i], buf, sizeof buf);
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
        anthy_get_segment_stat(
            seat->anthy_context, seat->current_segment, &segment_stat);
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

    if (seat->surrounding_text == NULL) {
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

    struct anthywl_graphics_buffer *buffer = anthywl_graphics_buffer_get(
        seat->state->wl_shm, &seat->state->buffers,
        surface_width, surface_height);
    cairo_set_source_rgba(buffer->cairo, 0.0, 0.0, 0.0, 1.0);
    cairo_paint(buffer->cairo);
    cairo_set_source_surface(buffer->cairo, recording_cairo_surface, 0.0, 0.0);
    cairo_paint(buffer->cairo);

    return buffer;
}

static void anthywl_seat_draw_popup(struct anthywl_seat *seat) {
    struct anthywl_graphics_buffer *buffer = NULL;
    if (seat->is_selecting
        && (!seat->is_initial_selection || seat->surrounding_text == NULL))
    {
        buffer = anthywl_seat_selecting_draw_popup(seat);
    } else if (seat->is_composing
        && seat->buffer.len != 0
        && seat->surrounding_text == NULL)
    {
        buffer = anthywl_seat_composing_draw_popup(seat);
    }

    if (buffer) {
        wl_surface_attach(seat->wl_surface, buffer->wl_buffer, 0, 0);
        wl_surface_damage_buffer(seat->wl_surface, 0, 0,
            buffer->width, buffer->height);
    } else {
        wl_surface_attach(seat->wl_surface, NULL, 0, 0);
    }

    wl_surface_commit(seat->wl_surface);
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
    seat->zwp_input_popup_surface_v2 =
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
    free(seat->xkb_keymap_string);
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
    case XKB_KEY_Muhenkan:
        seat->is_composing = false;
        anthywl_buffer_clear(&seat->buffer);
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
        anthywl_buffer_convert_trailing_n(&seat->buffer);
        seat->is_selecting = true;
        seat->is_initial_selection = true;
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

    uint32_t codepoint = xkb_state_key_get_utf32(seat->xkb_state, keycode);
    if (codepoint != 0 && codepoint < 32)
        return false;

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
    seat->is_initial_selection = false;
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
    anthy_get_segment_stat(
        seat->anthy_context, seat->current_segment, &segment_stat);

    if (keysym >= XKB_KEY_1 && keysym <= XKB_KEY_9) {
        int n = keysym - XKB_KEY_1;
        if (n < segment_stat.nr_candidate - 1)
            seat->selected_candidates[seat->current_segment] = n;
        seat->is_initial_selection = false;
        anthywl_seat_selecting_update(seat);
        return true;
    }

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
    case XKB_KEY_space:
        if (seat->selected_candidates[seat->current_segment]
            != segment_stat.nr_candidate - 1)
        {
            seat->selected_candidates[seat->current_segment] += 1;
        }
        seat->is_initial_selection = false;
        anthywl_seat_selecting_update(seat);
        return true;
    case XKB_KEY_Escape:
    case XKB_KEY_BackSpace:
        seat->is_selecting = false;
        seat->is_initial_selection = false;
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
        seat->is_initial_selection = false;
        anthywl_seat_selecting_update(seat);
        return true;
    case XKB_KEY_Down:
        if (seat->selected_candidates[seat->current_segment]
            != segment_stat.nr_candidate - 1)
        {
            seat->selected_candidates[seat->current_segment] += 1;
        }
        seat->is_initial_selection = false;
        anthywl_seat_selecting_update(seat);
        return true;
    default:
        anthywl_seat_selecting_commit(seat);
        // FIXME: this key should be handled by the IME again after this
        return false;
    }
}

static bool anthywl_seat_handle_key(struct anthywl_seat *seat,
    xkb_keycode_t keycode)
{
    xkb_keysym_t keysym = xkb_state_key_get_one_sym(seat->xkb_state, keycode);
    if (seat->is_selecting)
        return anthywl_seat_selecting_handle_key_event(seat, keycode);
    if (seat->is_composing)
        return anthywl_seat_composing_handle_key_event(seat, keycode);
    if (keysym == XKB_KEY_Henkan_Mode) {
        seat->is_composing = true;
        return true;
    }
    return false;
}

static void timespec_correct(struct timespec *ts) {
    if (ts->tv_nsec >= 1000000000) {
        ts->tv_sec += 1;
        ts->tv_nsec -= 1000000000;
    }
}

static void anthywl_seat_repeat_timer_callback(struct anthywl_timer *timer) {
    struct anthywl_seat *seat = wl_container_of(timer, seat, repeat_timer);
    if (!anthywl_seat_handle_key(seat, seat->repeating_keycode)) {
        wl_list_remove(&timer->link);
        seat->repeating_keycode = 0;
    } else {
        clock_gettime(CLOCK_MONOTONIC, &seat->repeat_timer.time);
        timer->time.tv_nsec += 1000000000 / seat->repeat_rate;
        timespec_correct(&timer->time);
        timer->callback = anthywl_seat_repeat_timer_callback;
    }
}

static void zwp_input_method_keyboard_grab_v2_keymap(void *data,
    struct zwp_input_method_keyboard_grab_v2 *zwp_input_method_keyboard_grab_v2,
    uint32_t format, int32_t fd, uint32_t size)
{
    struct anthywl_seat *seat = data;
    char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (seat->xkb_keymap_string == NULL
        || strcmp(seat->xkb_keymap_string, map) != 0)
    {
        zwp_virtual_keyboard_v1_keymap(
            seat->zwp_virtual_keyboard_v1, format, fd, size);
        xkb_keymap_unref(seat->xkb_keymap);
        xkb_state_unref(seat->xkb_state);
        seat->xkb_keymap = xkb_keymap_new_from_string(seat->xkb_context, map,
            XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
        seat->xkb_state = xkb_state_new(seat->xkb_keymap);
        free(seat->xkb_keymap_string);
        seat->xkb_keymap_string = strdup(map);
    }
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
        if (xkb_keymap_key_repeats(seat->xkb_keymap, keycode)) {
            seat->repeating_keycode = keycode;
            clock_gettime(CLOCK_MONOTONIC, &seat->repeat_timer.time);
            seat->repeat_timer.time.tv_nsec += seat->repeat_delay * 1000000;
            timespec_correct(&seat->repeat_timer.time);
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
        timespec_correct(&seat->repeat_timer.time);
        seat->repeat_timer.callback = anthywl_seat_repeat_timer_callback;
        wl_list_insert(&seat->state->timers, &seat->repeat_timer.link);
        return;
    }

    if (state == WL_KEYBOARD_KEY_STATE_PRESSED && handled) {
        for (size_t i = 0;
            i < sizeof seat->pressed / sizeof seat->pressed[0]; i++)
        {
            if (seat->pressed[i] == 0) {
                seat->pressed[i] = keycode;
                goto forward;
            }
        }
    }

    if (state == WL_KEYBOARD_KEY_STATE_RELEASED) {
        for (size_t i = 0;
            i < sizeof seat->pressed / sizeof seat->pressed[0]; i++)
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
    uint32_t serial,
    uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked,
    uint32_t group)
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

static void zwp_input_method_v2_activate(
    void *data, struct zwp_input_method_v2 *zwp_input_method_v2)
{
    struct anthywl_seat *seat = data;
    seat->pending_activate = true;
    free(seat->pending_surrounding_text);
    seat->pending_surrounding_text = NULL;
    seat->pending_text_change_cause = 0;
    seat->pending_content_type_hint = 0;
    seat->pending_content_type_purpose = 0;
}

static void zwp_input_method_v2_deactivate(
    void *data, struct zwp_input_method_v2 *zwp_input_method_v2)
{
    struct anthywl_seat *seat = data;
    seat->pending_activate = false;
}

static void zwp_input_method_v2_surrounding_text(
    void *data, struct zwp_input_method_v2 *zwp_input_method_v2,
    char const *text, uint32_t cursor, uint32_t anchor)
{
    struct anthywl_seat *seat = data;
    free(seat->pending_surrounding_text);
    seat->pending_surrounding_text = strdup(text);
    seat->pending_surrounding_text_cursor = cursor;
    seat->pending_surrounding_text_anchor = anchor;
}

static void zwp_input_method_v2_text_change_cause(
    void *data, struct zwp_input_method_v2 *zwp_input_method_v2,
    uint32_t cause)
{
    struct anthywl_seat *seat = data;
    seat->pending_text_change_cause = cause;
}

static void zwp_input_method_v2_content_type(
    void *data, struct zwp_input_method_v2 *zwp_input_method_v2,
    uint32_t hint, uint32_t purpose)
{
    struct anthywl_seat *seat = data;
    seat->pending_content_type_hint = hint;
    seat->pending_content_type_purpose = purpose;
}

static void zwp_input_method_v2_done(
    void *data, struct zwp_input_method_v2 *zwp_input_method_v2)
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

static void zwp_input_method_v2_unavailable(
    void *data, struct zwp_input_method_v2 *zwp_input_method_v2)
{
    struct anthywl_seat *seat = data;
    fprintf(stderr, "Input method unavailable on seat \"%s\".\n", seat->name);
    anthywl_seat_destroy(seat);
}

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

static void wl_seat_global(struct anthywl_state *state, void *data) {
    struct wl_seat *wl_seat = data;
    struct anthywl_seat *seat = calloc(1, sizeof *seat);
    anthywl_seat_init(seat, state, wl_seat);
    wl_list_insert(&state->seats, &seat->link);
}

struct anthywl_global {
    char const *name;
    struct wl_interface const *interface;
    int version;
    bool is_singleton;
    union {
        ptrdiff_t offset;
        void (*callback)(struct anthywl_state *state, void *data);
    };
};

static int anthywl_global_compare(const void *a, const void *b) {
    return strcmp(
        ((struct anthywl_global const *)a)->name,
        ((struct anthywl_global const *)b)->name);
}

static struct anthywl_global const globals[] = {
    {
        .name = "wl_compositor",
        .interface = &wl_compositor_interface,
        .version = 4,
        .is_singleton = true,
        .offset = offsetof(struct anthywl_state, wl_compositor),
    },
    {
        .name = "wl_seat",
        .interface = &wl_seat_interface,
        .version = 7,
        .is_singleton = false,
        .callback = wl_seat_global,
    },
    {
        .name = "wl_shm",
        .interface = &wl_shm_interface,
        .version = 1,
        .is_singleton = true,
        .offset = offsetof(struct anthywl_state, wl_shm),
    },
    {
        .name = "zwp_input_method_manager_v2",
        .interface = &zwp_input_method_manager_v2_interface,
        .version = 1,
        .is_singleton = true,
        .offset = offsetof(struct anthywl_state, zwp_input_method_manager_v2),
    },
    {
        .name = "zwp_virtual_keyboard_manager_v1",
        .interface = &zwp_virtual_keyboard_manager_v1_interface,
        .version = 1,
        .is_singleton = true,
        .offset = offsetof(struct anthywl_state, zwp_virtual_keyboard_manager_v1),
    },
};

static void wl_registry_global(void *data, struct wl_registry *wl_registry,
    uint32_t name, char const *interface, uint32_t version)
{
    struct anthywl_global global = { .name = interface };
    struct anthywl_global *found = bsearch(&global, globals,
        sizeof globals / sizeof(struct anthywl_global),
        sizeof(struct anthywl_global), anthywl_global_compare);

    if (found == NULL)
        return;

    if (found->is_singleton) {
        *(void **)((uintptr_t)data + found->offset) = wl_registry_bind(
            wl_registry, name, found->interface, found->version);
    } else {
        found->callback(data, wl_registry_bind(
            wl_registry, name, found->interface, found->version));
    }
}

static void wl_registry_global_remove(void *data,
    struct wl_registry *wl_registry, uint32_t name)
{
}

static struct wl_registry_listener const wl_registry_listener = {
    .global = wl_registry_global,
    .global_remove = wl_registry_global_remove,
};

static bool anthywl_state_init(struct anthywl_state *state) {
    wl_list_init(&state->buffers);
    wl_list_init(&state->seats);
    wl_list_init(&state->timers);
 
    state->wl_display = wl_display_connect(NULL);
    if (state->wl_display == NULL) {
        perror("wl_display_connect");
        return false;
    }

    state->wl_registry = wl_display_get_registry(state->wl_display);
    wl_registry_add_listener(state->wl_registry, &wl_registry_listener, state);
    wl_display_roundtrip(state->wl_display);

    for (size_t i = 0; i < sizeof globals / sizeof globals[0]; i++) {
        const struct anthywl_global *global = &globals[i];
        if (!global->is_singleton)
            continue;
        struct wl_proxy **location =
            (struct wl_proxy **)((uintptr_t)state + global->offset);
        if (*location == NULL) {
            fprintf(
                stderr, "required interface unsupported by compositor: %s\n",
                global->name);
            return false;
        }
    }

    struct anthywl_seat *seat;
    wl_list_for_each(seat, &state->seats, link)
        anthywl_seat_init_protocols(seat);

    wl_display_flush(state->wl_display);

    return true;
}

static bool interrupted;
static void sigint(int signal) { interrupted = true; }

static int anthywl_state_next_timer(struct anthywl_state *state) {
    int timeout = INT_MAX;
    if (wl_list_empty(&state->timers))
        return -1;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    struct anthywl_timer *timer;
    wl_list_for_each(timer, &state->timers, link) {
        int time =
            (timer->time.tv_sec - now.tv_sec) * 1000 +
            (timer->time.tv_nsec - now.tv_nsec) / 1000000;
        if (time < timeout)
            timeout = time;
    }
    return timeout;
}

static void anthywl_state_run_timers(struct anthywl_state *state) {
    if (wl_list_empty(&state->timers))
        return;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    struct anthywl_timer *timer, *tmp;
    wl_list_for_each_safe(timer, tmp, &state->timers, link) {
        bool expired = timer->time.tv_sec < now.tv_sec ||
            (timer->time.tv_sec == now.tv_sec &&
             timer->time.tv_nsec < now.tv_nsec);
         if (expired)
             timer->callback(timer);
    }
}

static void anthywl_state_run(struct anthywl_state *state) {
    state->running = true;
    signal(SIGINT, sigint);
    while (state->running && !interrupted) {
        struct pollfd pfd = {
            .fd = wl_display_get_fd(state->wl_display),
            .events = POLLIN,
        };

        if (poll(&pfd, 1, anthywl_state_next_timer(state)) == -1) {
            if (errno == EINTR)
                continue;
            perror("poll");
            break;
        }

        while (wl_display_prepare_read(state->wl_display) != 0)
            wl_display_dispatch_pending(state->wl_display);

        if (pfd.events & POLLIN)
            wl_display_read_events(state->wl_display);
        else
            wl_display_cancel_read(state->wl_display);

        if (wl_display_dispatch_pending(state->wl_display) == -1) {
            perror("wl_display_dispatch");
            break;
        }

        anthywl_state_run_timers(state);

        wl_display_flush(state->wl_display);

        if (wl_list_empty(&state->seats)) {
            fprintf(stderr, "No seats with input-method available.\n");
            break;
        }
    }
    signal(SIGINT, SIG_DFL);
    state->running = false;
}

static void anthywl_state_finish(struct anthywl_state *state) {
    struct anthywl_seat *seat, *tmp_seat;
    wl_list_for_each_safe(seat, tmp_seat, &state->seats, link)
        anthywl_seat_destroy(seat);
    struct anthywl_graphics_buffer *graphics_buffer, *tmp_graphics_buffer;
    wl_list_for_each_safe(
        graphics_buffer, tmp_graphics_buffer, &state->buffers, link)
    {
        anthywl_graphics_buffer_destroy(graphics_buffer);
    }
    if (state->zwp_virtual_keyboard_manager_v1 != NULL) {
        zwp_virtual_keyboard_manager_v1_destroy(
            state->zwp_virtual_keyboard_manager_v1);
    }
    if (state->zwp_input_method_manager_v2 != NULL)
        zwp_input_method_manager_v2_destroy(state->zwp_input_method_manager_v2);
    if (state->wl_shm != NULL)
        wl_shm_destroy(state->wl_shm);
    if (state->wl_compositor != NULL)
        wl_compositor_destroy(state->wl_compositor);
    if (state->wl_registry != NULL)
        wl_registry_destroy(state->wl_registry);
    if (state->wl_display != NULL)
        wl_display_disconnect(state->wl_display);
}

int main(void) {
    if (anthy_init() != 0) {
        perror("anthy_init");
        return 1;
    }
    atexit(anthy_quit);
    struct anthywl_state state = {0};
    if (!anthywl_state_init(&state))
        return 1;
    anthywl_state_run(&state);
    anthywl_state_finish(&state);
}
