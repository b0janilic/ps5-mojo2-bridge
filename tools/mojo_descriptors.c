/* Read cached USB descriptors only: no open, claim, detach, configuration or audio writes. */
#include <libusb-1.0/libusb.h>
#include <stdio.h>

static void hex(const unsigned char *p, int n) {
    putchar('"');
    for (int i = 0; i < n; ++i) printf("%02x", p[i]);
    putchar('"');
}

int main(void) {
    libusb_context *ctx = NULL;
    libusb_device **list = NULL;
    int rc = libusb_init(&ctx);
    if (rc) { fprintf(stderr, "init: %s\n", libusb_error_name(rc)); return 1; }
    ssize_t n = libusb_get_device_list(ctx, &list);
    if (n < 0) { fprintf(stderr, "list: %s\n", libusb_error_name((int)n)); libusb_exit(ctx); return 1; }
    int found = 0, failed = 0;
    for (ssize_t i = 0; i < n; ++i) {
        struct libusb_device_descriptor d;
        if (libusb_get_device_descriptor(list[i], &d) || d.idVendor != 0x245f || d.idProduct != 0x0815) continue;
        if (found++) { fprintf(stderr, "Multiple Mojo 2 devices; disconnect extras.\n"); failed = 1; break; }
        printf("{\"vendor_id\":%u,\"product_id\":%u,\"bcd_usb\":%u,\"bcd_device\":%u,\"speed_enum\":%d,\"configurations\":[", d.idVendor, d.idProduct, d.bcdUSB, d.bcdDevice, libusb_get_device_speed(list[i]));
        for (int c = 0; c < d.bNumConfigurations; ++c) {
            struct libusb_config_descriptor *cfg = NULL;
            rc = libusb_get_config_descriptor(list[i], c, &cfg);
            if (rc) { fprintf(stderr, "config %d: %s\n", c, libusb_error_name(rc)); failed = 1; break; }
            if (c) putchar(',');
            printf("{\"index\":%d,\"value\":%u,\"total_length\":%u,\"attributes\":%u,\"max_power\":%u,\"extra_hex\":", c, cfg->bConfigurationValue, cfg->wTotalLength, cfg->bmAttributes, cfg->MaxPower);
            hex(cfg->extra, cfg->extra_length);
            printf(",\"interfaces\":[");
            int first = 1;
            for (int j = 0; j < cfg->bNumInterfaces; ++j) {
                const struct libusb_interface *iface = &cfg->interface[j];
                for (int k = 0; k < iface->num_altsetting; ++k) {
                    const struct libusb_interface_descriptor *a = &iface->altsetting[k];
                    if (!first) putchar(','); first = 0;
                    printf("{\"number\":%u,\"alt\":%u,\"class\":%u,\"subclass\":%u,\"protocol\":%u,\"extra_hex\":", a->bInterfaceNumber, a->bAlternateSetting, a->bInterfaceClass, a->bInterfaceSubClass, a->bInterfaceProtocol);
                    hex(a->extra, a->extra_length);
                    printf(",\"endpoints\":[");
                    for (int e = 0; e < a->bNumEndpoints; ++e) {
                        const struct libusb_endpoint_descriptor *ep = &a->endpoint[e];
                        if (e) putchar(',');
                        printf("{\"address\":%u,\"attributes\":%u,\"max_packet_size\":%u,\"interval\":%u,\"refresh\":%u,\"sync_address\":%u,\"extra_hex\":", ep->bEndpointAddress, ep->bmAttributes, ep->wMaxPacketSize, ep->bInterval, ep->bRefresh, ep->bSynchAddress);
                        hex(ep->extra, ep->extra_length);
                        putchar('}');
                    }
                    printf("]}");
                }
            }
            printf("]}");
            libusb_free_config_descriptor(cfg);
        }
        printf("]}\n");
    }
    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    if (!found) { fprintf(stderr, "Mojo 2 (245f:0815) not found.\n"); return 2; }
    return failed ? 1 : 0;
}
