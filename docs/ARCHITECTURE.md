# Architecture

`Avcap2 source 2 / mode 0 -> float32 stereo ring -> S32_LE -> USB_FS -> Mojo 2`

The capture side uses the Remote Play mix at 48 kHz, including game and menu
sounds. Upstream capture ABI/NIDs are credited in NOTICE.md. Capture reads are
nonblocking. No raw capture audio is saved or sent over the network.

Mojo transport uses one USB_FS context: asynchronous OUT endpoint 0x05, explicit
feedback IN 0x81, and two separate 64-packet OUT buffers. Feedback is Q16.16
samples per microframe. Fractional accounting schedules bounded 5/6/7-frame
stereo packets; startup uses nominal pacing until plausible feedback arrives.
The clock source and streaming alternate are configured for 48 kHz and restored
on cleanup when the device is still present.

The ring holds 8192 frames, prebuffers 1536, and trims excessive backlog to
1536 frames. It sanitizes nonfinite input, applies gain 0.25, clips, and ramps
playback/rebuffer transitions. Underruns produce silence and re-prime the ring.
Capture and USB clocks are independent: feedback-directed USB scheduling is
not a source-to-sink adaptive resampler.

A process lock prevents concurrent bridge instances. A second launch requests
stop through a marker. The resident instance retries detached devices, resets
capture state between sessions, and stops on uncertain cleanup of a device
that is still present. Stream counters are logged after session exit to avoid
normal report writes in the transfer loop.

On firmware 5.50, an exported AudioOut IPC client sends HDMI-only mute/unmute
commands to the existing audio service. It does not open a second hardware
audio descriptor or patch kernel code. Mute waits for a successful audio stream;
cleanup requests unmute. A persisted ownership marker supports recovery on the
next launch. Native PS5 output-device registration is not implemented.

The tested payload source is kept unchanged for this checkpoint. Legacy
inspection/tone/capture-only code and its tests remain in the source; current
compile-time mode selects the resident PCM bridge. Experimental native-output
diagnostics and private console captures are not part of this release.
