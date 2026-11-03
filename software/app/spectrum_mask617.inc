/* Included after the production FFT. No new ADC or DAC access. */
#include "mask_audit655.h"
#include "mask_learn_diag687.h"
#ifdef MONITOR_ASYNC694
#include "spectrum_pipeline693.h"
#endif
static struct sm655_receipt mask655_receipt;
static struct sm612_model *mask655_pending_model;
static struct sm612_model *mask655_audit_model;
static char mask655_audit_root[PATH_MAX],mask655_refs_root[PATH_MAX];
static uint64_t mask655_learn_revision;
static uint32_t mask655_learn_token;
static int mask655_pending,mask655_authorized,mask655_failed,mask655_invalidated;
static struct sm616_runtime mask617_runtime;
static struct sm616_result mask617_result;
static struct sm612_config mask617_config;
static struct sm614_store mask617_closed_store;
static struct sm615_store mask617_active_store;
static int mask617_ready,mask617_panel,mask617_clock_bad,mask617_was_eligible;
static uint64_t mask617_session,mask617_source_revision=1,mask617_command_revision;
static unsigned mask617_closed_count,mask686_confirmed,mask686_transient;
static struct m686_cadence monitor686_cadence;
static int monitor686_was_fast;
static double monitor686_next_paint;
static void mask617_source_changed(void);
static int monitor686_fast(void) {
 return m686_eligible(diag_ui_state.mode==DIAG_UI_DIAG && diag_ui_state.page==DIAG_PAGE_MONITOR,
                     paused,single_pending,sweep.active,reflect.active,reflect_ps_exclusive());
}
static void monitor686_tick(void) {
 int fast=monitor686_fast();
 if(fast!=monitor686_was_fast) {
  mask617_source_changed(); /* FREE-vs-SCOPE owner transition needs a new LEARN. */
  memset(&monitor686_cadence,0,sizeof monitor686_cadence);
  monitor686_next_paint=0;monitor686_was_fast=fast;
 }
}
static int mask686_page(void) {
 return (diag_ui_state.mode==DIAG_UI_SCOPE && tool_page==3) ||
        (diag_ui_state.mode==DIAG_UI_DIAG && diag_ui_state.page==DIAG_PAGE_MONITOR);
}
static const char *mask686_state_name(void) {
 static const char *names[]={"OFF","LEARN","READY","ARMED","CANDIDATE","ALARM","INVALID","SUSPENDED"};
 unsigned s=(unsigned)mask617_runtime.capture.core.state;
 return mask617_ready && s<8?names[s]:"UNAVAILABLE";
}
static char mask617_notice[112]="UNAVAILABLE - STORAGE NOT OPEN";
struct mask617_request_state {
 struct sm612_identity id;
 uint64_t command,source;
 sig_atomic_t control;
 unsigned trigger;
 uint32_t token;
 int eligible;
};
static struct mask617_request_state mask617_request;
static uint32_t mask617_fft_token;
#ifndef MASK617_NOW
static uint64_t mask617_clock(void) {
 struct timespec t;
 if(clock_gettime(CLOCK_MONOTONIC,&t)!=0 || t.tv_sec<0 || t.tv_nsec<0 || t.tv_nsec>=1000000000L) return UINT64_MAX;
 uint64_t sec=(uint64_t)t.tv_sec,us=(uint64_t)t.tv_nsec/1000u;
 return sec>(UINT64_MAX-us)/1000000u?UINT64_MAX:sec*1000000u+us;
}
#define MASK617_NOW() mask617_clock()
#endif
static void mask617_note(const char *message) {
 snprintf(mask617_notice,sizeof mask617_notice,"%s",message);view_dirty=1;
}
static uint64_t mask617_now(void) {
 uint64_t now=MASK617_NOW();
 if(now==UINT64_MAX) {mask655_authorized=0;mask655_invalidated=1;mask617_clock_bad=1;mask617_note("CLOCK ERROR - MONITORING DISABLED");}
 return now;
}
static int mask617_owner_eligible(void) {
 return mask686_page() && !paused && !single_pending &&
  !sweep.active && !reflect.active && !reflect_ps_exclusive();
}
static struct sm612_identity mask617_identity(void) {
 struct sm612_identity id={mask617_session,
  ((uint64_t)(uint32_t)control_generation<<32)|(uint32_t)trigger_revision,
  mask617_source_revision,ADC_SAMPLE_RATE_HZ,N,SM612_FORMAT};
 return id;
}
static int mask617_fft(void *unused,const uint32_t *raw,size_t count,double *peak,size_t bins) {
 (void)unused;
 uint64_t start690=p690_now();
 if(count!=N || bins!=N/2+1u || spectrum_ensure(raw,mask617_fft_token)!=0) {
  p690_end(P690_FFT,start690);return -1;
 }
 memcpy(peak,spectrum_cache.code,bins*sizeof *peak);p690_end(P690_FFT,start690);return 0;
}
#ifdef MONITOR_ASYNC694
#include "spectrum_async_ui694.inc"
#endif
static int mask617_closed(void *unused,const struct sm613_event *event) {
 (void)unused;
#ifdef MONITOR_ASYNC694
 if(mask694_selected)return sm693_pipeline_closed(mask694_pipeline,event);
#endif
 uint64_t part690=p690_now();
 int rc690=!mask655_audit_model || sm655_audit_check(mask655_audit_root,mask655_audit_model,&mask655_receipt)!=0;
 p690_end(P690_AUDIT,part690);if(rc690)return -1;
 part690=p690_now();rc690=sm655_audit_bind(mask655_refs_root,event,&mask655_receipt);
 p690_end(P690_BIND,part690);if(rc690)return -1;
 part690=p690_now();rc690=sm614_save(&mask617_closed_store,event);
 p690_end(P690_CLOSED,part690);if(rc690)return -1;
 mask617_closed_count++;
 if(event->confirmed) mask686_confirmed++; else mask686_transient++;
 printf("MONITOR_SPECTRUM_RECORD CLASS=%s TOKEN=%u BIN=%u DIR=%d RAW_RETAINED=1\n",
        event->confirmed?"CONFIRMED":"UNCONFIRMED",event->first.token,
        event->worst_decision.worst_bin,event->worst_decision.worst_direction);
 printf("MASK_CLOSED SESSION=%llu CAPTURE=%llu EVENT=%llu CONFIRMED=%d END=%u HOST_END_US=%llu\n",
  (unsigned long long)event->limits.identity.session,(unsigned long long)event->capture_id,
  (unsigned long long)event->event_id,event->confirmed,(unsigned)event->end,(unsigned long long)event->end_us);
 return 0;
}
static int mask617_checkpoint(void *unused,const struct sm615_view *view) {
 (void)unused;
#ifdef MONITOR_ASYNC694
 if(mask694_selected)return sm693_pipeline_checkpoint(mask694_pipeline,view);
#endif
 uint64_t part690=p690_now();
 int rc690=!mask655_audit_model || sm655_audit_check(mask655_audit_root,mask655_audit_model,&mask655_receipt)!=0;
 p690_end(P690_AUDIT,part690);if(rc690)return -1;
 part690=p690_now();rc690=sm655_audit_bind(mask655_refs_root,view->event,&mask655_receipt);
 p690_end(P690_BIND,part690);if(rc690)return -1;
 part690=p690_now();rc690=sm615_save(&mask617_active_store,view);
 p690_end(P690_CHECKPOINT,part690);return rc690;
}
static int mask617_private_dir(const char *path) {
 struct stat st;
 if(mkdir(path,0700)!=0 && errno!=EEXIST) return -1;
 return lstat(path,&st)!=0 || !S_ISDIR(st.st_mode) || st.st_uid!=geteuid() || (st.st_mode&0777)!=0700?-1:0;
}
static int mask617_open(const char *root,uint64_t session) {
 char closed[PATH_MAX],active[PATH_MAX];struct sm614_scan scan;
 if(mask655_pending || mask617_ready || !root || !session || mask617_clock_bad || mask617_runtime.capture.active ||
    mask617_runtime.pending || mask617_runtime.capture.blocked) return -1;
 mask617_closed_store.dirfd=mask617_closed_store.lockfd=-1;
 mask617_active_store.files.dirfd=mask617_active_store.files.lockfd=-1;
 int a=snprintf(closed,sizeof closed,"%s/closed",root),b=snprintf(active,sizeof active,"%s/active",root);
 if(a<0 || b<0 || (size_t)a>=sizeof closed || (size_t)b>=sizeof active ||
    mask617_private_dir(root)!=0 || mask617_private_dir(closed)!=0 || mask617_private_dir(active)!=0) goto fail;
 int ar=snprintf(mask655_audit_root,sizeof mask655_audit_root,"%s/audit655",root);
 int rr=snprintf(mask655_refs_root,sizeof mask655_refs_root,"%s/refs655",root);
 if(ar<0 || rr<0 || (size_t)ar>=sizeof mask655_audit_root || (size_t)rr>=sizeof mask655_refs_root) goto fail;
#ifdef MONITOR_ASYNC694
 if(mask694_selected) {
  mask694_pipeline=sm693_pipeline_create(closed,active,mask655_audit_root,mask655_refs_root,mask694_capacity);
  if(!mask694_pipeline)goto fail;
  mask694_error=mask694_retrying=0;mask694_checkpoints_saved=0;
  memset(&mask694_store_state,0,sizeof mask694_store_state);
  (void)mask694_refresh();
 } else
#endif
 if(sm614_open(&mask617_closed_store,closed,128u,UINT64_C(33554432))!=0 ||
    sm615_open(&mask617_active_store,active,64u,UINT64_C(33554432))!=0 ||
    sm614_scan(&mask617_closed_store,&scan)!=0 || sm615_scan(&mask617_active_store,&scan)!=0) goto fail;
 struct sm616_callbacks cb={mask617_fft,NULL,mask617_closed,NULL,mask617_checkpoint,NULL};
 if(sm616_init(&mask617_runtime,&cb,UINT64_C(2000000))!=0) goto fail;
 sm612_defaults(&mask617_config);
 mask617_session=session;mask617_source_revision=1;mask617_command_revision=0;
 mask617_clock_bad=0;mask617_closed_count=0;mask686_confirmed=mask686_transient=0;memset(&mask617_request,0,sizeof mask617_request);
 memset(&mask655_receipt,0,sizeof mask655_receipt);
 free(mask655_audit_model);mask655_audit_model=NULL;
 mask655_learn_revision=0;mask655_learn_token=0;mask655_authorized=mask655_failed=mask655_invalidated=0;
 mask617_ready=1;mask617_was_eligible=mask617_owner_eligible();
 mask617_note("READY FOR LEARN - OUTPUT NOT MODIFIED");return 0;
fail:
#ifdef MONITOR_ASYNC694
 if(mask694_pipeline && sm693_pipeline_destroy(mask694_pipeline)==0)mask694_pipeline=NULL;
#endif
 sm614_close(&mask617_closed_store);sm615_close(&mask617_active_store);
 mask617_note("UNAVAILABLE - PRIVATE STORAGE/INTEGRITY ERROR");return -1;
}
static int mask617_boot(void) {
 uint64_t session=0;size_t got=0;int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
 if(fd<0) {mask617_note("UNAVAILABLE - SESSION SOURCE ERROR");return -1;}
 while(got<sizeof session) {
  ssize_t n=read(fd,(unsigned char *)&session+got,sizeof session-got);
  if(n<0 && errno==EINTR) continue;
  if(n<=0) break;
  got+=(size_t)n;
 }
 int close_rc=close(fd);
 if(got!=sizeof session || !session || close_rc!=0) {mask617_note("UNAVAILABLE - SESSION SOURCE ERROR");return -1;}
 return mask617_open(DIAG_STORAGE_DIR "/mask617",session);
}
static int mask617_engaged(void) {
 enum sm612_state s=mask617_runtime.capture.core.state;
 return s==SM612_LEARNING || s==SM612_ARMED || s==SM612_CANDIDATE || s==SM612_ALARM;
}
/* Explicit analysis range, never an automatic exemption for an unstable bin.
 * Exact limits are already serialized in TEMPLATE655 and event CSV CONFIG. */
