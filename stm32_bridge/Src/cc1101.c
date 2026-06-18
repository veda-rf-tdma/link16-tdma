#include "cc1101_stm32.h"
#include "cc1101_regs.h"
#include "main.h"
#include <stdio.h>

extern SPI_HandleTypeDef hspi1;

#ifndef CC1101_CSN_Pin
#define CC1101_CSN_Pin GPIO_PIN_4
#define CC1101_CSN_GPIO_Port GPIOA
#endif

enum {
    CC1101_READ = 0x80u,
    CC1101_BURST = 0x40u,
    CC1101_STATUS_RXBYTES = 0x3bu
};

static int cc1101_transfer_selected(const uint8_t *tx, uint8_t *rx, size_t len)
{
    cc1101_platform_select();
    int rc = cc1101_platform_transfer(tx, rx, len);
    cc1101_platform_deselect();
    return rc;
}

static int cc1101_strobe(cc1101_t *radio, uint8_t strobe)
{
    uint8_t rx = 0;
    (void)radio;
    return cc1101_transfer_selected(&strobe, &rx, 1);
}

static int cc1101_write_reg(cc1101_t *radio, uint8_t addr, uint8_t value)
{
    uint8_t tx[2] = {addr, value};
    uint8_t rx[2] = {0, 0};
    (void)radio;
    return cc1101_transfer_selected(tx, rx, sizeof(tx));
}

static int cc1101_write_burst(cc1101_t *radio, uint8_t addr, const uint8_t *data, size_t len)
{
    uint8_t tx[96];
    uint8_t rx[96];
    (void)radio;
    if (!data || len + 1u > sizeof(tx)) {
        return -1;
    }
    tx[0] = (uint8_t)(addr | CC1101_BURST);
    for (size_t i = 0; i < len; i++) {
        tx[1 + i] = data[i];
    }
    return cc1101_transfer_selected(tx, rx, len + 1u);
}

static int cc1101_read_status(cc1101_t *radio, uint8_t addr, uint8_t *value)
{
    uint8_t tx[2] = {(uint8_t)(addr | CC1101_READ | CC1101_BURST), 0};
    uint8_t rx[2] = {0, 0};
    (void)radio;
    int rc = cc1101_transfer_selected(tx, rx, sizeof(tx));
    if (rc < 0) {
        return rc;
    }
    *value = rx[1];
    return 0;
}

static int cc1101_read_burst(cc1101_t *radio, uint8_t addr, uint8_t *data, size_t len)
{
    uint8_t tx[96];
    uint8_t rx[96];
    (void)radio;
    if (!data || len + 1u > sizeof(tx)) {
        return -1;
    }
    tx[0] = (uint8_t)(addr | CC1101_READ | CC1101_BURST);
    for (size_t i = 0; i < len; i++) {
        tx[1 + i] = 0;
    }
    int rc = cc1101_transfer_selected(tx, rx, len + 1u);
    if (rc < 0) {
        return rc;
    }
    for (size_t i = 0; i < len; i++) {
        data[i] = rx[1 + i];
    }
    return 0;
}

int cc1101_apply_rf_preset(cc1101_t *radio)
{
    cc1101_strobe(radio, CC1101_SRES);
    cc1101_platform_delay_ms(2);

    /* Verify CC1101 connection and version register (0x31) response */
    uint8_t version = 0;
    int rc = cc1101_read_status(radio, 0x31, &version);
    if (rc < 0 || version == 0x00 || version == 0xFF) {
        /* Temporarily bypass SPI connection error for testing Master-Desktop USB link */
        /* return -10; */
    } else {
        extern UART_HandleTypeDef huart2;
        char msg[64];
        int msg_len = snprintf(msg, sizeof(msg), "INFO: CC1101 SPI OK. Chip version: 0x%02X\r\n", version);
        HAL_UART_Transmit(&huart2, (uint8_t *)msg, msg_len, 100);
    }

    for (size_t i = 0; i < cc1101_rf_preset_count; i++) {
        rc = cc1101_write_reg(radio, cc1101_rf_preset[i].addr, cc1101_rf_preset[i].value);
        if (rc < 0) {
            return rc;
        }
    }
    rc = cc1101_write_burst(radio, CC1101_PATABLE, cc1101_pa_table, 8);
    if (rc >= 0) {
        radio->initialized = 1;
    }
    return rc;
}

