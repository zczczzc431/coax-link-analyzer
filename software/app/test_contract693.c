#include "spectrum_runtime616.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"FAIL693 line=%d %s\n",__LINE__,#x);exit(1);}} while(0)
struct fixture {
 struct sm616_runtime runtime;
 struct sm612_config config;
 struct sm612_identity identity;
 struct sm616_result result;
 uint32_t raw[64],token;
 uint64_t us;
 double peak;
 int close_rc,checkpoint_rc;
 unsigned closes,checkpoints;
 uint64_t seen_generation;
 struct sm613_event last_closed;
};
static int fake_fft(void *user,const uint32_t *raw,size_t n,double *code,size_t bins) {
 struct fixture *f=user;
 CHECK(n==64 && bins==33 && raw[0]==8192);
 memset(code,0,bins*sizeof *code);code[3]=f->peak;return 0;
}
static int closed(void *user,const struct sm613_event *event) {
 struct fixture *f=user;CHECK(event->closed);
 f->last_closed=*event;f->closes++;return f->close_rc;
}
static int checkpoint(void *user,const struct sm615_view *view) {
 struct fixture *f=user;
 CHECK(view->event && !view->event->closed && view->event->confirmed);
 CHECK(sm615_view_valid(view));
 f->seen_generation=view->generation;f->checkpoints++;return f->checkpoint_rc;
}
static int feed(struct fixture *f,double peak) {
 f->peak=peak;f->token++;f->us+=100;
 return sm616_process(&f->runtime,&f->identity,f->token,f->us,f->raw,64,1,0,1,&f->result);
}
static void setup_enter(struct fixture *f,int close_rc,int checkpoint_rc,unsigned enter) {
 memset(f,0,sizeof *f);
 f->close_rc=close_rc;f->checkpoint_rc=checkpoint_rc;
 struct sm616_callbacks cb={fake_fft,f,closed,f,checkpoint,f};
 CHECK(sm616_init(&f->runtime,&cb,100)==0);
 sm612_defaults(&f->config);memset(f->config.exclude,0,sizeof f->config.exclude);
 f->config.first_bin=1;f->config.last_bin=32;f->config.learn_frames=2;
 f->config.enter_frames=enter;f->config.recover_frames=2;
 f->config.floor_db=-100;f->config.min_learn_peak_db=-80;
 f->config.max_learn_spread_db=3;f->config.max_gap_us=10000;
 f->identity=(struct sm612_identity){1,2,3,16000000,64,SM612_FORMAT};
 for(unsigned i=0;i<64;i++)f->raw[i]=8192;
 CHECK(sm616_learn(&f->runtime,&f->config,&f->identity)==0);
 CHECK(feed(f,1000)==0 && feed(f,1000)==0);
 CHECK(f->runtime.capture.core.state==SM612_READY);
 CHECK(sm616_arm(&f->runtime,&f->identity,f->us)==0);
 CHECK(feed(f,1000)==0);
}
static void setup(struct fixture *f,int close_rc,int checkpoint_rc) {
 setup_enter(f,close_rc,checkpoint_rc,2);
}
static void alarm(struct fixture *f) {
 CHECK(feed(f,100)==0);
 CHECK(f->runtime.capture.core.state==SM612_CANDIDATE);
 CHECK(feed(f,100)==0);
 CHECK(f->runtime.capture.core.state==SM612_ALARM);
}
int main(void) {
 struct fixture *f=calloc(1,sizeof *f);CHECK(f!=NULL);
 setup(f,SM613_SINK_QUEUED,SM613_SINK_QUEUED);
 alarm(f);
 CHECK(f->result.checkpoints==1 && f->result.queued_checkpoints==1);
 CHECK(f->runtime.cp_reserved_generation==1 && !f->runtime.pending);
 CHECK(f->result.evidence.published==0 && f->result.evidence.queued==0);
 CHECK(feed(f,100)==0 && f->seen_generation==2);
 CHECK(f->result.queued_checkpoints==1 && f->runtime.cp_reserved_generation==2);
 CHECK(feed(f,1000)==0 && feed(f,1000)==0);
 CHECK(f->closes==1 && !f->runtime.capture.active);
 CHECK(f->result.evidence.queued==1 && f->result.evidence.published==0);
 CHECK(f->last_closed.confirmed && f->last_closed.first.sample[0]==8192);
 /* The exact same stream retains synchronous durable accounting by default. */
 setup(f,0,0);alarm(f);
 CHECK(f->result.checkpoints==1 && f->result.queued_checkpoints==0);
 CHECK(feed(f,1000)==0 && feed(f,1000)==0);
 CHECK(f->result.evidence.published==1 && f->result.evidence.queued==0);
 /* Ordinary nonzero failures have NOT become accepted handoffs. */
 const int rejected[]={-1,1,692,694};
 for(unsigned k=0;k<sizeof rejected/sizeof rejected[0];k++) {
  setup(f,rejected[k],0);alarm(f);
  CHECK(feed(f,1000)==0);CHECK(feed(f,1000)==-2);
  CHECK(f->runtime.capture.active && f->runtime.capture.blocked==SM613_STORE_ERROR);
  CHECK(f->result.evidence.queued==0 && f->result.evidence.published==0);
  uint64_t capture=f->runtime.capture.event.capture_id,clock=f->runtime.capture.clock_us;
  uint32_t first=f->runtime.capture.event.first.token,token=f->runtime.last_token;
  CHECK(sm616_retry(&f->runtime,&f->result)==-2);
  CHECK(f->result.fft_calls==0 && f->runtime.capture.clock_us==clock);
  f->close_rc=SM613_SINK_QUEUED;
  CHECK(sm616_retry(&f->runtime,&f->result)==0);
  CHECK(f->result.evidence.queued==1 && f->result.evidence.published==0);
  CHECK(f->last_closed.capture_id==capture && f->last_closed.first.token==first);
  CHECK(f->runtime.last_token==token && f->runtime.capture.clock_us==clock);
  CHECK(!f->runtime.capture.active && !f->runtime.capture.blocked);
 }
 /* A rejected ACTIVE is frozen and its reservation cannot advance. Retry
  * hands off that exact generation before the separately frozen CLOSED. */
 setup(f,SM613_SINK_QUEUED,1);
 CHECK(feed(f,100)==0);CHECK(feed(f,100)==-2);
 CHECK(f->runtime.pending && f->runtime.cp_reserved_generation==0);
 CHECK(f->result.queued_checkpoints==0 && f->result.evidence.queued==1);
 uint64_t generation=f->runtime.pending_view.generation;
 f->checkpoint_rc=SM613_SINK_QUEUED;
 CHECK(sm616_retry(&f->runtime,&f->result)==0);
 CHECK(f->result.queued_checkpoints==1 && f->seen_generation==generation);
 CHECK(f->runtime.cp_reserved_generation==generation && !f->runtime.pending);
 CHECK(f->runtime.capture.core.state==SM612_SUSPENDED);
 /* A callback adapter must prevent CLOSED overtaking a failed ACTIVE. These
  * callbacks model that producer gate: both refuse until ACTIVE retry. */
 setup(f,1,1);
 CHECK(feed(f,100)==0);CHECK(feed(f,100)==-2);
 CHECK(f->runtime.pending && f->runtime.capture.blocked);
 f->checkpoint_rc=SM613_SINK_QUEUED;
 CHECK(sm616_retry(&f->runtime,&f->result)==-2);
 CHECK(f->result.queued_checkpoints==1 && !f->runtime.pending);
 CHECK(f->runtime.capture.blocked && f->result.evidence.published==0);
 f->close_rc=SM613_SINK_QUEUED;
 CHECK(sm616_retry(&f->runtime,&f->result)==0);
 CHECK(f->result.queued_checkpoints==0 && f->result.evidence.queued==1);
 setup(f,SM613_SINK_QUEUED,SM613_SINK_QUEUED);alarm(f);
 CHECK(sm616_stop(&f->runtime,0,f->us,&f->result)==0);
 CHECK(f->result.evidence.queued==1 && f->result.evidence.published==0);
 setup(f,SM613_SINK_QUEUED,SM613_SINK_QUEUED);alarm(f);
 CHECK(sm616_poll(&f->runtime,f->us+f->config.max_gap_us+1,&f->result)==1);
 CHECK(f->result.evidence.status==SM613_TIMEOUT && f->result.evidence.queued==1);
 setup(f,SM613_SINK_QUEUED,SM613_SINK_QUEUED);alarm(f);
 CHECK(sm616_process(&f->runtime,&f->identity,f->token+1,f->us+100,
       f->raw,64,1,0,0,&f->result)==0);
 CHECK(f->result.status==SM616_OWNER_STOP && f->result.evidence.queued==1);
 setup(f,SM613_SINK_QUEUED,SM613_SINK_QUEUED);alarm(f);
 f->runtime.cp_reserved_generation=UINT64_MAX;
 CHECK(feed(f,100)==-2 && f->runtime.exhausted);
 CHECK(f->result.status==SM616_GENERATION_LIMIT && f->result.evidence.queued==1);
 CHECK(f->runtime.capture.core.state==SM612_SUSPENDED);
 /* Same-call gap can close one capture and confirm the next. Checkpoint
  * failure then closes the second; neither handoff may disappear from result. */
 for(unsigned mode=0;mode<2;mode++) {
  setup_enter(f,mode?0:SM613_SINK_QUEUED,SM613_SINK_QUEUED,1);
  CHECK(feed(f,100)==0 && f->runtime.capture.active);
  f->checkpoint_rc=1;f->token++;
  CHECK(feed(f,100)==-2 && f->closes==2);
  CHECK(f->result.evidence.queued==(mode?0u:2u));
  CHECK(f->result.evidence.published==(mode?2u:0u));
 }
 free(f);
 printf("MONITOR693_CONTRACT_PASS checks=%u QUEUED_IS_NOT_DURABLE\n",checks);
 return 0;
}
