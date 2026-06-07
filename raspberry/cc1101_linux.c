#define _POSIX_C_SOURCE 199309L

#include "cc1101_linux.h"
#include "cc1101_regs.h"

#ifndef _WIN32
#include <time.h>
#endif

enum {
    CC1101_READ = 0x80u,
    CC1101_BURST = 0x40u,
    CC1101_STATUS_PARTNUM = 0x30u,
    CC1101_STATUS_VERSION = 0x31u,
    CC1101_STATUS_MARCSTATE = 0x35u,
    CC1101_STATUS_RXBYTES = 0x3bu,
    CC1101_MARCSTATE_RX = 0x0du
};

static void sleep_ms(long ms)
{
#ifndef _WIN32
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, 0);
#else
    (void)ms;
#endif
}

int cc1101_linux_open(cc1101_linux_t *radio, const char *spi_device, int gdo0_gpio)
{
    if (!radio || !spi_device) {
        return -1;
    }
    radio->spi_device = spi_device;
    radio->gdo0_gpio = gdo0_gpio;
    return spi_linux_open(&radio->spi, spi_device);
}

void cc1101_linux_close(cc1101_linux_t *radio)
{
    if (radio) {
        spi_linux_close(&radio->spi);
    }
}

static int cc1101_linux_strobe(cc1101_linux_t *radio, uint8_t strobe)
{
    uint8_t rx = 0;
    return spi_linux_transfer(&radio->spi, &strobe, &rx, 1);
}

static int cc1101_linux_read_status(cc1101_linux_t *radio, uint8_t addr, uint8_t *value)
{
    uint8_t tx[2] = {(uint8_t)(addr | CC1101_READ | CC1101_BURST), 0};
    uint8_t rx[2] = {0, 0};
    int rc = spi_linux_transfer(&radio->spi, tx, rx, sizeof(tx));
    if (rc < 0) {
        return rc;
    }
    *value = rx[1];
    return 0;
}

/**
 * @brief Write one CC1101 register through Linux spidev.
 *
 * @param radio Raspberry CC1101 device state.
 * @param addr CC1101 register address.
 * @param value Value to write.
 * @return spidev transfer result, negative on failure.
 */
static int cc1101_linux_write_reg(cc1101_linux_t *radio, uint8_t addr, uint8_t value)
{
    uint8_t tx[2] = {addr, value};
    uint8_t rx[2] = {0, 0};
    return spi_linux_transfer(&radio->spi, tx, rx, sizeof(tx));
}

/**
 * @brief Write multiple bytes to a CC1101 burst register window.
 *
 * @param radio Raspberry CC1101 device state.
 * @param addr Start register address, such as CC1101_PATABLE or CC1101_TXFIFO.
 * @param data Bytes to write.
 * @param len Number of bytes in @p data.
 * @return spidev transfer result, or -1 if the temporary buffer is too small.
 */
static int cc1101_linux_write_burst(cc1101_linux_t *radio, uint8_t addr,
                                    const uint8_t *data, size_t len)
{
    uint8_t tx[96];
    uint8_t rx[96];
    if (len + 1u > sizeof(tx)) {
        return -1;
    }
    tx[0] = (uint8_t)(addr | CC1101_BURST);
    for (size_t i = 0; i < len; i++) {
        tx[1 + i] = data[i];
    }
    return spi_linux_transfer(&radio->spi, tx, rx, len + 1u);
}

static int cc1101_linux_read_burst(cc1101_linux_t *radio, uint8_t addr,
                                   uint8_t *data, size_t len)
{
    uint8_t tx[96];
    uint8_t rx[96];
    if (!data || len + 1u > sizeof(tx)) {
        return -1;
    }
    tx[0] = (uint8_t)(addr | CC1101_READ | CC1101_BURST);
    for (size_t i = 0; i < len; i++) {
        tx[1 + i] = 0;
    }
    int rc = spi_linux_transfer(&radio->spi, tx, rx, len + 1u);
    if (rc < 0) {
        return rc;
    }
    for (size_t i = 0; i < len; i++) {
        data[i] = rx[1 + i];
    }
    return 0;
}

