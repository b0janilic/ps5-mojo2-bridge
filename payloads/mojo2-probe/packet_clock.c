/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "packet_clock.h"

void mojo_clock_init(struct mojo_packet_clock *c) {
    *c = (struct mojo_packet_clock){.q16 = MOJO_NOMINAL_Q16};
}

int mojo_clock_feedback(struct mojo_packet_clock *c, uint32_t q16) {
    if (q16 < MOJO_NOMINAL_Q16 - 3932u || q16 > MOJO_NOMINAL_Q16 + 3932u) {
        c->rejected++;
        return 0;
    }
    c->q16 = q16;
    c->accepted++;
    return 1;
}

uint32_t mojo_clock_packet_bytes(struct mojo_packet_clock *c) {
    uint32_t phase = c->fraction + c->q16;
    c->fraction = phase & 0xffffu;
    return (phase >> 16) * 8u;
}
