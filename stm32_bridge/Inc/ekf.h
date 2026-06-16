#ifndef TDMA_EKF_H
#define TDMA_EKF_H

#include <stdint.h>

#define EKF_MAX_ANCHORS 3

typedef struct {
    char id[16];
    double x;
    double y;
} ekf_anchor_t;

typedef struct {
    double state[4];    /* [px, py, vx, vy] */
    double P[4][4];     /* State Covariance */
    double Q_pos;       /* Process noise position */
    double Q_vel;       /* Process noise velocity */
    double R_noise;     /* Measurement noise */
    ekf_anchor_t anchors[EKF_MAX_ANCHORS];
    int anchor_count;
} ekf_t;

void ekf_init(ekf_t *ekf, double px0, double py0);
void ekf_predict(ekf_t *ekf, double dt);
int ekf_update_apollonius(ekf_t *ekf, const char *anchor_a_id, const char *anchor_b_id, double z_ratio);
void ekf_set_anchor_position(ekf_t *ekf, const char *id, double x, double y);

#endif
