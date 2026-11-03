#ifndef SIGNAL_TRENDS_H
#define SIGNAL_TRENDS_H
#include <stdint.h>
#include <stddef.h>
#define SIGNAL_TREND_FRAMES 64
struct signal_timing {
    int valid;
    const char *reason;
    unsigned periods;
    double frequency_hz,period_ns,jitter_rms_ns,jitter_pp_ns;
};
struct signal_frame {
    uint32_t token;
    uint64_t observed_us;
    double rms_code,frequency_hz;
    int frequency_valid;
};
struct signal_trend {
    struct signal_frame frame[SIGNAL_TREND_FRAMES];
    unsigned count;
};
struct signal_trend_result {
    unsigned frames,frequency_frames,observed_changes;
    int amplitude_valid,frequency_valid;
    double span_ms,max_gap_ms,amplitude_index_pct,low_high_ratio_pct;
    double frequency_min_hz,frequency_max_hz,frequency_span_hz;
};
/* Raw within-frame crossing statistics, not oscillator aperture jitter. */
int signal_timing_measure(const uint32_t *raw,size_t n,double fs_hz,struct signal_timing *out);
void signal_trend_reset(struct signal_trend *s);
/* Monotonic observation time; captures are sparse, never concatenate samples. */
int signal_trend_push(struct signal_trend *s,const struct signal_frame *f);
int signal_trend_summarize(const struct signal_trend *s,double change_threshold_hz,struct signal_trend_result *out);
#endif
