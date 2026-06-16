#ifndef USB_CDC_BRIDGE_H
#define USB_CDC_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

int usb_cdc_bridge_init(void);
int usb_cdc_bridge_parse(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len);
int usb_cdc_bridge_poll_radio(uint8_t *out, size_t out_len);

#endif