int cc1101_linux_reset(cc1101_linux_t *radio)
{
    int rc = cc1101_linux_strobe(radio, CC1101_SRES);
    sleep_ms(2);
    return rc;
}

/**
 * @brief Apply the shared RF Studio register preset to a Raspberry CC1101.
 *
 * @param radio Raspberry CC1101 device state.
 * @return 0 or positive transfer count on success path, negative on failure.
 */
int cc1101_linux_apply_rf_preset(cc1101_linux_t *radio)
{
    for (size_t i = 0; i < cc1101_rf_preset_count; i++) {
        int rc = cc1101_linux_write_reg(radio, cc1101_rf_preset[i].addr,
                                        cc1101_rf_preset[i].value);
        if (rc < 0) {
            return rc;
        }
    }
    return cc1101_linux_write_burst(radio, CC1101_PATABLE, cc1101_pa_table, 8);
}

int cc1101_linux_read_part_info(cc1101_linux_t *radio, uint8_t *partnum, uint8_t *version)
{
    int rc = cc1101_linux_read_status(radio, CC1101_STATUS_PARTNUM, partnum);
    if (rc < 0) {
        return rc;
    }
    return cc1101_linux_read_status(radio, CC1101_STATUS_VERSION, version);
}

int cc1101_linux_start_rx(cc1101_linux_t *radio)
{
    uint8_t marcstate = 0;
    cc1101_linux_strobe(radio, CC1101_SIDLE);
    cc1101_linux_strobe(radio, CC1101_SFRX);
    int rc = cc1101_linux_strobe(radio, CC1101_SRX);
    if (rc < 0) {
        return rc;
    }
    sleep_ms(2);
    rc = cc1101_linux_read_status(radio, CC1101_STATUS_MARCSTATE, &marcstate);
    if (rc < 0) {
        return rc;
    }
    return marcstate == CC1101_MARCSTATE_RX ? 0 : (int)marcstate;
}

int cc1101_linux_send_packet(cc1101_linux_t *radio, const uint8_t *data, size_t len)
{
    if (!data || len == 0 || len > 64) {
        return -1;
    }
    cc1101_linux_strobe(radio, CC1101_SIDLE);
    cc1101_linux_strobe(radio, CC1101_SFTX);
    int rc = cc1101_linux_write_burst(radio, CC1101_TXFIFO, data, len);
    if (rc < 0) {
        return rc;
    }
    rc = cc1101_linux_strobe(radio, CC1101_STX);
    if (rc < 0) {
        return rc;
    }
    sleep_ms(100);
    return 0;
}

int cc1101_linux_poll_packet(cc1101_linux_t *radio, uint8_t *data, size_t max_len,
                             uint8_t *rssi, uint8_t *lqi)
{
    uint8_t rxbytes = 0;
    int rc = cc1101_linux_read_status(radio, CC1101_STATUS_RXBYTES, &rxbytes);
    if (rc < 0) {
        return rc;
    }
    if (rxbytes & 0x80u) {
        cc1101_linux_strobe(radio, CC1101_SIDLE);
        cc1101_linux_strobe(radio, CC1101_SFRX);
        return -2;
    }
    rxbytes &= 0x7fu;
    if (rxbytes == 0) {
        return 0;
    }

    uint8_t length = 0;
    rc = cc1101_linux_read_burst(radio, CC1101_RXFIFO, &length, 1);
    if (rc < 0) {
        return rc;
    }
    if (length == 0 || (size_t)length + 2u > max_len || (size_t)length + 3u > 96u) {
        cc1101_linux_strobe(radio, CC1101_SIDLE);
        cc1101_linux_strobe(radio, CC1101_SFRX);
        return -3;
    }

    uint8_t rx_fifo[96];
    rc = cc1101_linux_read_burst(radio, CC1101_RXFIFO, rx_fifo, (size_t)length + 2u);
    if (rc < 0) {
        return rc;
    }
    for (uint8_t i = 0; i < length; i++) {
        data[i] = rx_fifo[i];
    }
    if (rssi) {
        *rssi = rx_fifo[length];
    }
    if (lqi) {
        *lqi = rx_fifo[length + 1u];
    }
    return (int)length;
}