static int mask688_select_band(unsigned last_bin) {
 if(last_bin!=512 && last_bin!=614) return -1;
#ifdef MONITOR_ASYNC694
 if(mask694_busy()) {mask617_note("BAND REFUSED - QUEUED EVIDENCE / RETRY FIRST");return -1;}
#endif
 if(!mask617_ready || mask617_clock_bad || mask655_pending || mask655_failed ||
    mask617_runtime.pending || mask617_runtime.capture.blocked || mask617_runtime.capture.active ||
    mask617_engaged()) {
  mask617_note("BAND REFUSED - STOP/FINISH EVIDENCE OR RETRY FIRST");return -1;
 }
 if(mask617_config.last_bin==last_bin) return 0;
 if(mask617_source_revision==UINT64_MAX || mask617_command_revision==UINT64_MAX) {
  mask617_note("BAND REFUSED - REVISION EXHAUSTED");return -1;
 }
 mask617_config.last_bin=last_bin;
 mask617_source_revision++;mask617_command_revision++;
 mask655_authorized=0;mask655_invalidated=1;
 mask617_runtime.capture.core.template_valid=0;
 mask617_runtime.capture.core.state=SM612_NO_TEMPLATE;
 mask617_runtime.capture.core.reason=SM612_MANUAL_STOP;
 sd687_clear();
 mask617_note("BAND CHANGED - LEARN REQUIRED / OUT-OF-BAND NOT TESTED");
 printf("MASK688_BAND FIRST_BIN=%u LAST_BIN=%u LOW_HZ=%.17g HIGH_HZ=%.17g RELEARN_REQUIRED=1 OUT_OF_BAND_NOT_TESTED=1\n",
  mask617_config.first_bin,last_bin,mask617_config.first_bin*FFT_BIN_HZ,last_bin*FFT_BIN_HZ);
 return 0;
}
/* Explicit user-selected minimum reliable spectral-bin level, not an ADC
 * voltage calibration or a measured noise floor. Defaults remain unchanged. */
