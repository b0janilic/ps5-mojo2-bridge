PS5_PAYLOAD_SDK ?= $(HOME)/.cache/ps5-mojo/sdk-v0.44
LLVM_CONFIG ?= /opt/homebrew/opt/llvm@18/bin/llvm-config
export LLVM_CONFIG
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

ELF := ../../build/mojo2-probe.elf
CFLAGS := -std=c11 -Wall -Wextra -Werror -O2

.PHONY: all
all: $(ELF)

$(ELF): main.c usb_descriptors.c usb_descriptors.h feedback_fs.c feedback_fs.h packet_clock.c packet_clock.h test_tone.c test_tone.h capture.c capture.h pcm_ring.c pcm_ring.h control.c control.h hdmi.c hdmi.h hdmi_ipc.c hdmi_ipc.h
	mkdir -p ../../build
	"$(CC)" $(CFLAGS) -o $@ main.c usb_descriptors.c feedback_fs.c packet_clock.c test_tone.c capture.c pcm_ring.c control.c hdmi.c hdmi_ipc.c -lkernel_sys
