/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../payloads/mojo2-probe/hdmi.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int binds, mutes, unmutes, hardware_muted;
static int fail_open, fail_mute, fail_unmute, fail_write, fail_sync, fail_close;
static char marker[256];

int mojo_test_open(const char *path, int flags, ...) {
    assert(!strcmp(path, marker));
    va_list ap; va_start(ap, flags);
    int mode = va_arg(ap, int);
    va_end(ap);
    assert(flags == (O_WRONLY | O_CREAT | O_TRUNC) && mode == 0600);
    return open(path, flags, mode);
}

int mojo_test_close(int fd) {
    int rc = close(fd);
    if (fail_close) { errno = EIO; return -1; }
    return rc;
}

ssize_t mojo_test_write(int fd, const void *buffer, size_t size) {
    if (fail_write) return write(fd, buffer, size / 2);
    return write(fd, buffer, size);
}

int mojo_test_fsync(int fd) {
    if (fail_sync) { errno = EIO; return -1; }
    return fsync(fd);
}

int mojo_hdmi_ipc_open(struct mojo_hdmi_ipc *c, FILE *log) {
    (void)log;
    binds++;
    memset(c, 0, sizeof(*c));
    if (fail_open) return -1;
    c->client = &binds;
    return 0;
}

int mojo_hdmi_ipc_set(struct mojo_hdmi_ipc *c, uint32_t mute) {
    assert(c->client == &binds && mute <= 1);
    c->transport = c->service = 0;
    if (mute) {
        assert(access(marker, F_OK) == 0);
        mutes++;
        hardware_muted = 1;
        if (fail_mute) { c->service = -123; errno = EIO; return -1; }
    } else {
        unmutes++;
        if (fail_unmute) { c->transport = -321; errno = EIO; return -1; }
        hardware_muted = 0;
    }
    return 0;
}

static void stale(void) {
    int fd = open(marker, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    assert(fd >= 0 && close(fd) == 0);
}

int main(void) {
    char folder[] = "/tmp/mojo-hdmi-test-XXXXXX";
    assert(mkdtemp(folder));
    snprintf(marker, sizeof(marker), "%s/owned", folder);
    FILE *log = tmpfile(); assert(log);
    struct mojo_hdmi h;

    const uint32_t unsupported[] = {0, 0x05000000, 0x05510000, 0x13600000};
    for (unsigned i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); ++i) {
        assert(mojo_hdmi_open(&h, unsupported[i], marker, log) == 0);
        assert(mojo_hdmi_prepare(&h, log) == 0 && mojo_hdmi_mute(&h) == 0);
        assert(mojo_hdmi_close(&h, log) == 0 && !binds && !mutes && !unmutes);
    }
    fail_open = 1;
    assert(mojo_hdmi_open(&h, 0x05500000, marker, log) == 0);
    assert(mojo_hdmi_close(&h, log) == 0);
    stale();
    assert(mojo_hdmi_open(&h, 0x05500000, marker, log) == -1);
    assert(mojo_hdmi_close(&h, log) == 0 && access(marker, F_OK) == 0);
    fail_open = 0;
    assert(mojo_hdmi_open(&h, 0x05500001, marker, log) == 1); /* Recover prior crash. */
    assert(!hardware_muted && access(marker, F_OK) != 0 && unmutes == 1);
    for (unsigned cycle = 0; cycle < 20; ++cycle) {
        assert(mojo_hdmi_prepare(&h, log) == 1);
        assert(mojo_hdmi_prepare(&h, log) == -1); /* Cannot overwrite active ownership. */
        assert(mojo_hdmi_mute(&h) == 1 && hardware_muted);
        assert(mojo_hdmi_mute(&h) == 0); /* One mute RPC per session. */
        assert(mojo_hdmi_restore(&h, log) == 0 && !hardware_muted);
        assert(mojo_hdmi_restore(&h, log) == 0 && access(marker, F_OK) != 0);
    }
    assert(mutes == 20 && unmutes == 21);
    assert(mojo_hdmi_prepare(&h, log) == 1);
    fail_mute = 1;
    assert(mojo_hdmi_mute(&h) == -1 && h.armed && hardware_muted);
    assert(h.mute_service == -123 && h.mute_transport == 0);
    assert(mojo_hdmi_mute(&h) == 0); /* Failed mute cannot loop. */
    fail_unmute = 1;
    assert(mojo_hdmi_restore(&h, log) == -1 && h.armed && hardware_muted);
    assert(access(marker, F_OK) == 0);
    fail_mute = fail_unmute = 0;
    assert(mojo_hdmi_restore(&h, log) == 0 && !hardware_muted);
    assert(mojo_hdmi_close(&h, log) == 0 && !h.ipc.client);

    for (unsigned failure = 0; failure < 3; ++failure) {
        assert(mojo_hdmi_open(&h, 0x05500000, marker, log) == 1);
        fail_write = failure == 0;
        fail_sync = failure == 1;
        fail_close = failure == 2;
        int before = mutes;
        assert(mojo_hdmi_prepare(&h, log) == 0);
        assert(mojo_hdmi_mute(&h) == 0 && mutes == before);
        fail_write = fail_sync = fail_close = 0;
        assert(mojo_hdmi_close(&h, log) == 0 && !hardware_muted);
        assert(unlink(marker) == 0);
    }
    stale(); hardware_muted = 1; fail_unmute = 1;
    assert(mojo_hdmi_open(&h, 0x05500000, marker, log) == -1 && h.armed);
    assert(mojo_hdmi_close(&h, log) == -1 && access(marker, F_OK) == 0);
    fail_unmute = 0;
    assert(mojo_hdmi_open(&h, 0x05500000, marker, log) == 1 && !hardware_muted);
    assert(mojo_hdmi_close(&h, log) == 0 && access(marker, F_OK) != 0);
    fail_unmute = 1;
    assert(mojo_hdmi_open(&h, 0x05500008, marker, log) == 0 && !h.armed && !h.available);
    assert(mojo_hdmi_prepare(&h, log) == 0 && mojo_hdmi_mute(&h) == 0);
    assert(mojo_hdmi_close(&h, log) == 0);
    fail_unmute = 0;
    assert(rmdir(folder) == 0);
    fclose(log);
    puts("HDMI: firmware gate, 20 route cycles, preflight, partial-mute rollback, persistent recovery and journal failures passed");
    return 0;
}
