#ifndef CC1101_LINUX_H
#define CC1101_LINUX_H

#include <stddef.h>
#include <stdint.h>
#include "spi_linux.h"

typedef struct {
    const char *spi_device;
    int gdo0_gpio;
    spi_linux_t spi;
} cc1101_linux_t;

int cc1101_linux_open(cc1101_linux_t *radio, const char *spi_device, int gdo0_gpio);
void cc1101_linux_close(cc1101_linux_t *radio);
int cc1101_linux_reset(cc1101_linux_t *radio);
int cc1101_linux_apply_rf_preset(cc1101_linux_t *radio);
int cc1101_linux_read_part_info(cc1101_linux_t *radio, uint8_t *partnum, uint8_t *version);
int cc1101_linux_start_rx(cc1101_linux_t *radio);
int cc1101_linux_send_packet(cc1101_linux_t *radio, const uint8_t *data, size_t len);
int cc1101_linux_poll_packet(cc1101_linux_t *radio, uint8_t *data, size_t max_len,
                             uint8_t *rssi, uint8_t *lqi);

#endif
