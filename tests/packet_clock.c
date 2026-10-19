/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../payloads/mojo2-probe/packet_clock.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    struct mojo_packet_clock c;
    mojo_clock_init(&c);
    for (unsigned i = 0; i < 8000; ++i) assert(mojo_clock_packet_bytes(&c) == 48);
    assert(c.fraction == 0);
    const uint32_t rates[] = {MOJO_NOMINAL_Q16 - 10, MOJO_NOMINAL_Q16 + 10,
                             MOJO_NOMINAL_Q16 - 3932, MOJO_NOMINAL_Q16 + 3932};
    uint64_t expected_q16 = 0, actual_samples = 0;
    unsigned variable = 0;
    for (unsigned block = 0; block < 10000; ++block) {
        uint32_t rate = rates[block % 4];
        assert(mojo_clock_feedback(&c, rate) == 1);
        for (unsigned i = 0; i < 64; ++i) {
            uint32_t bytes = mojo_clock_packet_bytes(&c);
            assert(bytes >= 40 && bytes <= MOJO_MAX_PCM_BYTES && bytes % 8 == 0);
            variable += bytes != 48;
            actual_samples += bytes / 8;
            expected_q16 += rate;
            /* No cumulative rounding drift, including across rate/buffer changes. */
            assert(actual_samples == expected_q16 / 65536);
            assert(c.fraction == expected_q16 % 65536);
        }
    }
    assert(variable && c.accepted == 10000 && c.rejected == 0);
    const uint32_t invalid[] = {0, 1, UINT32_MAX, MOJO_NOMINAL_Q16 - 3933, MOJO_NOMINAL_Q16 + 3933};
    uint32_t last = c.q16, phase = c.fraction;
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(mojo_clock_feedback(&c, invalid[i]) == 0);
        assert(c.q16 == last && c.fraction == phase);
    }
    assert(c.rejected == 5);
    puts("Packet clock: nominal rate, fractional conservation, changing rates, bounds and invalid feedback passed");
    return 0;
}
