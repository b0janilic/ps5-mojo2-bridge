/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "hdmi_ipc.h"
#include <ps5/kernel.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

int sceKernelLoadStartModule(const char *, size_t, const void *, uint32_t, void *, int *);

struct audio_command_header { uint32_t pid, command; uint64_t count; };
_Static_assert(sizeof(struct audio_command_header) == 16, "Audio control header");
_Static_assert(sizeof(struct mojo_ipc_buffer) == 16, "IPMI buffer descriptor");
_Static_assert(offsetof(struct mojo_ipc_buffer, size) == 8, "IPMI buffer length offset");

static int load(const char *path, FILE *log) {
    int status = 0;
    fprintf(log, "HDMI_IPC_LOAD begin %s\n", path); fflush(log);
    int module = sceKernelLoadStartModule(path, 0, NULL, 0, NULL, &status);
    fprintf(log, "HDMI_IPC_LOAD handle=%d status=0x%08x\n", module, (unsigned)status); fflush(log);
    return module >= 0 && status >= 0 ? module : -1;
}

int mojo_hdmi_ipc_open(struct mojo_hdmi_ipc *c, FILE *log) {
    memset(c, 0, sizeof(*c));
    if (load("/system/common/lib/libSceIpmi.sprx", log) < 0) return -1;
    int module = load("/system/common/lib/libSceAudioOut.sprx", log);
    if (module < 0) return -1;
    intptr_t address = kernel_dynlib_resolve(getpid(), (uint32_t)module, "29hgt4+HPYs");
    fprintf(log, "HDMI_IPC_RESOLVE GetIpcHandle found=%d\n", address > 0); fflush(log);
    if (address <= 0) return -1;
    int (*get_handle)(void **) = (int (*)(void **))address;
    void *wrapper = NULL;
    fprintf(log, "HDMI_IPC_CALL GetIpcHandle begin\n"); fflush(log);
    int rc = get_handle(&wrapper);
    fprintf(log, "HDMI_IPC_CALL GetIpcHandle rc=0x%08x wrapper=%d\n", (unsigned)rc, wrapper != NULL); fflush(log);
    if (rc != 0 || !wrapper || (uintptr_t)wrapper % sizeof(void *)) return -1;
    /* This export returns a wrapper whose first member is the IPMI client.
     * Native AudioOut control helpers use client->vtable[11] for sync RPC. */
    void *client = *(void **)wrapper;
    if (!client || (uintptr_t)client % sizeof(void *)) return -1;
    void **vtable = *(void ***)client;
    if (!vtable || (uintptr_t)vtable % sizeof(void *) || !vtable[11]) return -1;
    c->client = client;
    c->invoke = (mojo_ipc_invoke)vtable[11];
    fprintf(log, "HDMI_IPC_BOUND: existing audio server; method=0x12340000 command=0x45 mask=0x10\n");
    fflush(log);
    return 0;
}

int mojo_hdmi_ipc_set(struct mojo_hdmi_ipc *c, uint32_t mute) {
    if (!c->client || !c->invoke || mute > 1) { errno = EINVAL; return -1; }
    struct audio_command_header header = {(uint32_t)getpid(), 0x45, 1};
    uint8_t record[192] = {0};
    record[0] = 0x10; /* HDMI only; do not touch the USB-C device mask. */
    record[1] = (uint8_t)mute;
    struct mojo_ipc_buffer input[2] = {{&header, sizeof(header)}, {record, sizeof(record)}};
    int32_t status = INT32_MIN; /* A missing service status cannot look like success. */
    c->transport = c->invoke(c->client, 0x12340000, input, 2, &status, NULL, 0);
    c->service = status;
    if (c->transport != 0 || c->service != 0) { errno = EIO; return -1; }
    return 0;
}
