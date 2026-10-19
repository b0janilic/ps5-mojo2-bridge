# PS5 Mojo 2 Bridge

Game and system audio from a jailbroken PS5 to a **Chord Mojo 2 over USB**.

**Experimental v0.13.0 checkpoint. Tested on one PS5 running firmware 5.50.**
This is a resident audio bridge: it captures the console mix and sends it to
Mojo 2 through a device-specific USB Audio Class 2 transport.

## What works

- Game audio and menu sounds, 48 kHz stereo.
- Launch the ELF once to start; launch it again to request a clean stop.
- HDMI mutes after Mojo playback starts and returns when the bridge stops.
- Powering Mojo off returns audio to HDMI; powering it on resumes USB audio
  and mutes HDMI again in the tested setup.

**Mojo does not appear in the PS5 output-device menu.** Native system output
registration is unfinished research and is outside this release. This is not
a generic UAC2 driver or support for arbitrary DACs.

## Download and use

Get `mojo2-probe.elf` from [Releases](https://github.com/b0janilic/ps5-mojo2-bridge/releases).
The existing filename is retained for payload-host compatibility.

1. On the tested firmware 5.50 setup, complete your usual jailbreak and start
   an ELF loader. This project does not include a jailbreak or loader.
2. Select HDMI as the PS5 audio output. Connect Mojo 2 by USB and connect your
   headphones or amplifier to Mojo. Start with a low physical volume.
3. Copy the ELF to your payload host's folder and launch it manually.
4. To stop, launch the **same ELF** again. Wait for the stopped notification
   before launching it a third time to restart.

The initial tests use a fixed digital gain of **0.25 (about -12 dB)**. Output is
48 kHz stereo signed 32-bit PCM. It is not a bit-perfect or high-resolution
passthrough mode. Adjust listening volume on Mojo.

Payload folder locations depend on your host/loader. No autoload configuration
is installed. Network access is not required for normal playback.

## Test status and limits

The owner confirmed playback, start/stop, HDMI mute/unmute and a Mojo power
cycle on 2026-10-19. Hardware validation is **short functional testing**, not a
long-run stability certification. See [the validation record](docs/validation/0.13.0.json).

- Tested hardware: original PS5, firmware 5.50, Mojo 2 (`245f:0815`).
- Automatic HDMI control is gated to firmware 5.50. Other firmware is untested;
  the USB bridge may still attempt playback with HDMI control disabled.
- Long sessions, clock drift, rest mode/resume and other DACs/firmware have not
  been validated. Ring backlog correction can discard audio frames; there is
  no fine adaptive resampler yet. End-to-end latency has not been measured.
- HDMI cleanup requests **unmuted**, rather than restoring an unknown prior
  mute state. A process crash can leave HDMI muted until relaunch or reboot.
  A persistent recovery marker is used on the next launch; it is not an
  independent watchdog.

## Build from source

The build was verified on macOS with Xcode Command Line Tools, Python 3 and
LLVM 18.1.8. It uses the public [PS5 Payload SDK v0.44](https://github.com/ps5-payload-dev/sdk/releases/tag/v0.44).
The setup script checks the pinned SDK archive SHA-256 before extraction.
Sony libraries, firmware files and SDK archives are not included in this repo.

```sh
brew install llvm@18
python3 tools/setup_ps5_sdk.py
make ps5 LLVM_CONFIG="$(brew --prefix llvm@18)/bin/llvm-config"
```

Output: `build/mojo2-probe.elf`.
If the pinned SDK is already installed, the setup script exits without changing
it; continue with `make ps5`. You can override `PS5_PAYLOAD_SDK` and
`LLVM_CONFIG` when using another supported toolchain location. Other build
hosts are not yet verified.

Run the host tests and package a local distribution:

```sh
make check
python3 tools/package_probe.py
```

Tests cover descriptor parsing and malformed-input mutations, USB transfer
lifecycle failures, feedback packet accounting, capture setup/cleanup, PCM
buffering, process locks/stop requests, and HDMI IPC/rollback failures. These
use mocks and sanitizers; they do not replace hardware testing. Packaging
marks a build console-tested only when its ELF hash matches the recorded
validated binary. Release assets retain the exact tested ELF, whose hash is
`10fc53397816dbc230e641be39c3894d43bf76b4e73c436f07c0b29f367af6a7`.

## Optional FTP helper

FTP is only a development convenience. Start your console's FTP server and
supply your own address and payload-host directory:

```sh
python3 tools/ps5_ftp.py deploy --host YOUR_PS5_IP --port 2121 --remote-dir /YOUR/PAYLOAD/FOLDER
python3 tools/ps5_ftp.py get-log --host YOUR_PS5_IP --port 2121
python3 tools/ps5_ftp.py stop --host YOUR_PS5_IP --port 2121
```

`deploy` verifies the local ELF/manifest, backs up a prior remote ELF locally,
and verifies the upload by reading it back before and after renaming. It never
executes the payload. `stop` creates the bridge's stop marker.
You can store `host`, `port` and `remote_dir` in a local `.ps5-local.json`, which
is ignored by Git and excluded from source packages. The helper uses anonymous
FTP; it is not an encrypted transport.

The console report is `/data/mojo2-probe.txt`. Most streaming statistics are
written when a session ends, so an unchanged report during playback is expected.
The bridge uses `/data/mojo2-bridge.lock`, `/data/mojo2-stop`, and
`/data/mojo2-hdmi-owned`. On clean stop it also attempts to copy its report to
mounted USB volumes. See [the architecture notes](docs/ARCHITECTURE.md).

## Credits and license

The capture ABI and its implementation are adapted from
[FGG-PlayPods](https://github.com/FGGstore/FGG-PlayPods) and
[ps5-solobridge](https://github.com/hebiii994/ps5-solobridge).
Their work made capturing the game and menu mix possible. This project adds
Mojo-specific USB feedback transport, lifecycle/reconnect handling and the
firmware-gated HDMI control path. See [NOTICE.md](NOTICE.md).

The combined payload is distributed under **GNU GPL version 3**; see [COPYING](COPYING).
Individual SPDX notices are retained. Development used AI assistance for code,
analysis, documentation and host tests; the owner performed the console tests.

This is an independent homebrew project, with no affiliation or endorsement
from Sony, Chord Electronics, Focusrite or the upstream projects.
