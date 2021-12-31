#include <assert.h>
#include <errno.h>
#include <limits.h>
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
#include <scfg.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

#include "input-method-unstable-v2-client-protocol.h"
#include "text-input-unstable-v3-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"

#include "buffer.h"
#include "graphics_buffer.h"

#define min(a, b)               \
    ({                          \
        __typeof__(a) _a = (a); \
        __typeof__(b) _b = (b); \
        _a < _b ? _a : _b;      \
    })

#define max(a, b)               \
    ({                          \
        __typeof__(a) _a = (a); \
        __typeof__(b) _b = (b); \
        _a > _b ? _a : _b;      \
    })

enum anthywl_modifier {
    ANTHYWL_SHIFT_INDEX,
    ANTHYWL_CAPS_INDEX,
    ANTHYWL_CTRL_INDEX,
    ANTHYWL_ALT_INDEX,
    ANTHYWL_NUM_INDEX,
    ANTHYWL_MOD3_INDEX,
    ANTHYWL_LOGO_INDEX,
    ANTHYWL_MOD5_INDEX,

    ANTHYWL_MODIFIER_COUNT,

    ANTHYWL_SHIFT = 1 << ANTHYWL_SHIFT_INDEX,
    ANTHYWL_CAPS = 1 << ANTHYWL_CAPS_INDEX,
    ANTHYWL_CTRL = 1 << ANTHYWL_CTRL_INDEX,
    ANTHYWL_ALT = 1 << ANTHYWL_ALT_INDEX,
    ANTHYWL_NUM = 1 << ANTHYWL_NUM_INDEX,
    ANTHYWL_MOD3 = 1 << ANTHYWL_MOD3_INDEX,
    ANTHYWL_LOGO = 1 << ANTHYWL_LOGO_INDEX,
    ANTHYWL_MOD5 = 1 << ANTHYWL_MOD5_INDEX,
};

enum anthywl_action {
    ANTHYWL_ACTION_INVALID,
    ANTHYWL_ACTION_ENABLE,
    ANTHYWL_ACTION_DISABLE,
    ANTHYWL_ACTION_TOGGLE,
    ANTHYWL_ACTION_DELETE_LEFT,
    ANTHYWL_ACTION_DELETE_RIGHT,
    ANTHYWL_ACTION_MOVE_LEFT,
    ANTHYWL_ACTION_MOVE_RIGHT,
    ANTHYWL_ACTION_EXPAND_LEFT,
    ANTHYWL_ACTION_EXPAND_RIGHT,
    ANTHYWL_ACTION_SELECT,
    ANTHYWL_ACTION_COMPOSE,
    ANTHYWL_ACTION_ACCEPT,
    ANTHYWL_ACTION_DISCARD,
    ANTHYWL_ACTION_PREV_CANDIDATE,
    ANTHYWL_ACTION_NEXT_CANDIDATE,
    ANTHYWL_ACTION_CYCLE_CANDIDATE,
    _ANTHYWL_ACTION_LAST,
};

struct anthywl_state {
    bool running;
    struct wl_display *wl_display;
    struct wl_registry *wl_registry;
    struct wl_compositor *wl_compositor;
    struct wl_shm *wl_shm;
    struct zwp_input_method_manager_v2 *zwp_input_method_manager_v2;
    struct zwp_virtual_keyboard_manager_v1 *zwp_virtual_keyboard_manager_v1;
    struct wl_cursor_theme *wl_cursor_theme;
    int wl_cursor_theme_size;
    int wl_cursor_theme_scale;
    struct wl_list buffers;
    struct wl_list seats;
    struct wl_list outputs;
    struct wl_list timers;
    struct wl_array global_bindings;
    struct wl_array composing_bindings;
    struct wl_array selecting_bindings;
    int max_scale;
};

struct anthywl_timer {
    struct wl_list link;
    struct timespec time;
    void (*callback)(struct anthywl_timer *timer);
};

struct anthywl_output {
    struct wl_list link;
    struct anthywl_state *state;
    struct wl_output *wl_output;
    int pending_scale, scale;
};

struct anthywl_seat {
    struct wl_list link;
    struct anthywl_state *state;
    struct wl_seat *wl_seat;

    bool are_protocols_initted;
    struct zwp_input_method_v2 *zwp_input_method_v2;
    struct zwp_input_method_keyboard_grab_v2 *zwp_input_method_keyboard_grab_v2;
    struct zwp_virtual_keyboard_v1 *zwp_virtual_keyboard_v1;

    // wl_pointer
    struct wl_pointer *wl_pointer;
    uint32_t wl_pointer_serial;
    struct wl_surface *wl_surface_cursor;
    struct anthywl_timer cursor_timer;

    char *xkb_keymap_string;
    struct xkb_context *xkb_context;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;
    xkb_mod_index_t mod_indices[ANTHYWL_MODIFIER_COUNT];
    struct wl_array global_bindings;
    struct wl_array composing_bindings;
    struct wl_array selecting_bindings;

    // wl_seat
    char *name;

    // wl_output
    struct wl_array outputs;
    int scale;

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
    uint32_t repeating_timestamp;
    struct anthywl_timer repeat_timer;

    // popup
    struct anthywl_buffer buffer;

    // composing
    bool is_composing;
    bool is_composing_popup_visible;

    // selecting
    bool is_selecting;
    bool is_selecting_popup_visible;
    int current_segment;
    int segment_count;
    int *selected_candidates;
    anthy_context_t anthy_context;

    // popup
    struct wl_surface *wl_surface;
    struct zwp_input_popup_surface_v2 *zwp_input_popup_surface_v2;
};

struct anthywl_binding {
    xkb_keysym_t keysym;
    enum anthywl_modifier modifiers;
    enum anthywl_action action;
};

struct anthywl_seat_binding {
    xkb_keycode_t keycode;
    xkb_mod_mask_t mod_mask;
    enum anthywl_action action;
};

