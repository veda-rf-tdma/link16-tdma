#ifndef SERIAL_WIN_H
#define SERIAL_WIN_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#endif

typedef struct {
#ifdef _WIN32
    HANDLE handle;
#else
    int handle;
#endif
} serial_win_t;

/**
 * @brief Open a Windows COM port for the STM32 USB CDC bridge.
 *
 * @param serial Serial handle storage.
 * @param port_name Port name such as COM3.
 * @param baud Baud rate, normally WIN_BRIDGE_BAUD.
 * @return 0 on success, negative error code on failure.
 */
int serial_win_open(serial_win_t *serial, const char *port_name, uint32_t baud);

/**
 * @brief Write raw bytes to the STM32 bridge serial port.
 *
 * @param serial Open serial port.
 * @param data Bytes to write.
 * @param len Number of bytes to write.
 * @return Number of bytes written, or negative error code.
 */
int serial_win_write(serial_win_t *serial, const uint8_t *data, size_t len);

/**
 * @brief Read raw bytes from the STM32 bridge serial port.
 *
 * @param serial Open serial port.
 * @param data Destination buffer.
 * @param max_len Maximum bytes to read.
 * @return Number of bytes read, 0 on timeout, or negative error code.
 */
int serial_win_read(serial_win_t *serial, uint8_t *data, size_t max_len);

/**
 * @brief Close the Windows COM port if it is open.
 *
 * @param serial Serial handle storage.
 */
void serial_win_close(serial_win_t *serial);

#endif
