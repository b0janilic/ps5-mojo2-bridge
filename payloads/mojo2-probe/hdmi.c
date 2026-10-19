/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "hdmi.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

static int set_mute(struct mojo_hdmi *h, uint32_t value) {
    return mojo_hdmi_ipc_set(&h->ipc, value);
}

int mojo_hdmi_open(struct mojo_hdmi *h, uint32_t firmware,
                   const char *marker, FILE *log) {
    memset(h, 0, sizeof(*h));
    h->marker = marker;
    fprintf(log, "HDMI_INIT firmware=0x%08x; inspected ABI=5.50 only\n", firmware);
    if ((firmware & 0xffff0000u) != 0x05500000u) {
        fprintf(log, "HDMI_DISABLED: firmware not matched; no audio control operations\n");
        return 0;
    }
    int stale = access(marker, F_OK) == 0;
    if (!stale && errno != ENOENT) {
        fprintf(log, "HDMI_DISABLED: rollback marker inaccessible errno=%d\n", errno);
        return 0;
    }
    if (mojo_hdmi_ipc_open(&h->ipc, log)) {
        fprintf(log, "HDMI_DISABLED: audio service binding failed stale=%d\n", stale);
        return stale ? -1 : 0;
    }
    h->available = 1;
    if (stale) {
        h->armed = 1;
        fprintf(log, "HDMI_RECOVER: prior rollback marker; requesting HDMI unmute\n");
        if (mojo_hdmi_restore(h, log)) return -1;
    } else {
        /* Check that the service accepts HDMI control before USB starts.
         * This only requests the expected idle state: HDMI unmuted. */
        fprintf(log, "HDMI_IPC_PREFLIGHT unmute begin\n"); fflush(log);
        int rc = set_mute(h, 0);
        fprintf(log, "HDMI_IPC_PREFLIGHT rc=%d transport=0x%08x service=0x%08x\n",
                rc, (unsigned)h->ipc.transport, (unsigned)h->ipc.service); fflush(log);
        if (rc) { h->available = 0; return 0; }
    }
    fprintf(log, "HDMI_READY: will mute only after USB PCM playback starts; cleanup requests unmute\n");
    fflush(log);
    return 1;
}

int mojo_hdmi_prepare(struct mojo_hdmi *h, FILE *log) {
    if (!h->available) return 0;
    if (h->armed) return -1;
    h->mute_attempted = h->muted = h->mute_errno = 0;
    h->mute_transport = h->mute_service = 0;
    int fd = open(h->marker, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) goto fail;
    const char intent[] = "Mojo bridge owns HDMI mute; unmute on next launch or session exit.\n";
    int ok = write(fd, intent, sizeof(intent) - 1) == (ssize_t)(sizeof(intent) - 1);
    if (ok && fsync(fd)) ok = 0;
    if (close(fd)) ok = 0;
    if (!ok) goto fail;
    h->armed = 1;
    fprintf(log, "HDMI_PREPARED: rollback intent saved before USB starts\n");
    fflush(log);
    return 1;
fail:
    fprintf(log, "HDMI_DISABLED: cannot persist rollback intent errno=%d\n", errno);
    /* No mute has been attempted; retaining a partial marker is harmless. */
    h->available = 0;
    return 0;
}

int mojo_hdmi_mute(struct mojo_hdmi *h) {
    if (!h->available || !h->armed || h->mute_attempted) return 0;
    h->mute_attempted = 1;
    if (set_mute(h, 1)) {
        h->mute_errno = errno;
        h->mute_transport = h->ipc.transport;
        h->mute_service = h->ipc.service;
        h->available = 0; /* Do not retry mute repeatedly in this run. */
        return -1;
    }
    h->muted = 1;
    return 1;
}

int mojo_hdmi_restore(struct mojo_hdmi *h, FILE *log) {
    if (!h->armed) return 0;
    /* Even a failed mute may have partially affected hardware. */
    int rc = set_mute(h, 0);
    int error = rc ? errno : 0;
    fprintf(log, "HDMI_RESTORE mute_attempted=%d mute_success=%d mute_errno=%d mute_transport=0x%08x mute_service=0x%08x unmute_rc=%d errno=%d transport=0x%08x service=0x%08x\n",
            h->mute_attempted, h->muted, h->mute_errno, (unsigned)h->mute_transport,
            (unsigned)h->mute_service, rc, error, (unsigned)h->ipc.transport, (unsigned)h->ipc.service);
    fflush(log);
    if (rc) return -1; /* Keep marker and ownership for another recovery attempt. */
    h->armed = h->muted = 0;
    if (unlink(h->marker) && errno != ENOENT) {
        fprintf(log, "HDMI_MARKER: removal failed errno=%d; next launch will repeat unmute\n", errno);
    }
    return 0;
}

int mojo_hdmi_close(struct mojo_hdmi *h, FILE *log) {
    int result = mojo_hdmi_restore(h, log);
    memset(&h->ipc, 0, sizeof(h->ipc));
    h->available = 0;
    return result;
}
