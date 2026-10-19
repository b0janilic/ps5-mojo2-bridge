/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MOJO_HDMI_H
#define MOJO_HDMI_H
#include <stdint.h>
#include <stdio.h>
#include "hdmi_ipc.h"

struct mojo_hdmi {
    int available, armed, muted, mute_attempted, mute_errno;
    int mute_transport, mute_service;
    struct mojo_hdmi_ipc ipc;
    const char *marker;
};
/* Only the 5.50 audio service ABI has been inspected. 1=ready, 0=disabled,
 * -1=stale mute could not be undone; caller must not start another session. */
int mojo_hdmi_open(struct mojo_hdmi *h, uint32_t firmware,
                   const char *marker, FILE *log);
/* Persist rollback intent BEFORE starting USB transfers. */
int mojo_hdmi_prepare(struct mojo_hdmi *h, FILE *log);
/* No file writes or notifications here: called from the USB streaming loop. */
int mojo_hdmi_mute(struct mojo_hdmi *h);
/* Unmute on every session exit, including a failed/partially applied mute. */
int mojo_hdmi_restore(struct mojo_hdmi *h, FILE *log);
int mojo_hdmi_close(struct mojo_hdmi *h, FILE *log);
#endif
