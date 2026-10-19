/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../payloads/mojo2-probe/test_tone.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int64_t decode(const uint8_t *p) {
    uint32_t u = p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return u < 0x80000000u ? (int64_t)u : (int64_t)u - 4294967296LL;
}

int main(void) {
    struct mojo_test_tone t = {0};
    uint8_t frame[8];
    for (unsigned i = 0; i < 1000; ++i) {
        memset(frame, 0xff, sizeof(frame));
        mojo_tone_frame(&t, frame);
        assert(decode(frame) == 0 && decode(frame + 4) == 0 && t.position == 0);
    }
    mojo_tone_arm(&t);
    int64_t prev = 0, peak = 0, steady[48] = {0};
    unsigned rising = 0, nonzero = 0;
    for (unsigned i = 0; i < MOJO_TONE_FRAMES + MOJO_TONE_TAIL; ++i) {
        assert(!mojo_tone_finished(&t));
        mojo_tone_frame(&t, frame);
        int64_t v = decode(frame);
        assert(v == decode(frame + 4));
        int64_t magnitude = v < 0 ? -v : v;
        assert(magnitude <= MOJO_TONE_PEAK);
        if (magnitude > peak) peak = magnitude;
        if (i == 0 || i >= MOJO_TONE_FRAMES - 1) assert(v == 0);
        if (i < MOJO_TONE_RAMP)
            assert(magnitude <= (int64_t)MOJO_TONE_PEAK * i / MOJO_TONE_RAMP);
        if (i < MOJO_TONE_FRAMES && i >= MOJO_TONE_FRAMES - MOJO_TONE_RAMP)
            assert(magnitude <= (int64_t)MOJO_TONE_PEAK * (MOJO_TONE_FRAMES - 1 - i) / MOJO_TONE_RAMP);
        if (i >= MOJO_TONE_RAMP && i < MOJO_TONE_RAMP + 48) steady[i % 48] = v;
        else if (i >= MOJO_TONE_RAMP + 48 && i < MOJO_TONE_FRAMES - MOJO_TONE_RAMP)
            assert(v == steady[i % 48]); /* Exact 48-sample period = 1 kHz at 48 kHz. */
        if (v > 0 && prev <= 0) rising++;
        prev = v;
        nonzero += v != 0;
        if (i == 1234) {
            mojo_tone_arm(&t); /* Repeated arm cannot restart or extend a running tone. */
            assert(t.position == i + 1);
        }
    }
    assert(mojo_tone_finished(&t) && rising == 1000 && peak == MOJO_TONE_PEAK);
    assert(nonzero == t.nonzero_frames && nonzero > 40000);
    for (unsigned i = 0; i < 100000; ++i) {
        memset(frame, 0xff, sizeof(frame));
        mojo_tone_frame(&t, frame);
        assert(decode(frame) == 0 && decode(frame + 4) == 0);
    }
    assert(t.position == MOJO_TONE_FRAMES + MOJO_TONE_TAIL);
    puts("Tone: S32_LE stereo, 1 kHz period, peak limit, ramps, finite duration and silence tail passed");
    return 0;
}
