/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host fault injection against the real SDK USB structures, not USB emulation. */
#include "../payloads/mojo2-probe/feedback_fs.h"
#include <sys/ioctl.h>
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>

static struct usb_fs_endpoint *eps;
static unsigned fail_slot, next_slot, started[MOJO_FS_SLOTS], stopped[MOJO_FS_SLOTS], closed[MOJO_FS_SLOTS], uninits;
static unsigned long fail_request;
static int pending, oversize, status, bad_limits, allow_tone, nonzero_seen;

int mojo_test_ioctl(int fd, unsigned long request, ...) {
    assert(fd == 77);
    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    unsigned slot = 0;
    if (request == USB_FS_OPEN) slot = ((struct usb_fs_open *)arg)->ep_index;
    else if (request == USB_FS_START) slot = ((struct usb_fs_start *)arg)->ep_index;
    else if (request == USB_FS_STOP) slot = ((struct usb_fs_stop *)arg)->ep_index;
    else if (request == USB_FS_CLOSE) slot = ((struct usb_fs_close *)arg)->ep_index;
    if (request == USB_FS_STOP) stopped[slot]++;
    if (request == USB_FS_CLOSE) closed[slot]++;
    if (request == USB_FS_UNINIT) uninits++;
    if (request == fail_request && slot == fail_slot) { errno = EIO; return -1; }
    if (request == USB_FS_INIT) {
        struct usb_fs_init *p = arg;
        assert(p->ep_index_max == MOJO_FS_SLOTS);
        eps = p->pEndpoints;
    } else if (request == USB_FS_OPEN) {
        struct usb_fs_open *p = arg;
        assert(p->ep_no == (slot ? 0x05 : 0x81));
        p->max_packet_length = slot ? 1024 : 4;
        if (bad_limits) p->max_frames = 0;
    } else if (request == USB_FS_START) {
        started[slot]++;
        assert(eps[slot].nFrames == (slot ? 64 : 8));
        for (unsigned i = 0; i < eps[slot].nFrames; ++i) {
            unsigned bytes = eps[slot].pLength[i];
            if (slot) assert(bytes >= 40 && bytes <= 56 && bytes % 8 == 0);
            else assert(bytes == 4);
            const unsigned char *p = eps[slot].ppBuffer[i];
            for (unsigned j = 0; j < bytes; ++j) {
                if (!allow_tone || !slot) assert(p[j] == 0);
                else nonzero_seen |= p[j] != 0;
            }
        }
    } else if (request == USB_FS_COMPLETE) {
        if (pending) { errno = EBUSY; return -1; }
        ((struct usb_fs_complete *)arg)->ep_index = next_slot;
        if (next_slot < MOJO_FS_SLOTS) {
            eps[next_slot].status = status;
            eps[next_slot].aFrames = eps[next_slot].nFrames;
            if (oversize) eps[next_slot].pLength[0] = 4096;
        }
    }
    return 0;
}

static void reset(void) {
    eps = NULL;
    fail_request = fail_slot = next_slot = uninits = 0;
    pending = oversize = status = bad_limits = allow_tone = nonzero_seen = 0;
    memset(started, 0, sizeof(started));
    memset(stopped, 0, sizeof(stopped));
    memset(closed, 0, sizeof(closed));
}

