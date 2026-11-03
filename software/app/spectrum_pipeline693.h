#ifndef SPECTRUM_PIPELINE693_H
#define SPECTRUM_PIPELINE693_H
#include "spectrum_async693.h"

struct sm693_pipeline;
enum sm693_write_error {
 SM693_AUDIT_CHECK_ERROR=-1001,SM693_AUDIT_BIND_ERROR=-1002,
 SM693_CLOSED_ERROR_BASE=-1100,SM693_ACTIVE_ERROR_BASE=-1200
};
struct sm693_pipeline_status {
 struct sm693_status queue;
 int producer_blocked,last_submit_rc,have_template;
 enum sm693_kind blocked_kind;
};
/* Owner thread only. Existing dedicated private dirs, no mkdir or deletion.
 * Stores belong exclusively to the worker until a successful destroy. */
struct sm693_pipeline *sm693_pipeline_create(const char *closed_dir,const char *active_dir,
 const char *audit_dir,const char *refs_dir,unsigned capacity);
/* New learning is admitted only after all old receipts are consumed and the
 * producer gate is empty. Failed provenance revokes the current template. */
int sm693_pipeline_template(struct sm693_pipeline *,const struct sm612_model *,
 const struct sm655_receipt *);
/* sm616 callback adapters: 693=complete copy transferred, never durable 0.
 * Failed ACTIVE submission gates CLOSED even if a slot becomes free; only an
 * exact byte-identical retry clears that gate. Failure takes no ownership.
 * The caller/runtime retains rejected ACTIVE and CLOSED snapshots separately.
 * Retry ACTIVE then CLOSED after polling completed slots; no automatic retry. */
int sm693_pipeline_closed(void *,const struct sm613_event *);
int sm693_pipeline_checkpoint(void *,const struct sm615_view *);
int sm693_pipeline_poll(struct sm693_pipeline *,struct sm693_completion *);
int sm693_pipeline_status(struct sm693_pipeline *,struct sm693_pipeline_status *);
int sm693_pipeline_retry_failed(struct sm693_pipeline *);
/* drain waits worker I/O then reports BUSY if producer still owns frozen RAW.
 * poll receipts and retry the runtime before destroy; no implicit drops. */
int sm693_pipeline_drain(struct sm693_pipeline *);
int sm693_pipeline_destroy(struct sm693_pipeline *);
/* Read-only inventory after all writer work has stopped; no live-store race. */
int sm693_pipeline_scan(struct sm693_pipeline *,struct sm614_scan *,struct sm614_scan *);
#ifdef SM693_TESTING
/* May change fault injection only when worker has no pending/inflight job. */
int sm693_pipeline_inject(struct sm693_pipeline *,enum sm693_kind,enum sm614_fault,uint64_t);
/* Deterministic offline slow-writer barrier. Set only on an empty queue;
 * callback must not use pipeline/queue APIs. Absent from production builds. */
int sm693_pipeline_test_hook(struct sm693_pipeline *,void (*)(void *,const struct sm693_job *),void *);
#endif
#endif
