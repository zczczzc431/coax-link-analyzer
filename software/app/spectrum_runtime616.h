#ifndef SPECTRUM_RUNTIME616_H
#define SPECTRUM_RUNTIME616_H
#include "spectrum_checkpoint615.h"

/* Single acquisition owner, borrowed callbacks, no reentry. FFT receives this runtime's
 * copied unsigned 14-bit raw frame, never fitted/averaged display samples.
 * Write exactly bins peak-amplitude CODE values, return 0 only on success. */
typedef int (*sm616_fft)(void *,const uint32_t *,size_t,double *,size_t);
typedef int (*sm616_checkpoint)(void *,const struct sm615_view *);
/* Checkpoint 0=durable, SM613_SINK_QUEUED=immutable ownership transfer.
 * Other results freeze the exact ACTIVE generation for retry. Async writers
 * own durable accounting separately; this scheduler only reserves generation
 * numbers and never advances durable counts on a queued return. */
struct sm616_callbacks {
 sm616_fft fft; void *fft_user;
 sm613_sink closed; void *closed_user;
 sm616_checkpoint checkpoint; void *checkpoint_user;
};
enum sm616_status { SM616_OK,SM616_IDLE,SM616_DUPLICATE,SM616_OWNER_STOP,
 SM616_BAD_INPUT,SM616_BAD_TIME,SM616_FFT_ERROR,SM616_EVIDENCE_ERROR,
 SM616_CHECKPOINT_ERROR,SM616_GENERATION_LIMIT };
struct sm616_result {
 struct sm613_result evidence;
 enum sm616_status status;
 unsigned fft_calls,checkpoints; /* Callback ATTEMPTS this call, even failures;
                                 * never durable-success or lifetime totals. */
 unsigned queued_checkpoints; /* Accepted immutable handoffs this call. */
};
struct sm616_runtime {
 struct sm613_context capture;
 struct sm616_callbacks cb;
 uint64_t period_us;
 /* Last accepted reservation controls cadence/unique keys. With a queued sink
  * these fields do NOT establish that a checkpoint is durable. */
 uint64_t cp_reserved_session,cp_reserved_capture,cp_reserved_generation,cp_reserved_us;
 int have_token,pending,exhausted;
 uint32_t last_token;
 struct sm612_identity token_identity;
 uint32_t raw[SM613_MAX_SAMPLES];
 double peak[SM612_MAX_BINS];
 /* Frozen ACTIVE copy survives the subsequent real SUSPEND closure. */
 struct sm613_event pending_event;
 struct sm615_view pending_view;
};
/* Large, non-copyable object (pending_view points inside it): static/heap only.
 * period_us>0; no filesystem/MMIO/thread/automatic learning or arming here. */
int sm616_init(struct sm616_runtime *,const struct sm616_callbacks *,uint64_t period_us);
int sm616_learn(struct sm616_runtime *,const struct sm612_config *,const struct sm612_identity *);
int sm616_arm(struct sm616_runtime *,const struct sm612_identity *,uint64_t now_us);
/* eligible=0 means acquisition/DAC/source ownership lost: stop BEFORE any FFT.
 * Identity must be live and truthful. Same adjacent token+identity is a render
 * duplicate: poll only, no FFT/feed/watch extension. Does not promise a global
 * token cache. Return 0=accepted/idle/duplicate, 1=new timeout, -1=reject,
 * -2=storage backpressure/limit. Inspect status AND capture state, not rc=PASS. */
int sm616_process(struct sm616_runtime *,const struct sm612_identity *,uint32_t token,
 uint64_t now_us,const uint32_t *,size_t count,int valid,int overload,int eligible,
 struct sm616_result *);
int sm616_poll(struct sm616_runtime *,uint64_t now_us,struct sm616_result *);
int sm616_stop(struct sm616_runtime *,int suspended,uint64_t now_us,struct sm616_result *);
/* Pending ACTIVE first, then any separately blocked CLOSED record. No ARM,
 * FFT/feed, clock update or new checkpoint generation on retry. */
int sm616_retry(struct sm616_runtime *,struct sm616_result *);
#endif
