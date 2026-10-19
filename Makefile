CC = clang
USB_PREFIX ?= /opt/homebrew
PS5_PAYLOAD_SDK ?= $(HOME)/.cache/ps5-mojo/sdk-v0.44
CFLAGS = -std=c11 -Wall -Wextra -Werror

.PHONY: all inspect ps5 test test-fs test-clock test-tone test-capture test-pcm test-control test-hdmi test-hdmi-ipc
all: build/mojo-descriptors build/mojo-audio-info

build:
	mkdir -p build

build/mojo-descriptors: tools/mojo_descriptors.c | build
	$(CC) $(CFLAGS) -I$(USB_PREFIX)/include $< -L$(USB_PREFIX)/lib -lusb-1.0 -o $@

build/mojo-audio-info: tools/mojo_audio_info.c | build
	$(CC) $(CFLAGS) $< -framework CoreAudio -framework CoreFoundation -o $@

inspect: all
	./build/mojo-descriptors
	./build/mojo-audio-info

ps5:
	$(MAKE) -C payloads/mojo2-probe

build/test-descriptor-parser: tests/descriptor_parser.c payloads/mojo2-probe/usb_descriptors.c payloads/mojo2-probe/usb_descriptors.h | build
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined tests/descriptor_parser.c payloads/mojo2-probe/usb_descriptors.c -o $@

test: build/test-descriptor-parser
	python3 tests/make_fixture.py
	./build/test-descriptor-parser build/mojo2-config1-reconstructed.bin

# Use the real SDK USB declarations with macOS libc; ioctl is fault-injected.
build/test-feedback-fs: tests/feedback_fs.c payloads/mojo2-probe/feedback_fs.c payloads/mojo2-probe/feedback_fs.h payloads/mojo2-probe/packet_clock.c payloads/mojo2-probe/packet_clock.h payloads/mojo2-probe/test_tone.c payloads/mojo2-probe/test_tone.h payloads/mojo2-probe/pcm_ring.c payloads/mojo2-probe/pcm_ring.h | build
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -idirafter "$(PS5_PAYLOAD_SDK)/target/include" -include stdint.h -D_SYS_STDINT_H_ '-D__packed=__attribute__((__packed__))' -Dioctl=mojo_test_ioctl tests/feedback_fs.c payloads/mojo2-probe/feedback_fs.c payloads/mojo2-probe/packet_clock.c payloads/mojo2-probe/test_tone.c payloads/mojo2-probe/pcm_ring.c -o $@

test-fs: build/test-feedback-fs
	./build/test-feedback-fs

build/test-packet-clock: tests/packet_clock.c payloads/mojo2-probe/packet_clock.c payloads/mojo2-probe/packet_clock.h | build
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined tests/packet_clock.c payloads/mojo2-probe/packet_clock.c -o $@

test-clock: build/test-packet-clock
	./build/test-packet-clock

build/test-tone: tests/test_tone.c payloads/mojo2-probe/test_tone.c payloads/mojo2-probe/test_tone.h | build
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined tests/test_tone.c payloads/mojo2-probe/test_tone.c -o $@

test-tone: build/test-tone
	./build/test-tone

build/test-capture: tests/capture.c payloads/mojo2-probe/capture.c payloads/mojo2-probe/capture.h | build
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -idirafter "$(PS5_PAYLOAD_SDK)/target/include" tests/capture.c payloads/mojo2-probe/capture.c -o $@

test-capture: build/test-capture
	./build/test-capture

build/test-pcm: tests/pcm_ring.c payloads/mojo2-probe/pcm_ring.c payloads/mojo2-probe/pcm_ring.h payloads/mojo2-probe/packet_clock.c payloads/mojo2-probe/packet_clock.h | build
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined tests/pcm_ring.c payloads/mojo2-probe/pcm_ring.c payloads/mojo2-probe/packet_clock.c -o $@

test-pcm: build/test-pcm
	./build/test-pcm

build/test-control: tests/control.c payloads/mojo2-probe/control.c payloads/mojo2-probe/control.h | build
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined tests/control.c payloads/mojo2-probe/control.c -o $@

test-control: build/test-control
	./build/test-control

build/hdmi-test.o: payloads/mojo2-probe/hdmi.c payloads/mojo2-probe/hdmi.h payloads/mojo2-probe/hdmi_ipc.h | build
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -Dopen=mojo_test_open -Dclose=mojo_test_close -Dwrite=mojo_test_write -Dfsync=mojo_test_fsync -c $< -o $@

build/test-hdmi: tests/hdmi.c build/hdmi-test.o payloads/mojo2-probe/hdmi.h | build
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined tests/hdmi.c build/hdmi-test.o -o $@

test-hdmi: build/test-hdmi
	./build/test-hdmi

build/test-hdmi-ipc: tests/hdmi_ipc.c payloads/mojo2-probe/hdmi_ipc.c payloads/mojo2-probe/hdmi_ipc.h | build
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -idirafter "$(PS5_PAYLOAD_SDK)/target/include" tests/hdmi_ipc.c payloads/mojo2-probe/hdmi_ipc.c -o $@

test-hdmi-ipc: build/test-hdmi-ipc
	./build/test-hdmi-ipc

.PHONY: check
check: test test-fs test-clock test-tone test-capture test-pcm test-control test-hdmi test-hdmi-ipc
