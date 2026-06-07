#ifndef CC1101_REGS_H
#define CC1101_REGS_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t addr;
    uint8_t value;
} cc1101_reg_value_t;

extern const cc1101_reg_value_t cc1101_rf_preset[];
extern const size_t cc1101_rf_preset_count;
extern const uint8_t cc1101_pa_table[8];

enum {
    CC1101_IOCFG2   = 0x00,
    CC1101_IOCFG1   = 0x01,
    CC1101_IOCFG0   = 0x02,
    CC1101_FIFOTHR  = 0x03,
    CC1101_SYNC1    = 0x04,
    CC1101_SYNC0    = 0x05,
    CC1101_PKTLEN   = 0x06,
    CC1101_PKTCTRL1 = 0x07,
    CC1101_PKTCTRL0 = 0x08,
    CC1101_ADDR     = 0x09,
    CC1101_CHANNR   = 0x0a,
    CC1101_FSCTRL1  = 0x0b,
    CC1101_FSCTRL0  = 0x0c,
    CC1101_FREQ2    = 0x0d,
    CC1101_FREQ1    = 0x0e,
    CC1101_FREQ0    = 0x0f,
    CC1101_MDMCFG4  = 0x10,
    CC1101_MDMCFG3  = 0x11,
    CC1101_MDMCFG2  = 0x12,
    CC1101_MDMCFG1  = 0x13,
    CC1101_MDMCFG0  = 0x14,
    CC1101_DEVIATN  = 0x15,
    CC1101_MCSM2    = 0x16,
    CC1101_MCSM1    = 0x17,
    CC1101_MCSM0    = 0x18,
    CC1101_FOCCFG   = 0x19,
    CC1101_BSCFG    = 0x1a,
    CC1101_AGCCTRL2 = 0x1b,
    CC1101_AGCCTRL1 = 0x1c,
    CC1101_AGCCTRL0 = 0x1d,
    CC1101_FREND1   = 0x21,
    CC1101_FREND0   = 0x22,
    CC1101_FSCAL3   = 0x23,
    CC1101_FSCAL2   = 0x24,
    CC1101_FSCAL1   = 0x25,
    CC1101_FSCAL0   = 0x26,
    CC1101_TEST2    = 0x2c,
    CC1101_TEST1    = 0x2d,
    CC1101_TEST0    = 0x2e,
    CC1101_PATABLE  = 0x3e,
    CC1101_TXFIFO   = 0x3f,
    CC1101_RXFIFO   = 0x3f
};

enum {
    CC1101_SRES  = 0x30,
    CC1101_SFSTXON = 0x31,
    CC1101_SXOFF = 0x32,
    CC1101_SCAL  = 0x33,
    CC1101_SRX   = 0x34,
    CC1101_STX   = 0x35,
    CC1101_SIDLE = 0x36,
    CC1101_SFRX  = 0x3a,
    CC1101_SFTX  = 0x3b
};

#endif
