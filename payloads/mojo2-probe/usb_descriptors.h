/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MOJO_USB_DESCRIPTORS_H
#define MOJO_USB_DESCRIPTORS_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct mojo_usb_summary {
    int control_if, stream_if, alt, channels, subslot, bits;
    int clock_source, clock_selector, out_ep, feedback_ep;
    int async_out, feedback_bytes;
};

int mojo_parse_config(const uint8_t *buf, size_t len, FILE *log,
                      struct mojo_usb_summary *summary);
int mojo_matches_capture(const struct mojo_usb_summary *s);
int mojo_parse_rate_ranges(const uint8_t *buf, size_t len, FILE *log, int *supports_48k);
#endif
