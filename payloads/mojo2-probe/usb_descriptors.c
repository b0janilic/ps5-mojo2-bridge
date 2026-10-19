/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "usb_descriptors.h"
#include <string.h>

static unsigned u16(const uint8_t *p) { return p[0] | ((unsigned)p[1] << 8); }

int mojo_parse_config(const uint8_t *buf, size_t len, FILE *log,
                      struct mojo_usb_summary *s) {
    memset(s, 0, sizeof(*s));
    s->control_if = s->stream_if = s->alt = -1;
    s->clock_source = s->clock_selector = -1;
    if (len < 9 || buf[0] != 9 || buf[1] != 2) {
        fprintf(log, "ERROR: invalid configuration header\n"); return -1;
    }
    size_t total = u16(buf + 2);
    if (total < 9 || total > len) {
        fprintf(log, "ERROR: truncated configuration (%zu/%zu)\n", len, total); return -1;
    }
    int iface = -1, alt = -1, cls = -1, sub = -1, proto = -1;
    fprintf(log, "CONFIG value=%u bytes=%zu interfaces=%u\n", buf[5], total, buf[4]);
    for (size_t pos = 0; pos < total;) {
        if (total - pos < 2) goto malformed;
        const uint8_t *d = buf + pos;
        size_t n = d[0];
        if (n < 2 || n > total - pos) goto malformed;
        if (d[1] == 4) {
            if (n < 9) goto malformed;
            iface = d[2]; alt = d[3]; cls = d[5]; sub = d[6]; proto = d[7];
            fprintf(log, "INTERFACE %d alt=%d class=%02x sub=%02x proto=%02x eps=%u\n",
                    iface, alt, cls, sub, proto, d[4]);
            if (cls == 1 && sub == 1 && proto == 0x20) s->control_if = iface;
        } else if (d[1] == 5) {
            if (n < 7) goto malformed;
            fprintf(log, "  ENDPOINT 0x%02x attributes=0x%02x max_packet=%u interval=%u\n",
                    d[2], d[3], u16(d + 4), d[6]);
            if (cls == 1 && sub == 2 && proto == 0x20 && alt == 1) {
                if ((d[3] & 3) == 1 && !(d[2] & 0x80) && ((d[3] >> 4) & 3) == 0) {
                    s->out_ep = d[2]; s->async_out = ((d[3] >> 2) & 3) == 1;
                }
                if ((d[3] & 3) == 1 && (d[2] & 0x80) && ((d[3] >> 4) & 3) == 1) {
                    s->feedback_ep = d[2]; s->feedback_bytes = u16(d + 4) & 0x7ff;
                }
            }
        } else if (d[1] == 0x24) {
            if (n < 3) goto malformed;
            if (cls == 1 && sub == 1 && proto == 0x20) {
                if (d[2] == 0x0a) {
                    if (n < 8) goto malformed;
                    s->clock_source = d[3];
                    fprintf(log, "  CLOCK SOURCE id=0x%02x attributes=0x%02x controls=0x%02x\n", d[3], d[4], d[5]);
                } else if (d[2] == 0x0b) {
                    if (n < 7 || n < (size_t)7 + d[4]) goto malformed;
                    s->clock_selector = d[3];
                    fprintf(log, "  CLOCK SELECTOR id=0x%02x inputs=%u\n", d[3], d[4]);
                }
            } else if (cls == 1 && sub == 2 && proto == 0x20 && alt == 1) {
                if (d[2] == 1) {
                    if (n < 16) goto malformed;
                    s->stream_if = iface; s->alt = alt; s->channels = d[10];
                    fprintf(log, "  STREAM channels=%u format_type=%u terminal=0x%02x\n", d[10], d[5], d[3]);
                } else if (d[2] == 2) {
                    if (n < 6) goto malformed;
                    s->subslot = d[4]; s->bits = d[5];
                    fprintf(log, "  FORMAT type=%u subslot=%u bits=%u\n", d[3], d[4], d[5]);
                }
            }
        }
        pos += n;
    }
    return 0;
malformed:
    fprintf(log, "ERROR: malformed descriptor\n");
    return -1;
}

int mojo_matches_capture(const struct mojo_usb_summary *s) {
    return s->control_if == 1 && s->stream_if == 2 && s->alt == 1 &&
           s->channels == 2 && s->subslot == 4 && s->bits == 32 &&
           s->clock_source == 0x29 && s->clock_selector == 0x28 &&
           s->out_ep == 0x05 && s->async_out && s->feedback_ep == 0x81 &&
           s->feedback_bytes == 4;
}

static uint32_t u32(const uint8_t *p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int mojo_parse_rate_ranges(const uint8_t *buf, size_t len, FILE *log, int *supports_48k) {
    *supports_48k = -1;
    if (len < 2) return -1;
    unsigned count = u16(buf);
    if (!count || count > 64 || len != 2 + 12 * (size_t)count) {
        fprintf(log, "ERROR: invalid rate range length/count\n"); return -1;
    }
    int supported = 0;
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t *r = buf + 2 + 12 * i;
        uint32_t min = u32(r), max = u32(r + 4), step = u32(r + 8);
        fprintf(log, "  RATE RANGE min=%u max=%u step=%u\n", min, max, step);
        if (!min || max < min) return -1;
        if (min <= 48000 && max >= 48000 && (!step || (48000 - min) % step == 0)) supported = 1;
    }
    *supports_48k = supported;
    return 0;
}