static int mask689_select_level(double floor_db) {
 if(floor_db!=-100 && floor_db!=-60) return -1;
#ifdef MONITOR_ASYNC694
 if(mask694_busy()) {mask617_note("LEVEL REFUSED - QUEUED EVIDENCE / RETRY FIRST");return -1;}
#endif
 if(!mask617_ready || mask617_clock_bad || mask655_pending || mask655_failed ||
    mask617_runtime.pending || mask617_runtime.capture.blocked || mask617_runtime.capture.active ||
    mask617_engaged()) {
  mask617_note("LEVEL REFUSED - STOP/FINISH EVIDENCE OR RETRY FIRST");return -1;
 }
 if(mask617_config.floor_db==floor_db) return 0;
 if(mask617_source_revision==UINT64_MAX || mask617_command_revision==UINT64_MAX) {
  mask617_note("LEVEL REFUSED - REVISION EXHAUSTED");return -1;
 }
 mask617_config.floor_db=floor_db;
 mask617_source_revision++;mask617_command_revision++;
 mask655_authorized=0;mask655_invalidated=1;
 mask617_runtime.capture.core.template_valid=0;
 mask617_runtime.capture.core.state=SM612_NO_TEMPLATE;
 mask617_runtime.capture.core.reason=SM612_MANUAL_STOP;
 sd687_clear();
 mask617_note("LEVEL CHANGED - RELEARN / BELOW-LEVEL DETAILS NOT TESTED");
 printf("MASK689_LEVEL FLOOR_DBFS=%.0f MIN_PEAK_CODE=%.17g LOWER_QUALIFY_DBFS=%.0f RELEARN_REQUIRED=1 BELOW_LEVEL_NOT_TESTED=1\n",
  floor_db,8191.0*pow(10.0,floor_db/20.0),floor_db+mask617_config.lower_signal_margin_db);
 return 0;
}
static void mask617_source_changed(void) {
 if(!mask617_ready) return;
 mask655_authorized=0;
 mask655_invalidated=1;
 if(mask617_source_revision==UINT64_MAX) {mask617_clock_bad=1;mask617_note("SOURCE REVISION EXHAUSTED - DISABLED");return;}
 mask617_source_revision++;
 uint64_t now=mask617_now();
 if(now!=UINT64_MAX && !mask617_clock_bad && mask617_engaged())
  (void)sm616_stop(&mask617_runtime,1,now,&mask617_result);
 if(!mask617_clock_bad) mask617_note("SOURCE/OWNER CHANGED - RELEARN REQUIRED");
}
static void mask617_tick(void) {
#ifdef MONITOR_ASYNC694
 if(mask694_pump()!=0)return;
#endif
 if(!mask617_ready || mask617_clock_bad) return;
 uint64_t now=mask617_now();if(now==UINT64_MAX) return;
 int eligible=mask617_owner_eligible();
 if(mask617_was_eligible && !eligible) {
  /* source_changed samples a newer real clock. Never poll with the older now
   * captured above it, or a valid transition becomes a false BAD_TIME. */
  mask617_source_changed();mask617_was_eligible=0;return;
 }
 mask617_was_eligible=eligible;
 struct sm612_identity id=mask617_identity();
 if(mask617_runtime.capture.core.template_valid && (!eligible || !sm612_identity_equal(&id,&mask617_runtime.capture.core.identity))) {
  mask655_authorized=0;mask655_invalidated=1;
 }
 if(mask617_engaged() && (!eligible || !sm612_identity_equal(&id,&mask617_runtime.capture.core.identity))) {
  (void)sm616_stop(&mask617_runtime,1,now,&mask617_result);
  mask617_note("CONTROL/OWNER CHANGED - EXPLICIT REARM REQUIRED");return;
 }
 enum sm612_state before=mask617_runtime.capture.core.state;
 int rc=sm616_poll(&mask617_runtime,now,&mask617_result);
 if(rc==1) mask617_note("NO ACCEPTED FRAMES - TIMEOUT SUSPENDED");
 else if(rc<0) mask617_note("STORAGE/TIME ERROR - RETRY REQUIRED");
 else if(before!=mask617_runtime.capture.core.state) view_dirty=1;
}