static char const default_config[] =
    "global-bindings {\n"
    "    Ctrl+Shift+Backspace toggle\n"
    "}\n\n"
    "composing-bindings {\n"
    "    space select\n"
    "    Return accept\n"
    "    Escape discard\n"
    "    Backspace delete-left\n"
    "    Left move-left\n"
    "    Left move-right\n"
    "}\n\n"
    "selecting-bindings {\n"
    "    Escape discard\n"
    "    Return accept\n"
    "    BackSpace delete-left\n"
    "    Left move-left\n"
    "    Right move-right\n"
    "    Shift+Left expand-left\n"
    "    Shift+Right expand-right\n"
    "    Up prev-candidate\n"
    "    Down next-candidate\n"
    "    space cycle-candidate\n"
    "}\n";

static enum anthywl_action anthywl_action_from_string(const char *name) {
    if (strcmp(name, "enable") == 0)
        return ANTHYWL_ACTION_ENABLE;
    if (strcmp(name, "disable") == 0)
        return ANTHYWL_ACTION_DISABLE;
    if (strcmp(name, "toggle") == 0)
        return ANTHYWL_ACTION_TOGGLE;
    if (strcmp(name, "delete-left") == 0)
        return ANTHYWL_ACTION_DELETE_LEFT;
    if (strcmp(name, "delete-right") == 0)
        return ANTHYWL_ACTION_DELETE_RIGHT;
    if (strcmp(name, "move-left") == 0)
        return ANTHYWL_ACTION_MOVE_LEFT;
    if (strcmp(name, "move-right") == 0)
        return ANTHYWL_ACTION_MOVE_RIGHT;
    if (strcmp(name, "expand-left") == 0)
        return ANTHYWL_ACTION_EXPAND_LEFT;
    if (strcmp(name, "expand-right") == 0)
        return ANTHYWL_ACTION_EXPAND_RIGHT;
    if (strcmp(name, "select") == 0)
        return ANTHYWL_ACTION_SELECT;
    if (strcmp(name, "compose") == 0)
        return ANTHYWL_ACTION_COMPOSE;
    if (strcmp(name, "accept") == 0)
        return ANTHYWL_ACTION_ACCEPT;
    if (strcmp(name, "discard") == 0)
        return ANTHYWL_ACTION_DISCARD;
    if (strcmp(name, "prev-candidate") == 0)
        return ANTHYWL_ACTION_PREV_CANDIDATE;
    if (strcmp(name, "next-candidate") == 0)
        return ANTHYWL_ACTION_NEXT_CANDIDATE;
    if (strcmp(name, "cycle-candidate") == 0)
        return ANTHYWL_ACTION_CYCLE_CANDIDATE;
    return ANTHYWL_ACTION_INVALID;
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
    struct anthywl_seat *seat, int scale)
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
        surface_width * scale, surface_height * scale);
    cairo_scale(buffer->cairo, scale, scale);
    cairo_set_operator(buffer->cairo, CAIRO_OPERATOR_CLEAR);
    cairo_paint(buffer->cairo);
    cairo_set_operator(buffer->cairo, CAIRO_OPERATOR_OVER);
    cairo_set_source_rgba(buffer->cairo, 0.0, 0.0, 0.0, 1.0);
    cairo_paint(buffer->cairo);
    cairo_set_source_surface(buffer->cairo, recording_cairo_surface, 0.0, 0.0);
    cairo_paint(buffer->cairo);

    return buffer;
}

