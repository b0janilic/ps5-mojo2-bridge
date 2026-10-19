/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MOJO_PACKET_CLOCK_H
#define MOJO_PACKET_CLOCK_H
#include <stdint.h>

#define MOJO_NOMINAL_Q16 (6u << 16)
#define MOJO_MAX_PCM_BYTES 56u /* Seven stereo S32 samples. */
struct mojo_packet_clock {
    uint32_t q16, fraction, accepted, rejected;
};
void mojo_clock_init(struct mojo_packet_clock *c);
/* Accept only feedback within 1% of 48 kHz; keep the previous rate otherwise. */
int mojo_clock_feedback(struct mojo_packet_clock *c, uint32_t q16);
uint32_t mojo_clock_packet_bytes(struct mojo_packet_clock *c);
#endif
