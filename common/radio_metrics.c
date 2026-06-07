#include "radio_metrics.h"
#include <math.h>
#include "config.h"

double cc1101_rssi_dbm(uint8_t raw_rssi)
{
    int signed_rssi = raw_rssi >= 128u ? (int)raw_rssi - 256 : (int)raw_rssi;
    return ((double)signed_rssi / 2.0) - CC1101_RSSI_OFFSET_DB;
}

double radio_estimate_distance_m(double rssi_dbm)
{
    double path_loss_db = CC1101_TX_POWER_DBM - rssi_dbm;
    double denominator = 10.0 * RADIO_PATH_LOSS_EXPONENT;
    double reference_loss_db = 32.44 + (20.0 * log10(CC1101_CARRIER_MHZ)) - 60.0;
    double exponent = (path_loss_db - reference_loss_db) / denominator;
    if (exponent < -3.0) {
        exponent = -3.0;
    }
    return pow(10.0, exponent);
}