int main(void) {
    FILE *log = tmpfile();
    assert(log);
    struct mojo_feedback_fs r;
    unsigned slot;
    reset();
    assert(mojo_fs_open(&r, 77, 0x81, 0x05, log) == 0);
    assert(mojo_fs_start(&r, 1, log) == 0);
    assert(mojo_fs_start(&r, 0, log) == 0);
    uint32_t saved[MOJO_FS_OUT_FRAMES];
    memcpy(saved, r.lengths[1], sizeof(saved));
    r.pcm[0][0][0] = 0x5a;
    assert(mojo_clock_feedback(&r.clock, MOJO_NOMINAL_Q16 - 10));
    assert(mojo_fs_start(&r, 2, log) == 0);
    assert(r.pcm[0][0][0] == 0x5a && memcmp(saved, r.lengths[1], sizeof(saved)) == 0);
    r.pcm[0][0][0] = 0;
    assert(mojo_fs_start(&r, 1, log) == -1); /* Never mutate an in-flight buffer. */
    pending = 1;
    assert(mojo_fs_complete(&r, &slot, log) == 0 && r.active[0] && r.active[1]);
    pending = 0; next_slot = 1;
    assert(mojo_fs_complete(&r, &slot, log) == 1 && slot == 1 && r.active[0] && !r.active[1]);
    assert(mojo_fs_start(&r, 1, log) == 0);
    next_slot = 0; status = 20; /* USB timeout status is preserved for caller. */
    assert(mojo_fs_complete(&r, &slot, log) == 1 && r.endpoints[0].status == 20);
    assert(mojo_fs_close(&r, log) == 0);
    assert(stopped[1] == 1 && stopped[0] == 0 && closed[0] == 1 && closed[1] == 1 && stopped[2] == 1 && closed[2] == 1 && uninits == 1);

    /* Partial initialization must close only the endpoint that opened. */
    reset(); fail_request = USB_FS_OPEN; fail_slot = 1;
    assert(mojo_fs_open(&r, 77, 0x81, 0x05, log) == -1);
    assert(mojo_fs_close(&r, log) == 0 && closed[0] == 1 && !closed[1] && uninits == 1);
    reset(); fail_request = USB_FS_OPEN; fail_slot = 2;
    assert(mojo_fs_open(&r, 77, 0x81, 0x05, log) == -1);
    assert(mojo_fs_close(&r, log) == 0 && closed[0] == 1 && closed[1] == 1 && !closed[2] && uninits == 1);
    reset(); fail_request = USB_FS_INIT;
    assert(mojo_fs_open(&r, 77, 0x81, 0x05, log) == -1);
    assert(mojo_fs_close(&r, log) == 0 && !uninits && !closed[0] && !closed[1]);
    reset(); bad_limits = 1;
    assert(mojo_fs_open(&r, 77, 0x81, 0x05, log) == -1);
    assert(mojo_fs_close(&r, log) == 0 && closed[0] == 1 && uninits == 1);

    /* Failed STOP/CLOSE must not skip cleanup of the other endpoint or UNINIT. */
    const unsigned long failures[] = {USB_FS_STOP, USB_FS_CLOSE, USB_FS_UNINIT};
    for (unsigned i = 0; i < 3; ++i) {
        reset();
        assert(mojo_fs_open(&r, 77, 0x81, 0x05, log) == 0);
        assert(mojo_fs_start(&r, 0, log) == 0 && mojo_fs_start(&r, 1, log) == 0 && mojo_fs_start(&r, 2, log) == 0);
        fail_request = failures[i];
        assert(mojo_fs_close(&r, log) == -1);
        assert(stopped[0] == 1 && stopped[1] == 1 && closed[0] == 1 && closed[1] == 1 && stopped[2] == 1 && closed[2] == 1 && uninits == 1);
    }
    for (unsigned i = 0; i < 2; ++i) {
        reset();
        assert(mojo_fs_open(&r, 77, 0x81, 0x05, log) == 0);
        assert(mojo_fs_start(&r, 0, log) == 0);
        if (i) oversize = 1;
        else next_slot = 255;
        assert(mojo_fs_complete(&r, &slot, log) == -1);
        assert(mojo_fs_close(&r, log) == 0);
    }
    /* Tone filling and resubmitting one OUT buffer preserve the other buffer. */
    reset();
    assert(mojo_fs_open(&r, 77, 0x81, 0x05, log) == 0);
    mojo_tone_arm(&r.tone);
    allow_tone = 1;
    assert(mojo_fs_start(&r, 1, log) == 0 && mojo_fs_start(&r, 2, log) == 0);
    assert(nonzero_seen);
    uint8_t pcm_saved[sizeof(r.pcm[0])];
    memcpy(pcm_saved, r.pcm[0], sizeof(pcm_saved));
    next_slot = 2;
    assert(mojo_fs_complete(&r, &slot, log) == 1 && slot == 2);
    assert(mojo_fs_start(&r, 2, log) == 0);
    assert(memcmp(pcm_saved, r.pcm[0], sizeof(pcm_saved)) == 0 && r.active[1]);
    assert(mojo_fs_close(&r, log) == 0);
    reset();
    assert(mojo_fs_open(&r, 77, 0x81, 0x05, log) == 0);
    static struct mojo_pcm_ring source;
    static float input[MOJO_RING_PREBUFFER * 2];
    for (unsigned i = 0; i < MOJO_RING_PREBUFFER; ++i) { input[2 * i] = 0.5f; input[2 * i + 1] = -0.5f; }
    mojo_pcm_init(&source);
    mojo_pcm_push(&source, input, MOJO_RING_PREBUFFER);
    source.enabled = 1;
    r.source = &source;
    allow_tone = 1;
    assert(mojo_fs_start(&r, 1, log) == 0 && mojo_fs_start(&r, 2, log) == 0);
    assert(nonzero_seen && source.nonzero_output && !r.tone.armed && !r.tone.nonzero_frames);
    memcpy(pcm_saved, r.pcm[0], sizeof(pcm_saved));
    next_slot = 2;
    assert(mojo_fs_complete(&r, &slot, log) == 1 && mojo_fs_start(&r, 2, log) == 0);
    assert(memcmp(pcm_saved, r.pcm[0], sizeof(pcm_saved)) == 0);
    assert(mojo_fs_close(&r, log) == 0);
    fclose(log);
    puts("USB_FS dispatch, buffer validation and cleanup fault injection passed (host only)");
    return 0;
}
