/* SPDX-License-Identifier: GPL-3.0-only */
/* ABI/NIDs derived from FGG-PlayPods and ps5-solobridge; see NOTICE.md. */
#ifndef MOJO_CAPTURE_H
#define MOJO_CAPTURE_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#define MOJO_CAPTURE_BYTES 8192u
struct mojo_capture {
    FILE *log;
    void *handle;
    int initialized, started, cleanup_ok;
    unsigned empty, overruns, errors;
    int (*initialize)(void), (*terminate)(void);
    int (*open_audio)(void **, const void *, uint8_t);
    int (*start)(void *), (*stop)(void *), (*close)(void *);
    int (*read_audio)(void *, void *, size_t, void *, uint32_t);
};
/* 1 = started, 0 = service unavailable, -1 = missing module/export. */
int mojo_capture_open(struct mojo_capture *c, FILE *log);
/* Reuses loaded module bindings after a session ends; no additional module load. */
int mojo_capture_restart(struct mojo_capture *c);
/* Nonblocking byte count; 0 = empty/overrun, -1 = malformed or service error. */
int mojo_capture_read(struct mojo_capture *c, float *buffer, size_t bytes);
int mojo_capture_close(struct mojo_capture *c);
#endif
