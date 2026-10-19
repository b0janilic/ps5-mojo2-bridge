/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../payloads/mojo2-probe/usb_descriptors.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *in = fopen(argv[1], "rb"); assert(in);
    uint8_t good[4096], work[4096];
    size_t n = fread(good, 1, sizeof(good), in);
    assert(!ferror(in) && n == 176); fclose(in);
    FILE *log = fopen("/dev/null", "w"); assert(log);
    struct mojo_usb_summary s;
    assert(mojo_parse_config(good, n, log, &s) == 0);
    assert(mojo_matches_capture(&s));
    /* Every incomplete prefix must be rejected, with no out-of-bounds access. */
    for (size_t cut = 0; cut < n; ++cut)
        assert(mojo_parse_config(good, cut, log, &s) == -1);
    /* A structurally valid configuration without a feedback endpoint is not a match. */
    memcpy(work, good, n);
    int changed = 0;
    for (size_t pos = 0; pos < n; pos += work[pos]) {
        if (work[pos + 1] == 5 && work[pos + 2] == 0x81) {
            work[pos + 3] = 3; changed = 1; break;
        }
    }
    assert(changed);
    assert(mojo_parse_config(work, n, log, &s) == 0);
    assert(!mojo_matches_capture(&s));
    /* A zero-length descriptor must fail rather than hang. */
    memcpy(work, good, n); work[9] = 0;
    assert(mojo_parse_config(work, n, log, &s) == -1);
    /* Malformed descriptor mutations are exercised under ASan/UBSan. */
    uint32_t state = 0x245f0815;
    for (unsigned i = 0; i < 20000; ++i) {
        memcpy(work, good, n);
        state = state * 1664525u + 1013904223u;
        size_t pos = state % n;
        state = state * 1664525u + 1013904223u;
        work[pos] = (uint8_t)(state >> 24);
        (void)mojo_parse_config(work, n, log, &s);
    }
    /* Exercise UAC2 clock range decoding, including off-grid and malformed ranges. */
    int supports;
    const uint8_t fixed_48k[] = {1,0, 0x80,0xbb,0,0, 0x80,0xbb,0,0, 0,0,0,0};
    assert(mojo_parse_rate_ranges(fixed_48k, sizeof(fixed_48k), log, &supports) == 0 && supports == 1);
    for (size_t cut = 0; cut < sizeof(fixed_48k); ++cut)
        assert(mojo_parse_rate_ranges(fixed_48k, cut, log, &supports) == -1 && supports == -1);
    const uint8_t off_grid[] = {1,0, 0x44,0xac,0,0, 0x88,0x58,1,0, 0x44,0xac,0,0};
    assert(mojo_parse_rate_ranges(off_grid, sizeof(off_grid), log, &supports) == 0 && supports == 0);
    const uint8_t continuous[] = {1,0, 0,0x7d,0,0, 0,0x77,1,0, 0,0,0,0};
    assert(mojo_parse_rate_ranges(continuous, sizeof(continuous), log, &supports) == 0 && supports == 1);
    const uint8_t backwards[] = {1,0, 0,0x77,1,0, 0,0x7d,0,0, 0,0,0,0};
    assert(mojo_parse_rate_ranges(backwards, sizeof(backwards), log, &supports) == -1 && supports == -1);
    fclose(log);
    puts("PASS: captured Mojo profile, 176 truncations, missing feedback, zero-length descriptor, 20000 mutations, clock range decoding (ASan/UBSan)");
    return 0;
}