static struct anthywl_graphics_buffer *anthywl_seat_selecting_draw_popup(
    struct anthywl_seat *seat, int scale)
{
    cairo_surface_t *recording_cairo_surface =
        cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA, NULL);
    cairo_t *recording_cairo = cairo_create(recording_cairo_surface);
    PangoLayout *layout = pango_cairo_create_layout(recording_cairo);

    double x = BORDER + PADDING, y = BORDER + PADDING;
    double line_y = 0;
    double max_x = 0;
    cairo_move_to(recording_cairo, x, y);

    if (seat->is_composing_popup_visible) {
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
        int selected_candidate =
            seat->selected_candidates[seat->current_segment];
        int candidate_offset = selected_candidate / 5 * 5;
        for (int i = candidate_offset;
            i < min(candidate_offset + 5, segment_stat.nr_candidate); i++)
        {
            anthy_get_segment(
                seat->anthy_context, seat->current_segment, i, buf, sizeof buf);
            PangoAttrList *attrs = pango_attr_list_new();
            if (i == selected_candidate) {
                PangoAttribute *attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
                pango_attr_list_insert(attrs, attr);
            }
            char *text;
            asprintf(&text, "%d. %s", i - candidate_offset + 1, buf);
            pango_layout_set_text(layout, text, -1);
            free(text);
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

    if (seat->is_composing_popup_visible) {
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
        surface_width * scale, surface_height * scale);
    cairo_scale(buffer->cairo, scale, scale);
    cairo_set_operator(buffer->cairo, CAIRO_OPERATOR_CLEAR);
    cairo_paint(buffer->cairo);
    cairo_set_operator(buffer->cairo, CAIRO_OPERATOR_OVER);
    cairo_set_source_rgba(buffer->cairo, 0.0, 0.0, 0.0, 1.0);
    cairo_paint(buffer->cairo);
    cairo_set_source_surface(buffer->cairo, recording_cairo_surface, 0.0, 0.0);
    cairo_paint(buffer->cairo);

    return buffer;
}

static void anthywl_seat_draw_popup(struct anthywl_seat *seat) {
    int scale = seat->scale != 0 ? seat->scale : seat->state->max_scale;

    struct anthywl_graphics_buffer *buffer = NULL;
    if (seat->is_selecting && seat->is_selecting_popup_visible) {
        buffer = anthywl_seat_selecting_draw_popup(seat, scale);
    } else if (seat->is_composing
        && seat->buffer.len != 0
        && seat->is_composing_popup_visible)
    {
        buffer = anthywl_seat_composing_draw_popup(seat, scale);
    }

    if (buffer) {
        wl_surface_attach(seat->wl_surface, buffer->wl_buffer, 0, 0);
        wl_surface_damage_buffer(seat->wl_surface, 0, 0,
            buffer->width, buffer->height);
        wl_surface_set_buffer_scale(seat->wl_surface, scale);
    } else {
        wl_surface_attach(seat->wl_surface, NULL, 0, 0);
    }

    wl_surface_commit(seat->wl_surface);
}

static struct wl_seat_listener const wl_seat_listener;
static void anthywl_seat_init_protocols(struct anthywl_seat *seat);
static void anthywl_seat_cursor_timer_callback(struct anthywl_timer *timer);
static void anthywl_seat_repeat_timer_callback(struct anthywl_timer *timer);

static void anthywl_seat_init(struct anthywl_seat *seat,
    struct anthywl_state *state, struct wl_seat *wl_seat)
{
    seat->state = state;
    seat->wl_seat = wl_seat;
    wl_seat_add_listener(wl_seat, &wl_seat_listener, seat);
    seat->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    seat->cursor_timer.callback = anthywl_seat_cursor_timer_callback;
    wl_array_init(&seat->outputs);
    if (state->running)
        anthywl_seat_init_protocols(seat);
    anthywl_buffer_init(&seat->buffer);
    seat->anthy_context = anthy_create_context();
    anthy_context_set_encoding(seat->anthy_context, ANTHY_UTF8_ENCODING);
    seat->repeat_timer.callback = anthywl_seat_repeat_timer_callback;
    seat->is_composing = true;
}

static struct zwp_input_method_v2_listener const zwp_input_method_v2_listener;
static struct zwp_input_method_keyboard_grab_v2_listener const
    zwp_input_method_keyboard_grab_v2_listener;

static void wl_surface_enter(void *data, struct wl_surface *wl_surface,
    struct wl_output *wl_output)
{
    struct anthywl_seat *seat = data;
    struct anthywl_output *output = wl_output_get_user_data(wl_output);
    struct anthywl_output **output_iter;
    wl_array_for_each(output_iter, &seat->outputs) {
        if (*output_iter == NULL) {
            *output_iter = output;
            goto rescale;
        }
    }
    *(void **)wl_array_add(&seat->outputs, sizeof(void *)) = output;
rescale:;
    int scale = output->scale > seat->scale ? output->scale : seat->scale;
    seat->scale = scale;
}

static void wl_surface_leave(void *data, struct wl_surface *wl_surface,
    struct wl_output *wl_output)
{
    struct anthywl_seat *seat = data;
    struct anthywl_output *output = wl_output_get_user_data(wl_output);
    seat->scale = output->scale;
    struct anthywl_output **output_iter;
    wl_array_for_each(output_iter, &seat->outputs) {
        if (*output_iter == output) {
            *output_iter = NULL;
            break;
        }
    }
    int scale = 0;
    wl_array_for_each(output_iter, &seat->outputs) {
        int scale_iter = *output_iter != NULL
            ? (*output_iter)->scale
            : 0;
        scale = scale_iter > scale ? scale_iter : scale;
    }
    seat->scale = scale;
}

static struct wl_surface_listener const wl_surface_listener = {
    .enter = wl_surface_enter,
    .leave = wl_surface_leave,
};

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
    wl_surface_add_listener(seat->wl_surface, &wl_surface_listener, seat);
    anthywl_seat_draw_popup(seat);
    seat->zwp_input_popup_surface_v2 =
        zwp_input_method_v2_get_input_popup_surface(
            seat->zwp_input_method_v2, seat->wl_surface);
    zwp_input_popup_surface_v2_add_listener(seat->zwp_input_popup_surface_v2,
        &zwp_input_popup_surface_v2_listener, seat);
    seat->wl_surface_cursor =
        wl_compositor_create_surface(seat->state->wl_compositor);
    wl_surface_add_listener(
        seat->wl_surface_cursor, &wl_surface_listener, seat);
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
    seat->is_selecting_popup_visible = false;
    anthywl_buffer_clear(&seat->buffer);

    anthywl_seat_draw_popup(seat);

    wl_array_release(&buffer);
}

static int anthywl_binding_compare(void const *_a, void const *_b) {
    const struct anthywl_binding *a = _a;
    const struct anthywl_binding *b = _b;
    int keysym = a->keysym - b->keysym;
    if (keysym != 0)
        return keysym;
    int modifiers = a->modifiers - b->modifiers;
    if (modifiers != 0)
        return modifiers;
    return a->action - b->action;
}

static int anthywl_seat_binding_compare(void const *_a, void const *_b) {
    const struct anthywl_seat_binding *a = _a;
    const struct anthywl_seat_binding *b = _b;
    int keycode = a->keycode - b->keycode;
    if (keycode != 0)
        return keycode;
    int mod_mask = a->mod_mask - b->mod_mask;
    if (mod_mask != 0)
        return mod_mask;
    return a->action - b->action;
}

static int anthywl_seat_binding_compare_without_action(
    void const *_a, void const *_b)
{
    const struct anthywl_seat_binding *a = _a;
    const struct anthywl_seat_binding *b = _b;
    int keycode = a->keycode - b->keycode;
    if (keycode != 0)
        return keycode;
    return a->mod_mask - b->mod_mask;
}


static bool anthywl_seat_handle_enable(struct anthywl_seat *seat) {
    seat->is_composing = true;
    return true;
}

static bool anthywl_seat_handle_disable(struct anthywl_seat *seat) {
    seat->is_composing = false;
    seat->is_selecting_popup_visible = false;
    anthywl_buffer_clear(&seat->buffer);
    anthywl_seat_composing_update(seat);
    return true;
}

