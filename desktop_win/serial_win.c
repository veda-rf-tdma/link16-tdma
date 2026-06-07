#include "serial_win.h"
#include <stdio.h>

int serial_win_open(serial_win_t *serial, const char *port_name, uint32_t baud)
{
#ifdef _WIN32
    char path[64];
    snprintf(path, sizeof(path), "\\\\.\\%s", port_name);
    serial->handle = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (serial->handle == INVALID_HANDLE_VALUE) {
        return -1;
    }

    DCB dcb;
    SecureZeroMemory(&dcb, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(serial->handle, &dcb)) {
        serial_win_close(serial);
        return -2;
    }

    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    if (!SetCommState(serial->handle, &dcb)) {
        serial_win_close(serial);
        return -3;
    }

    COMMTIMEOUTS timeouts;
    SecureZeroMemory(&timeouts, sizeof(timeouts));
    timeouts.ReadIntervalTimeout = 20;
    timeouts.ReadTotalTimeoutConstant = 20;
    timeouts.ReadTotalTimeoutMultiplier = 2;
    timeouts.WriteTotalTimeoutConstant = 100;
    timeouts.WriteTotalTimeoutMultiplier = 2;
    SetCommTimeouts(serial->handle, &timeouts);
    return 0;
#else
    (void)port_name;
    (void)baud;
    serial->handle = -1;
    return -1;
#endif
}

int serial_win_write(serial_win_t *serial, const uint8_t *data, size_t len)
{
#ifdef _WIN32
    DWORD written = 0;
    if (!WriteFile(serial->handle, data, (DWORD)len, &written, NULL)) {
        return -1;
    }
    return (int)written;
#else
    (void)serial;
    (void)data;
    (void)len;
    return -1;
#endif
}

int serial_win_read(serial_win_t *serial, uint8_t *data, size_t max_len)
{
#ifdef _WIN32
    DWORD read_count = 0;
    if (!ReadFile(serial->handle, data, (DWORD)max_len, &read_count, NULL)) {
        return -1;
    }
    return (int)read_count;
#else
    (void)serial;
    (void)data;
    (void)max_len;
    return 0;
#endif
}

void serial_win_close(serial_win_t *serial)
{
#ifdef _WIN32
    if (serial->handle && serial->handle != INVALID_HANDLE_VALUE) {
        CloseHandle(serial->handle);
        serial->handle = INVALID_HANDLE_VALUE;
    }
#else
    serial->handle = -1;
#endif
}
