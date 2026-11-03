#ifndef FR_VECTOR_MATH_H
#define FR_VECTOR_MATH_H
#include <complex.h>
#include <stdio.h>
#include <stdint.h>
#define FR_VECTOR_POINTS 64u
#define FR_DTF_BINS 256u
enum fr_vector_source {FR_VECTOR_UNKNOWN,FR_VECTOR_IMPORTED,FR_VECTOR_SYNTHETIC,FR_VECTOR_BOARD};
enum fr_vector_error {FR_VECTOR_OK,FR_VECTOR_NO_PHASE,FR_VECTOR_NO_CAL,
    FR_VECTOR_BAD_DATA,FR_VECTOR_GRID,FR_VECTOR_SINGULAR,FR_VECTOR_FILE};
/* phase_coherent/reference_id are provider facts. No RMS-only conversion or
 * Linux request timestamps can establish them. Imported declarations remain
 * imported, never promote a file to a verified board measurement. */
struct fr_vector_trace {
    unsigned points;
    double hz[FR_VECTOR_POINTS];
    double complex s11[FR_VECTOR_POINTS];
    enum fr_vector_source source;
    int phase_coherent,calibrated;
    uint64_t reference_id;
};
struct fr_vector_cal {
    unsigned points;
    double hz[FR_VECTOR_POINTS];
    double complex directivity[FR_VECTOR_POINTS],tracking[FR_VECTOR_POINTS],source_match[FR_VECTOR_POINTS];
    uint64_t reference_id;
    enum fr_vector_source source;
    int ready;
};
enum fr_dtf_window {FR_DTF_RECT,FR_DTF_HANN};
struct fr_dtf_result {
    double magnitude[FR_DTF_BINS];
    double distance_step_m,period_m,bandwidth_scale_m;
    double velocity_factor;
    unsigned points;
    int peak_bin; /* strongest response, not a proven physical fault */
    enum fr_dtf_window window;
    enum fr_vector_source source;
    enum fr_vector_error error;
    int ready;
};
const char *fr_vector_error_name(enum fr_vector_error);
enum fr_vector_error fr_vector_validate(const struct fr_vector_trace *,int require_cal);
/* Pure preparation for a FUTURE hardware first-sample counter. effective_ftw
 * is phase cycles/ADC sample in Q0.32, derived from verified coherent clocks.
 * An unknown common source phase cancels in same-epoch FWD/REV ratios only.
 * This function grants no phase permission and does NOT obtain a counter from
 * today's mailbox. Never substitute a PS time or ring index for that counter. */
int fr_vector_counter_phase(uint32_t sample_counter,uint32_t effective_ftw,double *phase);
struct fr_vector_frame {
    const uint32_t *samples;
    unsigned count;
    double sample_rate_hz,frequency_hz,source_phase_at_first_sample;
    uint64_t reference_id;
    enum fr_vector_source source;
    int phase_anchor_verified;
};
/* Least-squares DC + cos + sin, rotated by VERIFIED source phase at first
 * sample (or a verified shared reference with a common unknown phase offset
 * which cancels in FWD/REV). PS ARM/DONE time cannot populate this anchor. The
 * caller must obtain a common hardware reference and validate phase quality.
 * Returns PEAK phasor in ADC codes; residual is per-sample RMS in codes. */
enum fr_vector_error fr_vector_fit_frame(const struct fr_vector_frame *,double complex *,double *residual_rms);
enum fr_vector_error fr_vector_frame_ratio(const struct fr_vector_frame *fwd,
    const struct fr_vector_frame *rev,double min_fwd_peak_code,double complex *ratio);
/* Optional quality gate for FUTURE verified complex capture. Each residual
 * must be <= absolute_floor + relative_fraction * fitted_peak, in ADC codes.
 * The absolute term permits a quiet matched REV near zero; no REV amplitude
 * threshold is imposed. Limits require measured noise/accuracy validation,
 * not chosen here. This is a model-consistency test, NOT phase verification
 * or an SNR/uncertainty guarantee. Outputs remain untouched on failure. */
struct fr_vector_quality_limits {
    double min_fwd_peak_code,absolute_residual_code,relative_residual_fraction;
};
struct fr_vector_quality_result {
    double fwd_peak_code,rev_peak_code,fwd_residual_code,rev_residual_code;
};
enum fr_vector_error fr_vector_frame_ratio_checked(const struct fr_vector_frame *fwd,
    const struct fr_vector_frame *rev,const struct fr_vector_quality_limits *,
    double complex *ratio,struct fr_vector_quality_result *);
/* Ideal standards Gamma_LOAD=0, OPEN=+1, SHORT=-1. Models/connector delay must
 * be validated for the physical standards before claiming instrument accuracy.
 * Same reference plane and coherent measurement setup for all four traces. */
enum fr_vector_error fr_vector_cal_build(struct fr_vector_cal *,const struct fr_vector_trace *load,
    const struct fr_vector_trace *open,const struct fr_vector_trace *shorted);
enum fr_vector_error fr_vector_cal_apply(const struct fr_vector_cal *,const struct fr_vector_trace *,
    struct fr_vector_trace *);
/* RL=-20log10|Gamma|, VSWR=(1+|Gamma|)/(1-|Gamma|). Infinite limits are retained;
 * |Gamma|>1 produces a negative RL and undefined passive VSWR, never clamped. */
int fr_vector_metrics(double complex gamma,double *rl_db,double *vswr);
/* Uniform positive-frequency BANDPASS impulse envelope. No DC extrapolation.
 * Zero-padding improves display sampling, not physical bandwidth resolution.
 * Uses positive-sign complex IFFT and one-way reflection distance c*VF*t/2. */
enum fr_vector_error fr_dtf_compute(const struct fr_vector_trace *,double velocity_factor,
    enum fr_dtf_window,struct fr_dtf_result *);
/* Restricted Touchstone v1 ONE PORT: Hz/kHz/MHz/GHz S RI/MA/DB R 50.
 * Three values per line, max64, finite increasing frequencies. No wrapped
 * rows/multiport/v2. Provider declaration is mandatory for compute:
 * ! zynq_vector_v1 source=IMPORTED|SYNTHETIC phase=COHERENT|UNKNOWN calibration=APPLIED|NONE reference_id=N
 * Calibration provenance is asserted by the provider, not verified by parser. */
enum fr_vector_error fr_vector_read_s1p(FILE *,struct fr_vector_trace *);
int fr_vector_write_s1p(FILE *,const struct fr_vector_trace *);
int fr_dtf_write_csv(FILE *,const struct fr_dtf_result *);
#endif
