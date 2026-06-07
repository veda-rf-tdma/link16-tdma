#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ekf.h"

#define MAX_LINE_LEN 1024
#define MAX_FIELDS 64

/* Helper to split a string by delimiter and return array of tokens */
static int split_line(char *line, const char *delim, char **fields, int max_fields)
{
    int count = 0;
    char *token = strtok(line, delim);
    while (token && count < max_fields) {
        fields[count++] = token;
        token = strtok(NULL, delim);
    }
    return count;
}

int main(int argc, char **argv)
{
    const char *input_path = "analysis_out/ekf_observations.csv";
    const char *output_path = "analysis_out/ekf_trajectory.csv";

    if (argc > 1) input_path = argv[1];
    if (argc > 2) output_path = argv[2];

    FILE *fin = fopen(input_path, "r");
    if (!fin) {
        fprintf(stderr, "ERROR: Cannot open input file %s\n", input_path);
        return 1;
    }

    FILE *fout = fopen(output_path, "w");
    if (!fout) {
        fprintf(stderr, "ERROR: Cannot open output file %s\n", output_path);
        fclose(fin);
        return 1;
    }

    char line[MAX_LINE_LEN];
    if (!fgets(line, sizeof(line), fin)) {
        fprintf(stderr, "ERROR: Empty input file\n");
        fclose(fin);
        fclose(fout);
        return 1;
    }

    /* Parse header to find column indices */
    char header_line[MAX_LINE_LEN];
    strcpy(header_line, line);
    header_line[strcspn(header_line, "\r\n")] = '\0';

    char *header_fields[MAX_FIELDS];
    int header_count = split_line(header_line, ",", header_fields, MAX_FIELDS);

    int idx_frame = -1;
    int idx_timestamp = -1;
    int idx_q = -1;
    int idx_r = -1;
    int idx_ratios = -1;

    for (int i = 0; i < header_count; i++) {
        if (strcmp(header_fields[i], "frame_no") == 0) idx_frame = i;
        else if (strcmp(header_fields[i], "timestamp") == 0) idx_timestamp = i;
        else if (strcmp(header_fields[i], "ekf_process_noise") == 0) idx_q = i;
        else if (strcmp(header_fields[i], "ekf_measurement_noise") == 0) idx_r = i;
        else if (strcmp(header_fields[i], "apollonius_distance_ratios") == 0) idx_ratios = i;
    }

    if (idx_frame == -1 || idx_timestamp == -1 || idx_ratios == -1) {
        fprintf(stderr, "ERROR: Missing required columns in header\n");
        fclose(fin);
        fclose(fout);
        return 1;
    }

    /* Instantiate EKF state using shared C EKF core */
    ekf_t ekf_inst;
    /* Initialize with the center of default anchors (2.5, 1.44) or generic (2.5, 2.16) */
    ekf_init(&ekf_inst, 2.5, 2.165);

    double dt = 0.1; /* 100ms frame interval */

    /* Write output CSV header */
    fprintf(fout, "frame_no,timestamp,px,py,vx,vy,uncertainty_pos\n");

    while (fgets(line, sizeof(line), fin)) {
        line[strcspn(line, "\r\n")] = '\0';

        char line_copy[MAX_LINE_LEN];
        strcpy(line_copy, line);

        char *fields[MAX_FIELDS];
        int f_count = 0;
        char *ptr = line_copy;
        while (f_count < MAX_FIELDS) {
            fields[f_count++] = ptr;
            char *comma = strchr(ptr, ',');
            if (!comma) break;
            *comma = '\0';
            ptr = comma + 1;
        }

        if (f_count < header_count) continue;

        int frame_no = atoi(fields[idx_frame]);
        const char *timestamp = fields[idx_timestamp];

        double q_noise = 0.08;
        double r_noise = 1.2;
        if (idx_q != -1 && strlen(fields[idx_q]) > 0) q_noise = atof(fields[idx_q]);
        if (idx_r != -1 && strlen(fields[idx_r]) > 0) r_noise = atof(fields[idx_r]);

        /* Update EKF config parameters dynamically if they change in logs */
        ekf_inst.Q_pos = q_noise;
        ekf_inst.Q_vel = q_noise / 5.0;
        ekf_inst.R_noise = r_noise;

        /* EKF Prediction Step */
        ekf_predict(&ekf_inst, dt);

        /* EKF Update Step (Sequential updates for each Apollonius ratio) */
        char *ratios_field = fields[idx_ratios];
        if (ratios_field[0] == '"') {
            ratios_field++;
            size_t flen = strlen(ratios_field);
            if (flen > 0 && ratios_field[flen - 1] == '"') {
                ratios_field[flen - 1] = '\0';
            }
        }

        if (strlen(ratios_field) > 0) {
            char ratio_buf[MAX_LINE_LEN];
            strcpy(ratio_buf, ratios_field);

            char *pairs[MAX_FIELDS];
            int pair_count = split_line(ratio_buf, ";", pairs, MAX_FIELDS);

            for (int p = 0; p < pair_count; p++) {
                char *eq = strchr(pairs[p], '=');
                if (!eq) continue;
                *eq = '\0';
                char *ratio_val_str = eq + 1;

                char *slash = strchr(pairs[p], '/');
                if (!slash) continue;
                *slash = '\0';
                char *anchor_a_id = pairs[p];
                char *anchor_b_id = slash + 1;

                double z = atof(ratio_val_str);

                /* Perform EKF sequential update using shared math function */
                ekf_update_apollonius(&ekf_inst, anchor_a_id, anchor_b_id, z);
            }
        }

        double uncertainty = sqrt(ekf_inst.P[0][0] + ekf_inst.P[1][1]);
        fprintf(fout, "%d,%s,%.4f,%.4f,%.4f,%.4f,%.4f\n",
                frame_no, timestamp, ekf_inst.state[0], ekf_inst.state[1],
                ekf_inst.state[2], ekf_inst.state[3], uncertainty);
    }

    fclose(fin);
    fclose(fout);
    printf("EKF tracking complete (using shared core). Trajectory written to %s\n", output_path);
    return 0;
}
