#ifndef SPECTRUM_MASK612_H
#define SPECTRUM_MASK612_H
#include <stddef.h>
#include <stdint.h>

#define SM612_MAX_BINS 1025u
#define SM612_FORMAT 0x06120001u
enum sm612_state { SM612_NO_TEMPLATE, SM612_LEARNING, SM612_READY,
    SM612_ARMED, SM612_CANDIDATE, SM612_ALARM, SM612_INVALID, SM612_SUSPENDED };
enum sm612_reason { SM612_OK, SM612_BAD_CONFIG, SM612_BAD_IDENTITY,
    SM612_IDENTITY_MISMATCH, SM612_BAD_FRAME, SM612_OVERLOAD,
    SM612_DUPLICATE, SM612_STALE_FRAME, SM612_SEQUENCE_GAP,
    SM612_NO_SIGNAL, SM612_UNSTABLE_TEMPLATE, SM612_NOT_ARMED,
    SM612_MANUAL_STOP, SM612_OWNER_SUSPEND, SM612_COUNTER_LIMIT };
enum sm612_flag {
    SM612_FIRST=1u, SM612_CONFIRMED=2u, SM612_WORST=4u,
    SM612_RECOVERED=8u, SM612_GAP=16u, SM612_REJECTED=32u,
    SM612_INTERRUPTED=64u, SM612_UNCONFIRMED=128u, SM612_LEARNED=256u
};
struct sm612_identity {
    uint64_t session;           /* Nonzero host/capture session. */
    uint64_t input_signature;   /* Channel/gain/attenuation declaration revision. */
    uint64_t source_signature;  /* External/internal source + settings revision. */
    double fs_hz;
    uint32_t fft_size;
    uint32_t format;            /* Window/detrend/normalization ABI, SM612_FORMAT. */
};
struct sm612_config {
    unsigned first_bin,last_bin,learn_frames,enter_frames,recover_frames;
    double upper_margin_db,lower_margin_db,floor_db,lower_signal_margin_db;
    double min_learn_peak_db,max_learn_spread_db,hysteresis_db;
    uint64_t max_gap_us;
    int upper_enabled,lower_enabled;
    unsigned char exclude[SM612_MAX_BINS]; /* 1 means explicitly protected. */
};
struct sm612_frame {
    struct sm612_identity identity;
    uint32_t token;
    uint64_t timestamp_us;      /* Monotonic observation time, not ADC epoch. */
    const double *code;         /* Raw single-sided peak amplitudes, ADC CODE. */
    size_t bins;
    int valid;                 /* ADC protocol/copy integrity, NOT tone lock. */
    int overload;              /* Clipping/rail quality alarm, not spectrum PASS. */
};
struct sm612_decision {
    enum sm612_state state;
    enum sm612_reason reason;
    unsigned flags;
    uint64_t event_id,ended_event_id,first_us,worst_us;
    uint32_t frame_token,first_token,worst_token;
    uint64_t timestamp_us;
    unsigned current_bin,worst_bin;
    int current_direction,worst_direction; /* +1 upper, -1 lower, 0 none. */
    double current_excess_db,worst_excess_db;
};
struct sm612_model {
    struct sm612_identity identity;
    struct sm612_config config;
    enum sm612_state state;
    enum sm612_reason reason;
    size_t bins;
    unsigned learned,enter_count,recover_count;
    int template_valid,have_frame;
    uint32_t last_token,first_token,worst_token;
    uint64_t last_us,next_event,event_id,first_us,worst_us;
    unsigned worst_bin;
    int worst_direction;
    double worst_excess_db;
    double sum_power[SM612_MAX_BINS],min_db[SM612_MAX_BINS],max_db[SM612_MAX_BINS];
    double baseline_db[SM612_MAX_BINS],upper_db[SM612_MAX_BINS],lower_db[SM612_MAX_BINS];
    unsigned char lower_valid[SM612_MAX_BINS];
};
void sm612_defaults(struct sm612_config *config);
void sm612_init(struct sm612_model *model);
/* No implicit stop/relearn while armed, candidate or alarm. Stop first. */
int sm612_begin_learn(struct sm612_model *model,const struct sm612_config *config,
                     const struct sm612_identity *identity);
int sm612_arm(struct sm612_model *model,const struct sm612_identity *identity);
void sm612_stop(struct sm612_model *model,int suspended,struct sm612_decision *out);
/* 0=accepted/idle, -1=rejected. Inspect flags/reason, never infer PASS from rc. */
int sm612_push(struct sm612_model *model,const struct sm612_frame *frame,
               struct sm612_decision *out);
int sm612_identity_equal(const struct sm612_identity *a,const struct sm612_identity *b);
#endif
