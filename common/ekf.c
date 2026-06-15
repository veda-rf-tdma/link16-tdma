#include "ekf.h"
#include <string.h>
#include <math.h>

#if defined(_WIN32)
#define strcasecmp _stricmp
#elif defined(__arm__)
#define strcasecmp strcmp
#else
#include <strings.h> /* for strcasecmp on POSIX systems */
#endif

void ekf_init(ekf_t *ekf, double px0, double py0)
{
    memset(ekf, 0, sizeof(*ekf));
    
    ekf->state[0] = px0;
    ekf->state[1] = py0;
    ekf->state[2] = 0.0;
    ekf->state[3] = 0.0;

    /* High initial uncertainty for position, moderate for velocity */
    ekf->P[0][0] = 10.0;
    ekf->P[1][1] = 10.0;
    ekf->P[2][2] = 2.0;
    ekf->P[3][3] = 2.0;

    ekf->Q_pos = 0.08;
    ekf->Q_vel = 0.08 / 5.0;
    ekf->R_noise = 1.2;

    /* Initialize default anchor coordinates */
    strcpy(ekf->anchors[0].id, "0x21");
    ekf->anchors[0].x = 0.0;
    ekf->anchors[0].y = 0.0;

    strcpy(ekf->anchors[1].id, "0x22");
    ekf->anchors[1].x = 5.0;
    ekf->anchors[1].y = 0.0;

    strcpy(ekf->anchors[2].id, "0x23");
    ekf->anchors[2].x = 2.5;
    ekf->anchors[2].y = 4.330127;

    ekf->anchor_count = 3;
}

void ekf_predict(ekf_t *ekf, double dt)
{
    /* State transition: state = F * state */
    double px = ekf->state[0] + ekf->state[2] * dt;
    double py = ekf->state[1] + ekf->state[3] * dt;
    double vx = ekf->state[2];
    double vy = ekf->state[3];
    
    ekf->state[0] = px;
    ekf->state[1] = py;
    ekf->state[2] = vx;
    ekf->state[3] = vy;

    /* Covariance prediction: P = F * P * F^T + Q */
    double FP[4][4];
    for (int j = 0; j < 4; j++) {
        FP[0][j] = ekf->P[0][j] + dt * ekf->P[2][j];
        FP[1][j] = ekf->P[1][j] + dt * ekf->P[3][j];
        FP[2][j] = ekf->P[2][j];
        FP[3][j] = ekf->P[3][j];
    }

    double P_pred[4][4];
    for (int i = 0; i < 4; i++) {
        P_pred[i][0] = FP[i][0] + dt * FP[i][2];
        P_pred[i][1] = FP[i][1] + dt * FP[i][3];
        P_pred[i][2] = FP[i][2];
        P_pred[i][3] = FP[i][3];
    }

    /* Add process noise Q */
    P_pred[0][0] += ekf->Q_pos;
    P_pred[1][1] += ekf->Q_pos;
    P_pred[2][2] += ekf->Q_vel;
    P_pred[3][3] += ekf->Q_vel;

    memcpy(ekf->P, P_pred, sizeof(ekf->P));
}

static int find_anchor_coords(ekf_t *ekf, const char *id, double *x, double *y)
{
    for (int i = 0; i < ekf->anchor_count; i++) {
        if (strcasecmp(ekf->anchors[i].id, id) == 0) {
            *x = ekf->anchors[i].x;
            *y = ekf->anchors[i].y;
            return 1;
        }
    }
    return 0;
}

int ekf_update_apollonius(ekf_t *ekf, const char *anchor_a_id, const char *anchor_b_id, double z_ratio)
{
    double xa, ya, xb, yb;
    if (!find_anchor_coords(ekf, anchor_a_id, &xa, &ya) ||
        !find_anchor_coords(ekf, anchor_b_id, &xb, &yb)) {
        return -1;
    }

    double da = sqrt((ekf->state[0] - xa) * (ekf->state[0] - xa) + 
                    (ekf->state[1] - ya) * (ekf->state[1] - ya));
    double db = sqrt((ekf->state[0] - xb) * (ekf->state[0] - xb) + 
                    (ekf->state[1] - yb) * (ekf->state[1] - yb));

    if (da < 1e-4 || db < 1e-4) {
        return -2;
    }

    double h = da / db;

    /* Jacobian vector H = [hx, hy, 0, 0] */
    double hx = (ekf->state[0] - xa) / (da * db) - (da * (ekf->state[0] - xb)) / (db * db * db);
    double hy = (ekf->state[1] - ya) / (da * db) - (da * (ekf->state[1] - yb)) / (db * db * db);

    double y_innov = z_ratio - h;

    /* Innovation covariance S = H * P * H^T + R */
    double hp[4];
    hp[0] = hx * ekf->P[0][0] + hy * ekf->P[1][0];
    hp[1] = hx * ekf->P[0][1] + hy * ekf->P[1][1];
    hp[2] = hx * ekf->P[0][2] + hy * ekf->P[1][2];
    hp[3] = hx * ekf->P[0][3] + hy * ekf->P[1][3];

    double s = hp[0] * hx + hp[1] * hy + ekf->R_noise;
    if (s < 1e-6) {
        return -3;
    }

    /* Kalman Gain K (4x1) */
    double K[4];
    K[0] = (ekf->P[0][0] * hx + ekf->P[0][1] * hy) / s;
    K[1] = (ekf->P[1][0] * hx + ekf->P[1][1] * hy) / s;
    K[2] = (ekf->P[2][0] * hx + ekf->P[2][1] * hy) / s;
    K[3] = (ekf->P[3][0] * hx + ekf->P[3][1] * hy) / s;

    /* Update state */
    ekf->state[0] += K[0] * y_innov;
    ekf->state[1] += K[1] * y_innov;
    ekf->state[2] += K[2] * y_innov;
    ekf->state[3] += K[3] * y_innov;

    /* Update covariance P = (I - K * H) * P */
    double P_new[4][4];
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            P_new[r][c] = ekf->P[r][c] - K[r] * (hx * ekf->P[0][c] + hy * ekf->P[1][c]);
        }
    }
    memcpy(ekf->P, P_new, sizeof(ekf->P));

    return 0;
}

void ekf_set_anchor_position(ekf_t *ekf, const char *id, double x, double y)
{
    for (int i = 0; i < ekf->anchor_count; i++) {
        if (strcasecmp(ekf->anchors[i].id, id) == 0) {
            ekf->anchors[i].x = x;
            ekf->anchors[i].y = y;
            return;
        }
    }
    /* Add new anchor if it doesn't exist and there is space */
    if (ekf->anchor_count < EKF_MAX_ANCHORS) {
        strncpy(ekf->anchors[ekf->anchor_count].id, id, sizeof(ekf->anchors[ekf->anchor_count].id) - 1);
        ekf->anchors[ekf->anchor_count].id[sizeof(ekf->anchors[ekf->anchor_count].id) - 1] = '\0';
        ekf->anchors[ekf->anchor_count].x = x;
        ekf->anchors[ekf->anchor_count].y = y;
        ekf->anchor_count++;
    }
}