static bool anthywl_seat_handle_toggle(struct anthywl_seat *seat) {
    if (seat->is_composing)
        return anthywl_seat_handle_disable(seat);
    else
        return anthywl_seat_handle_enable(seat);
}

static bool anthywl_seat_handle_delete_left(struct anthywl_seat *seat) {
    if (!seat->is_composing)
        return true;
    if (seat->buffer.len == 0)
        return true;
    if (seat->is_selecting) {
        seat->is_selecting = false;
        seat->is_selecting_popup_visible = false;
    }
    anthywl_buffer_delete_backwards(&seat->buffer, 1);
    anthywl_seat_composing_update(seat);
    return true;
}

static bool anthywl_seat_handle_delete_right(struct anthywl_seat *seat) {
    if (!seat->is_composing)
        return true;
    if (seat->buffer.len == 0)
        return true;
    if (seat->is_selecting) {
        seat->is_selecting = false;
        seat->is_selecting_popup_visible = false;
    }
    anthywl_buffer_delete_forwards(&seat->buffer, 1);
    anthywl_seat_composing_update(seat);
    return true;
}

static bool anthywl_seat_handle_move_left(struct anthywl_seat *seat) {
    if (!seat->is_composing)
        return true;
    if (seat->buffer.len == 0)
        return true;
    if (seat->is_selecting) {
        anthy_commit_segment(
            seat->anthy_context,
            seat->current_segment,
            seat->selected_candidates[seat->current_segment]);
        if (seat->current_segment != 0)
            seat->current_segment -= 1;
        anthywl_seat_selecting_update(seat);
        return true;
    }
    anthywl_buffer_move_left(&seat->buffer);
    anthywl_seat_composing_update(seat);
    return true;
}

static bool anthywl_seat_handle_move_right(struct anthywl_seat *seat) {
    if (!seat->is_composing)
        return true;
    if (seat->buffer.len == 0)
        return true;
    if (seat->is_selecting) {
        anthy_commit_segment(
            seat->anthy_context,
            seat->current_segment,
            seat->selected_candidates[seat->current_segment]);
        if (seat->current_segment != seat->segment_count - 1)
            seat->current_segment += 1;
        anthywl_seat_selecting_update(seat);
        return true;
    }
    anthywl_buffer_move_right(&seat->buffer);
    anthywl_seat_composing_update(seat);
    return true;
}

static void anthywl_seat_expand(struct anthywl_seat *seat, int amount) {
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
}

static bool anthywl_seat_handle_expand_left(struct anthywl_seat *seat) {
    if (!seat->is_selecting)
        return true;
    anthywl_seat_expand(seat, -1);
    return true;
}

static bool anthywl_seat_handle_expand_right(struct anthywl_seat *seat) {
    if (!seat->is_selecting)
        return true;
    anthywl_seat_expand(seat, 1);
    return true;
}

static bool anthywl_seat_handle_select(struct anthywl_seat *seat) {
    if (!seat->is_composing)
        return true;
    if (seat->is_selecting)
        return true;
    if (seat->buffer.len == 0)
        return true;
    anthywl_buffer_convert_trailing_n(&seat->buffer);
    seat->is_selecting = true;
    seat->is_selecting_popup_visible = seat->is_composing_popup_visible;
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
}

static bool anthywl_seat_handle_compose(struct anthywl_seat *seat) {
    if (!seat->is_composing)
        return anthywl_seat_handle_enable(seat);
    if (!seat->is_selecting)
        return true;
    seat->is_selecting = false;
    anthywl_seat_composing_update(seat);
    return true;
}

static bool anthywl_seat_handle_accept(struct anthywl_seat *seat) {
    if (!seat->is_composing)
        return true;
    if (seat->buffer.len == 0)
        return true;
    if (seat->is_selecting)
        anthywl_seat_selecting_commit(seat);
    else
        anthywl_seat_composing_commit(seat);
    return true;
}

static bool anthywl_seat_handle_discard(struct anthywl_seat *seat) {
    if (!seat->is_composing)
        return true;
    if (seat->is_selecting)
        seat->is_selecting = false;
    anthywl_buffer_clear(&seat->buffer);
    anthywl_seat_composing_commit(seat);
    return true;
}

static bool anthywl_seat_handle_prev_candidate(struct anthywl_seat *seat) {
    if (!seat->is_selecting)
        return true;

    struct anthy_conv_stat conv_stat;
    anthy_get_stat(seat->anthy_context, &conv_stat);
    assert(conv_stat.nr_segment == seat->segment_count);

    struct anthy_segment_stat segment_stat;
    anthy_get_segment_stat(
        seat->anthy_context, seat->current_segment, &segment_stat);

    if (seat->selected_candidates[seat->current_segment] != 0)
        seat->selected_candidates[seat->current_segment] -= 1;
    seat->is_selecting_popup_visible = true;
    anthywl_seat_selecting_update(seat);

    return true;
}


static bool anthywl_seat_handle_next_candidate(struct anthywl_seat *seat) {
    if (!seat->is_selecting)
        return true;

    struct anthy_conv_stat conv_stat;
    anthy_get_stat(seat->anthy_context, &conv_stat);
    assert(conv_stat.nr_segment == seat->segment_count);

    struct anthy_segment_stat segment_stat;
    anthy_get_segment_stat(
        seat->anthy_context, seat->current_segment, &segment_stat);

    if (seat->selected_candidates[seat->current_segment]
        != segment_stat.nr_candidate - 1)
    {
        seat->selected_candidates[seat->current_segment] += 1;
    }
    seat->is_selecting_popup_visible = true;
    anthywl_seat_selecting_update(seat);

    return true;
}

