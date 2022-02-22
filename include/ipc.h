#pragma once

#include <stddef.h>
#include <varlink.h>

struct anthywl_ipc {
    VarlinkService *service;
};

int anthywl_ipc_addr(char *addr, size_t size);
bool anthywl_ipc_init(struct anthywl_ipc *ipc);
void anthywl_ipc_finish(struct anthywl_ipc *ipc);
long anthywl_ipc_handle_action(VarlinkService *service, VarlinkCall *call,
    VarlinkObject *parameters, uint64_t flags, void *userdata);
