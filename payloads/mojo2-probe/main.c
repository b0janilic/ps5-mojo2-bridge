/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ps5/kernel.h>
#include "usb_descriptors.h"
#include "feedback_fs.h"
#include "capture.h"
#include "control.h"
#include "hdmi.h"

#define REPORT "/data/mojo2-probe.txt"
#define PROBE_VERSION "0.13.0"
#define CAPTURE_ONLY 0
#define BRIDGE_MODE 1
#define RESIDENT_MODE 1
static struct mojo_control control = {.fd = -1};
static struct mojo_hdmi hdmi;
static int stop_requested;

static int want_stop(void) {
    if (stop_requested) return 1;
    if (control.fd >= 0 && mojo_control_stop(&control) != 0) stop_requested = 1;
    return stop_requested;
}

/* Notification ABI used by the SDK hello_world sample. */
typedef struct { char reserved[45]; char message[3075]; } notify_request_t;
int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

static void notify(const char *msg) {
    notify_request_t r = {0};
    snprintf(r.message, sizeof(r.message), "%s", msg);
    sceKernelSendNotificationRequest(0, &r, sizeof(r), 0);
}

static int copy_report(FILE *report, const char *destination) {
    FILE *dst = fopen(destination, "w");
    if (!dst) return -1;
    rewind(report);
    char b[4096];
    size_t n;
    int failed = 0;
    while ((n = fread(b, 1, sizeof(b), report)) != 0) {
        if (fwrite(b, 1, n, dst) != n) { failed = 1; break; }
    }
    if (ferror(report)) failed = 1;
    if (fflush(dst)) failed = 1;
    if (fsync(fileno(dst))) failed = 1;
    if (fclose(dst)) failed = 1;
    clearerr(report);
    return failed ? -1 : 0;
}

/* Recognize whole device nodes, not endpoints or arbitrary /dev paths. */
static int is_ugen(const char *name) {
    unsigned bus, addr; char tail;
    return sscanf(name, "ugen%u.%u%c", &bus, &addr, &tail) == 2;
}