static bool anthywl_seat_handle_cycle_candidate(struct anthywl_seat *seat) {
    if (!seat->is_selecting)
        return true;

    struct anthy_conv_stat conv_stat;
    anthy_get_stat(seat->anthy_context, &conv_stat);
    assert(conv_stat.nr_segment == seat->segment_count);

    struct anthy_segment_stat segment_stat;
    anthy_get_segment_stat(
        seat->anthy_context, seat->current_segment, &segment_stat);

    if (seat->selected_candidates[seat->current_segment]
        != segment_stat.nr_candidate - 1)
    {
        seat->selected_candidates[seat->current_segment] += 1;
    }
    seat->is_selecting_popup_visible = true;
    anthywl_seat_selecting_update(seat);

    return true;
}

static bool(*anthywl_seat_action_handlers[_ANTHYWL_ACTION_LAST])
    (struct anthywl_seat *) =
{
    [ANTHYWL_ACTION_ENABLE] = anthywl_seat_handle_enable,
    [ANTHYWL_ACTION_DISABLE] = anthywl_seat_handle_disable,
    [ANTHYWL_ACTION_TOGGLE] = anthywl_seat_handle_toggle,
    [ANTHYWL_ACTION_DELETE_LEFT] = anthywl_seat_handle_delete_left,
    [ANTHYWL_ACTION_DELETE_RIGHT] = anthywl_seat_handle_delete_right,
    [ANTHYWL_ACTION_MOVE_LEFT] = anthywl_seat_handle_move_left,
    [ANTHYWL_ACTION_MOVE_RIGHT] = anthywl_seat_handle_move_right,
    [ANTHYWL_ACTION_EXPAND_LEFT] = anthywl_seat_handle_expand_left,
    [ANTHYWL_ACTION_EXPAND_RIGHT] = anthywl_seat_handle_expand_right,
    [ANTHYWL_ACTION_SELECT] = anthywl_seat_handle_select,
    [ANTHYWL_ACTION_COMPOSE] = anthywl_seat_handle_compose,
    [ANTHYWL_ACTION_ACCEPT] = anthywl_seat_handle_accept,
    [ANTHYWL_ACTION_DISCARD] = anthywl_seat_handle_discard,
    [ANTHYWL_ACTION_PREV_CANDIDATE] = anthywl_seat_handle_prev_candidate,
    [ANTHYWL_ACTION_NEXT_CANDIDATE] = anthywl_seat_handle_next_candidate,
    [ANTHYWL_ACTION_CYCLE_CANDIDATE] = anthywl_seat_handle_cycle_candidate,
};

static bool anthywl_seat_handle_action(struct anthywl_seat *seat,
    enum anthywl_action action)
{
    if (action <= ANTHYWL_ACTION_INVALID || action >= _ANTHYWL_ACTION_LAST)
        return false;
    bool (*handler)(struct anthywl_seat *)
        = anthywl_seat_action_handlers[action];
    if (!handler)
        return false;
    return handler(seat);
}

static bool anthywl_seat_handle_key_bindings(struct anthywl_seat *seat,
    struct wl_array *bindings, struct anthywl_seat_binding *press)
{
    struct anthywl_seat_binding *found = bsearch(press, bindings->data,
        bindings->size / sizeof(struct anthywl_seat_binding),
        sizeof(struct anthywl_seat_binding),
        anthywl_seat_binding_compare_without_action);
    if (found == NULL)
        return false;
    return anthywl_seat_handle_action(seat, found->action);
}

