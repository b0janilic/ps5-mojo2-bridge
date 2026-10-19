/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MOJO_PCM_RING_H
#define MOJO_PCM_RING_H
#include <stdint.h>
#include <stddef.h>
#define MOJO_RING_CAPACITY 8192u
#define MOJO_RING_PREBUFFER 1536u
#define MOJO_RING_LIMIT 4096u
#define MOJO_PCM_RAMP 960u
struct mojo_pcm_ring {
    float data[MOJO_RING_CAPACITY][2];
    unsigned head, fill, max_fill;
    int enabled, playing, fading;
    unsigned fade_in, fade_left;
    uint64_t pushed, rendered, nonzero_input, nonzero_output, silent, dropped;
    unsigned underruns, nonfinite, clipped;
};
void mojo_pcm_init(struct mojo_pcm_ring *r);
void mojo_pcm_push(struct mojo_pcm_ring *r, const float *input, size_t frames);
void mojo_pcm_fade_out(struct mojo_pcm_ring *r);
/* One stereo S32_LE frame, with fixed 0.25 gain, clipping and startup ramp. */
void mojo_pcm_frame(struct mojo_pcm_ring *r, uint8_t frame[8]);
#endif
