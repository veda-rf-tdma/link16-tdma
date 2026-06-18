#ifndef USB_CDC_BRIDGE_H
#define USB_CDC_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    BRIDGE_CMD_SET_CONFIG = 1,
    BRIDGE_CMD_TX_PACKET = 2,
    BRIDGE_CMD_START_RX = 3,
    BRIDGE_CMD_GET_STATUS = 4,
    BRIDGE_EVT_RX_PACKET = 0x81,
    BRIDGE_EVT_TX_DONE = 0x82,
    BRIDGE_EVT_ERROR = 0x83
} bridge_msg_type_t;

int usb_cdc_bridge_init(void);
int usb_cdc_bridge_parse(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len);
int usb_cdc_bridge_poll_radio(uint8_t *out, size_t out_len);
void usb_cdc_bridge_tick(void);

#endif