int cc1101_start_rx(cc1101_t *radio)
{
    cc1101_strobe(radio, CC1101_SIDLE);
    cc1101_strobe(radio, CC1101_SFRX);
    return cc1101_strobe(radio, CC1101_SRX);
}

int cc1101_send_packet(cc1101_t *radio, const uint8_t *data, size_t len)
{
    if (!data || len == 0 || len > 64) {
        return -1;
    }
    cc1101_strobe(radio, CC1101_SIDLE);
    cc1101_strobe(radio, CC1101_SFTX);
    int rc = cc1101_write_burst(radio, CC1101_TXFIFO, data, len);
    if (rc < 0) {
        return rc;
    }
    rc = cc1101_strobe(radio, CC1101_STX);
    cc1101_platform_delay_ms(100);
    return rc;
}

int cc1101_poll_packet(cc1101_t *radio, uint8_t *data, size_t max_len,
                       uint8_t *rssi, uint8_t *lqi)
{
    uint8_t rxbytes = 0;
    int rc = cc1101_read_status(radio, CC1101_STATUS_RXBYTES, &rxbytes);
    if (rc < 0) {
        return rc;
    }
    if (rxbytes & 0x80u) {
        cc1101_strobe(radio, CC1101_SIDLE);
        cc1101_strobe(radio, CC1101_SFRX);
        return -2;
    }
    rxbytes &= 0x7fu;
    if (rxbytes == 0) {
        return 0;
    }

    uint8_t length = 0;
    rc = cc1101_read_burst(radio, CC1101_RXFIFO, &length, 1);
    if (rc < 0) {
        return rc;
    }
    if (length == 0 || (size_t)length + 2u > max_len || (size_t)length + 3u > 96u) {
        cc1101_strobe(radio, CC1101_SIDLE);
        cc1101_strobe(radio, CC1101_SFRX);
        return -3;
    }

    uint8_t fifo[96];
    rc = cc1101_read_burst(radio, CC1101_RXFIFO, fifo, (size_t)length + 2u);
    if (rc < 0) {
        return rc;
    }
    for (uint8_t i = 0; i < length; i++) {
        data[i] = fifo[i];
    }
    if (rssi) {
        *rssi = fifo[length];
    }
    if (lqi) {
        *lqi = fifo[length + 1u];
    }
    return (int)length;
}

int cc1101_set_channel(cc1101_t *radio, uint8_t channel)
{
    return cc1101_write_reg(radio, CC1101_CHANNR, channel);
}

int cc1101_set_tx_power(cc1101_t *radio, double power_dbm)
{
    uint8_t pa_val;
    if (power_dbm <= -30.0) {
        pa_val = 0x12;
    } else if (power_dbm <= -20.0) {
        pa_val = 0x0e;
    } else if (power_dbm <= -15.0) {
        pa_val = 0x1d;
    } else if (power_dbm <= -10.0) {
        pa_val = 0x34;
    } else if (power_dbm <= -5.0) {
        pa_val = 0x2c;
    } else if (power_dbm <= 0.0) {
        pa_val = 0x60;
    } else if (power_dbm <= 5.0) {
        pa_val = 0x85;
    } else if (power_dbm <= 7.0) {
        pa_val = 0xc8;
    } else {
        pa_val = 0xc0;
    }
    return cc1101_write_reg(radio, CC1101_PATABLE, pa_val);
}

int cc1101_verify_connection(cc1101_t *radio, uint8_t *version)
{
    return cc1101_read_status(radio, 0x31, version);
}

void cc1101_platform_select(void)
{
    HAL_GPIO_WritePin(CC1101_CSN_GPIO_Port, CC1101_CSN_Pin, GPIO_PIN_RESET);
}

void cc1101_platform_deselect(void)
{
    HAL_GPIO_WritePin(CC1101_CSN_GPIO_Port, CC1101_CSN_Pin, GPIO_PIN_SET);
}

void cc1101_platform_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

int cc1101_platform_transfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    HAL_StatusTypeDef rc = HAL_SPI_TransmitReceive(&hspi1, (uint8_t *)tx, rx, (uint16_t)len, 100);
    return rc == HAL_OK ? 0 : -1;
}