/* Retry uses the immutable failed LEARN snapshot, never live/new captures.
 * No automatic ARM. Historical receipts remain valid for old event closure.
 */
static int mask655_commit_pending(void) {
 if(!mask655_pending || !mask655_pending_model) return mask655_pending?-1:0;
 struct sm655_receipt receipt;
 if(sm655_audit_write(mask655_audit_root,mask655_pending_model,mask655_learn_revision,mask655_learn_token,&receipt)!=0) {
  mask655_failed=1;mask655_authorized=0;mask617_note("AUDIT ERROR - RETRY REQUIRED / ARM BLOCKED");return -1;
 }
#ifdef MONITOR_ASYNC694
 if(mask694_selected && sm693_pipeline_template(mask694_pipeline,mask655_pending_model,&receipt)!=0) {
  mask655_failed=1;mask655_authorized=0;
  mask617_note("ASYNC TEMPLATE BIND FAILED - RETRY REQUIRED");return -1;
 }
#endif
 mask655_receipt=receipt;mask655_pending=0;mask655_failed=0;
 struct sm612_identity id=mask617_identity();
 mask655_authorized=!mask655_invalidated && !mask617_clock_bad && mask617_owner_eligible() &&
  sm612_identity_equal(&id,&mask655_pending_model->identity) &&
  (mask617_runtime.capture.core.state==SM612_READY || mask617_runtime.capture.core.state==SM612_SUSPENDED);
 free(mask655_audit_model);mask655_audit_model=mask655_pending_model;mask655_pending_model=NULL;
 printf("MASK655_AUDIT COMMITTED LEARN=%llu TOKEN=%u UPPER=%u LOWER=%u SHA=%s TEMPLATE=%s\n",
  (unsigned long long)receipt.learn_command,receipt.learn_token,receipt.upper_count,receipt.lower_count,
  receipt.sha256,receipt.template_sha256);
 return 0;
}

