/* Read-only CoreAudio inspection. Does not set format, volume, output or start audio. */
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *property(AudioObjectID object, AudioObjectPropertySelector selector,
                      AudioObjectPropertyScope scope, UInt32 *bytes) {
    AudioObjectPropertyAddress a = {selector, scope, kAudioObjectPropertyElementMain};
    if (AudioObjectGetPropertyDataSize(object, &a, 0, NULL, bytes) != noErr) return NULL;
    void *data = calloc(1, *bytes ? *bytes : 1);
    if (!data) return NULL;
    if (AudioObjectGetPropertyData(object, &a, 0, NULL, bytes, data) != noErr) {
        free(data); return NULL;
    }
    return data;
}

int main(void) {
    UInt32 bytes = 0;
    AudioDeviceID *devices = property(kAudioObjectSystemObject, kAudioHardwarePropertyDevices,
                                     kAudioObjectPropertyScopeGlobal, &bytes);
    if (!devices) return 1;
    int found = 0, failed = 0;
    for (unsigned i = 0; i < bytes / sizeof(*devices); ++i) {
        UInt32 size = 0;
        CFStringRef *ref = property(devices[i], kAudioObjectPropertyName,
                                   kAudioObjectPropertyScopeGlobal, &size);
        if (!ref) continue;
        char name[256] = {0};
        CFStringGetCString(*ref, name, sizeof(name), kCFStringEncodingUTF8);
        CFRelease(*ref); free(ref);
        if (strcmp(name, "Mojo2")) continue;
        if (found++) { failed = 1; break; }
        AudioValueRange *rates = property(devices[i], kAudioDevicePropertyAvailableNominalSampleRates,
                                         kAudioObjectPropertyScopeGlobal, &size);
        if (!rates) { failed = 1; break; }
        printf("{\"name\":\"Mojo2\",\"available_sample_rates\":[");
        for (unsigned j = 0; j < size / sizeof(*rates); ++j) {
            if (j) putchar(',');
            printf("{\"min\":%.0f,\"max\":%.0f}", rates[j].mMinimum, rates[j].mMaximum);
        }
        free(rates);
        AudioStreamID *streams = property(devices[i], kAudioDevicePropertyStreams,
                                         kAudioObjectPropertyScopeOutput, &size);
        if (!streams) { failed = 1; break; }
        unsigned count = size / sizeof(*streams);
        printf("],\"output_streams\":[");
        for (unsigned j = 0; j < count; ++j) {
            if (j) putchar(',');
            AudioStreamRangedDescription *formats = property(streams[j], kAudioStreamPropertyAvailablePhysicalFormats,
                                                              kAudioObjectPropertyScopeGlobal, &size);
            if (!formats) { failed = 1; break; }
            printf("{\"physical_formats\":[");
            for (unsigned k = 0; k < size / sizeof(*formats); ++k) {
                if (k) putchar(',');
                const AudioStreamBasicDescription *f = &formats[k].mFormat;
                printf("{\"min_rate\":%.0f,\"max_rate\":%.0f,\"format_id\":%u,\"flags\":%u,\"channels\":%u,\"bits_per_channel\":%u,\"bytes_per_frame\":%u}",
                       formats[k].mSampleRateRange.mMinimum, formats[k].mSampleRateRange.mMaximum,
                       f->mFormatID, f->mFormatFlags, f->mChannelsPerFrame, f->mBitsPerChannel, f->mBytesPerFrame);
            }
            free(formats);
            printf("]}");
        }
        free(streams);
        printf("]}\n");
    }
    free(devices);
    if (!found) fprintf(stderr, "Mojo2 not found in CoreAudio.\n");
    return (!found || failed) ? 1 : 0;
}
