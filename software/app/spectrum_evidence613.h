#ifndef SPECTRUM_EVIDENCE613_H
#define SPECTRUM_EVIDENCE613_H
#include "spectrum_mask612.h"

#define SM613_MAX_SAMPLES 2048u
#define SM613_PRE 16u
#define SM613_POST 8u
enum sm613_end { SM613_END_NONE, SM613_END_RECOVERED, SM613_END_TRANSIENT,
    SM613_END_GAP, SM613_END_QUALITY, SM613_END_IDENTITY,
    SM613_END_STOP, SM613_END_SUSPEND, SM613_END_TIMEOUT };
enum sm613_status { SM613_OK, SM613_CORE_REJECTED, SM613_BAD_RAW,
    SM613_STORE_ERROR, SM613_CAPTURE_LIMIT, SM613_BUSY, SM613_BAD_TIME,
    SM613_TIMEOUT, SM613_BAD_ARGUMENT };

struct sm613_raw {
    uint32_t token;
    uint64_t timestamp_us;
    unsigned count;
    int valid,overload;
    uint32_t sample[SM613_MAX_SAMPLES]; /* Decoded unsigned 14-bit ADC CODE. */
};
struct sm613_template {
    struct sm612_identity identity;
    struct sm612_config config;
    size_t bins;
    double baseline_db[SM612_MAX_BINS],upper_db[SM612_MAX_BINS],lower_db[SM612_MAX_BINS];
    unsigned char lower_valid[SM612_MAX_BINS];
};
struct sm613_event {
    uint64_t capture_id,event_id; /* Capture id also names unconfirmed transients. */
    int confirmed,closed;
    enum sm613_end end;
    enum sm612_reason end_reason;
    uint64_t end_us;
    unsigned pre_count,post_count;
    struct sm613_template limits;
    struct sm613_raw pre[SM613_PRE],first,worst,post[SM613_POST],end_frame;
    int end_frame_present;
    double first_code[SM612_MAX_BINS],worst_code[SM612_MAX_BINS];
    struct sm612_decision first_decision,worst_decision;
};
/* Callback result contracts are deliberately distinct. 0 is durable success.
 * 693 means an immutable, complete deep copy is now owned by a bounded async
 * writer; it is NOT published/durable. All other results are failures.
 * The async owner MUST retain RAW until a durable receipt, expose backpressure
 * and failures, and gate LEARN/ARM/exit on outstanding jobs. This core neither
 * polls receipts nor promises crash survival for a queued RAM snapshot.
 * The borrowed callback view is valid only during this call. Idempotent for
 * (session,capture_id), no reentry. */
#define SM613_SINK_QUEUED 693
typedef int (*sm613_sink)(void *user,const struct sm613_event *event);
struct sm613_result {
    struct sm612_decision measurement;
    enum sm613_status status;
    int core_rc;
    unsigned published; /* Durable callback results only. */
    unsigned queued;    /* Immutable ownership handoffs, never durable success. */
};
struct sm613_context {
    struct sm612_model core;
    struct sm613_raw history[SM613_PRE];
    unsigned history_count,history_next;
    struct sm613_event event;
    int active,blocked,watching;
    uint64_t next_capture,watch_us,clock_us;
    uint64_t unretained_event_id;
    uint32_t unretained_first_token;
    int unretained_first;
    sm613_sink sink;
    void *sink_user;
};

/* Large object: use static/heap storage, never a small thread stack. */
int sm613_init(struct sm613_context *ctx,sm613_sink sink,void *user);
int sm613_learn(struct sm613_context *ctx,const struct sm612_config *config,
                const struct sm612_identity *identity);
int sm613_arm(struct sm613_context *ctx,const struct sm612_identity *identity,uint64_t now_us);
/* Calls the unchanged core once except a prevalidated BAD_TIME/argument/block.
 * 0=accepted/idle, -1=core/raw/time reject,
 * -2=blocked evidence failure. Idle/learning is NOT a measurement PASS.
 * Samples and frame spectrum must originate from the SAME ADC frame.
 * Zero AC is valid and must NOT be rejected for absence of a tone. */
int sm613_feed(struct sm613_context *ctx,const struct sm612_frame *frame,
               const uint32_t *raw,size_t count,struct sm613_result *out);
/* now_us must be >= the control-clock high-water mark and last core frame. */
int sm613_stop(struct sm613_context *ctx,int suspended,uint64_t now_us,struct sm613_result *out);
/* 1=timeout suspended, 0=no timeout/idle, -1=bad time, -2=store blocked. */
int sm613_poll(struct sm613_context *ctx,uint64_t now_us,struct sm613_result *out);
/* Retries ONLY the frozen failed sink event, never arms or consumes a frame. */
int sm613_retry(struct sm613_context *ctx,struct sm613_result *out);
#endif