static bool anthywl_seat_handle_key(struct anthywl_seat *seat,
    xkb_keycode_t keycode)
{
    xkb_keysym_t keysym = xkb_state_key_get_one_sym(seat->xkb_state, keycode);
    struct anthywl_seat_binding press = {
        .keycode = keycode,
        .mod_mask = 0,
        .action = 0,
    };
    xkb_mod_mask_t mod_count = xkb_keymap_num_mods(seat->xkb_keymap);
    for (xkb_mod_index_t i = 0; i < mod_count; i++) {
        bool is_active = xkb_state_mod_index_is_active(seat->xkb_state,
            i, XKB_STATE_MODS_EFFECTIVE);
        bool is_consumed = xkb_state_mod_index_is_consumed(seat->xkb_state,
            keycode, i);
        bool is_relevant = (i != seat->mod_indices[ANTHYWL_CAPS_INDEX])
            && (i != seat->mod_indices[ANTHYWL_NUM_INDEX]);
        (void)is_consumed;
        if (is_active && is_relevant)
            press.mod_mask |= 1 << i;
    }
handle:
    if (seat->is_selecting && anthywl_seat_handle_key_bindings(
        seat, &seat->selecting_bindings, &press))
    {
        return true;
    }
    if (seat->is_composing && seat->buffer.len != 0
        && anthywl_seat_handle_key_bindings(
        seat, &seat->composing_bindings, &press))
    {
        return true;
    }
    if (anthywl_seat_handle_key_bindings(
        seat, &seat->global_bindings, &press))
    {
        return true;
    }
    if (keysym == XKB_KEY_Shift_L
        || keysym == XKB_KEY_Shift_R
        || keysym == XKB_KEY_Control_L
        || keysym == XKB_KEY_Control_R
        || keysym == XKB_KEY_Alt_L
        || keysym == XKB_KEY_Alt_R
        || keysym == XKB_KEY_Super_L
        || keysym == XKB_KEY_Super_R
        || keysym == XKB_KEY_Hyper_L
        || keysym == XKB_KEY_Hyper_R)
    {
        return false;
    }
    if (seat->is_selecting) {
        anthywl_seat_selecting_commit(seat);
        goto handle;
    }
    if (seat->is_composing) {
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

    return false;
}

static void timespec_correct(struct timespec *ts) {
    while (ts->tv_nsec >= 1000000000) {
        ts->tv_sec += 1;
        ts->tv_nsec -= 1000000000;
    }
}

static void anthywl_seat_repeat_timer_callback(struct anthywl_timer *timer) {
    struct anthywl_seat *seat = wl_container_of(timer, seat, repeat_timer);
    seat->repeating_timestamp += 1000 / seat->repeat_rate;
    if (!anthywl_seat_handle_key(seat, seat->repeating_keycode)) {
        wl_list_remove(&timer->link);
        zwp_virtual_keyboard_v1_key(
            seat->zwp_virtual_keyboard_v1, seat->repeating_timestamp,
            seat->repeating_keycode - 8, WL_KEYBOARD_KEY_STATE_PRESSED);
        seat->repeating_keycode = 0;
    } else {
        clock_gettime(CLOCK_MONOTONIC, &seat->repeat_timer.time);
        timer->time.tv_nsec += 1000000000 / seat->repeat_rate;
        timespec_correct(&timer->time);
    }
}

struct keycode_matches {
    struct anthywl_seat *seat;
    xkb_keysym_t keysym;
    struct wl_array keycodes;
};

static void find_keycode(struct xkb_keymap *keymap, xkb_keycode_t keycode,
    void *data)
{
    struct keycode_matches *matches = data;
    xkb_keysym_t keysym = xkb_state_key_get_one_sym(
		matches->seat->xkb_state, keycode);
    if (keysym == XKB_KEY_NoSymbol)
        return;
    if (matches->keysym == keysym) {
        xkb_keycode_t *keycode_spot =
            wl_array_add(&matches->keycodes, sizeof(xkb_keycode_t));
        *keycode_spot = keycode;
    }
}

static void anthywl_seat_setup_bindings(struct anthywl_seat *seat,
    struct wl_array *state_bindings, struct wl_array *seat_bindings)
{
    struct anthywl_binding *state_binding;
    struct keycode_matches matches = {
        .seat = seat,
    };
    wl_array_init(&matches.keycodes);
    wl_array_for_each(state_binding, state_bindings) {
        matches.keysym = state_binding->keysym;
        matches.keycodes.size = 0;
        xkb_keymap_key_for_each(seat->xkb_keymap, find_keycode, &matches);
        assert(matches.keycodes.size);
        xkb_mod_mask_t mod_mask = 0;
        for (int i = 0; i < ANTHYWL_MODIFIER_COUNT; i++) {
            if (state_binding->modifiers & (1 << i)) {
                mod_mask |= 1 << seat->mod_indices[i];
            }
        }
        xkb_keycode_t *keycode;
        wl_array_for_each(keycode, &matches.keycodes) {
            struct anthywl_seat_binding *seat_binding = wl_array_add(
                seat_bindings, sizeof(struct anthywl_seat_binding));
            seat_binding->keycode = *keycode;
            seat_binding->mod_mask = mod_mask;
            seat_binding->action = state_binding->action;
        }
    }
    qsort(seat_bindings->data,
        seat_bindings->size / sizeof(struct anthywl_seat_binding),
        sizeof(struct anthywl_seat_binding), anthywl_seat_binding_compare);
    wl_array_release(&matches.keycodes);
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
        seat->mod_indices[ANTHYWL_SHIFT_INDEX] = xkb_keymap_mod_get_index(
            seat->xkb_keymap, XKB_MOD_NAME_SHIFT);
        seat->mod_indices[ANTHYWL_CAPS_INDEX] = xkb_keymap_mod_get_index(
            seat->xkb_keymap, XKB_MOD_NAME_CAPS);
        seat->mod_indices[ANTHYWL_CTRL_INDEX] = xkb_keymap_mod_get_index(
            seat->xkb_keymap, XKB_MOD_NAME_CTRL);
        seat->mod_indices[ANTHYWL_ALT_INDEX] = xkb_keymap_mod_get_index(
            seat->xkb_keymap, XKB_MOD_NAME_ALT);
        seat->mod_indices[ANTHYWL_NUM_INDEX] = xkb_keymap_mod_get_index(
            seat->xkb_keymap, XKB_MOD_NAME_ALT);
        seat->mod_indices[ANTHYWL_MOD3_INDEX] = xkb_keymap_mod_get_index(
            seat->xkb_keymap, "Mod3");
        seat->mod_indices[ANTHYWL_LOGO_INDEX] = xkb_keymap_mod_get_index(
            seat->xkb_keymap, XKB_MOD_NAME_LOGO);
        seat->mod_indices[ANTHYWL_MOD5_INDEX] = xkb_keymap_mod_get_index(
            seat->xkb_keymap, "Mod5");
        seat->xkb_state = xkb_state_new(seat->xkb_keymap);
        free(seat->xkb_keymap_string);
        seat->xkb_keymap_string = strdup(map);
        anthywl_seat_setup_bindings(
            seat, &seat->state->global_bindings, &seat->global_bindings);
        anthywl_seat_setup_bindings(
            seat, &seat->state->selecting_bindings, &seat->selecting_bindings);
        anthywl_seat_setup_bindings(
            seat, &seat->state->composing_bindings, &seat->composing_bindings);
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
            seat->repeating_timestamp = time + seat->repeat_delay;
            clock_gettime(CLOCK_MONOTONIC, &seat->repeat_timer.time);
            seat->repeat_timer.time.tv_nsec += seat->repeat_delay * 1000000;
            timespec_correct(&seat->repeat_timer.time);
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
        && xkb_keymap_key_repeats(seat->xkb_keymap, keycode)
        && handled)
    {
        seat->repeating_keycode = keycode;
        seat->repeating_timestamp = time + seat->repeat_delay;
        clock_gettime(CLOCK_MONOTONIC, &seat->repeat_timer.time);
        seat->repeat_timer.time.tv_nsec += seat->repeat_delay * 1000000;
        timespec_correct(&seat->repeat_timer.time);
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
        seat->is_composing_popup_visible = false;
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

static void anthywl_seat_cursor_timer_callback(struct anthywl_timer *timer);
static void anthywl_reload_cursor_theme(struct anthywl_state *state);

static void anthywl_seat_cursor_update(struct anthywl_seat *seat) {
    uint32_t duration;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    int scale = seat->state->wl_cursor_theme_scale;
    struct wl_cursor_theme *wl_cursor_theme = seat->state->wl_cursor_theme;
    struct wl_cursor *wl_cursor =
        wl_cursor_theme_get_cursor(wl_cursor_theme, "left_ptr");
    int time_ms = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    int frame =
        wl_cursor_frame_and_duration(wl_cursor, time_ms, &duration);
    struct wl_cursor_image *wl_cursor_image = wl_cursor->images[frame];
    struct wl_buffer *wl_buffer = wl_cursor_image_get_buffer(wl_cursor_image);
    wl_surface_attach(seat->wl_surface_cursor, wl_buffer, 0, 0);
    wl_surface_damage(seat->wl_surface_cursor, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_set_buffer_scale(seat->wl_surface_cursor, scale);
    wl_surface_commit(seat->wl_surface_cursor);
    wl_pointer_set_cursor(
        seat->wl_pointer, seat->wl_pointer_serial, seat->wl_surface_cursor,
        wl_cursor_image->hotspot_x / scale, wl_cursor_image->hotspot_y / scale);
    if (duration == 0) {
        wl_list_remove(&seat->cursor_timer.link);
        wl_list_init(&seat->cursor_timer.link);
    } else {
        clock_gettime(CLOCK_MONOTONIC, &seat->cursor_timer.time);
        seat->cursor_timer.time.tv_nsec += 1000000 * duration;
        timespec_correct(&seat->cursor_timer.time);
    }
}

static void anthywl_seat_cursor_timer_callback(struct anthywl_timer *timer) {
    struct anthywl_seat *seat = wl_container_of(timer, seat, cursor_timer);
    anthywl_seat_cursor_update(seat);
}

static void wl_pointer_enter(void *data, struct wl_pointer *wl_pointer,
    uint32_t serial, struct wl_surface *surface,
    wl_fixed_t surface_x, wl_fixed_t surface_y)
{
    struct anthywl_seat *seat = data;
    seat->wl_pointer_serial = serial;
    wl_list_insert(&seat->state->timers, &seat->cursor_timer.link);
    anthywl_seat_cursor_update(seat);
}

static void wl_pointer_leave(void *data, struct wl_pointer *wl_pointer,
    uint32_t serial, struct wl_surface *surface)
{
    struct anthywl_seat *seat = data;
    wl_list_remove(&seat->cursor_timer.link);
}

static void wl_pointer_motion(void *data, struct wl_pointer *wl_pointer,
    uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y)
{
}

static void wl_pointer_button(void *data, struct wl_pointer *wl_pointer,
    uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
}

static void wl_pointer_axis(void *data, struct wl_pointer *wl_pointer,
    uint32_t time, uint32_t axis, wl_fixed_t value)
{
}

static void wl_pointer_frame(void *data, struct wl_pointer *wl_pointer) {
}

static void wl_pointer_axis_source(void *data, struct wl_pointer *wl_pointer,
    uint32_t axis_source)
{
}

static void wl_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer,
    uint32_t time, uint32_t axis)
{
}

static void wl_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer,
    uint32_t axis, int32_t discrete)
{
}

static struct wl_pointer_listener const wl_pointer_listener = {
    .enter = wl_pointer_enter,
    .leave = wl_pointer_leave,
    .motion = wl_pointer_motion,
    .button = wl_pointer_button,
    .axis = wl_pointer_axis,
    .frame = wl_pointer_frame,
    .axis_source = wl_pointer_axis_source,
    .axis_stop = wl_pointer_axis_stop,
    .axis_discrete = wl_pointer_axis_discrete,
};

static void wl_seat_capabilities(void *data, struct wl_seat *wl_seat,
    uint32_t capabilities)
{
    struct anthywl_seat *seat = data;
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && seat->wl_pointer == NULL)
    {
        seat->wl_pointer = wl_seat_get_pointer(wl_seat);
        wl_pointer_add_listener(seat->wl_pointer, &wl_pointer_listener, seat);
    }
    if (!(capabilities & WL_SEAT_CAPABILITY_POINTER)
        && seat->wl_pointer != NULL)
    {
        wl_pointer_release(seat->wl_pointer);
        seat->wl_pointer = NULL;
    }
}

