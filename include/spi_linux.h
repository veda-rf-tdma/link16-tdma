#ifndef SPI_LINUX_H
#define SPI_LINUX_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int fd;
    const char *device;
} spi_linux_t;

/**
 * @brief Open and configure a Linux spidev device for CC1101 access.
 *
 * @param spi SPI device state.
 * @param device Device path such as /dev/spidev0.0.
 * @return 0 on success, negative error code on failure.
 */
int spi_linux_open(spi_linux_t *spi, const char *device);

/**
 * @brief Run one full-duplex SPI transfer.
 *
 * @param spi Open SPI device.
 * @param tx Bytes to transmit.
 * @param rx Buffer for received bytes.
 * @param len Number of bytes to transfer.
 * @return ioctl result on Linux, or negative error code.
 */
int spi_linux_transfer(spi_linux_t *spi, const uint8_t *tx, uint8_t *rx, size_t len);

/**
 * @brief Close an open Linux spidev handle.
 *
 * @param spi SPI device state.
 */
void spi_linux_close(spi_linux_t *spi);

#endif
