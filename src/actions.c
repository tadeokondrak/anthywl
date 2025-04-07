#include <assert.h>
#include <string.h>

#include "anthywl.h"
#include "actions.h"

#define ARRAY_LEN(x) (sizeof (x) / sizeof *(x))

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
    memset(seat->selected_candidates, 0, conv_stat.nr_segment * sizeof(int));
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
    seat->is_selecting_popup_visible = true;
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

enum anthywl_action anthywl_action_from_string(const char *name) {
    // TODO: use bsearch
    static struct{
        char const *name;
        enum anthywl_action action;
    } const actions[] = {
        { "enable", ANTHYWL_ACTION_ENABLE },
        { "disable", ANTHYWL_ACTION_DISABLE },
        { "toggle", ANTHYWL_ACTION_TOGGLE },
        { "delete-left", ANTHYWL_ACTION_DELETE_LEFT },
        { "delete-right", ANTHYWL_ACTION_DELETE_RIGHT },
        { "move-left", ANTHYWL_ACTION_MOVE_LEFT },
        { "move-right", ANTHYWL_ACTION_MOVE_RIGHT },
        { "expand-left", ANTHYWL_ACTION_EXPAND_LEFT },
        { "expand-right", ANTHYWL_ACTION_EXPAND_RIGHT },
        { "select", ANTHYWL_ACTION_SELECT },
        { "compose", ANTHYWL_ACTION_COMPOSE },
        { "accept", ANTHYWL_ACTION_ACCEPT },
        { "discard", ANTHYWL_ACTION_DISCARD },
        { "prev-candidate", ANTHYWL_ACTION_PREV_CANDIDATE },
        { "next-candidate", ANTHYWL_ACTION_NEXT_CANDIDATE },
        { "cycle-candidate", ANTHYWL_ACTION_CYCLE_CANDIDATE },
    };

    for (size_t i = 0; i < ARRAY_LEN(actions); i++) {
        if (strcmp(name, actions[i].name) == 0)
            return actions[i].action;
    }

    return ANTHYWL_ACTION_INVALID;
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

bool anthywl_seat_handle_action(struct anthywl_seat *seat,
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