static int mask617_action(int action) {
 if(action==4) {mask617_panel=0;view_dirty=1;return 0;}
 if(!mask617_ready || mask617_clock_bad) {mask617_note("UNAVAILABLE - CHECK STORAGE/CLOCK");return -1;}
#ifdef MONITOR_ASYNC694
 (void)mask694_pump();
 if((action==0 || action==1) && mask694_busy()) {
  mask617_note("LEARN/ARM REFUSED - QUEUED OR FAILED EVIDENCE");return -1;
 }
 if(action==3 && mask694_selected && !mask655_pending) return mask694_retry();
#endif
 uint64_t now=mask617_now();if(now==UINT64_MAX) return -1;
 struct sm612_identity id=mask617_identity();int rc=-1;
 if((action==0 || action==1) && !mask617_owner_eligible()) {
  mask617_note("REFUSED - FFT/MONITOR RUN MUST OWN CAPTURE");return -1;
 }
 if(mask617_command_revision==UINT64_MAX) {mask617_clock_bad=1;mask617_note("COMMAND REVISION EXHAUSTED");return -1;}
 if(action==0) {
  if(mask655_pending || mask655_learn_revision==UINT64_MAX) {
   mask617_note("UNSAVED AUDIT / REVISION EXHAUSTED - LEARN BLOCKED");return -1;
  }
  rc=sm616_learn(&mask617_runtime,&mask617_config,&id);
  if(rc==0) {
   sd687_begin();
   mask655_learn_revision++;mask655_authorized=mask655_failed=mask655_invalidated=0;
   memset(&mask655_receipt,0,sizeof mask655_receipt);free(mask655_audit_model);mask655_audit_model=NULL;
  }
 }
 else if(action==1) {
  const struct sm612_model *m=&mask617_runtime.capture.core;
  if(!mask655_authorized || mask655_pending || mask655_failed || !mask655_receipt.lower_count ||
     !sm612_identity_equal(&id,&m->identity) ||
     sm655_audit_check(mask655_audit_root,m,&mask655_receipt)!=0) {
   mask655_authorized=0;mask617_note("AUDIT/COVERAGE STALE OR FAILED - ARM BLOCKED");return -1;
  }
  rc=sm616_arm(&mask617_runtime,&id,now);
 }
 else if(action==2) rc=sm616_stop(&mask617_runtime,0,now,&mask617_result);
 else if(action==3) {
  int audit_was_pending=mask655_pending;
  rc=mask655_commit_pending();
  if(rc==0 && (!audit_was_pending || mask617_runtime.pending || mask617_runtime.capture.blocked))
   rc=sm616_retry(&mask617_runtime,&mask617_result);
 }
 if((action==0 || action==1) && rc==0) mask617_command_revision++;
 if(action==2) mask617_command_revision++;
 mask617_note(rc<0?"COMMAND REFUSED/FAILED - CHECK STATE AND STORAGE":
  action==0?"LEARNING RAW SPECTRA - NO AUTOMATIC ARM":
  action==1?"ARMED - RAW TEMPLATE LIMITS ACTIVE":
  action==2?"STOPPED - NO AUTOMATIC ARM":"RETRY FINISHED - EXPLICIT ARM REQUIRED");
#ifdef MONITOR_ASYNC694
 if(action==2 && mask694_selected && mask694_busy())
  mask617_note("STOPPED - SAVING QUEUED EVIDENCE / SAVED IS DURABLE ONLY");
#endif
 printf("MASK_ACTION=%d RC=%d STATE=%u REASON=%u PENDING=%d BLOCKED=%d\n",action,rc,
  (unsigned)mask617_runtime.capture.core.state,(unsigned)mask617_runtime.capture.core.reason,
  mask617_runtime.pending,mask617_runtime.capture.blocked);
 if(action==1 && rc==0)p690_start();
 if(action==2)p690_report(stdout,"STOP");
 return rc;
}
static void mask617_request_capture(uint32_t token) {
 mask617_request.token=token;mask617_request.id=mask617_identity();
 mask617_request.command=mask617_command_revision;mask617_request.source=mask617_source_revision;
 mask617_request.control=control_generation;mask617_request.trigger=trigger_revision;
 mask617_request.eligible=mask617_ready && !mask617_clock_bad && mask617_owner_eligible();
}
static int mask617_on_frame(const uint32_t *raw,const struct metrics *v,uint32_t token,int raw_owned) {
 if(!mask617_ready) return 0;
#ifdef MONITOR_ASYNC694
 if(mask694_pump()!=0)return -1;
#endif
 if(mask617_clock_bad || !raw || !v) return -1;
 uint64_t now=mask617_now();if(now==UINT64_MAX) return -1;
 struct sm612_identity id=mask617_identity();
 if(mask617_request.token!=token || mask617_request.command!=mask617_command_revision ||
    mask617_request.source!=mask617_source_revision || mask617_request.control!=control_generation ||
    mask617_request.trigger!=trigger_revision || !sm612_identity_equal(&id,&mask617_request.id)) {
  mask617_note("OLD IN-FLIGHT FRAME NOT FED TO MASK");return -1;
 }
 int eligible=mask617_request.eligible && mask617_owner_eligible() && !raw_owned;
 if(!eligible) {
  mask655_authorized=0;mask655_invalidated=1;
  if(mask617_engaged()) (void)sm616_stop(&mask617_runtime,1,now,&mask617_result);
  return -1;
 }
 mask617_fft_token=token;
 int learning687=mask617_runtime.capture.core.state==SM612_LEARNING;
 if(learning687) sd687_capture(raw,N,token,now);
 int rc=sm616_process(&mask617_runtime,&mask617_request.id,token,now,raw,N,1,v->rail,1,&mask617_result);
 if(learning687 && mask617_runtime.capture.core.state!=SM612_LEARNING)
  sd687_finish(&mask617_runtime.capture.core);
 if(mask617_result.evidence.measurement.flags&SM612_LEARNED) {
  mask655_authorized=0;mask655_pending=1;mask655_learn_token=token;
  mask655_pending_model=malloc(sizeof *mask655_pending_model);
  if(mask655_pending_model) *mask655_pending_model=mask617_runtime.capture.core;
  if(!mask655_pending_model || mask655_commit_pending()!=0) {
   mask655_failed=1;mask617_note("AUDIT ERROR - RETRY REQUIRED / ARM BLOCKED");rc=-1;
  }
 }
 if(rc<0) mask617_note(mask655_pending?"AUDIT ERROR - RETRY REQUIRED / ARM BLOCKED":"MASK QUALITY/STORAGE ERROR - INSPECT STATE");
 else if(mask617_result.evidence.measurement.flags&SM612_GAP)
  mask617_note("OBSERVATION TOKEN/TIME GAP - NOT PHYSICAL FAULT DURATION");
 else if(mask617_result.evidence.measurement.flags&SM612_LEARNED)
  mask617_note("AUDITED TEMPLATE READY - EXPLICIT ARM / PARTIAL LOWER COVERAGE");
 if(tool_page==3) (void)spectrum_ensure(raw,token);
 view_dirty=1;return rc;
}
static int mask617_shutdown(void) {
#ifdef MONITOR_ASYNC694
 if(mask694_selected) {
  int rc694=mask694_shutdown();
  if(rc694==0)sd687_clear();
  return rc694;
 }
#endif
 sd687_clear();
 if(!mask617_ready) {
  if(mask655_pending || mask655_failed || mask617_clock_bad || mask617_runtime.capture.active ||
     mask617_runtime.pending || mask617_runtime.capture.blocked) {
   mask617_note("SHUTDOWN ERROR - UNSAVED RAM EVIDENCE MAY BE LOST");return -1;
  }
  return 0;
 }
 int rc=0;
 if(mask617_clock_bad) rc=-1;
 else if(mask655_commit_pending()!=0) rc=-1;
 else {
  if(mask617_runtime.pending || mask617_runtime.capture.blocked)
   if(sm616_retry(&mask617_runtime,&mask617_result)!=0) rc=-1;
  uint64_t now=mask617_now();
  if(now==UINT64_MAX || rc || sm616_stop(&mask617_runtime,0,now,&mask617_result)!=0) rc=-1;
 }
 if(rc || mask655_pending || mask617_runtime.pending || mask617_runtime.capture.blocked) {
  mask617_note("SHUTDOWN ERROR - UNSAVED RAM EVIDENCE MAY BE LOST");rc=-1;
 }
 sm614_close(&mask617_closed_store);sm615_close(&mask617_active_store);
 mask617_ready=0;return rc;
}
static int mask617_touch(int x,int y,int w,int h) {
 if(!mask686_page()) return 0;
 if(y>=4 && y<34 && x>=185 && x<305) {mask617_panel=1;view_dirty=1;return 1;}
 if(!mask617_panel) return 0;
 if(y>=74 && y<100) {
  if(x>=535 && x<650) {(void)mask689_select_level(-60);return 1;}
  if(x>=660 && x<775) {(void)mask689_select_level(-100);return 1;}
 }
 if(y>=42 && y<70) {
  if(x>=535 && x<650) {(void)mask688_select_band(512);return 1;}
  if(x>=660 && x<775) {(void)mask688_select_band(614);return 1;}
 }
 if(y<38 && x>=320 && x<495) {mask617_panel=0;return 0;}
 if(w==800 && h==480 && y>=375 && y<420 && x>=20 && x<780) {
  int action=(x-20)/152;if((x-20)%152<146) (void)mask617_action(action);
 }
 if(y<38 && x>=w-260) return 0; /* Existing POWER dispatcher. */
 return 1;
}