static uint32_t le32(const uint8_t *p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Only device-to-host UAC2 GET_CUR (1) / GET_RANGE (2) requests. */
static int get_control(int fd, FILE *log, const char *label, int iface, int entity,
                       int selector, int request, uint8_t *data, uint16_t size) {
    struct usb_ctl_request ctl = {0};
    ctl.ucr_data = data;
    ctl.ucr_request.bmRequestType = UT_READ_CLASS_INTERFACE;
    ctl.ucr_request.bRequest = request;
    USETW(ctl.ucr_request.wValue, selector << 8);
    USETW(ctl.ucr_request.wIndex, (entity << 8) | iface);
    USETW(ctl.ucr_request.wLength, size);
    fprintf(log, "GET %s entity=0x%02x selector=%d request=%d bytes=%u\n", label, entity, selector, request, size);
    fflush(log);
    if (ioctl(fd, USB_DO_REQUEST, &ctl) != 0) {
        fprintf(log, "  ERROR errno=%d\n", errno); fflush(log); return -1;
    }
    fprintf(log, "  actual_length=%u raw=", ctl.ucr_actlen);
    for (unsigned i = 0; i < size; ++i) fprintf(log, "%02x", data[i]);
    fputc('\n', log); fflush(log);
    if (ctl.ucr_actlen != size) {
        fprintf(log, "  ERROR: short or unreported control response\n"); return -1;
    }
    return 0;
}

static void probe_controls(const char *path, FILE *log, const struct mojo_usb_summary *s,
                           int *current_rate, int *valid, int *supports_48k) {
    fprintf(log, "\nUAC2 CONTROL QUERIES (GET only)\n"); fflush(log);
    /* Some FreeBSD versions require an O_RDWR control handle even for GETs. */
    int fd = open(path, O_RDWR);
    if (fd < 0) { fprintf(log, "Control open O_RDWR errno=%d\n", errno); return; }
    struct usb_device_descriptor dd = {0};
    if (ioctl(fd, USB_GET_DEVICE_DESC, &dd) || UGETW(dd.idVendor) != 0x245f || UGETW(dd.idProduct) != 0x0815) {
        fprintf(log, "ERROR: control handle device identity mismatch\n"); close(fd); return;
    }
    struct usb_alt_interface alt = {.uai_interface_index = s->stream_if};
    if (ioctl(fd, USB_GET_ALTINTERFACE, &alt) == 0)
        fprintf(log, "STREAM alternate index=%u (unchanged)\n", alt.uai_alt_index);
    else fprintf(log, "USB_GET_ALTINTERFACE errno=%d\n", errno);
    uint8_t rate[4] = {0}, clock_valid[1] = {0}, pin[1] = {0}, header[2] = {0};
    if (get_control(fd, log, "current rate", s->control_if, s->clock_source, 1, 1, rate, sizeof(rate)) == 0) {
        uint32_t hz = le32(rate);
        fprintf(log, "CURRENT_RATE_HZ=%u\n", hz);
        if (hz <= 2147483647u) *current_rate = (int)hz;
    }
    if (get_control(fd, log, "clock validity", s->control_if, s->clock_source, 2, 1, clock_valid, sizeof(clock_valid)) == 0) {
        *valid = clock_valid[0] != 0;
        fprintf(log, "CLOCK_VALID=%d\n", *valid);
    }
    if (get_control(fd, log, "clock selector", s->control_if, s->clock_selector, 1, 1, pin, sizeof(pin)) == 0)
        fprintf(log, "CLOCK_SELECTOR_PIN=%u\n", pin[0]);
    if (get_control(fd, log, "rate range count", s->control_if, s->clock_source, 1, 2, header, sizeof(header)) == 0) {
        unsigned count = header[0] | ((unsigned)header[1] << 8);
        fprintf(log, "RATE_RANGE_COUNT=%u\n", count);
        if (count && count <= 64) {
            uint8_t ranges[2 + 12 * 64] = {0};
            uint16_t size = 2 + 12 * count;
            if (get_control(fd, log, "rate ranges", s->control_if, s->clock_source, 1, 2, ranges, size) == 0 &&
                mojo_parse_rate_ranges(ranges, size, log, supports_48k) == 0)
                fprintf(log, "SUPPORTS_48000=%d\n", *supports_48k);
        } else fprintf(log, "ERROR: rate range count outside bounded query limit\n");
    }
    fprintf(log, "END CONTROL QUERIES: no settings changed\n"); fflush(log);
    close(fd);
}

struct feedback_result {
    unsigned long long packets, plausible;
    int restored;
    unsigned long long out_packets, completions, transfer_errors;
    int device_gone;
};

static int64_t monotonic_ms(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return -1;
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static int64_t monotonic_us(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return -1;
    return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

static int set_rate(int fd, FILE *log, int iface, int entity, uint32_t rate) {
    uint8_t data[4] = {rate & 255, (rate >> 8) & 255, (rate >> 16) & 255, rate >> 24};
    struct usb_ctl_request ctl = {0};
    ctl.ucr_data = data;
    ctl.ucr_request.bmRequestType = UT_WRITE_CLASS_INTERFACE;
    ctl.ucr_request.bRequest = 1;
    USETW(ctl.ucr_request.wValue, 1 << 8);
    USETW(ctl.ucr_request.wIndex, (entity << 8) | iface);
    USETW(ctl.ucr_request.wLength, sizeof(data));
    fprintf(log, "SET clock rate=%u\n", rate); fflush(log);
    int rc = ioctl(fd, USB_DO_REQUEST, &ctl);
    fprintf(log, "  result=%d errno=%d actual_length=%u\n", rc, rc ? errno : 0, ctl.ucr_actlen);
    fflush(log);
    return rc;
}

static int set_alt(int fd, FILE *log, int iface, int alt) {
    struct usb_alt_interface a = {.uai_interface_index = iface, .uai_alt_index = alt};
    fprintf(log, "SET alternate interface=%d index=%d\n", iface, alt); fflush(log);
    int rc = ioctl(fd, USB_SET_ALTINTERFACE, &a);
    fprintf(log, "  result=%d errno=%d\n", rc, rc ? errno : 0); fflush(log);
    return rc;
}

/* Resident bridge session; restores state before retry/stop when the device remains present. */
static struct feedback_result probe_feedback(const char *path, FILE *log,
        const struct mojo_usb_summary *s, int rate_known, int clock_valid, int supports_48k) {
    struct feedback_result result = {.restored = -1};
    fprintf(log, "\nRESIDENT AUDIO BRIDGE: capture -> stereo PCM -> USB; gain=0.25\n"); fflush(log);
    if (rate_known <= 0 || clock_valid != 1 || supports_48k != 1 || monotonic_ms() < 0) {
        fprintf(log, "SKIP: clock/rate/time prerequisites not confirmed\n"); return result;
    }
    unsigned bus, address; char extra;
    if (sscanf(path, "/dev/ugen%u.%u%c", &bus, &address, &extra) != 2) return result;
    int fd = open(path, O_RDWR);
    struct mojo_feedback_fs reader = {0};
    static struct mojo_capture capture = {.cleanup_ok = 1};
    static int capture_loaded;
    capture.log = log;
    int capture_attempted = 0, capture_status = 0;
    static struct mojo_pcm_ring ring;
    _Alignas(64) static float capture_samples[MOJO_CAPTURE_BYTES / sizeof(float)];
    mojo_pcm_init(&ring);
    int rate_touched = 0, alt_touched = 0, fs_cleanup_ok = 1;
    uint32_t original_rate = 0;
    uint8_t original_alt = 0;
    if (fd < 0) { fprintf(log, "Control reopen errno=%d\n", errno); return result; }
    struct usb_device_descriptor dd = {0};
    if (ioctl(fd, USB_GET_DEVICE_DESC, &dd) || UGETW(dd.idVendor) != 0x245f || UGETW(dd.idProduct) != 0x0815) {
        fprintf(log, "SKIP: control device identity not confirmed\n"); goto cleanup;
    }
    int config = -1;
    struct usb_alt_interface alt = {.uai_interface_index = s->stream_if};
    uint8_t rate[4] = {0}, pin[1] = {0};
    if (ioctl(fd, USB_GET_CONFIG, &config) || config != 0 ||
        ioctl(fd, USB_GET_ALTINTERFACE, &alt) || alt.uai_alt_index != 0) {
        fprintf(log, "SKIP: audio interface is not idle in configuration index 0\n"); goto cleanup;
    }
    original_alt = alt.uai_alt_index;
    if (get_control(fd, log, "saved rate", s->control_if, s->clock_source, 1, 1, rate, 4) ||
        get_control(fd, log, "saved selector", s->control_if, s->clock_selector, 1, 1, pin, 1)) goto cleanup;
    original_rate = le32(rate);
    if (!original_rate || original_rate > 768000 || pin[0] != 1) {
        fprintf(log, "SKIP: original rate/selector state is unsuitable\n"); goto cleanup;
    }
    fprintf(log, "SAVED_STATE rate=%u alt=%u selector_pin=%u\n", original_rate, original_alt, pin[0]);
    notify("Mojo2 " PROBE_VERSION ": connecting audio; launch ELF again to stop");
    if (original_rate != 48000) {
        rate_touched = 1;
        if (set_rate(fd, log, s->control_if, s->clock_source, 48000)) goto cleanup;
    }
    if (get_control(fd, log, "48k readback", s->control_if, s->clock_source, 1, 1, rate, 4) || le32(rate) != 48000) {
        fprintf(log, "ERROR: 48k setting not confirmed\n"); goto cleanup;
    }
    alt_touched = 1;
    if (set_alt(fd, log, s->stream_if, s->alt)) goto cleanup;
    if (mojo_fs_open(&reader, fd, s->feedback_ep, s->out_ep, log)) goto cleanup;
    if (BRIDGE_MODE) {
        capture_attempted = 1;
        capture_status = capture_loaded ? mojo_capture_restart(&capture) : mojo_capture_open(&capture, log);
        if (capture_status >= 0) capture_loaded = 1;
        if (capture_status != 1) goto cleanup;
        reader.source = &ring;
    }
    fprintf(log, "Both endpoints use USB_FS: feedback=0x%02x OUT=0x%02x\n", s->feedback_ep, s->out_ep);
    fprintf(log, "OUT: two queued transfers, 64 ISO frames each, feedback-driven 40/48/56 PCM bytes/frame\n");
    fprintf(log, "Resident session: no duration cap; stop marker checked at 100 ms intervals, then 100 ms fade/drain\n"); fflush(log);
    if (mojo_hdmi_prepare(&hdmi, log) < 0) goto cleanup;
    if (mojo_fs_start(&reader, MOJO_FS_OUT, log) ||
        mojo_fs_start(&reader, MOJO_FS_OUT_SECOND, log) ||
        mojo_fs_start(&reader, MOJO_FS_FEEDBACK, log)) goto cleanup;
    int64_t start = monotonic_us();
    if (start < 0) goto cleanup;
    uint32_t min = UINT32_MAX, max = 0;
    uint64_t sum = 0;
    unsigned long long short_reads = 0, empty_reads = 0, again_reads = 0;
    unsigned long long out_completions = 0, out_short = 0;
    uint64_t out_bytes = 0;
    unsigned long long packet_sizes[8] = {0};
    struct { int index; uint32_t value; } samples[32];
    unsigned sample_count = 0;
    int64_t elapsed = 0, tone_started_us = -1, next_stop_check = 0, stop_at = -1;
    int64_t last_out_us = 0, last_feedback_us = 0;
    for (;;) {
        int64_t now = monotonic_us();
        if (now < start) break;
        elapsed = now - start;
        if (RESIDENT_MODE && elapsed >= next_stop_check) {
            next_stop_check = elapsed + 100000;
            if (want_stop() && stop_at < 0) {
                mojo_pcm_fade_out(&ring);
                stop_at = elapsed + 100000;
            }
        }
        if (stop_at >= 0 && elapsed >= stop_at) break;
        if (RESIDENT_MODE && (elapsed - last_out_us > 2000000 || elapsed - last_feedback_us > 2000000)) {
            fprintf(log, "BRIDGE_STALL: OUT or valid feedback idle for over 2 s; restarting session\n");
            break;
        }
        if (!RESIDENT_MODE && elapsed >= (BRIDGE_MODE ? 30000000 : 3000000)) break;
        if (!BRIDGE_MODE && result.packets >= 128 && out_completions >= 2 && elapsed >= 1500000 && mojo_tone_finished(&reader.tone)) break;
        if (BRIDGE_MODE) {
            if (!RESIDENT_MODE && elapsed >= 29900000) mojo_pcm_fade_out(&ring);
            int read_error = 0;
            for (unsigned reads = 0; reads < 2; ++reads) {
                int bytes = mojo_capture_read(&capture, capture_samples, sizeof(capture_samples));
                if (bytes < 0) { read_error = 1; break; }
                if (!bytes) break;
                mojo_pcm_push(&ring, capture_samples, (unsigned)bytes / 8);
            }
            if (read_error) break;
        }
        unsigned slot = 0;
        int completed = mojo_fs_complete(&reader, &slot, log);
        if (completed < 0) break;
        if (!completed) { again_reads++; usleep(500); continue; }
        struct usb_fs_endpoint *ep = &reader.endpoints[slot];
        if (slot == MOJO_FS_FEEDBACK) result.completions++;
        if (ep->status != 0) result.transfer_errors++;
        else if (slot != MOJO_FS_FEEDBACK) {
            last_out_us = elapsed;
            out_completions++;
            if (ep->aFrames != MOJO_FS_OUT_FRAMES) out_short++;
            for (unsigned i = 0; i < ep->aFrames; ++i) {
                out_bytes += reader.lengths[slot][i];
                uint32_t n = reader.lengths[slot][i];
                if (n == reader.requested[slot][i]) {
                    result.out_packets++;
                    if (n % 8 == 0 && n / 8 < 8) packet_sizes[n / 8]++;
                } else out_short++;
            }
        } else for (unsigned i = 0; i < ep->aFrames && (RESIDENT_MODE || result.packets < (BRIDGE_MODE ? 32768 : 4096)); ++i) {
            uint32_t n = reader.lengths[slot][i];
            const uint8_t *data = reader.data[i];
            if (n == 4) {
                uint32_t value = le32(data);
                if (sample_count < 32 && (result.packets < 16 || result.packets % 128 == 0)) {
                    samples[sample_count].index = result.packets;
                    samples[sample_count++].value = value;
                }
                result.packets++;
                if (value < min) min = value;
                if (value > max) max = value;
                sum += value;
                if (mojo_clock_feedback(&reader.clock, value)) { result.plausible++; last_feedback_us = elapsed; }
            } else if (n > 0) short_reads++;
            else empty_reads++;
        }
        if (result.transfer_errors) break;
        if (elapsed >= 100000 && reader.clock.accepted >= 16 && out_completions >= 2) {
            if (BRIDGE_MODE) ring.enabled = 1;
            else if (!reader.tone.armed) {
                mojo_tone_arm(&reader.tone);
                tone_started_us = elapsed;
            }
        }
        /* Wait for actual captured PCM output, not merely a connected USB node.
         * No log/notification writes in the running transfer loop. */
        if (BRIDGE_MODE && !out_short && ring.playing && ring.nonzero_output && ring.rendered >= 48000 &&
            elapsed >= 1000000 && stop_at < 0 && mojo_hdmi_mute(&hdmi) < 0) break;
        if ((slot != MOJO_FS_FEEDBACK || (RESIDENT_MODE || result.packets < (BRIDGE_MODE ? 32768 : 4096))) && mojo_fs_start(&reader, slot, log)) break;
    }
    /* Stop queued USB transfers before potentially slow report writes. */
    if (mojo_fs_close(&reader, log)) fs_cleanup_ok = 0;
    if (mojo_hdmi_restore(&hdmi, log)) stop_requested = 1;
    fprintf(log, "STREAM_ELAPSED_US=%lld\n", (long long)elapsed);
    for (unsigned i = 0; i < sample_count; ++i) {
        uint32_t v = samples[i].value;
        fprintf(log, "  FEEDBACK sample=%d raw=%08x Q16.16=%.8f implied_hz=%.3f\n",
                samples[i].index, v, v / 65536.0, v / 65536.0 * 8000.0);
    }
    for (unsigned slot = 0; slot < MOJO_FS_SLOTS; ++slot)
        fprintf(log, "USB_FS_SLOT slot=%u completions=%u first_time=%u last_time=%u\n",
                slot, reader.completions[slot], reader.first_time[slot], reader.last_time[slot]);
    fprintf(log, "PACKET_CLOCK accepted=%u rejected=%u last_q16=%u fraction=%u packets_5=%llu packets_6=%llu packets_7=%llu\n",
            reader.clock.accepted, reader.clock.rejected, reader.clock.q16, reader.clock.fraction,
            packet_sizes[5], packet_sizes[6], packet_sizes[7]);
    fprintf(log, "FEEDBACK_RESULT packets=%llu plausible_48k=%llu short=%llu empty=%llu pending=%llu\n",
            result.packets, result.plausible, short_reads, empty_reads, again_reads);
    if (result.packets)
        fprintf(log, "FEEDBACK_STATS min_raw=%u max_raw=%u mean_samples_per_microframe=%.8f\n",
                min, max, (double)sum / result.packets / 65536.0);
    fprintf(log, "PCM_RESULT completed_packets=%llu completed_bytes=%llu out_completions=%llu short=%llu\n",
            result.out_packets, (unsigned long long)out_bytes, out_completions, out_short);
    fprintf(log, "TONE_RESULT armed=%d started_us=%lld generated_frames=%u nonzero_frames=%u finished=%d peak_s32=%d\n",
            reader.tone.armed, (long long)tone_started_us, reader.tone.position, reader.tone.nonzero_frames,
            mojo_tone_finished(&reader.tone), MOJO_TONE_PEAK);
    fprintf(log, "OUT counts USB_FS completions; user confirms game/system audio audibility\n");
    fprintf(log, "USB_FS_RESULT feedback_completions=%llu out_completions=%llu transfer_errors=%llu\n",
            result.completions, out_completions, result.transfer_errors);
cleanup:;
    if (reader.initialized && mojo_fs_close(&reader, log)) fs_cleanup_ok = 0;
    /* Also handles setup failures and a partially applied/failed mute. */
    if (mojo_hdmi_restore(&hdmi, log)) stop_requested = 1;
    if (capture_attempted) {
        if (mojo_capture_close(&capture)) fs_cleanup_ok = 0;
        fprintf(log, "BRIDGE_CAPTURE opened=%d frames=%llu nonzero=%llu errors=%u empty=%u overruns=%u closed=%d\n",
                capture_status, (unsigned long long)ring.pushed, (unsigned long long)ring.nonzero_input,
                capture.errors, capture.empty, capture.overruns, capture.cleanup_ok);
        fprintf(log, "BRIDGE_PCM rendered=%llu nonzero=%llu silent=%llu dropped=%llu underruns=%u nonfinite=%u clipped=%u fill=%u max_fill=%u gain=0.25\n",
                (unsigned long long)ring.rendered, (unsigned long long)ring.nonzero_output,
                (unsigned long long)ring.silent, (unsigned long long)ring.dropped, ring.underruns,
                ring.nonfinite, ring.clipped, ring.fill, ring.max_fill);
        memset(capture_samples, 0, sizeof(capture_samples));
        memset(ring.data, 0, sizeof(ring.data));
    }
    struct usb_device_descriptor current_dd = {0};
    int present = access(path, F_OK) == 0 && ioctl(fd, USB_GET_DEVICE_DESC, &current_dd) == 0 &&
                  UGETW(current_dd.idVendor) == 0x245f && UGETW(current_dd.idProduct) == 0x0815;
    if (!present) {
        result.device_gone = 1;
        if (alt_touched || rate_touched) result.restored = 0;
        fprintf(log, "DEVICE_GONE: old device handle unavailable; USB state cannot be restored on a detached device\n");
    }
    if ((alt_touched || rate_touched) && present) {
        int ok = fs_cleanup_ok;
        if (alt_touched && set_alt(fd, log, s->stream_if, original_alt)) ok = 0;
        if (rate_touched && set_rate(fd, log, s->control_if, s->clock_source, original_rate)) ok = 0;
        uint8_t restored_rate[4] = {0};
        struct usb_alt_interface restored_alt = {.uai_interface_index = s->stream_if};
        if (ioctl(fd, USB_GET_ALTINTERFACE, &restored_alt) || restored_alt.uai_alt_index != original_alt) ok = 0;
        if (get_control(fd, log, "restored rate", s->control_if, s->clock_source, 1, 1, restored_rate, 4) ||
            le32(restored_rate) != original_rate) ok = 0;
        result.restored = ok;
        fprintf(log, "RESTORE_RESULT ok=%d rate=%u alt=%u\n", ok, le32(restored_rate), restored_alt.uai_alt_index);
    }
    fprintf(log, "END RESIDENT BRIDGE SESSION\n"); fflush(log);
    close(fd);
    return result;
}

struct capture_result {
    int opened, cleanup_ok;
    unsigned blocks, nonzero_blocks, frames, invalid_samples;
    unsigned errors, empty, overruns;
    float peak[2];
};

static struct capture_result probe_capture(FILE *log) {
    struct capture_result r = {.cleanup_ok = 1};
    struct mojo_capture c;
    fprintf(log, "\nCAPTURE_ONLY: USB state unchanged; capture levels only, no recorded audio saved\n"); fflush(log);
    notify("Mojo2 " PROBE_VERSION ": capture test, make menu/game sounds for 8 seconds");
    r.opened = mojo_capture_open(&c, log);
    if (r.opened == 1) {
        _Alignas(64) static float samples[MOJO_CAPTURE_BYTES / sizeof(float)];
        double squares[2] = {0};
        unsigned valid[2] = {0};
        int64_t start = monotonic_ms(), elapsed = 0;
        if (start >= 0) for (unsigned attempts = 0; attempts < 20000; ++attempts) {
            int64_t now = monotonic_ms();
            if (now < start || now - start >= 8000) break;
            elapsed = now - start;
            int bytes = mojo_capture_read(&c, samples, sizeof(samples));
            if (bytes < 0) break;
            if (!bytes) { usleep(2000); continue; }
            r.blocks++;
            r.frames += (unsigned)bytes / 8;
            int nonzero = 0;
            for (unsigned i = 0; i < (unsigned)bytes / sizeof(float); ++i) {
                uint32_t bits;
                memcpy(&bits, &samples[i], sizeof(bits));
                if ((bits & 0x7f800000u) == 0x7f800000u) { r.invalid_samples++; continue; }
                unsigned ch = i & 1;
                float value = samples[i], magnitude = value < 0 ? -value : value;
                if (magnitude > r.peak[ch]) r.peak[ch] = magnitude;
                if (magnitude > 0.0000001f) nonzero = 1;
                squares[ch] += (double)value * value;
                valid[ch]++;
            }
            r.nonzero_blocks += nonzero;
        }
        memset(samples, 0, sizeof(samples));
        fprintf(log, "CAPTURE_LEVELS elapsed_ms=%lld blocks=%u nonzero_blocks=%u frames=%u invalid_samples=%u\n",
                (long long)elapsed, r.blocks, r.nonzero_blocks, r.frames, r.invalid_samples);
        fprintf(log, "CAPTURE_LEVELS peak_l=%.9g peak_r=%.9g mean_square_l=%.9g mean_square_r=%.9g\n",
                (double)r.peak[0], (double)r.peak[1], valid[0] ? squares[0] / valid[0] : 0,
                valid[1] ? squares[1] / valid[1] : 0);
    }
    r.errors = c.errors;
    r.empty = c.empty;
    r.overruns = c.overruns;
    if (mojo_capture_close(&c)) r.cleanup_ok = 0;
    fprintf(log, "CAPTURE_RESULT opened=%d blocks=%u nonzero_blocks=%u frames=%u invalid_samples=%u errors=%u empty=%u overruns=%u cleanup_ok=%d\n",
            r.opened, r.blocks, r.nonzero_blocks, r.frames, r.invalid_samples, r.errors, r.empty, r.overruns, r.cleanup_ok);
    fprintf(log, "END CAPTURE TEST: no USB streaming or audio output in this version\n"); fflush(log);
    return r;
}

int main(void) {
    int primary = mojo_control_begin(&control, "/data/mojo2-bridge.lock", "/data/mojo2-stop");
    if (primary <= 0) {
        notify(primary == 0 ? "Mojo2: stop requested; wait for stopped notification" : "Mojo2: instance lock/stop request failed; USB unchanged");
        return primary == 0 ? 0 : 1;
    }
    FILE *log = fopen(REPORT, "w+");
    if (!log) {
        notify("Mojo2 probe: cannot write /data/mojo2-probe.txt"); mojo_control_end(&control); return 1;
    }
    notify("Mojo2 probe " PROBE_VERSION ": reading USB descriptors and clock");
    fprintf(log, "Mojo2 USB probe %s; target PS5 5.50\n", PROBE_VERSION);
    fprintf(log, "Resident bridge: Avcap2 source 2/mode 0 -> ring -> USB_FS, gain=0.25, retry detached devices/sessions, relaunch same ELF to stop. Experimental HDMI-only mute on 5.50.\n");
    fprintf(log, "pid=%d uid=%d\n", getpid(), getuid());
    int hdmi_ready = mojo_hdmi_open(&hdmi, kernel_get_fw_version(), "/data/mojo2-hdmi-owned", log);
    if (hdmi_ready < 0) {
        mojo_hdmi_close(&hdmi, log);
        fclose(log);
        mojo_control_end(&control);
        notify("Mojo2: HDMI recovery failed; bridge not started. Relaunch for recovery; see log");
        return 1;
    }
    int scanned = 0, opened = 0, denied = 0, descriptors = 0, found = 0, matched = 0;
    int current_rate = -1, clock_valid = -1, supports_48k = -1;
    struct feedback_result feedback = {.restored = -1};
    struct capture_result captured = {0};
    unsigned scan_round = 0;
    notify(hdmi_ready ? "Mojo2 " PROBE_VERSION ": HDMI auto-mute test; launch again to stop/restore TV" :
                       "Mojo2 " PROBE_VERSION ": HDMI control unavailable; mirrored audio mode");
    while (!want_stop()) {
    DIR *dev = opendir("/dev");
    if (!dev) fprintf(log, "ERROR: opendir /dev errno=%d\n", errno);
    else {
        struct dirent *ent;
        while ((ent = readdir(dev)) != NULL) {
            if (!is_ugen(ent->d_name)) continue;
            scanned++;
            char path[512];
            snprintf(path, sizeof(path), "/dev/%s", ent->d_name);
            int fd = open(path, O_RDONLY);
            if (fd < 0) {
                denied++; if (scan_round == 0) fprintf(log, "%s: open errno=%d\n", path, errno); continue;
            }
            opened++;
            struct usb_device_descriptor dd = {0};
            if (ioctl(fd, USB_GET_DEVICE_DESC, &dd) != 0) {
                fprintf(log, "%s: USB_GET_DEVICE_DESC errno=%d\n", path, errno);
                close(fd); continue;
            }
            descriptors++;
            if (UGETW(dd.idVendor) != 0x245f || UGETW(dd.idProduct) != 0x0815) {
                close(fd); continue;
            }
            found++;
            fprintf(log, "\nMOJO2 %s vid=245f pid=0815 bcdUSB=%04x bcdDevice=%04x configs=%u\n",
                    path, UGETW(dd.bcdUSB), UGETW(dd.bcdDevice), dd.bNumConfigurations);
            int active = -1;
            if (ioctl(fd, USB_GET_CONFIG, &active) == 0)
                fprintf(log, "USB_GET_CONFIG=%d\n", active);
            else fprintf(log, "USB_GET_CONFIG errno=%d\n", errno);
            for (unsigned c = 0; c < dd.bNumConfigurations; ++c) {
                uint8_t buf[4096] = {0};
                struct usb_gen_descriptor gd = {0};
                gd.ugd_data = buf; gd.ugd_maxlen = sizeof(buf); gd.ugd_config_index = c;
                fprintf(log, "\nCONFIG INDEX %u\n", c);
                if (ioctl(fd, USB_GET_FULL_DESC, &gd) != 0) {
                    fprintf(log, "USB_GET_FULL_DESC errno=%d\n", errno); continue;
                }
                size_t n = gd.ugd_actlen;
                if (!n && buf[0] >= 4) n = buf[2] | ((size_t)buf[3] << 8);
                if (n > sizeof(buf)) { fprintf(log, "ERROR: descriptor too large\n"); continue; }
                fprintf(log, "RAW_HEX ");
                for (size_t j = 0; j < n; ++j) fprintf(log, "%02x", buf[j]);
                fputc('\n', log);
                struct mojo_usb_summary summary;
                if (mojo_parse_config(buf, n, log, &summary) == 0 && mojo_matches_capture(&summary)) {
                    matched++;
                    fprintf(log, "PROFILE_MATCH: matches Mac capture; asynchronous OUT + explicit feedback\n");
                    /* Close the read handle before opening this same device for control GETs. */
                    close(fd);
                    current_rate = clock_valid = supports_48k = -1;
                    probe_controls(path, log, &summary, &current_rate, &clock_valid, &supports_48k);
                    if (!CAPTURE_ONLY) feedback = probe_feedback(path, log, &summary, current_rate, clock_valid, supports_48k);
                    /* A failed restore on a still-present device requires inspection, not repeated setup. */
                    if (feedback.restored == 0 && !feedback.device_gone) stop_requested = 1;
                    fd = open(path, O_RDONLY);
                    if (fd < 0) { fprintf(log, "Reopen descriptors errno=%d\n", errno); break; }
                }
            }
            if (fd >= 0) close(fd);
        }
        closedir(dev);
    }
        scan_round++;
        fflush(log);
        if (!RESIDENT_MODE || want_stop()) break;
        if (scan_round == 1 || feedback.device_gone) notify("Mojo2: waiting/retrying USB audio; HDMI unmuted");
        for (unsigned pause = 0; pause < 10 && !want_stop(); ++pause) usleep(100000);
    }
    if (CAPTURE_ONLY && matched) captured = probe_capture(log);
    fprintf(log, "\nRESULT scanned=%d opened=%d open_failed=%d device_desc_ok=%d mojo_found=%d profile_matches=%d\n",
            scanned, opened, denied, descriptors, found, matched);
    fprintf(log, "CLOCK_RESULT current_rate=%d clock_valid=%d supports_48000=%d (-1 means unknown)\n", current_rate, clock_valid, supports_48k);
    fprintf(log, "FEEDBACK_SUMMARY packets=%llu plausible=%llu restored=%d out_packets=%llu completions=%llu transfer_errors=%llu (-1 means no settings touched)\n",
            feedback.packets, feedback.plausible, feedback.restored, feedback.out_packets, feedback.completions, feedback.transfer_errors);
    int hdmi_restored = mojo_hdmi_close(&hdmi, log) == 0;
    fprintf(log, "Resident bridge stopped. HDMI cleanup_ok=%d; no generated tone. HDMI mute, device detach/reattach and power-button behavior need console validation.\n", hdmi_restored);
    int report_ok = fflush(log) == 0;
    if (fsync(fileno(log))) report_ok = 0;
    int copies = 0;
    if (report_ok) {
        for (int i = 0; i < 8; ++i) {
            char mount[64], path[128]; struct stat st;
            snprintf(mount, sizeof(mount), "/mnt/usb%d", i);
            if (stat(mount, &st) || !S_ISDIR(st.st_mode)) continue;
            snprintf(path, sizeof(path), "%s/mojo2-probe.txt", mount);
            if (copy_report(log, path) == 0) copies++;
        }
    }
    fclose(log);
    char msg[256];
    snprintf(msg, sizeof(msg), "Mojo2 " PROBE_VERSION " stopped: OUT=%llu fb=%llu plausible=%llu restored=%d USB logs=%d. %s",
             feedback.out_packets, feedback.packets, feedback.plausible, feedback.restored, copies, report_ok ? REPORT : "Log write failed");
    if (CAPTURE_ONLY)
        snprintf(msg, sizeof(msg), "Mojo2 " PROBE_VERSION " capture: opened=%d blocks=%u nonzero=%u errors=%u closed=%d. %s",
                 captured.opened, captured.blocks, captured.nonzero_blocks, captured.errors, captured.cleanup_ok,
                 report_ok ? REPORT : "Log write failed");
    if (!hdmi_restored) snprintf(msg, sizeof(msg), "Mojo2 stopped but HDMI unmute failed. Relaunch ELF for recovery; see /data/mojo2-probe.txt");
    mojo_control_end(&control);
    notify(msg);
    return report_ok && hdmi_restored ? 0 : 1;
}
