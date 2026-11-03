#ifndef FR_RAW_CAPTURE_H
#define FR_RAW_CAPTURE_H
#include <stdint.h>
#include <stdio.h>

#define FR_RAW_MAX_POINTS 64u
#define FR_RAW_MAX_RMS_CODE 16383.0

enum fr_raw_path { FR_RAW_FWD = 0, FR_RAW_REV = 1 };
enum fr_raw_quality {
    FR_RAW_LOW_FWD = 1u, FR_RAW_LOW_REV = 2u, FR_RAW_CLIPPED = 4u
};
enum fr_raw_error {
    FR_RAW_ERROR_NONE = 0, FR_RAW_ERROR_CONFIG, FR_RAW_ERROR_CLOCK,
    FR_RAW_ERROR_FREQUENCY, FR_RAW_ERROR_SWITCH, FR_RAW_ERROR_TIMEOUT,
    FR_RAW_ERROR_FRAME, FR_RAW_ERROR_ABORTED
};
enum fr_raw_rc { FR_RAW_FAILED = -1, FR_RAW_IGNORED = 0,
                 FR_RAW_PROGRESS = 1, FR_RAW_COMPLETE = 2 };
#define FR_RAW_FREQUENCY_PENDING 1

struct fr_raw_config {
    unsigned points, frames_per_path, guard_frames;
    uint32_t frequency_hz[FR_RAW_MAX_POINTS];
    uint64_t settle_us, timeout_us;
    double min_fwd_rms, min_rev_rms;
};
struct fr_raw_backend {
    void *ctx;
    int (*set_frequency)(void *ctx, uint32_t hz);
    int (*select_path)(void *ctx, enum fr_raw_path path);
    uint64_t (*monotonic_us)(void *ctx);
};
struct fr_raw_frame {
    /* Capture provenance must be saved when ARMING, not when reading DONE.
     * sequence is a caller-maintained monotonic uint64_t (unwrap HW tokens).
     * Never retag an old/in-flight frame with the current epoch/frequency. */
    uint64_t sequence, epoch, started_us, completed_us;
    uint32_t frequency_hz;
    double rms_ac_code;
    int clipped;
};
struct fr_raw_point {
    uint32_t frequency_hz;
    double fwd_rms, rev_rms, sample_ratio;
    unsigned fwd_frames, rev_frames, quality;
    uint64_t fwd_first_seq, fwd_last_seq, rev_first_seq, rev_last_seq;
    int complete;
};
struct fr_raw_capture {
    struct fr_raw_config config;
    struct fr_raw_backend backend;
    struct fr_raw_point trace[FR_RAW_MAX_POINTS];
    unsigned point, guard_left, average_count;
    enum fr_raw_path path;
    enum fr_raw_error error;
    int active, completed;
    uint64_t epoch, ready_us, deadline_us, last_clock_us, last_sequence;
    uint64_t ignored_frames;
    double sum_squares;
    /* v2 optional async frequency preparation. Legacy backend layout is
     * unchanged; start_async opts in, preserving synchronous call semantics. */
    int frequency_pending;
    int (*frequency_poll)(void *ctx);
    uint64_t frequency_deadline_us;
};

/* Initialize the object to {0} before first use. Starting an active session
 * is rejected without changing it; abort before starting a different one.
 * Offline-safe core: no MMIO, GPIO, device access, sleep or filesystem paths.
 * Callbacks are mandatory; the UI must not attach a hardware backend until
 * candidate PL, pin limits, RF path and unused-port matching are verified.
 * The caller owns DAC OFF / GPIO lifetime; abort does NOT shut hardware off. */
int fr_raw_start(struct fr_raw_capture *s, const struct fr_raw_config *cfg,
                 struct fr_raw_backend backend, uint64_t sequence_floor);
/* Side-effect-free synchronous configuration validation; async caller also
 * enforces integral kHz and the chosen front end's minimum settling time. */
int fr_raw_config_valid(const struct fr_raw_config *);
/* set_frequency: 0=confirmed, 1=pending, otherwise failure. frequency_poll:
 * 0=pending, 1=confirmed, otherwise failure. Both must be nonblocking.
 * No path selection, epoch/new ARM or frame averaging before confirmation.
 * Stop/abort does NOT cancel a command job; the owner must drain it and OFF. */
int fr_raw_start_async(struct fr_raw_capture *, const struct fr_raw_config *,
                       struct fr_raw_backend, uint64_t sequence_floor,
                       int (*frequency_poll)(void *ctx));
int fr_raw_can_arm(const struct fr_raw_capture *, uint64_t now_us);
int fr_raw_push(struct fr_raw_capture *s, const struct fr_raw_frame *frame);
int fr_raw_poll(struct fr_raw_capture *s);
void fr_raw_abort(struct fr_raw_capture *s);
const char *fr_raw_quality_name(const struct fr_raw_point *point);
const char *fr_raw_error_name(enum fr_raw_error error);
/* Raw ADC-code traces / ratio only. No Gamma, RL, VSWR, complex S11 or DTF.
 * Hardware/calibration remain UNVERIFIED in this candidate's export. Caller
 * handles temporary-file + fsync + rename for a deployed atomic exporter. */
int fr_raw_write_csv(FILE *file, const struct fr_raw_capture *s);
#endif
