#include "spectrum_runtime616.h"
#include <math.h>
#include <string.h>

static void result_init(const struct sm616_runtime *r,struct sm616_result *out) {
 memset(out,0,sizeof *out);
 if(r) {
  out->evidence.measurement.state=r->capture.core.state;
  out->evidence.measurement.reason=r->capture.core.reason;
 }
}
static int blocked(const struct sm616_runtime *r) {
 return r->pending || r->capture.blocked || r->exhausted;
}
static int block_result(const struct sm616_runtime *r,struct sm616_result *out) {
 out->status=r->pending?SM616_CHECKPOINT_ERROR:
  r->exhausted?SM616_GENERATION_LIMIT:SM616_EVIDENCE_ERROR;
 out->evidence.status=(enum sm613_status)r->capture.blocked;
 return -2;
}
static int engaged(const struct sm616_runtime *r) {
 enum sm612_state s=r->capture.core.state;
 return s==SM612_LEARNING || s==SM612_ARMED || s==SM612_CANDIDATE || s==SM612_ALARM;
}
static void reserve_accepted(struct sm616_runtime *r) {
 r->cp_reserved_session=r->pending_event.limits.identity.session;
 r->cp_reserved_capture=r->pending_event.capture_id;
 r->cp_reserved_generation=r->pending_view.generation;
 r->cp_reserved_us=r->pending_view.snapshot_us;
 r->pending=0;
}
/* One feed may close an old event and start another at a gap boundary.
 * A subsequent checkpoint failure closes the new event too. Preserve the
 * earlier ownership/durable receipts when stop initializes its own result. */
