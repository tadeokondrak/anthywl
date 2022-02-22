#include "ipc.h"
#include "anthywl.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

int anthywl_ipc_addr(char *addr, size_t size) {
    char const *wayland_display = getenv("WAYLAND_DISPLAY");
    char const *xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (wayland_display == NULL || wayland_display[0] == '\0') {
        fprintf(stderr, "WAYLAND_DISPLAY is not set\n");
        return -1;
    }
    if (xdg_runtime_dir == NULL || xdg_runtime_dir[0] == '\0') {
        fprintf(stderr, "XDG_RUNTIME_DIR is not set\n");
        return -1;
    }
    return snprintf(addr, size, "unix:%s/ca.tadeo.anthywl.%s",
        xdg_runtime_dir, wayland_display);
}

static char const ca_tadeo_anthywl_interface[] =
#include "ca.tadeo.anthywl.varlink.inc"
;

bool anthywl_ipc_init(struct anthywl_ipc *ipc) {
    char ipc_addr[PATH_MAX];
    if (anthywl_ipc_addr(ipc_addr, sizeof ipc_addr) < 0)
        return false;
    long res = varlink_service_new(&ipc->service,
        "tadeokondrak", "anthywl", "0.0.0",
        "https://github.com/tadeokondrak/anthywl", ipc_addr, -1);
    if (res < 0) {
        fprintf(stderr, "Failed to start varlink service: %s\n",
            varlink_error_string(-res));
        return false;
    }
    res = varlink_service_add_interface(ipc->service,
        ca_tadeo_anthywl_interface,
        "Action", anthywl_ipc_handle_action, ipc,
        NULL);
    if (res < 0) {
        fprintf(stderr, "Failed to set up varlink service: %s\n",
            varlink_error_string(-res));
        return false;
    }
    return true;
}

void anthywl_ipc_finish(struct anthywl_ipc *ipc) {
    if (ipc->service != NULL)
        varlink_service_free(ipc->service);
}

long anthywl_ipc_handle_action(VarlinkService *service, VarlinkCall *call,
    VarlinkObject *parameters, uint64_t flags, void *userdata)
{
    long res;
    char const *seat_name, *action_name;
    struct anthywl_state *state = wl_container_of(userdata, state, ipc);
    if ((res = varlink_object_get_string(parameters, "seat", &seat_name)) < 0)
        return varlink_call_reply_invalid_parameter(call, "seat");
    if ((res = varlink_object_get_string(parameters, "action", &action_name)) < 0)
        return varlink_call_reply_invalid_parameter(call, "action");
    enum anthywl_action action = anthywl_action_from_string(action_name);
    if (action == ANTHYWL_ACTION_INVALID)
        return varlink_call_reply_invalid_parameter(call, "action");

    bool found = false;
    struct anthywl_seat *seat;
    wl_list_for_each(seat, &state->seats, link) {
        if (strcmp(seat->name, seat_name) == 0) {
            found = true;
            break;
        }
    }
    if (!found) {
        VarlinkObject *no_such_seat;
        if ((res = varlink_object_new(&no_such_seat)) < 0)
            return res;
        varlink_object_set_string(no_such_seat, "seat", seat_name);
        return varlink_call_reply_error(
            call, "ca.tadeo.anthywl.NoSuchSeat", no_such_seat);
    }

    anthywl_seat_handle_action(seat, action);

    return varlink_call_reply(call, NULL, 0);
}
