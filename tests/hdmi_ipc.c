/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../payloads/mojo2-probe/hdmi_ipc.h"
#include <ps5/kernel.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

static void *vtable[12];
static struct { void **vtable; } client;
static void *wrapper;
static int failure, calls, loads, transport, service, omit_status;
static unsigned requested;

static int invoke(void *object, uint32_t method, const struct mojo_ipc_buffer *input,
                  uint32_t ninput, int32_t *status, void *output, uint32_t noutput) {
    calls++;
    assert(object == &client && method == 0x12340000 && ninput == 2);
    assert(status && !output && !noutput);
    assert(input[0].size == 16 && input[1].size == 192);
    uint32_t pid, command;
    uint64_t count;
    const uint8_t *header = input[0].data, *record = input[1].data;
    memcpy(&pid, header, 4); memcpy(&command, header + 4, 4); memcpy(&count, header + 8, 8);
    assert(pid == (uint32_t)getpid() && command == 0x45 && count == 1);
    assert(record[0] == 0x10 && record[1] == requested);
    for (unsigned i = 2; i < 192; ++i) assert(record[i] == 0);
    if (!omit_status) *status = service;
    return transport;
}

static int get_handle(void **out) {
    if (failure == 4) return -4;
    *out = failure == 5 ? NULL : failure == 9 ? (void *)((uintptr_t)&wrapper + 1) : &wrapper;
    return 0;
}

int sceKernelLoadStartModule(const char *path, size_t argc, const void *argv,
                             uint32_t flags, void *option, int *result) {
    loads++;
    assert(!argc && !argv && !flags && !option);
    *result = 0;
    if (!strcmp(path, "/system/common/lib/libSceIpmi.sprx")) return failure == 1 ? -1 : 10;
    assert(!strcmp(path, "/system/common/lib/libSceAudioOut.sprx"));
    return failure == 2 ? -2 : 20;
}

intptr_t kernel_dynlib_resolve(pid_t pid, uint32_t module, const char *nid) {
    assert(pid == getpid() && module == 20 && !strcmp(nid, "29hgt4+HPYs"));
    return failure == 3 ? 0 : (intptr_t)get_handle;
}

static void reset(void) {
    memset(vtable, 0, sizeof(vtable));
    vtable[11] = (void *)invoke;
    client.vtable = vtable;
    wrapper = &client;
    failure = calls = loads = transport = service = omit_status = 0;
}

int main(void) {
    FILE *log = tmpfile(); assert(log);
    struct mojo_hdmi_ipc c;
    reset();
    assert(mojo_hdmi_ipc_open(&c, log) == 0 && loads == 2 && !calls);
    for (requested = 0; requested < 2; ++requested) {
        assert(mojo_hdmi_ipc_set(&c, requested) == 0 && c.transport == 0 && c.service == 0);
        transport = -7;
        assert(mojo_hdmi_ipc_set(&c, requested) == -1 && c.transport == -7);
        transport = 0; service = -8;
        assert(mojo_hdmi_ipc_set(&c, requested) == -1 && c.service == -8);
        service = 0; omit_status = 1;
        assert(mojo_hdmi_ipc_set(&c, requested) == -1 && c.service == INT32_MIN);
        omit_status = 0;
    }
    int before = calls;
    assert(mojo_hdmi_ipc_set(&c, 2) == -1 && calls == before);
    for (int stage = 1; stage <= 11; ++stage) {
        reset(); failure = stage;
        if (stage == 6) wrapper = NULL;
        if (stage == 7) client.vtable = NULL;
        if (stage == 8) vtable[11] = NULL;
        if (stage == 10) wrapper = (void *)((uintptr_t)&client + 1);
        if (stage == 11) client.vtable = (void **)((uintptr_t)vtable + 1);
        assert(mojo_hdmi_ipc_open(&c, log) == -1 && !c.client && !c.invoke && !calls);
        assert(mojo_hdmi_ipc_set(&c, 1) == -1 && !calls);
    }
    fclose(log);
    puts("HDMI IPC: binding faults, exact method/header/192-byte HDMI-only record, mute/unmute, transport/service errors and missing status passed (host only)");
    return 0;
}
