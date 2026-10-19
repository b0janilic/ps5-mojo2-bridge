/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../payloads/mojo2-probe/control.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

int main(void) {
    char dir[] = "/tmp/mojo-control-XXXXXX";
    assert(mkdtemp(dir));
    char lock[256], stop[256];
    snprintf(lock, sizeof(lock), "%s/lock", dir);
    snprintf(stop, sizeof(stop), "%s/stop", dir);
    int marker = open(stop, O_WRONLY | O_CREAT, 0600);
    assert(marker >= 0 && close(marker) == 0);
    struct mojo_control c;
    assert(mojo_control_begin(&c, lock, stop) == 1);
    assert(mojo_control_stop(&c) == 0); /* Old marker is cleared only by owner. */
    pid_t pid = fork(); assert(pid >= 0);
    if (!pid) {
        close(c.fd); /* Parent retains the original lock reference. */
        struct mojo_control second;
        _exit(mojo_control_begin(&second, lock, stop) == 0 ? 0 : 1);
    }
    int status;
    assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(mojo_control_stop(&c) == 1);
    mojo_control_end(&c);
    assert(c.fd == -1 && access(stop, F_OK) == -1);
    assert(mojo_control_begin(&c, lock, stop) == 1);
    mojo_control_end(&c);
    /* Process death releases the lock without deleting its inode. */
    pid = fork(); assert(pid >= 0);
    if (!pid) {
        struct mojo_control abandoned;
        _exit(mojo_control_begin(&abandoned, lock, stop) == 1 ? 0 : 1);
    }
    assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(mojo_control_begin(&c, lock, stop) == 1);
    mojo_control_end(&c);
    assert(unlink(lock) == 0 && rmdir(dir) == 0);
    puts("Instance lock: duplicate requests stop, stale marker cleanup and process-exit recovery passed (host only)");
    return 0;
}
