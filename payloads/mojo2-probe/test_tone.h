/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MOJO_TEST_TONE_H
#define MOJO_TEST_TONE_H
#include <stdint.h>

#define MOJO_TONE_FRAMES 48000u
#define MOJO_TONE_RAMP 960u
#define MOJO_TONE_TAIL 4800u
#define MOJO_TONE_PEAK 16777216 /* -42.14 dBFS peak in signed 32-bit PCM. */
struct mojo_test_tone {
    int armed;
    uint32_t position, nonzero_frames;
};
void mojo_tone_arm(struct mojo_test_tone *t);
/* Writes one stereo S32_LE frame. Unarmed/finished generators write zero. */
void mojo_tone_frame(struct mojo_test_tone *t, uint8_t frame[8]);
int mojo_tone_finished(const struct mojo_test_tone *t);
#endif
