/* SPDX-License-Identifier: GPL-3.0-only */
/* ABI/NIDs derived from FGG-PlayPods and ps5-solobridge; see NOTICE.md. */
#include "capture.h"
#include <ps5/kernel.h>
#include <string.h>
#include <unistd.h>

int sceKernelLoadStartModule(const char *, size_t, const void *, uint32_t, void *, int *);

struct audio_open_param {
    uint64_t size, unk08;
    uint32_t unk10, source, mode, unk1c;
    uint64_t unk20;
    uint32_t bitrate, pad;
    uint64_t unk30;
};
_Static_assert(sizeof(struct audio_open_param) == 0x38, "OpenAudio ABI size");
_Static_assert(offsetof(struct audio_open_param, source) == 0x14, "OpenAudio source offset");
_Static_assert(offsetof(struct audio_open_param, mode) == 0x18, "OpenAudio mode offset");

static intptr_t resolve(int module, const char *nid, const char *name, FILE *log) {
    intptr_t address = kernel_dynlib_resolve(getpid(), (uint32_t)module, nid);
    fprintf(log, "CAPTURE_RESOLVE %s nid=%s found=%d\n", name, nid, address > 0);
    fflush(log);
    return address > 0 ? address : 0;
}

static int load_module(const char *path, FILE *log) {
    int result = 0;
    fprintf(log, "CAPTURE_LOAD begin %s\n", path); fflush(log);
    int module = sceKernelLoadStartModule(path, 0, NULL, 0, NULL, &result);
    fprintf(log, "CAPTURE_LOAD handle=%d status=0x%08x\n", module, (unsigned)result); fflush(log);
    return module >= 0 && result >= 0 ? module : -1;
}

static int start_capture(struct mojo_capture *c);

int mojo_capture_open(struct mojo_capture *c, FILE *log) {
    memset(c, 0, sizeof(*c));
    c->log = log;
    c->cleanup_ok = 1;
    if (load_module("/system/common/lib/libSceIpmi.sprx", log) < 0) return -1;
    int module = load_module("/system/common/lib/libSceAvcap2.sprx", log);
    if (module < 0) return -1;
    c->initialize = (int (*)(void))resolve(module, "svzPXluOz8U", "Initialize", log);
    c->terminate = (int (*)(void))resolve(module, "gojqghDU+1Y", "Terminate", log);
    c->open_audio = (int (*)(void **, const void *, uint8_t))resolve(module, "3nHpE7Dp5SE", "OpenAudio", log);
    c->close = (int (*)(void *))resolve(module, "tR7gPe1i8hw", "Close", log);
    c->start = (int (*)(void *))resolve(module, "8CGNCwBsItI", "Start", log);
    c->stop = (int (*)(void *))resolve(module, "Z5dKK0xnQ8g", "Stop", log);
    c->read_audio = (int (*)(void *, void *, size_t, void *, uint32_t))resolve(module, "WhsHggqdwcg", "ReadAudio", log);
    if (!c->initialize || !c->terminate || !c->open_audio || !c->close || !c->start || !c->stop || !c->read_audio)
        return -1;
    return start_capture(c);
}

static int start_capture(struct mojo_capture *c) {
    fprintf(c->log, "CAPTURE_CALL Initialize begin\n"); fflush(c->log);
    int rc = c->initialize();
    fprintf(c->log, "CAPTURE_CALL Initialize rc=0x%08x\n", (unsigned)rc); fflush(c->log);
    if (rc < 0) return 0;
    c->initialized = 1;
    struct audio_open_param param = {.size = sizeof(param), .unk08 = 0x18,
        .source = 2, .mode = 0, .unk1c = 0xffffffffu};
    fprintf(c->log, "CAPTURE_CALL OpenAudio begin source=2 mode=0 size=%zu\n", sizeof(param)); fflush(c->log);
    rc = c->open_audio(&c->handle, &param, 0);
    fprintf(c->log, "CAPTURE_CALL OpenAudio rc=0x%08x handle_present=%d\n", (unsigned)rc, c->handle != NULL); fflush(c->log);
    if (rc < 0 || !c->handle) {
        c->handle = NULL; /* An error does not establish ownership of an output handle. */
        mojo_capture_close(c);
        return 0;
    }
    fprintf(c->log, "CAPTURE_CALL Start begin\n"); fflush(c->log);
    rc = c->start(c->handle);
    fprintf(c->log, "CAPTURE_CALL Start rc=0x%08x\n", (unsigned)rc); fflush(c->log);
    if (rc < 0) { mojo_capture_close(c); return 0; }
    c->started = 1;
    return 1;
}

int mojo_capture_restart(struct mojo_capture *c) {
    if (!c->initialize || !c->terminate || !c->open_audio || !c->close || !c->start || !c->stop || !c->read_audio)
        return -1;
    if (mojo_capture_close(c)) return -1;
    c->empty = c->overruns = c->errors = 0;
    return start_capture(c);
}

int mojo_capture_read(struct mojo_capture *c, float *buffer, size_t bytes) {
    if (!c->started || !buffer || bytes < 8 || bytes % 8) return -1;
    /* Upstream ABI returns 16 bytes; reserve extra zeroed space for this first 5.50 probe. */
    _Alignas(16) uint8_t info[64] = {0};
    int rc = c->read_audio(c->handle, buffer, bytes, info, 1);
    if ((uint32_t)rc == 0x81950002u) { c->empty++; return 0; }
    if ((uint32_t)rc == 0x81950004u) { c->overruns++; return 0; }
    if (rc < 0 || (size_t)rc > bytes || rc % 8) {
        fprintf(c->log, "CAPTURE_READ error rc=0x%08x bytes=%zu\n", (unsigned)rc, bytes); fflush(c->log);
        c->errors++;
        return -1;
    }
    if (!rc) c->empty++;
    return rc;
}

int mojo_capture_close(struct mojo_capture *c) {
    void *handle = c->handle;
    int started = c->started, initialized = c->initialized;
    c->handle = NULL;
    c->started = c->initialized = 0;
    int rc;
    if (started) {
        fprintf(c->log, "CAPTURE_CLEANUP Stop begin\n"); fflush(c->log);
        rc = c->stop(handle);
        fprintf(c->log, "CAPTURE_CLEANUP Stop rc=0x%08x\n", (unsigned)rc);
        if (rc < 0) c->cleanup_ok = 0;
    }
    if (handle) {
        fprintf(c->log, "CAPTURE_CLEANUP Close begin\n"); fflush(c->log);
        rc = c->close(handle);
        fprintf(c->log, "CAPTURE_CLEANUP Close rc=0x%08x\n", (unsigned)rc);
        if (rc < 0) c->cleanup_ok = 0;
    }
    if (initialized) {
        fprintf(c->log, "CAPTURE_CLEANUP Terminate begin\n"); fflush(c->log);
        rc = c->terminate();
        fprintf(c->log, "CAPTURE_CLEANUP Terminate rc=0x%08x\n", (unsigned)rc);
        if (rc < 0) c->cleanup_ok = 0;
    }
    fprintf(c->log, "CAPTURE_CLEANUP ok=%d\n", c->cleanup_ok); fflush(c->log);
    return c->cleanup_ok ? 0 : -1;
}
