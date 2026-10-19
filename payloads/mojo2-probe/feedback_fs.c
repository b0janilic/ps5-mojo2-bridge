/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "feedback_fs.h"
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>

static unsigned frame_count(unsigned slot) {
    return slot == MOJO_FS_FEEDBACK ? MOJO_FS_FRAMES : MOJO_FS_OUT_FRAMES;
}

static unsigned frame_bytes(unsigned slot) {
    return slot == MOJO_FS_FEEDBACK ? 4 : MOJO_FS_OUT_BYTES;
}

int mojo_fs_open(struct mojo_feedback_fs *r, int fd, uint8_t feedback, uint8_t out, FILE *log) {
    memset(r, 0, sizeof(*r));
    r->fd = fd;
    mojo_clock_init(&r->clock);
    for (unsigned slot = 0; slot < MOJO_FS_SLOTS; ++slot) {
        for (unsigned i = 0; i < frame_count(slot); ++i)
            r->pointers[slot][i] = slot == MOJO_FS_FEEDBACK ? (void *)r->data[i] : (void *)r->pcm[slot - 1][i];
        r->endpoints[slot].ppBuffer = r->pointers[slot];
        r->endpoints[slot].pLength = r->lengths[slot];
        r->endpoints[slot].nFrames = frame_count(slot);
        r->endpoints[slot].flags = USB_FS_FLAG_SINGLE_SHORT_OK | USB_FS_FLAG_MULTI_SHORT_OK;
        r->endpoints[slot].timeout = 250;
    }
    struct usb_fs_init init = {.pEndpoints = r->endpoints, .ep_index_max = MOJO_FS_SLOTS};
    fprintf(log, "USB_FS_INIT endpoint_slots=%u\n", MOJO_FS_SLOTS); fflush(log);
    if (ioctl(fd, USB_FS_INIT, &init)) {
        fprintf(log, "  ERROR errno=%d\n", errno); return -1;
    }
    r->initialized = 1;
    for (unsigned slot = 0; slot < MOJO_FS_SLOTS; ++slot) {
        unsigned bytes = frame_count(slot) * frame_bytes(slot);
        struct usb_fs_open open = {.max_bufsize = bytes,
            .max_frames = frame_count(slot), .ep_index = slot,
            .ep_no = slot == MOJO_FS_FEEDBACK ? feedback : out};
        fprintf(log, "USB_FS_OPEN slot=%u address=0x%02x frames=%u\n", slot, open.ep_no, frame_count(slot)); fflush(log);
        if (ioctl(fd, USB_FS_OPEN, &open)) {
            fprintf(log, "  ERROR errno=%d\n", errno); return -1;
        }
        r->opened[slot] = 1;
        fprintf(log, "  frames=%u max_packet=%u buffer=%u\n", open.max_frames, open.max_packet_length, open.max_bufsize);
        unsigned expected_max = slot == MOJO_FS_FEEDBACK ? 4 : 1024;
        if (open.max_frames < frame_count(slot) || open.max_packet_length != expected_max || open.max_bufsize < bytes) {
            fprintf(log, "  ERROR: unexpected transfer limits\n"); return -1;
        }
    }
    return 0;
}

int mojo_fs_start(struct mojo_feedback_fs *r, unsigned slot, FILE *log) {
    if (slot >= MOJO_FS_SLOTS || !r->opened[slot] || r->active[slot]) return -1;
    if (slot == MOJO_FS_FEEDBACK) memset(r->data, 0, sizeof(r->data));
    else memset(r->pcm[slot - 1], 0, sizeof(r->pcm[slot - 1]));
    for (unsigned i = 0; i < frame_count(slot); ++i) {
        uint32_t n = slot == MOJO_FS_FEEDBACK ? 4 : mojo_clock_packet_bytes(&r->clock);
        r->lengths[slot][i] = r->requested[slot][i] = n;
        if (slot != MOJO_FS_FEEDBACK)
            for (unsigned offset = 0; offset < n; offset += 8) {
                uint8_t *frame = &r->pcm[slot - 1][i][offset];
                if (r->source) mojo_pcm_frame(r->source, frame);
                else mojo_tone_frame(&r->tone, frame);
            }
    }
    r->endpoints[slot].nFrames = frame_count(slot);
    r->endpoints[slot].aFrames = 0;
    r->endpoints[slot].status = -1;
    struct usb_fs_start start = {.ep_index = slot};
    if (ioctl(r->fd, USB_FS_START, &start)) {
        fprintf(log, "USB_FS_START slot=%u ERROR errno=%d\n", slot, errno); return -1;
    }
    r->active[slot] = 1;
    return 0;
}

int mojo_fs_complete(struct mojo_feedback_fs *r, unsigned *slot, FILE *log) {
    struct usb_fs_complete complete = {0};
    if (ioctl(r->fd, USB_FS_COMPLETE, &complete)) {
        if (errno == EBUSY || errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        fprintf(log, "USB_FS_COMPLETE ERROR errno=%d\n", errno); return -1;
    }
    *slot = complete.ep_index;
    if (*slot >= MOJO_FS_SLOTS || !r->active[*slot]) {
        fprintf(log, "  ERROR: invalid/unexpected completion slot=%u\n", *slot); return -1;
    }
    r->active[*slot] = 0;
    struct usb_fs_endpoint *ep = &r->endpoints[*slot];
    if (++r->completions[*slot] == 1) r->first_time[*slot] = ep->isoc_time_complete;
    r->last_time[*slot] = ep->isoc_time_complete;
    /* Defer normal trace output until transfers stop: file I/O can delay resubmission. */
    if (ep->status != 0)
        fprintf(log, "USB_FS_COMPLETION slot=%u status=%d actual_frames=%u time=%u\n",
                *slot, ep->status, ep->aFrames, ep->isoc_time_complete);
    if (ep->aFrames > frame_count(*slot)) {
        fprintf(log, "  ERROR: invalid actual frame count\n"); return -1;
    }
    for (unsigned i = 0; i < ep->aFrames; ++i) {
        if (r->lengths[*slot][i] > r->requested[*slot][i]) {
            fprintf(log, "  ERROR: actual frame length exceeds request\n"); return -1;
        }
    }
    return 1;
}

int mojo_fs_close(struct mojo_feedback_fs *r, FILE *log) {
    int ok = 1;
    for (unsigned slot = 0; slot < MOJO_FS_SLOTS; ++slot) {
        if (r->active[slot]) {
            struct usb_fs_stop stop = {.ep_index = slot};
            if (ioctl(r->fd, USB_FS_STOP, &stop)) { fprintf(log, "USB_FS_STOP slot=%u errno=%d\n", slot, errno); ok = 0; }
            r->active[slot] = 0;
        }
        if (r->opened[slot]) {
            struct usb_fs_close close = {.ep_index = slot};
            if (ioctl(r->fd, USB_FS_CLOSE, &close)) { fprintf(log, "USB_FS_CLOSE slot=%u errno=%d\n", slot, errno); ok = 0; }
            r->opened[slot] = 0;
        }
    }
    if (r->initialized) {
        struct usb_fs_uninit uninit = {0};
        if (ioctl(r->fd, USB_FS_UNINIT, &uninit)) { fprintf(log, "USB_FS_UNINIT errno=%d\n", errno); ok = 0; }
        r->initialized = 0;
    }
    fprintf(log, "USB_FS_CLEANUP ok=%d\n", ok); fflush(log);
    return ok ? 0 : -1;
}
