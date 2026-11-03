/* Included after MASK globals/clock helpers. Main-thread state only.
 * The sole storage worker never touches UI, ADC/DAC, or p690 timers. */
static struct sm693_pipeline *mask694_pipeline;
static struct sm693_pipeline_status mask694_store_state;
static int mask694_error,mask694_retrying;
static unsigned mask694_capacity=4;
static unsigned mask694_checkpoints_saved;
static int mask617_engaged(void);

static int mask694_refresh(void) {
 if(!mask694_pipeline)return -1;
 struct sm693_pipeline_status fresh;
 if(sm693_pipeline_status(mask694_pipeline,&fresh)!=0)return -1;
 if(memcmp(&fresh,&mask694_store_state,sizeof fresh))view_dirty=1;
 mask694_store_state=fresh;return 0;
}
static int mask694_busy(void) {
 if(!mask694_selected)return 0;
 if(!mask694_pipeline || mask694_refresh()!=0)return 1;
 return mask694_error || mask694_retrying || mask694_store_state.queue.occupied ||
  mask694_store_state.producer_blocked;
}
static void mask694_record(const struct sm693_completion *c) {
 if(c->kind==SM693_ACTIVE) {mask694_checkpoints_saved++;return;}
 mask617_closed_count++;
 if(c->confirmed)mask686_confirmed++;else mask686_transient++;
 printf("MONITOR_SPECTRUM_RECORD CLASS=%s TOKEN=%u BIN=%u DIR=%d RAW_RETAINED=1 DURABLE694=1 SEQ=%llu\n",
  c->confirmed?"CONFIRMED":"UNCONFIRMED",c->first_token,c->worst_bin,c->worst_direction,
  (unsigned long long)c->seq);
 printf("MASK_CLOSED SESSION=%llu CAPTURE=%llu EVENT=%llu CONFIRMED=%d END=%u HOST_END_US=%llu\n",
  (unsigned long long)c->session,(unsigned long long)c->capture_id,
  (unsigned long long)c->event_id,c->confirmed,c->end,(unsigned long long)c->end_us);
}
static int mask694_pump(void) {
 if(!mask694_selected || !mask694_pipeline)return 0;
 struct sm693_completion c;
 int rc;
 while((rc=sm693_pipeline_poll(mask694_pipeline,&c))==1) {
  mask694_record(&c);view_dirty=1;
 }
 if(rc<0 || mask694_refresh()!=0) {mask694_error=1;return -1;}
 if(mask694_store_state.queue.failed) {
  if(!mask694_error) {
   mask694_error=1;mask694_retrying=0;
   uint64_t now=mask617_now();
   if(now!=UINT64_MAX && mask617_engaged())
    (void)sm616_stop(&mask617_runtime,1,now,&mask617_result);
   printf("MASK694_STORE_FAILED SEQ=%llu CAUSE=%d RAM_RETAINED=1 REARM_BLOCKED=1\n",
    (unsigned long long)mask694_store_state.queue.failed_seq,mask694_store_state.queue.writer_rc);
  }
  mask617_note("SAVE FAILED - RAM RETAINED / RETRY / NO AUTOMATIC ARM");
  (void)mask694_refresh();return -1;
 }
 if(mask694_retrying) {
  if((mask617_runtime.pending || mask617_runtime.capture.blocked) &&
     mask694_store_state.queue.occupied<mask694_store_state.queue.capacity) {
   rc=sm616_retry(&mask617_runtime,&mask617_result);
   if(rc<0 && mask617_runtime.capture.blocked &&
      mask617_runtime.capture.blocked!=SM613_STORE_ERROR) {
    mask694_error=1;mask694_retrying=0;
    mask617_note("RETRY BLOCKED - CAPTURE LIMIT / RAM RETAINED");return -1;
   }
  }
  if(mask694_refresh()!=0)return -1;
  if(!mask694_store_state.queue.occupied && !mask694_store_state.producer_blocked &&
     !mask617_runtime.pending && !mask617_runtime.capture.blocked) {
   mask694_retrying=0;mask694_error=0;
   mask617_note("RETRY DURABLE - EXPLICIT ARM REQUIRED");
  }
 }
 return mask694_error?-1:0;
}
static int mask694_retry(void) {
 if(!mask694_pipeline)return -1;
 (void)mask694_pump();
 if(mask694_store_state.queue.failed) {
  if(sm693_pipeline_retry_failed(mask694_pipeline)!=0)return -1;
 }
 mask694_error=0;mask694_retrying=1;
 mask617_note("RETRY REQUESTED - SAVING ORIGINAL SNAPSHOTS");
 (void)mask694_refresh();return 0;
}
static int mask694_shutdown(void) {
 if(!mask694_pipeline)return 0;
 if(mask617_clock_bad || mask655_pending || mask655_failed)return -1;
 /* At most one frozen ACTIVE and one CLOSED remain in the runtime. Completed
  * queue jobs are drained/polled before either original key is retried. */
 for(unsigned pass=0;pass<5;pass++) {
  int rc=sm693_pipeline_drain(mask694_pipeline);
  if(rc!=0 && rc!=SM693_BUSY) {(void)mask694_pump();return -1;}
  if(mask694_pump()!=0)return -1;
  if(mask617_runtime.pending || mask617_runtime.capture.blocked) {
   if(sm616_retry(&mask617_runtime,&mask617_result)<0)continue;
  }
  uint64_t now=mask617_now();if(now==UINT64_MAX)return -1;
  if(sm616_stop(&mask617_runtime,0,now,&mask617_result)<0)continue;
  rc=sm693_pipeline_drain(mask694_pipeline);
  if(rc!=0 && rc!=SM693_BUSY) {(void)mask694_pump();return -1;}
  if(mask694_pump()!=0)return -1;
  if(!mask617_runtime.pending && !mask617_runtime.capture.blocked &&
     !mask694_store_state.queue.occupied && !mask694_store_state.producer_blocked) {
   if(sm693_pipeline_destroy(mask694_pipeline)!=0)return -1;
   mask694_pipeline=NULL;mask694_retrying=mask694_error=0;mask617_ready=0;
   memset(&mask694_store_state,0,sizeof mask694_store_state);
   printf("MASK694_SHUTDOWN_DURABLE_AND_JOINED\n");return 0;
  }
 }
 mask617_note("EXIT BLOCKED - UNSAVED RAM RETAINED / RETRY");return -1;
}
