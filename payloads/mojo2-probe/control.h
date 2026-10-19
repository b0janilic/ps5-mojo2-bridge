/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MOJO_CONTROL_H
#define MOJO_CONTROL_H
struct mojo_control { int fd; const char *stop_path; };
/* 1 = acquired, 0 = existing instance asked to stop, -1 = error (errno). */
int mojo_control_begin(struct mojo_control *c, const char *lock_path, const char *stop_path);
/* 1 = stop requested, 0 = continue, -1 = stat error. */
int mojo_control_stop(const struct mojo_control *c);
void mojo_control_end(struct mojo_control *c);
#endif
