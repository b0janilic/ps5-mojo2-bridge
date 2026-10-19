/* SPDX-License-Identifier: GPL-3.0-only */
/* Fault-injected platform bindings; these do not establish firmware compatibility. */
#include "../payloads/mojo2-probe/capture.h"
#include <ps5/kernel.h>
#include <assert.h>
#include <string.h>

static unsigned calls[7], loads;
static int failure, read_result = MOJO_CAPTURE_BYTES, null_handle;
static int handle;
static int initialize(void) { calls[0]++; return failure == 4 ? -4 : 0; }
static int terminate(void) { calls[5]++; return failure == 9 ? -9 : 0; }
static int open_audio(void **out, const void *param, uint8_t kind) {
    calls[1]++;
    const uint8_t *p = param;
    uint64_t size, unk08;
    uint32_t source, mode, unk1c;
    memcpy(&size, p, 8); memcpy(&unk08, p + 8, 8);
    memcpy(&source, p + 0x14, 4); memcpy(&mode, p + 0x18, 4); memcpy(&unk1c, p + 0x1c, 4);
    assert(size == 0x38 && unk08 == 0x18 && source == 2 && mode == 0 && unk1c == UINT32_MAX && kind == 0);
    if (failure == 5) return -5;
    *out = null_handle ? NULL : &handle;
    return 0;
}
static int start(void *h) { assert(h == &handle); calls[2]++; return failure == 6 ? -6 : 0; }
static int stop(void *h) { assert(h == &handle); calls[3]++; return failure == 7 ? -7 : 0; }
static int close_audio(void *h) { assert(h == &handle); calls[4]++; return failure == 8 ? -8 : 0; }
static int read_audio(void *h, void *buffer, size_t bytes, void *info, uint32_t flags) {
    assert(h == &handle && flags == 1);
    calls[6]++;
    memset(buffer, 0, bytes);
    memset(info, 0, 16);
    return read_result;
}

int sceKernelLoadStartModule(const char *path, size_t argc, const void *argv, uint32_t flags, void *opt, int *result) {
    loads++;
    assert(!argc && !argv && !flags && !opt);
    *result = 0;
    if (strcmp(path, "/system/common/lib/libSceIpmi.sprx") == 0) return failure == 1 ? -1 : 10;
    assert(strcmp(path, "/system/common/lib/libSceAvcap2.sprx") == 0);
    return failure == 2 ? -2 : 20;
}

intptr_t kernel_dynlib_resolve(pid_t pid, uint32_t module, const char *nid) {
    (void)pid;
    assert(module == 20);
    if (failure == 3) return 0;
    if (!strcmp(nid, "svzPXluOz8U")) return (intptr_t)initialize;
    if (!strcmp(nid, "gojqghDU+1Y")) return (intptr_t)terminate;
    if (!strcmp(nid, "3nHpE7Dp5SE")) return (intptr_t)open_audio;
    if (!strcmp(nid, "tR7gPe1i8hw")) return (intptr_t)close_audio;
    if (!strcmp(nid, "8CGNCwBsItI")) return (intptr_t)start;
    if (!strcmp(nid, "Z5dKK0xnQ8g")) return (intptr_t)stop;
    if (!strcmp(nid, "WhsHggqdwcg")) return (intptr_t)read_audio;
    assert(0);
    return 0;
}

static void reset(void) {
    memset(calls, 0, sizeof(calls));
    loads = 0;
    failure = null_handle = 0;
    read_result = MOJO_CAPTURE_BYTES;
}

int main(void) {
    FILE *log = tmpfile(); assert(log);
    struct mojo_capture c;
    float buffer[MOJO_CAPTURE_BYTES / 4];
    reset();
    assert(mojo_capture_open(&c, log) == 1);
    assert(mojo_capture_read(&c, buffer, sizeof(buffer)) == MOJO_CAPTURE_BYTES);
    read_result = (int)0x81950002u;
    assert(mojo_capture_read(&c, buffer, sizeof(buffer)) == 0 && c.empty == 1);
    read_result = (int)0x81950004u;
    assert(mojo_capture_read(&c, buffer, sizeof(buffer)) == 0 && c.overruns == 1);
    const int bad[] = {(int)0x81950008u, 7, MOJO_CAPTURE_BYTES + 8};
    for (unsigned i = 0; i < 3; ++i) {
        read_result = bad[i];
        assert(mojo_capture_read(&c, buffer, sizeof(buffer)) == -1);
    }
    assert(c.errors == 3);
    unsigned reads = calls[6];
    assert(mojo_capture_read(&c, NULL, sizeof(buffer)) == -1);
    assert(mojo_capture_read(&c, buffer, 7) == -1 && calls[6] == reads);
    assert(mojo_capture_close(&c) == 0 && calls[3] == 1 && calls[4] == 1 && calls[5] == 1);
    assert(mojo_capture_close(&c) == 0 && calls[3] == 1 && calls[4] == 1 && calls[5] == 1);
    assert(mojo_capture_read(&c, buffer, sizeof(buffer)) == -1 && calls[6] == reads);
    for (int stage = 1; stage <= 6; ++stage) {
        reset(); failure = stage;
        int rc = mojo_capture_open(&c, log);
        assert(rc == (stage <= 3 ? -1 : 0));
        assert(mojo_capture_close(&c) == 0);
        assert(calls[3] == 0);
        assert(calls[4] == (unsigned)(stage == 6));
        assert(calls[5] == (unsigned)(stage == 5 || stage == 6));
    }
    reset(); null_handle = 1;
    assert(mojo_capture_open(&c, log) == 0 && calls[4] == 0 && calls[5] == 1);
    for (int stage = 7; stage <= 9; ++stage) {
        reset();
        assert(mojo_capture_open(&c, log) == 1);
        failure = stage;
        assert(mojo_capture_close(&c) == -1);
        assert(calls[3] == 1 && calls[4] == 1 && calls[5] == 1);
        assert(mojo_capture_close(&c) == -1 && calls[3] == 1 && calls[4] == 1 && calls[5] == 1);
    }
    reset();
    assert(mojo_capture_open(&c, log) == 1 && loads == 2);
    assert(mojo_capture_close(&c) == 0);
    failure = 1; /* A fresh module load would fail; restart must reuse bindings. */
    assert(mojo_capture_restart(&c) == 1 && loads == 2);
    assert(calls[0] == 2 && calls[1] == 2 && calls[2] == 2);
    assert(mojo_capture_close(&c) == 0);
    reset();
    assert(mojo_capture_open(&c, log) == 1);
    failure = 7;
    assert(mojo_capture_close(&c) == -1);
    assert(mojo_capture_restart(&c) == -1 && calls[0] == 1 && loads == 2);
    fclose(log);
    puts("Capture ABI setup, read validation, partial initialization and cleanup fault injection passed (host only)");
    return 0;
}
