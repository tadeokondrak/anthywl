#pragma once

#include <stdbool.h>

struct anthywl_seat;

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
    ANTHYWL_ACTION_SELECT_UNCONVERTED_CANDIDATE,
    ANTHYWL_ACTION_SELECT_KATAKANA_CANDIDATE,
    ANTHYWL_ACTION_SELECT_HIRAGANA_CANDIDATE,
    ANTHYWL_ACTION_SELECT_HALFKANA_CANDIDATE,
    _ANTHYWL_ACTION_LAST,
};

enum anthywl_action anthywl_action_from_string(const char *name);
bool anthywl_seat_handle_action(struct anthywl_seat *seat,
    enum anthywl_action action);
