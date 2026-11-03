#ifndef DIAG_HEALTH_FINGERPRINT_H
#define DIAG_HEALTH_FINGERPRINT_H

#include <stddef.h>
#define HF_MAX_POINTS 64

#ifdef __cplusplus
extern "C" {
#endif

/* Quality values deliberately match measurement provenance, not a visual
 * pass/fail decision.  Only HF_POINT_VALID contributes to the score. */
enum hf_point_quality {
    HF_POINT_VALID = 0,
    HF_POINT_LOW_SIGNAL,
    HF_POINT_CLIPPED,
    HF_POINT_RETRY_EXHAUSTED,
    HF_POINT_STALE,
    HF_POINT_CONFIG_MISMATCH
};

enum hf_label {
    HF_LABEL_NORMAL = 0,
    HF_LABEL_WARNING,
    HF_LABEL_FAIL,
    HF_LABEL_INVALID_BASELINE
};

struct hf_point {
    double frequency_mhz;
    double measured_db;
    double reference_db;
    enum hf_point_quality quality;
};

struct hf_config {
    /* Absolute delta thresholds in dB.  They are policy values for a
     * diagnostic score, not a claim of calibrated VNA accuracy. */
    double warn_delta_db;
    double fail_delta_db;
    double min_valid_ratio;
    double warn_pass_ratio;
};

struct hf_result {
    enum hf_label label;
    unsigned total_points;
    unsigned valid_points;
    unsigned low_signal_points;
    unsigned clipped_points;
    unsigned retry_exhausted_points;
    unsigned stale_points;
    unsigned config_mismatch_points;
    unsigned pass_points;
    double valid_ratio;
    double pass_ratio;
    double max_abs_delta_db;
    double mean_abs_delta_db;
    double p95_abs_delta_db;
    double health_score;
    char reason[96];
};

struct hf_config hf_config_default(void);
const char *hf_label_name(enum hf_label label);
const char *hf_quality_name(enum hf_point_quality quality);

/* Check the frequency grid before comparing two runs.  Both arrays must be
 * finite, strictly increasing, and have the same length and values within
 * tolerance. */
int hf_grid_compatible(const double *measured_frequency_mhz,
                       const double *reference_frequency_mhz,
                       size_t count, double tolerance_mhz);

/* Evaluate a scalar amplitude fingerprint.  The function does not mutate
 * input points (at most HF_MAX_POINTS) and returns -1 for invalid arguments/configuration, 0 for a
 * computed result (including INVALID_BASELINE), and never emits S11/VSWR/DTF
 * quantities. */
int hf_evaluate(const struct hf_config *config,
                const struct hf_point *points, size_t count,
                struct hf_result *result);

#ifdef __cplusplus
}
#endif

#endif
