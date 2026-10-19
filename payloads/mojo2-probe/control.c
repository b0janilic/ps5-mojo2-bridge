/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "control.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

int mojo_control_begin(struct mojo_control *c, const char *lock_path, const char *stop_path) {
    *c = (struct mojo_control){.fd = -1, .stop_path = stop_path};
    int fd = open(lock_path, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return -1;
    if (flock(fd, LOCK_EX | LOCK_NB)) {
        int error = errno;
        close(fd);
        if (error != EWOULDBLOCK && error != EAGAIN) { errno = error; return -1; }
        int stop = open(stop_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (stop < 0) return -1;
        return close(stop) ? -1 : 0;
    }
    if (unlink(stop_path) && errno != ENOENT) {
        int error = errno; close(fd); errno = error; return -1;
    }
    c->fd = fd;
    return 1;
}

int mojo_control_stop(const struct mojo_control *c) {
    struct stat st;
    if (!stat(c->stop_path, &st)) return 1;
    return errno == ENOENT ? 0 : -1;
}

void mojo_control_end(struct mojo_control *c) {
    if (c->fd >= 0) {
        unlink(c->stop_path);
        close(c->fd);
        c->fd = -1;
    }
    /* Keep the lock file's inode: deleting it can create parallel lock domains. */
}
