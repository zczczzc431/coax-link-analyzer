#ifndef SPECTRUM_QUALIFICATION654_H
#define SPECTRUM_QUALIFICATION654_H
#include "spectrum_mask612.h"

#define SM654_POLICY_VERSION 1u
enum sm654_coverage { SM654_OUTSIDE, SM654_EXCLUDED, SM654_BELOW_ABSOLUTE,
 SM654_NOT_PEAK, SM654_INSUFFICIENT_REFERENCE, SM654_LOW_CONTRAST, SM654_SUPPORTED };
struct sm654_policy {
 unsigned radius,guard,peak_radius,min_training;
 double contrast_db;
};
struct sm654_selection {
 unsigned seeds,qualified,insufficient,low_contrast,dominant_bin;
 unsigned char coverage[SM612_MAX_BINS];
 unsigned training_count[SM612_MAX_BINS];
 double baseline_db[SM612_MAX_BINS],background_db[SM612_MAX_BINS];
};
void sm654_defaults(struct sm654_policy *policy);
/* Pure qualification only. No min/max/spread input, FFT, ADC, IO or state mutation.
 * mean_power is (single-sided peak CODE / 8191)^2 averaged in LINEAR power.
 * Background is local reference-cell mean, NOT calibrated noise or CFAR Pfa.
 * Return -1 => output unusable; caller must not ARM or use partial results.
 * Legacy612 mode is untouched. A future dual-limit654 caller must require
 * dominant-bin SUPPORT and at least one lower bound, then enforce original
 * max-min<=3dB on supported peaks. Upper checks MUST NOT skip other bins.
 */
int sm654_select(const struct sm612_config *config,const struct sm654_policy *policy,
                 const double *mean_power,size_t bins,struct sm654_selection *out);
#endif
