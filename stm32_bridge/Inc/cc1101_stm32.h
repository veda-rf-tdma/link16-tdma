#ifndef CC1101_STM32_H
#define CC1101_STM32_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t initialized;
} cc1101_t;

void cc1101_platform_select(void);
void cc1101_platform_deselect(void);
void cc1101_platform_delay_ms(uint32_t ms);
int cc1101_platform_transfer(const uint8_t *tx, uint8_t *rx, size_t len);

int cc1101_apply_rf_preset(cc1101_t *radio);
int cc1101_start_rx(cc1101_t *radio);
int cc1101_send_packet(cc1101_t *radio, const uint8_t *data, size_t len);
int cc1101_poll_packet(cc1101_t *radio, uint8_t *data, size_t max_len,
                       uint8_t *rssi, uint8_t *lqi);
int cc1101_set_channel(cc1101_t *radio, uint8_t channel);
int cc1101_set_tx_power(cc1101_t *radio, double power_dbm);

#endif
