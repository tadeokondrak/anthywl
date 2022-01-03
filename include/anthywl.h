#pragma once

#include <anthy/anthy.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wayland-client-core.h>
#include <xkbcommon/xkbcommon.h>

#include "input-method-unstable-v2-client-protocol.h"
#include "text-input-unstable-v3-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"

#include "actions.h"
#include "buffer.h"
#include "config.h"

enum anthyl_modifier_index {
    ANTHYWL_SHIFT_INDEX,
    ANTHYWL_CAPS_INDEX,
    ANTHYWL_CTRL_INDEX,
    ANTHYWL_ALT_INDEX,
    ANTHYWL_NUM_INDEX,
    ANTHYWL_MOD3_INDEX,
    ANTHYWL_LOGO_INDEX,
    ANTHYWL_MOD5_INDEX,
    _ANTHYWL_MOD_LAST,
};

enum anthywl_modifier {
    ANTHYWL_SHIFT = 1 << ANTHYWL_SHIFT_INDEX,
    ANTHYWL_CAPS = 1 << ANTHYWL_CAPS_INDEX,
    ANTHYWL_CTRL = 1 << ANTHYWL_CTRL_INDEX,
    ANTHYWL_ALT = 1 << ANTHYWL_ALT_INDEX,
    ANTHYWL_NUM = 1 << ANTHYWL_NUM_INDEX,
    ANTHYWL_MOD3 = 1 << ANTHYWL_MOD3_INDEX,
    ANTHYWL_LOGO = 1 << ANTHYWL_LOGO_INDEX,
    ANTHYWL_MOD5 = 1 << ANTHYWL_MOD5_INDEX,
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
    struct anthywl_config config;
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
    xkb_mod_index_t mod_indices[_ANTHYWL_MOD_LAST];
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

void zwp_input_popup_surface_v2_text_input_rectangle(void *data,
    struct zwp_input_popup_surface_v2 *zwp_input_popup_surface_v2,
    int32_t x, int32_t y, int32_t width, int32_t height);

void wl_surface_enter(void *data, struct wl_surface *wl_surface,
    struct wl_output *wl_output);
void wl_surface_leave(void *data, struct wl_surface *wl_surface,
    struct wl_output *wl_output);

void zwp_input_method_keyboard_grab_v2_keymap(void *data,
    struct zwp_input_method_keyboard_grab_v2 *zwp_input_method_keyboard_grab_v2,
    uint32_t format, int32_t fd, uint32_t size);
void zwp_input_method_keyboard_grab_v2_key(void *data,
    struct zwp_input_method_keyboard_grab_v2 *zwp_input_method_keyboard_grab_v2,
    uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
void zwp_input_method_keyboard_grab_v2_modifiers(void *data,
    struct zwp_input_method_keyboard_grab_v2 *zwp_input_method_keyboard_grab_v2,
    uint32_t serial,
    uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked,
    uint32_t group);
void zwp_input_method_keyboard_grab_v2_repeat_info(void *data,
    struct zwp_input_method_keyboard_grab_v2 *zwp_input_method_keyboard_grab_v2,
    int32_t rate, int32_t delay);

void zwp_input_method_v2_activate(
    void *data, struct zwp_input_method_v2 *zwp_input_method_v2);
void zwp_input_method_v2_deactivate(
    void *data, struct zwp_input_method_v2 *zwp_input_method_v2);
void zwp_input_method_v2_surrounding_text(
    void *data, struct zwp_input_method_v2 *zwp_input_method_v2,
    char const *text, uint32_t cursor, uint32_t anchor);
void zwp_input_method_v2_text_change_cause(
    void *data, struct zwp_input_method_v2 *zwp_input_method_v2,
    uint32_t cause);
void zwp_input_method_v2_content_type(
    void *data, struct zwp_input_method_v2 *zwp_input_method_v2,
    uint32_t hint, uint32_t purpose);
void zwp_input_method_v2_done(
    void *data, struct zwp_input_method_v2 *zwp_input_method_v2);
void zwp_input_method_v2_unavailable(
    void *data, struct zwp_input_method_v2 *zwp_input_method_v2);

void wl_pointer_enter(void *data, struct wl_pointer *wl_pointer,
    uint32_t serial, struct wl_surface *surface,
    wl_fixed_t surface_x, wl_fixed_t surface_y);
void wl_pointer_leave(void *data, struct wl_pointer *wl_pointer,
    uint32_t serial, struct wl_surface *surface);
void wl_pointer_motion(void *data, struct wl_pointer *wl_pointer,
    uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y);
void wl_pointer_button(void *data, struct wl_pointer *wl_pointer,
    uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
void wl_pointer_axis(void *data, struct wl_pointer *wl_pointer,
    uint32_t time, uint32_t axis, wl_fixed_t value);
void wl_pointer_frame(void *data, struct wl_pointer *wl_pointer);
void wl_pointer_axis_source(void *data, struct wl_pointer *wl_pointer,
    uint32_t axis_source);
void wl_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer,
    uint32_t time, uint32_t axis);
void wl_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer,
    uint32_t axis, int32_t discrete);
void wl_seat_capabilities(void *data, struct wl_seat *wl_seat,
    uint32_t capabilities);

void wl_seat_name(void *data, struct wl_seat *wl_seat,
    char const *name);
void wl_seat_global(struct anthywl_state *state, void *data);

void wl_output_geometry(void *data, struct wl_output *wl_output,
    int32_t x, int32_t y, int32_t physical_width, int32_t physical_height,
    int32_t subpixel, const char *make, const char *model, int32_t transform);
void wl_output_mode(void *data, struct wl_output *wl_output,
    uint32_t flags, int32_t width, int32_t height, int32_t refresh);
void wl_output_done(void *data, struct wl_output *wl_output);
void wl_output_scale(void *data, struct wl_output *wl_output,
    int32_t factor);
void wl_output_global(struct anthywl_state *state, void *data);

void wl_registry_global(void *data, struct wl_registry *wl_registry,
    uint32_t name, char const *interface, uint32_t version);
void wl_registry_global_remove(void *data,
    struct wl_registry *wl_registry, uint32_t name);

struct anthywl_graphics_buffer *anthywl_seat_composing_draw_popup(
    struct anthywl_seat *seat, int scale);
struct anthywl_graphics_buffer *anthywl_seat_selecting_draw_popup(
    struct anthywl_seat *seat, int scale);
void anthywl_seat_draw_popup(struct anthywl_seat *seat);
void anthywl_seat_init(struct anthywl_seat *seat,
    struct anthywl_state *state, struct wl_seat *wl_seat);
void anthywl_seat_init_protocols(struct anthywl_seat *seat);
void anthywl_seat_destroy(struct anthywl_seat *seat);
void anthywl_seat_composing_update(struct anthywl_seat *seat);
void anthywl_seat_composing_commit(struct anthywl_seat *seat);
void anthywl_seat_selecting_update(struct anthywl_seat *seat);
void anthywl_seat_selecting_commit(struct anthywl_seat *seat);
int anthywl_binding_compare(void const *_a, void const *_b);
int anthywl_seat_binding_compare(void const *_a, void const *_b);
int anthywl_seat_binding_compare_without_action(
    void const *_a, void const *_b);
bool anthywl_seat_handle_key_bindings(struct anthywl_seat *seat,
    struct wl_array *bindings, struct anthywl_seat_binding *press);
bool anthywl_seat_handle_key(struct anthywl_seat *seat, xkb_keycode_t keycode);
void anthywl_seat_repeat_timer_callback(struct anthywl_timer *timer);
void anthywl_seat_setup_bindings(struct anthywl_seat *seat,
    struct wl_array *state_bindings, struct wl_array *seat_bindings);
void anthywl_seat_cursor_update(struct anthywl_seat *seat);
void anthywl_seat_cursor_timer_callback(struct anthywl_timer *timer);

void anthywl_reload_cursor_theme(struct anthywl_state *state);
bool anthywl_state_init(struct anthywl_state *state);
int anthywl_state_next_timer(struct anthywl_state *state);
void anthywl_state_run_timers(struct anthywl_state *state);
void anthywl_state_run(struct anthywl_state *state);
void anthywl_state_finish(struct anthywl_state *state);

struct zwp_input_popup_surface_v2_listener const
    zwp_input_popup_surface_v2_listener;
struct wl_seat_listener const wl_seat_listener;
struct wl_surface_listener const wl_surface_listener;
struct zwp_input_method_keyboard_grab_v2_listener const
    zwp_input_method_keyboard_grab_v2_listener;
struct zwp_input_method_v2_listener const zwp_input_method_v2_listener;
struct wl_pointer_listener const wl_pointer_listener;
struct wl_output_listener const wl_output_listener;
struct wl_registry_listener const wl_registry_listener;