static void wl_seat_name(void *data, struct wl_seat *wl_seat,
    char const *name)
{
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

static void wl_output_geometry(void *data, struct wl_output *wl_output,
    int32_t x, int32_t y, int32_t physical_width, int32_t physical_height,
    int32_t subpixel, const char *make, const char *model, int32_t transform)
{
}

static void wl_output_mode(void *data, struct wl_output *wl_output,
    uint32_t flags, int32_t width, int32_t height, int32_t refresh)
{
}

static void wl_output_done(void *data, struct wl_output *wl_output) {
    struct anthywl_output *output = data;
    output->scale = output->pending_scale;

    int scale = 0;
    struct anthywl_output *output_iter;
    wl_list_for_each(output_iter, &output->state->outputs, link) {
        if (output_iter->scale > scale)
            scale = output_iter->scale;
    }
    output->state->max_scale = scale;
    if (output->state->wl_cursor_theme_scale != output->state->max_scale / 24)
        anthywl_reload_cursor_theme(output->state);
}

static void wl_output_scale(void *data, struct wl_output *wl_output,
    int32_t factor)
{
    struct anthywl_output *output = data;
    output->pending_scale = factor;
}

static struct wl_output_listener const wl_output_listener = {
    .geometry = wl_output_geometry,
    .mode = wl_output_mode,
    .done = wl_output_done,
    .scale = wl_output_scale,
};

static void wl_output_global(struct anthywl_state *state, void *data) {
    struct wl_output *wl_output = data;
    struct anthywl_output *output = calloc(1, sizeof *output);
    output->state = state;
    output->wl_output = wl_output;
    wl_output_add_listener(output->wl_output, &wl_output_listener, output);
    wl_list_insert(&state->outputs, &output->link);
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

static int anthywl_global_compare(void const *a, void const *b) {
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
        .name = "wl_output",
        .interface = &wl_output_interface,
        .version = 3,
        .is_singleton = false,
        .callback = wl_output_global,
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
    // TODO
}

static struct wl_registry_listener const wl_registry_listener = {
    .global = wl_registry_global,
    .global_remove = wl_registry_global_remove,
};

static void anthywl_reload_cursor_theme(struct anthywl_state *state) {
    const char *cursor_theme = getenv("XCURSOR_THEME");
    const char *env_cursor_size = getenv("XCURSOR_SIZE");
    int cursor_size = 24;
    if (env_cursor_size && strlen(env_cursor_size) > 0) {
        errno = 0;
        char *end;
        unsigned size = strtoul(env_cursor_size, &end, 10);
        if (!*end && errno == 0)
            cursor_size = size;
    }
    if (state->wl_cursor_theme != NULL)
        wl_cursor_theme_destroy(state->wl_cursor_theme);
    state->wl_cursor_theme = wl_cursor_theme_load(
        cursor_theme, cursor_size * state->max_scale, state->wl_shm);
    state->wl_cursor_theme_size = cursor_size;
    state->wl_cursor_theme_scale = state->max_scale;
}


static void anthywl_state_load_config_bindings(struct anthywl_state *state,
    struct scfg_block *block, struct wl_array *bindings)
{
    for (size_t i = 0; i < block->directives_len; i++) {
        struct scfg_directive *directive = &block->directives[i];
        char *s = directive->name, *p;
        struct anthywl_binding binding = { 0 };
        while ((p = strchr(s, '+'))) {
            *p = 0;
            if (strcmp(s, "Shift") == 0)
                binding.modifiers |= ANTHYWL_SHIFT;
            else if (strcmp(s, "Lock") == 0)
                binding.modifiers |= ANTHYWL_CAPS;
            else if (strcmp(s, "Ctrl") == 0 || strcmp(s, "Control") == 0)
                binding.modifiers |= ANTHYWL_CTRL;
            else if (strcmp(s, "Mod1") == 0 || strcmp(s, "Alt") == 0)
                binding.modifiers |= ANTHYWL_ALT;
            else if (strcmp(s, "Mod2") == 0)
                binding.modifiers |= ANTHYWL_NUM;
            else if (strcmp(s, "Mod3") == 0)
                binding.modifiers |= ANTHYWL_MOD3;
            else if (strcmp(s, "Mod4") == 0)
                binding.modifiers |= ANTHYWL_LOGO;
            else if (strcmp(s, "Mod5") == 0)
                binding.modifiers |= ANTHYWL_MOD5;
            else {
                fprintf(stderr, "invalid modifier %s, binding ignored\n", s);
                return;
            }
            s = p + 1;
        }
        binding.keysym = xkb_keysym_from_name(s,  XKB_KEYSYM_CASE_INSENSITIVE);
        if (binding.keysym == XKB_KEY_NoSymbol) {
            fprintf(stderr, "invalid key %s, binding ignored\n", s);
            return;
        }
        binding.action = anthywl_action_from_string(directive->params[0]);
        *(struct anthywl_binding *)wl_array_add(bindings, sizeof binding) =
            binding;
    }
    qsort(bindings->data, bindings->size / sizeof(struct anthywl_binding),
        sizeof(struct anthywl_binding), anthywl_binding_compare);
}

static void anthywl_state_load_config_root(struct anthywl_state *state,
    struct scfg_block *root)
{
    for (size_t i = 0; i < root->directives_len; i++) {
        struct scfg_directive *directive = &root->directives[i];
        if (strcmp(directive->name, "global-bindings") == 0) {
            anthywl_state_load_config_bindings(
                state, &directive->children, &state->global_bindings);
        } else if (strcmp(directive->name, "composing-bindings") == 0) {
            anthywl_state_load_config_bindings(
                state, &directive->children, &state->composing_bindings);
        } else if (strcmp(directive->name, "selecting-bindings") == 0) {
            anthywl_state_load_config_bindings(
                state, &directive->children, &state->selecting_bindings);
        } else {
            fprintf(stderr, "unknown section '%s'\n", directive->name);
        }
    }
}

static bool anthywl_state_load_config(struct anthywl_state *state) {
    char path[PATH_MAX];
    char const *prefix;
    if ((prefix = getenv("XDG_CONFIG_HOME"))) {
        snprintf(path, sizeof path, "%s/anthywl/config", prefix);
    } else if ((prefix = getenv("HOME"))) {
        snprintf(path, sizeof path, "%s/.config/anthywl/config", prefix);
    } else {
        fprintf(stderr, "cannot find config file\n");
        return false;
    }

    FILE *f = fopen(path, "r");
    if (f == NULL)
        f = fmemopen((char *)default_config, sizeof default_config, "r");

    if (f == NULL) {
        perror("failed to open default config file");
        return false;
    }

    struct scfg_block root;
    if (scfg_parse_file(&root, f))
        goto close;
    anthywl_state_load_config_root(state, &root);
    scfg_block_finish(&root);
close:
    fclose(f);
    return true;
}

static bool anthywl_state_init(struct anthywl_state *state) {
    wl_list_init(&state->buffers);
    wl_list_init(&state->seats);
    wl_list_init(&state->outputs);
    wl_list_init(&state->timers);
    wl_array_init(&state->global_bindings);
    wl_array_init(&state->composing_bindings);
    wl_array_init(&state->selecting_bindings);
    state->max_scale = 1;

    if (!anthywl_state_load_config(state))
        return false;

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

    anthywl_reload_cursor_theme(state);

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
    if (state->wl_cursor_theme != NULL)
        wl_cursor_theme_destroy(state->wl_cursor_theme);
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
