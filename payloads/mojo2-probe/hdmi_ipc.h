/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MOJO_HDMI_IPC_H
#define MOJO_HDMI_IPC_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

struct mojo_ipc_buffer { const void *data; uint64_t size; };
typedef int (*mojo_ipc_invoke)(void *, uint32_t, const struct mojo_ipc_buffer *,
                              uint32_t, int32_t *, void *, uint32_t);
struct mojo_hdmi_ipc {
    void *client;
    mojo_ipc_invoke invoke;
    int transport, service;
};
/* ABI inspected in the user's 5.50 AudioOut/AudioSystem libraries.
 * The caller must check firmware BEFORE binding. No fixed function addresses. */
int mojo_hdmi_ipc_open(struct mojo_hdmi_ipc *c, FILE *log);
/* Uses the existing audio server's HDMI-only command. No file writes. */
int mojo_hdmi_ipc_set(struct mojo_hdmi_ipc *c, uint32_t mute);
#endif
