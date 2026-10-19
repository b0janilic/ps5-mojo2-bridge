/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "test_tone.h"

/* 1 kHz at nominal 48 kHz; integer table avoids a target libm dependency. */
static const int32_t wave[48] = {
    0, 2189866, 4342263, 6420363, 8388608, 10213322, 11863283, 13310260,
    14529495, 15500126, 16205546, 16633685, 16777216, 16633685, 16205546, 15500126,
    14529495, 13310260, 11863283, 10213322, 8388608, 6420363, 4342263, 2189866,
    0, -2189866, -4342263, -6420363, -8388608, -10213322, -11863283, -13310260,
    -14529495, -15500126, -16205546, -16633685, -16777216, -16633685, -16205546, -15500126,
    -14529495, -13310260, -11863283, -10213322, -8388608, -6420363, -4342263, -2189866
};

void mojo_tone_arm(struct mojo_test_tone *t) {
    if (!t->armed) *t = (struct mojo_test_tone){.armed = 1};
}

int mojo_tone_finished(const struct mojo_test_tone *t) {
    return t->armed && t->position >= MOJO_TONE_FRAMES + MOJO_TONE_TAIL;
}

void mojo_tone_frame(struct mojo_test_tone *t, uint8_t frame[8]) {
    int32_t sample = 0;
    if (t->armed && !mojo_tone_finished(t)) {
        uint32_t p = t->position++;
        if (p < MOJO_TONE_FRAMES) {
            uint32_t gain = MOJO_TONE_RAMP;
            if (p < gain) gain = p;
            uint32_t remaining = MOJO_TONE_FRAMES - 1 - p;
            if (remaining < gain) gain = remaining;
            sample = (int32_t)((int64_t)wave[p % 48] * gain / MOJO_TONE_RAMP);
            if (sample) t->nonzero_frames++;
        }
    }
    uint32_t bits = (uint32_t)sample;
    for (unsigned channel = 0; channel < 2; ++channel)
        for (unsigned b = 0; b < 4; ++b)
            frame[channel * 4 + b] = (uint8_t)(bits >> (8 * b));
}
