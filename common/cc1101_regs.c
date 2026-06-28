#include "cc1101_regs.h"

/* RF Studio 기준 preset에서 중심 주파수를 433.92 MHz PoC 대역으로 조정.
   FREQ2/1/0 = 0x10/0xb0/0x71 gives 433.919830 MHz with a 26 MHz crystal.
   2-FSK, 37.9868 kBaud, CRC on,
   variable packet length, address check on, TX power 5 dBm. */
const cc1101_reg_value_t cc1101_rf_preset[] = {
    {CC1101_IOCFG0,   0x06},
    {CC1101_FIFOTHR,  0x07},
    {CC1101_SYNC1,    0xd3},
    {CC1101_SYNC0,    0x91},
    {CC1101_PKTLEN,   0xff},
    {CC1101_PKTCTRL1, 0x05},
    {CC1101_PKTCTRL0, 0x05},
    {CC1101_ADDR,     0x00},
    {CC1101_CHANNR,   0x05},
    {CC1101_FSCTRL1,  0x06},
    {CC1101_FSCTRL0,  0x00},
    {CC1101_FREQ2,    0x10},
    {CC1101_FREQ1,    0xb0},
    {CC1101_FREQ0,    0x71},
    {CC1101_MDMCFG4,  0x8a},
    {CC1101_MDMCFG3,  0x7f},
    {CC1101_MDMCFG2,  0x07},
    {CC1101_MDMCFG1,  0x00},
    {CC1101_MDMCFG0,  0xf8},
    {CC1101_DEVIATN,  0x47},
    {CC1101_MCSM2,    0x07},
    {CC1101_MCSM1,    0x0f},
    {CC1101_MCSM0,    0x18},
    {CC1101_FOCCFG,   0x16},
    {CC1101_BSCFG,    0x6c},
    {CC1101_AGCCTRL2, 0x03},
    {CC1101_AGCCTRL1, 0x40},
    {CC1101_AGCCTRL0, 0x91},
    {CC1101_FREND1,   0x56},
    {CC1101_FREND0,   0x10},
    {CC1101_FSCAL3,   0xe9},
    {CC1101_FSCAL2,   0x2a},
    {CC1101_FSCAL1,   0x00},
    {CC1101_FSCAL0,   0x1f},
    {CC1101_TEST2,    0x81},
    {CC1101_TEST1,    0x35},
    {CC1101_TEST0,    0x09},
};

const size_t cc1101_rf_preset_count =
    sizeof(cc1101_rf_preset) / sizeof(cc1101_rf_preset[0]);

const uint8_t cc1101_pa_table[8] = {0x85, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