static void suspend_preserving_handoffs(struct sm616_runtime *r,uint64_t now,
                                       struct sm616_result *out) {
 unsigned published=out->evidence.published,queued=out->evidence.queued;
 (void)sm613_stop(&r->capture,1,now,&out->evidence);
 out->evidence.published+=published;out->evidence.queued+=queued;
}
static int checkpoint(struct sm616_runtime *r,uint32_t token,uint64_t now,
                      struct sm616_result *out) {
 const struct sm613_event *e=&r->capture.event;
 if(!r->capture.active || !e->confirmed) return 0;
 int same=e->capture_id==r->cp_reserved_capture && e->limits.identity.session==r->cp_reserved_session;
 if(same && now-r->cp_reserved_us<r->period_us) return 0;
 if(same && r->cp_reserved_generation==UINT64_MAX) {
  r->exhausted=1;
  suspend_preserving_handoffs(r,now,out);
  out->status=SM616_GENERATION_LIMIT; return -2;
 }
 r->pending_event=*e;
 r->pending_view.event=&r->pending_event;
 r->pending_view.generation=same?r->cp_reserved_generation+1u:1u;
 r->pending_view.snapshot_us=now;
 r->pending_view.observed_us=now; r->pending_view.observed_token=token;
 r->pending=1;
 out->checkpoints++;
 int cp_rc=r->cb.checkpoint(r->cb.checkpoint_user,&r->pending_view);
 if(cp_rc!=0 && cp_rc!=SM613_SINK_QUEUED) {
  /* Only the capture event is CLOSED; pending_event stays truly ACTIVE. */
  suspend_preserving_handoffs(r,now,out);
  out->status=SM616_CHECKPOINT_ERROR; return -2;
 }
 if(cp_rc==SM613_SINK_QUEUED) out->queued_checkpoints++;
 reserve_accepted(r); return 0;
}
int sm616_init(struct sm616_runtime *r,const struct sm616_callbacks *cb,uint64_t period) {
 if(!r || !cb || !cb->fft || !cb->closed || !cb->checkpoint || !period) return -1;
 memset(r,0,sizeof *r); r->cb=*cb; r->period_us=period;
 return sm613_init(&r->capture,cb->closed,cb->closed_user);
}
int sm616_learn(struct sm616_runtime *r,const struct sm612_config *c,const struct sm612_identity *id) {
 if(!r || !r->cb.fft || blocked(r)) return -1;
 return sm613_learn(&r->capture,c,id);
}
int sm616_arm(struct sm616_runtime *r,const struct sm612_identity *id,uint64_t now) {
 if(!r || !r->cb.fft || blocked(r)) return -1;
 return sm613_arm(&r->capture,id,now);
}
int sm616_poll(struct sm616_runtime *r,uint64_t now,struct sm616_result *out) {
 if(!out) return -1;
 result_init(r,out);
 if(!r || !r->cb.fft) {out->status=SM616_BAD_INPUT;return -1;}
 if(blocked(r)) return block_result(r,out);
 int rc=sm613_poll(&r->capture,now,&out->evidence);
 if(rc<0) out->status=rc==-2?SM616_EVIDENCE_ERROR:SM616_BAD_TIME;
 return rc;
}
int sm616_stop(struct sm616_runtime *r,int suspended,uint64_t now,struct sm616_result *out) {
 if(!out) return -1;
 result_init(r,out);
 if(!r || !r->cb.fft) {out->status=SM616_BAD_INPUT;return -1;}
 if(blocked(r)) return block_result(r,out);
 int rc=sm613_stop(&r->capture,suspended,now,&out->evidence);
 if(rc<0) out->status=rc==-2?SM616_EVIDENCE_ERROR:SM616_BAD_TIME;
 return rc;
}
int sm616_process(struct sm616_runtime *r,const struct sm612_identity *id,uint32_t token,
 uint64_t now,const uint32_t *raw,size_t n,int valid,int overload,int eligible,
 struct sm616_result *out) {
 if(!out) return -1;
 result_init(r,out);
 if(!r || !r->cb.fft || !id) {out->status=SM616_BAD_INPUT;return -1;}
 if(blocked(r)) return block_result(r,out);
 if(now<r->capture.clock_us) {out->status=SM616_BAD_TIME;return -1;}
 if(eligible!=1) {
  int rc=sm613_stop(&r->capture,1,now,&out->evidence);
  out->status=rc==-2?SM616_EVIDENCE_ERROR:rc<0?SM616_BAD_TIME:SM616_OWNER_STOP;
  return rc;
 }
 if(!engaged(r)) {out->status=SM616_IDLE;return 0;}
 if(r->have_token && token==r->last_token && sm612_identity_equal(id,&r->token_identity)) {
  int rc=sm613_poll(&r->capture,now,&out->evidence);
  out->status=rc==-2?SM616_EVIDENCE_ERROR:rc<0?SM616_BAD_TIME:SM616_DUPLICATE;
  return rc;
 }
 int shape=raw && sm612_identity_equal(id,id) && n==id->fft_size;
 if(shape) for(size_t k=0;k<n;k++) if(raw[k]>16383u) {shape=0;break;}
 if(shape) memcpy(r->raw,raw,n*sizeof *raw);
 size_t bins=sm612_identity_equal(id,id)?id->fft_size/2u+1u:0;
 for(size_t k=0;k<SM612_MAX_BINS;k++) r->peak[k]=NAN;
 struct sm612_frame f={*id,token,now,r->peak,bins,valid==1,overload!=0};
 int fft_bad=0;
 if(shape && valid==1 && overload==0 && sm612_identity_equal(id,&r->capture.core.identity)) {
  out->fft_calls=1;
  if(r->cb.fft(r->cb.fft_user,r->raw,n,r->peak,bins)!=0) {f.valid=0;fft_bad=1;}
  for(size_t k=0;k<bins;k++) {
   double q=r->peak[k]/8191.0;
   if(!isfinite(r->peak[k]) || r->peak[k]<0 || !isfinite(q*q)) {f.valid=0;fft_bad=1;}
  }
 } else if(!shape || valid!=1 || (overload!=0 && overload!=1)) f.valid=0;
 r->have_token=1; r->last_token=token; r->token_identity=*id;
 int rc=sm613_feed(&r->capture,&f,shape?r->raw:NULL,shape?n:0,&out->evidence);
 if(rc<0) {
  out->status=rc==-2?SM616_EVIDENCE_ERROR:fft_bad?SM616_FFT_ERROR:SM616_BAD_INPUT;
  return rc;
 }
 if(checkpoint(r,token,now,out)!=0) return -2;
 return rc;
}
int sm616_retry(struct sm616_runtime *r,struct sm616_result *out) {
 if(!out) return -1;
 result_init(r,out);
 if(!r || !r->cb.fft) {out->status=SM616_BAD_INPUT;return -1;}
 if(r->pending) {
  out->checkpoints++;
  int cp_rc=r->cb.checkpoint(r->cb.checkpoint_user,&r->pending_view);
  if(cp_rc!=0 && cp_rc!=SM613_SINK_QUEUED) {
   out->status=SM616_CHECKPOINT_ERROR;return -2;
  }
  if(cp_rc==SM613_SINK_QUEUED) out->queued_checkpoints++;
  reserve_accepted(r);
 } else if(!r->capture.blocked) {
  out->status=r->exhausted?SM616_GENERATION_LIMIT:SM616_IDLE;
  return r->exhausted?-2:-1;
 }
 if(r->capture.blocked) {
  int rc=sm613_retry(&r->capture,&out->evidence);
  if(rc!=0) {out->status=SM616_EVIDENCE_ERROR;return rc;}
 }
 return 0;
}
