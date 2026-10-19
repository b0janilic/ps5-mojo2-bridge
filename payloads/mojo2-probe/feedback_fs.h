/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MOJO_FEEDBACK_FS_H
#define MOJO_FEEDBACK_FS_H
#include <sys/types.h>
#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>
#include <stdint.h>
#include <stdio.h>
#include "packet_clock.h"
#include "test_tone.h"
#include "pcm_ring.h"

#define MOJO_FS_SLOTS 3
#define MOJO_FS_FEEDBACK 0
#define MOJO_FS_OUT 1
#define MOJO_FS_OUT_SECOND 2
#define MOJO_FS_FRAMES 8
#define MOJO_FS_OUT_FRAMES 64
#define MOJO_FS_OUT_BYTES MOJO_MAX_PCM_BYTES
struct mojo_feedback_fs {
    int fd, initialized, opened[MOJO_FS_SLOTS], active[MOJO_FS_SLOTS];
    struct usb_fs_endpoint endpoints[MOJO_FS_SLOTS];
    unsigned completions[MOJO_FS_SLOTS];
    uint32_t first_time[MOJO_FS_SLOTS], last_time[MOJO_FS_SLOTS];
    struct mojo_packet_clock clock;
    struct mojo_test_tone tone;
    struct mojo_pcm_ring *source;
    void *pointers[MOJO_FS_SLOTS][MOJO_FS_OUT_FRAMES];
    uint32_t lengths[MOJO_FS_SLOTS][MOJO_FS_OUT_FRAMES];
    uint32_t requested[MOJO_FS_SLOTS][MOJO_FS_OUT_FRAMES];
    uint8_t data[MOJO_FS_FRAMES][4];
    uint8_t pcm[2][MOJO_FS_OUT_FRAMES][MOJO_FS_OUT_BYTES];
};
int mojo_fs_open(struct mojo_feedback_fs *r, int fd, uint8_t feedback, uint8_t out, FILE *log);
int mojo_fs_start(struct mojo_feedback_fs *r, unsigned slot, FILE *log);
/* 0 = pending, 1 = completion for *slot, -1 = ioctl/metadata error. */
int mojo_fs_complete(struct mojo_feedback_fs *r, unsigned *slot, FILE *log);
int mojo_fs_close(struct mojo_feedback_fs *r, FILE *log);
#endif
