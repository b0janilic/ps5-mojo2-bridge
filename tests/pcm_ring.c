/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../payloads/mojo2-probe/pcm_ring.h"
#include "../payloads/mojo2-probe/packet_clock.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int64_t decode(const uint8_t *p) {
    uint32_t u = p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return u < 0x80000000u ? (int64_t)u : (int64_t)u - 4294967296LL;
}

int main(void) {
    static struct mojo_pcm_ring r;
    static float input[40000];
    uint8_t frame[8];
    for (unsigned i = 0; i < 20000; ++i) { input[2 * i] = 0.5f; input[2 * i + 1] = -0.5f; }
    mojo_pcm_init(&r);
    mojo_pcm_push(&r, input, 1024);
    mojo_pcm_frame(&r, frame);
    assert(!decode(frame) && r.fill == 1024 && !r.playing);
    r.enabled = 1;
    mojo_pcm_frame(&r, frame);
    assert(!decode(frame) && r.fill == 1024 && !r.playing);
    mojo_pcm_push(&r, input, 512);
    for (unsigned i = 0; i <= MOJO_PCM_RAMP; ++i) mojo_pcm_frame(&r, frame);
    assert(r.playing && decode(frame) == 268435455 && decode(frame + 4) == -268435455);
    mojo_pcm_push(&r, input, 1024);
    mojo_pcm_fade_out(&r);
    for (unsigned i = 0; i < MOJO_PCM_RAMP; ++i) {
        mojo_pcm_frame(&r, frame);
        assert(decode(frame) >= 0 && decode(frame) <= 268435455);
    }
    assert(!decode(frame) && !decode(frame + 4) && !r.fade_left);
    unsigned fill = r.fill;
    mojo_pcm_fade_out(&r);
    for (unsigned i = 0; i < 1000; ++i) { mojo_pcm_frame(&r, frame); assert(!decode(frame)); }
    assert(r.fill == fill);

    /* Exhaustion counts one underrun; rendering waits for a fresh prebuffer. */
    mojo_pcm_init(&r); r.enabled = 1;
    mojo_pcm_push(&r, input, 1536);
    for (unsigned i = 0; i < 1536; ++i) mojo_pcm_frame(&r, frame);
    mojo_pcm_frame(&r, frame);
    assert(r.underruns == 1 && !r.playing && !decode(frame));
    for (unsigned i = 0; i < 20; ++i) mojo_pcm_frame(&r, frame);
    assert(r.underruns == 1);
    mojo_pcm_push(&r, input, 1536);
    mojo_pcm_frame(&r, frame);
    assert(r.playing && !decode(frame));

    mojo_pcm_init(&r);
    const uint32_t bad_bits[] = {0x7fc00000, 0x7f800000, 0xff800000};
    float bad[8] = {0};
    for (unsigned i = 0; i < 3; ++i) memcpy(&bad[i], &bad_bits[i], 4);
    bad[3] = 2; bad[4] = -2;
    mojo_pcm_push(&r, bad, 4);
    assert(r.nonfinite == 3 && r.clipped == 2);
    for (unsigned i = 0; i < 4; ++i) for (unsigned ch = 0; ch < 2; ++ch)
        assert(r.data[i][ch] >= -1 && r.data[i][ch] <= 1);

    /* Overlarge/backlogged pushes retain the most recent prebuffer in order. */
    mojo_pcm_init(&r);
    for (unsigned i = 0; i < 20000; ++i) input[2 * i] = input[2 * i + 1] = (float)i / 20000;
    mojo_pcm_push(&r, input, 20000);
    assert(r.fill == MOJO_RING_PREBUFFER && r.dropped == 20000 - MOJO_RING_PREBUFFER);
    for (unsigned i = 0; i < MOJO_RING_PREBUFFER; ++i)
        assert(r.data[(r.head + i) % MOJO_RING_CAPACITY][0] == input[2 * (20000 - MOJO_RING_PREBUFFER + i)]);

    /* 30-second model: bursty 1024-frame capture vs. asynchronous USB clock. */
    mojo_pcm_init(&r);
    for (unsigned i = 0; i < 1024; ++i) { input[2 * i] = 0.25f; input[2 * i + 1] = -0.125f; }
    struct mojo_packet_clock clock;
    mojo_clock_init(&clock);
    assert(mojo_clock_feedback(&clock, MOJO_NOMINAL_Q16 - 8));
    unsigned producer = 0;
    for (unsigned microframe = 0; microframe < 240000; ++microframe) {
        producer += 6;
        if (producer >= 1024) { mojo_pcm_push(&r, input, 1024); producer -= 1024; }
        if (microframe == 800) r.enabled = 1;
        unsigned count = mojo_clock_packet_bytes(&clock) / 8;
        for (unsigned i = 0; i < count; ++i) {
            mojo_pcm_frame(&r, frame);
            assert(decode(frame) >= 0 && decode(frame) <= 536870911);
            assert(decode(frame + 4) <= 0 && decode(frame + 4) >= -536870911);
        }
    }
    assert(r.underruns == 0 && r.rendered > 1400000 && r.nonzero_output > 1400000);
    puts("PCM ring: prebuffer, stereo/gain, ramps, underruns, sanitization, backlog and 30-second clock model passed");
    return 0;
}
