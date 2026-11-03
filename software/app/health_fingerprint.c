#include "health_fingerprint.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double clamp01(double x) {
    if (!isfinite(x) || x < 0.0) return 0.0;
    return x > 1.0 ? 1.0 : x;
}

static double abs_delta(const struct hf_point *p) {
    return fabs(p->measured_db - p->reference_db);
}

static int finite_config(const struct hf_config *c) {
    return c && isfinite(c->warn_delta_db) && isfinite(c->fail_delta_db) &&
           isfinite(c->min_valid_ratio) && isfinite(c->warn_pass_ratio) &&
           c->warn_delta_db > 0.0 && c->fail_delta_db > c->warn_delta_db &&
           c->min_valid_ratio > 0.0 && c->min_valid_ratio <= 1.0 &&
           c->warn_pass_ratio >= 0.0 && c->warn_pass_ratio <= 1.0;
}

struct hf_config hf_config_default(void) {
    struct hf_config c = {3.0, 6.0, 0.80, 0.90};
    return c;
}

const char *hf_label_name(enum hf_label label) {
    switch (label) {
    case HF_LABEL_NORMAL: return "NORMAL";
    case HF_LABEL_WARNING: return "WARNING";
    case HF_LABEL_FAIL: return "FAIL";
    case HF_LABEL_INVALID_BASELINE: return "INVALID_BASELINE";
    default: return "INVALID_BASELINE";
    }
}

const char *hf_quality_name(enum hf_point_quality quality) {
    switch (quality) {
    case HF_POINT_VALID: return "VALID";
    case HF_POINT_LOW_SIGNAL: return "LOW_SIGNAL";
    case HF_POINT_CLIPPED: return "CLIPPED";
    case HF_POINT_RETRY_EXHAUSTED: return "RETRY_EXHAUSTED";
    case HF_POINT_STALE: return "STALE";
    case HF_POINT_CONFIG_MISMATCH: return "CONFIG_MISMATCH";
    default: return "UNKNOWN";
    }
}

int hf_grid_compatible(const double *measured_frequency_mhz,
                       const double *reference_frequency_mhz,
                       size_t count, double tolerance_mhz) {
    if (!measured_frequency_mhz || !reference_frequency_mhz || count == 0 ||
        !isfinite(tolerance_mhz) || tolerance_mhz < 0.0)
        return 0;
    for (size_t i = 0; i < count; ++i) {
        double mf = measured_frequency_mhz[i];
        double rf = reference_frequency_mhz[i];
        if (!isfinite(mf) || !isfinite(rf) || mf <= 0.0 || rf <= 0.0 || fabs(mf - rf) > tolerance_mhz)
            return 0;
        if (i && (mf <= measured_frequency_mhz[i - 1] ||
                  rf <= reference_frequency_mhz[i - 1]))
            return 0;
    }
    return 1;
}

static int compare_double(const void *a, const void *b) {
    const double da = *(const double *)a;
    const double db = *(const double *)b;
    return da < db ? -1 : da > db ? 1 : 0;
}

