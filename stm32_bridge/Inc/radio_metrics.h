#ifndef RADIO_METRICS_H
#define RADIO_METRICS_H

#include <stdint.h>

double cc1101_rssi_dbm(uint8_t raw_rssi);
double radio_estimate_distance_m(double rssi_dbm);

#endif
