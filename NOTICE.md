# Capture ABI and attribution

`payloads/mojo2-probe/capture.c` and `capture.h` adapt the audio capture ABI
documented and implemented by these GPL-3.0 projects:

- FGGstore/FGG-PlayPods, `src/capture.c` and `src/capture.h`:
  https://github.com/FGGstore/FGG-PlayPods
- hebiii994/ps5-solobridge, `solobridge/capture.c` and `capture.h`, reviewed at
  commit `71fc2af65e04b188ed364274f58351f8f413fc60`:
  https://github.com/hebiii994/ps5-solobridge/tree/71fc2af65e04b188ed364274f58351f8f413fc60

The module paths, function NIDs, OpenAudio parameter layout, source 2 / mode 0,
read flags and empty/overrun codes come from those implementations. This project
adds per-call durable diagnostics, instance state, bounded capture probing and
host fault injection. Short functional tests on firmware 5.50 are recorded
in `docs/validation/0.13.0.json`; broader compatibility is not established.
The combined payload is distributed under GPL version 3; see `COPYING`. Other
files retain the license indicated in their SPDX headers.

The public PS5 Payload SDK v0.44 provides the build environment and FreeBSD
USB declarations used by the payload and host tests:
https://github.com/ps5-payload-dev/sdk/releases/tag/v0.44
It is fetched separately and is not redistributed in this repository.

Modifications for this project were developed with AI assistance and tested
on the owner's console. Upstream capture research and implementation remain
credited to their respective authors.
