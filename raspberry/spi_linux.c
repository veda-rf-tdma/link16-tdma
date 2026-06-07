#include "spi_linux.h"

#ifndef _WIN32
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

int spi_linux_open(spi_linux_t *spi, const char *device)
{
    spi->device = device;
#ifndef _WIN32
    spi->fd = open(device, O_RDWR);
    if (spi->fd < 0) {
        return -1;
    }

    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint32_t speed = 4000000;
    if (ioctl(spi->fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(spi->fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(spi->fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        spi_linux_close(spi);
        return -2;
    }
    return 0;
#else
    spi->fd = -1;
    return -1;
#endif
}

int spi_linux_transfer(spi_linux_t *spi, const uint8_t *tx, uint8_t *rx, size_t len)
{
#ifndef _WIN32
    struct spi_ioc_transfer tr = {0};
    tr.tx_buf = (unsigned long)tx;
    tr.rx_buf = (unsigned long)rx;
    tr.len = (uint32_t)len;
    tr.speed_hz = 4000000;
    tr.delay_usecs = 0;
    tr.bits_per_word = 8;
    tr.cs_change = 0;
    return ioctl(spi->fd, SPI_IOC_MESSAGE(1), &tr);
#else
    (void)spi;
    (void)tx;
    (void)rx;
    return (int)len;
#endif
}

void spi_linux_close(spi_linux_t *spi)
{
#ifndef _WIN32
    if (spi->fd >= 0) {
        close(spi->fd);
    }
#endif
    spi->fd = -1;
}
