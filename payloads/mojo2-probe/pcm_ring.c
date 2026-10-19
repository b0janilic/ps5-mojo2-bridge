/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pcm_ring.h"
#include <string.h>

void mojo_pcm_init(struct mojo_pcm_ring *r) { memset(r, 0, sizeof(*r)); }

static float sanitize(struct mojo_pcm_ring *r, float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    if ((bits & 0x7f800000u) == 0x7f800000u) { r->nonfinite++; return 0; }
    if (value > 1) { r->clipped++; return 1; }
    if (value < -1) { r->clipped++; return -1; }
    return value;
}

void mojo_pcm_push(struct mojo_pcm_ring *r, const float *input, size_t frames) {
    for (size_t i = 0; i < frames; ++i) {
        if (r->fill == MOJO_RING_CAPACITY) {
            r->head = (r->head + 1) % MOJO_RING_CAPACITY;
            r->fill--; r->dropped++;
        }
        unsigned tail = (r->head + r->fill) % MOJO_RING_CAPACITY;
        float l = sanitize(r, input[2 * i]), right = sanitize(r, input[2 * i + 1]);
        r->data[tail][0] = l; r->data[tail][1] = right;
        if (l > 0.0000001f || l < -0.0000001f || right > 0.0000001f || right < -0.0000001f)
            r->nonzero_input++;
        r->fill++; r->pushed++;
    }
    if (r->fill > r->max_fill) r->max_fill = r->fill;
    /* Avoid playing an old capture backlog; retain the most recent prebuffer. */
    if (r->fill > MOJO_RING_LIMIT) {
        unsigned discard = r->fill - MOJO_RING_PREBUFFER;
        r->head = (r->head + discard) % MOJO_RING_CAPACITY;
        r->fill -= discard; r->dropped += discard;
        r->fade_in = 0;
    }
}

void mojo_pcm_fade_out(struct mojo_pcm_ring *r) {
    if (!r->fading) { r->fading = 1; r->fade_left = MOJO_PCM_RAMP; }
}

void mojo_pcm_frame(struct mojo_pcm_ring *r, uint8_t frame[8]) {
    memset(frame, 0, 8);
    if (!r->enabled || (r->fading && !r->fade_left)) { r->silent++; return; }
    if (!r->playing && r->fill >= MOJO_RING_PREBUFFER) { r->playing = 1; r->fade_in = 0; }
    if (r->playing && !r->fill) { r->playing = 0; r->underruns++; r->fade_in = 0; }
    if (!r->playing) { r->silent++; return; }
    double gain = 0.25;
    if (r->fade_in < MOJO_PCM_RAMP) gain *= (double)r->fade_in++ / MOJO_PCM_RAMP;
    if (r->fading) gain *= (double)--r->fade_left / MOJO_PCM_RAMP;
    unsigned head = r->head;
    r->head = (head + 1) % MOJO_RING_CAPACITY;
    r->fill--; r->rendered++;
    int nonzero = 0;
    for (unsigned ch = 0; ch < 2; ++ch) {
        int32_t sample = (int32_t)((double)r->data[head][ch] * gain * 2147483647.0);
        nonzero |= sample != 0;
        uint32_t bits = (uint32_t)sample;
        for (unsigned b = 0; b < 4; ++b) frame[ch * 4 + b] = (uint8_t)(bits >> (8 * b));
    }
    r->nonzero_output += nonzero;
}
