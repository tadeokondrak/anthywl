#pragma once

#include <stdbool.h>
#include <wayland-client-core.h>

struct anthywl_config {
    bool active_at_startup;
    bool emulate_im_popups;
    struct wl_array global_bindings;
    struct wl_array composing_bindings;
    struct wl_array selecting_bindings;
};

void anthywl_config_init(struct anthywl_config *config);
bool anthywl_config_load(struct anthywl_config *config);
void anthywl_config_finish(struct anthywl_config *config);