int hf_evaluate(const struct hf_config *config,
                const struct hf_point *points, size_t count,
                struct hf_result *result) {
    if (!result) return -1;
    memset(result, 0, sizeof(*result));
    result->label = HF_LABEL_INVALID_BASELINE;
    if (!finite_config(config) || (count && !points) || count > HF_MAX_POINTS)
        return -1;
    result->total_points = (unsigned)count;
    if (count == 0) {
        snprintf(result->reason, sizeof(result->reason), "NO_POINTS");
        return 0;
    }
    double deltas[HF_MAX_POINTS];
    /* Validate before accumulating: rejected inputs must not leave a
     * partially populated result that can be mistaken for a valid run. */
    for (size_t i = 0; i < count; ++i) {
        const struct hf_point *p = &points[i];
        if (!isfinite(p->frequency_mhz) || p->frequency_mhz <= 0.0 ||
            (i && p->frequency_mhz <= points[i - 1].frequency_mhz) ||
            p->quality < HF_POINT_VALID || p->quality > HF_POINT_CONFIG_MISMATCH)
            return -1;
        if (p->quality == HF_POINT_VALID &&
            (!isfinite(p->measured_db) || !isfinite(p->reference_db) ||
             !isfinite(abs_delta(p)))) return -1;
    }
    for (size_t i = 0; i < count; ++i) {
        const struct hf_point *p = &points[i];
        switch (p->quality) {
        case HF_POINT_VALID:
            result->valid_points++;
            deltas[result->valid_points - 1] = abs_delta(p);
            break;
        case HF_POINT_LOW_SIGNAL: result->low_signal_points++; break;
        case HF_POINT_CLIPPED: result->clipped_points++; break;
        case HF_POINT_RETRY_EXHAUSTED: result->retry_exhausted_points++; break;
        case HF_POINT_STALE: result->stale_points++; break;
        case HF_POINT_CONFIG_MISMATCH: result->config_mismatch_points++; break;
        default: return -1;
        }
    }
    result->valid_ratio = (double)result->valid_points / (double)count;
    if (result->config_mismatch_points || result->valid_points == 0) {
        snprintf(result->reason, sizeof(result->reason),
                 result->config_mismatch_points ? "CONFIG_MISMATCH" : "NO_VALID_POINTS");
        return 0;
    }

    qsort(deltas, result->valid_points, sizeof(*deltas), compare_double);
    double sum = 0.0;
    for (unsigned i = 0; i < result->valid_points; ++i) {
        sum += deltas[i] / (double)result->valid_points;
        if (deltas[i] <= config->warn_delta_db) result->pass_points++;
    }
    result->mean_abs_delta_db = sum;
    result->max_abs_delta_db = deltas[result->valid_points - 1];
    size_t p95_index = (size_t)ceil(0.95 * (double)result->valid_points) - 1;
    if (p95_index >= result->valid_points) p95_index = result->valid_points - 1;
    result->p95_abs_delta_db = deltas[p95_index];
    result->pass_ratio = (double)result->pass_points / (double)result->valid_points;

    /* Policy score, not a calibrated RF quantity. At a fixed valid mask it
     * is nonincreasing with each absolute deviation. Mask changes alter the
     * population; compare coverage separately, not score alone. */
    double max_penalty = clamp01(result->max_abs_delta_db / config->fail_delta_db);
    double mean_penalty = clamp01(result->mean_abs_delta_db / config->fail_delta_db);
    double p95_penalty = clamp01(result->p95_abs_delta_db / config->fail_delta_db);
    double deviation_factor = 1.0 - (0.40 * max_penalty +
                                     0.30 * mean_penalty +
                                     0.30 * p95_penalty);
    if (deviation_factor < 0.0) deviation_factor = 0.0;
    result->health_score = 100.0 * result->valid_ratio * deviation_factor;

    if (result->valid_ratio < config->min_valid_ratio ||
        result->max_abs_delta_db > config->fail_delta_db) {
        result->label = HF_LABEL_FAIL;
        snprintf(result->reason, sizeof(result->reason),
                 result->valid_ratio < config->min_valid_ratio ?
                 "TOO_MANY_INVALID_POINTS" : "DELTA_OVER_FAIL_LIMIT");
    } else if (result->valid_points < count ||
               result->max_abs_delta_db > config->warn_delta_db ||
               result->pass_ratio < config->warn_pass_ratio) {
        result->label = HF_LABEL_WARNING;
        snprintf(result->reason, sizeof(result->reason),
                 result->valid_points < count ? "PARTIAL_VALID_POINTS" :
                 result->max_abs_delta_db > config->warn_delta_db ?
                 "DELTA_OVER_WARN_LIMIT" : "LOW_PASS_RATIO");
    } else {
        result->label = HF_LABEL_NORMAL;
        snprintf(result->reason, sizeof(result->reason), "WITHIN_BASELINE_LIMITS");
    }
    return 0;
}
