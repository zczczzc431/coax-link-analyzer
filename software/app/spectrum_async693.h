#ifndef SPECTRUM_ASYNC693_H
#define SPECTRUM_ASYNC693_H

#include "spectrum_checkpoint615.h"
#include "mask655/mask_audit655.h"

#define SM693_MAX_SLOTS 4u

enum sm693_kind { SM693_CLOSED = 1, SM693_ACTIVE = 2 };
enum sm693_rc {
    SM693_OK = 0, SM693_INVALID = -1, SM693_FULL = -2,
    SM693_BLOCKED = -3, SM693_OVERFLOW = -4, SM693_BUSY = -5,
    SM693_THREAD = -6
};

/* Immutable throughout a writer call, including retries. ACTIVE view.event is
 * rebound to this job's event; CLOSED view is zero. seq is a queue sequence,
 * never a replacement for the event's original persistent capture key. */
struct sm693_job {
    uint64_t seq;
    enum sm693_kind kind;
    struct sm613_event event;
    struct sm612_model audit_model;
    struct sm655_receipt receipt;
    struct sm615_view view;
};

/* Return zero ONLY after the original audit/bind/store durability contract has
 * succeeded. A nonzero result freezes this exact job until explicit retry.
 * Called by the sole worker, without the queue mutex. Do not call queue APIs,
 * mutate this job, or touch ADC/DAC/UI from this callback. */
typedef int (*sm693_writer)(void *user, const struct sm693_job *job);

struct sm693_completion {
    uint64_t seq,session,capture_id,event_id,generation;
    enum sm693_kind kind;
    int confirmed;
    uint32_t first_token;
    unsigned worst_bin,end;
    int worst_direction;
    uint64_t end_us;
};
struct sm693_status {
    unsigned capacity,occupied,pending,inflight,failed,completed;
    uint64_t submitted,durable,next_seq,failed_seq;
    int writer_rc;
};
struct sm693_queue;

/* All public operations belong to the creating thread. The caller must keep
 * user alive until destroy succeeds. One calloc reserves queue and all slots;
 * submit, writer dispatch, poll and retry allocate no memory. first_seq must be
 * nonzero. Reaching UINT64_MAX consumes the last sequence and next_seq becomes
 * zero; subsequent submissions fail without changing the output sequence. */
struct sm693_queue *sm693_create(unsigned capacity,uint64_t first_seq,
                               sm693_writer writer,void *user);
/* Zero means a deep-copied job was QUEUED, never that it was persisted. On any
 * failure no slot, counter or output seq is changed. Pending + inflight +
 * failed + completed equals occupied; completed jobs retain slots until poll.
 * Any FAILED job blocks new submits and all later writer dispatches. */
int sm693_submit_closed(struct sm693_queue *queue,const struct sm613_event *event,
                       const struct sm612_model *audit_model,
                       const struct sm655_receipt *receipt,uint64_t *seq);
int sm693_submit_active(struct sm693_queue *queue,const struct sm615_view *view,
                       const struct sm612_model *audit_model,
                       const struct sm655_receipt *receipt,uint64_t *seq);
/* 1 = oldest durable completion returned and slot released, 0 = none ready.
 * No failed/pending job can be returned as a successful completion. */
int sm693_poll(struct sm693_queue *queue,struct sm693_completion *out);
int sm693_get_status(struct sm693_queue *queue,struct sm693_status *out);
/* Only the precise failed_seq may retry; preserves original bytes and FIFO. */
int sm693_retry(struct sm693_queue *queue,uint64_t failed_seq);
/* Wait until no writer work remains, or return BLOCKED on a failed job. Does
 * not poll completions or erase a failed job. A callback must eventually return. */
int sm693_drain(struct sm693_queue *queue);
/* BUSY when ANY slot is occupied, including durable unpolled completions.
 * On success joins the idle worker and frees queue; on refusal queue survives. */
int sm693_destroy(struct sm693_queue *queue);

#endif
