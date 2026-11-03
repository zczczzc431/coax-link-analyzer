#define _POSIX_C_SOURCE 200809L
#include "spectrum_pipeline693.h"
#include "spectrum_runtime616.h"
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned checks;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"PIPELINE693_FAIL line=%d %s\n",__LINE__,#x);exit(1);}}while(0)
struct fixture {
 struct sm616_runtime runtime;
 struct sm612_config config;
 struct sm612_identity identity;
 struct sm612_model template_model;
 struct sm655_receipt receipt;
 struct sm616_result result;
 struct sm693_pipeline *pipeline;
 struct sm613_event closed;
 struct sm613_event active;
 struct sm615_view active_view;
 uint32_t raw[64],token;
 uint64_t us;
 double peak;
 char root[PATH_MAX],closed_dir[PATH_MAX],active_dir[PATH_MAX];
 char audit_dir[PATH_MAX],refs_dir[PATH_MAX];
};
static void join(char out[PATH_MAX],const char *base,const char *tail) {
 size_t a=strlen(base),b=strlen(tail);CHECK(a+b<PATH_MAX);
 memcpy(out,base,a);memcpy(out+a,tail,b+1);
}
static int fake_fft(void *user,const uint32_t *raw,size_t count,double *peak,size_t bins) {
 struct fixture *f=user;CHECK(count==64 && bins==33 && raw[0]==8192);
 memset(peak,0,bins*sizeof *peak);peak[3]=f->peak;return 0;
}
static int closed_callback(void *user,const struct sm613_event *event) {
 struct fixture *f=user;f->closed=*event;
 return sm693_pipeline_closed(f->pipeline,event);
}
static int active_callback(void *user,const struct sm615_view *view) {
 struct fixture *f=user;return sm693_pipeline_checkpoint(f->pipeline,view);
}
static int feed(struct fixture *f,double peak) {
 f->peak=peak;f->token++;f->us+=100;
 return sm616_process(&f->runtime,&f->identity,f->token,f->us,
   f->raw,64,1,0,1,&f->result);
}
static struct sm693_pipeline_status state(struct fixture *f) {
 struct sm693_pipeline_status s;
 CHECK(sm693_pipeline_status(f->pipeline,&s)==0);return s;
}
struct wrong_owner {struct fixture *fixture;int ok;};
static void *wrong_thread(void *arg) {
 struct wrong_owner *w=arg;struct fixture *f=w->fixture;
 struct sm693_pipeline_status s;struct sm693_completion c;
 w->ok=sm693_pipeline_status(f->pipeline,&s)==SM693_INVALID &&
  sm693_pipeline_closed(f->pipeline,&f->closed)==-1 &&
  sm693_pipeline_checkpoint(f->pipeline,&f->active_view)==-1 &&
  sm693_pipeline_template(f->pipeline,&f->template_model,&f->receipt)==SM693_INVALID &&
  sm693_pipeline_poll(f->pipeline,&c)==SM693_INVALID &&
  sm693_pipeline_destroy(f->pipeline)==SM693_INVALID;
 return NULL;
}
static void setup(struct fixture *f) {
 char cwd[PATH_MAX];memset(f,0,sizeof *f);
 CHECK(getcwd(cwd,sizeof cwd)!=NULL);
 join(f->root,cwd,"/results693/pipeline-XXXXXX");CHECK(mkdtemp(f->root)!=NULL);
 join(f->closed_dir,f->root,"/closed");join(f->active_dir,f->root,"/active");
 join(f->audit_dir,f->root,"/audit");join(f->refs_dir,f->root,"/refs");
 CHECK(mkdir(f->closed_dir,0700)==0 && mkdir(f->active_dir,0700)==0);
 struct sm616_callbacks cb={fake_fft,f,closed_callback,f,active_callback,f};
 CHECK(sm616_init(&f->runtime,&cb,100)==0);
 sm612_defaults(&f->config);memset(f->config.exclude,0,sizeof f->config.exclude);
 f->config.first_bin=1;f->config.last_bin=32;f->config.learn_frames=2;
 f->config.enter_frames=f->config.recover_frames=2;
 f->config.floor_db=-100;f->config.min_learn_peak_db=-80;
 f->config.max_learn_spread_db=3;f->config.max_gap_us=10000;
 f->identity=(struct sm612_identity){693,2,3,16000000,64,SM612_FORMAT};
 for(unsigned i=0;i<64;i++)f->raw[i]=8192;
 CHECK(sm616_learn(&f->runtime,&f->config,&f->identity)==0);
 CHECK(feed(f,1000)==0 && feed(f,1000)==0);
 CHECK(f->runtime.capture.core.state==SM612_READY);
 f->template_model=f->runtime.capture.core;
 CHECK(sm655_audit_write(f->audit_dir,&f->template_model,1,f->token,&f->receipt)==0);
 f->pipeline=sm693_pipeline_create(f->closed_dir,f->active_dir,f->audit_dir,f->refs_dir,1);
 CHECK(f->pipeline!=NULL);
 CHECK(sm693_pipeline_checkpoint(f->pipeline,&f->active_view)==-1);
 CHECK(state(f).last_submit_rc==SM693_INVALID && !state(f).producer_blocked);
 CHECK(sm693_pipeline_template(f->pipeline,&f->template_model,&f->receipt)==0);
 CHECK(sm616_arm(&f->runtime,&f->identity,f->us)==0);
 CHECK(feed(f,1000)==0 && feed(f,100)==0 && feed(f,100)==0);
 CHECK(f->result.queued_checkpoints==1 && f->result.evidence.published==0);
 memcpy(&f->active,&f->runtime.pending_event,sizeof f->active);
 f->active_view=f->runtime.pending_view;f->active_view.event=&f->active;
 pthread_t other;struct wrong_owner w={f,0};
 CHECK(pthread_create(&other,NULL,wrong_thread,&w)==0);
 CHECK(pthread_join(other,NULL)==0 && w.ok);
 CHECK(!state(f).producer_blocked && state(f).queue.submitted==1);
}
static void backpressure_and_real_store(struct fixture *f) {
 struct sm693_completion completion;
 struct sm693_pipeline_status s;
 struct sm614_scan closed_scan,active_scan;
 /* capacity=1 is deterministic even if I/O already finished: DONE retains its
  * slot until owner poll, so the second ACTIVE cannot leapfrog generation1. */
 CHECK(feed(f,100)==-2);
 CHECK(f->runtime.pending && f->runtime.capture.blocked);
 s=state(f);CHECK(s.producer_blocked && s.blocked_kind==SM693_ACTIVE);
 CHECK(sm693_pipeline_template(f->pipeline,&f->template_model,&f->receipt)==SM693_BUSY);
 CHECK(sm693_pipeline_destroy(f->pipeline)==SM693_BUSY);
 struct sm613_event *changed=malloc(sizeof *changed);CHECK(changed!=NULL);
 *changed=f->runtime.pending_event;changed->first.sample[0]++;
 struct sm615_view bad=f->runtime.pending_view;bad.event=changed;
 CHECK(sm693_pipeline_checkpoint(f->pipeline,&bad)!=SM613_SINK_QUEUED);
 free(changed);
 CHECK(sm693_pipeline_drain(f->pipeline)==SM693_BUSY);
 CHECK(sm693_pipeline_poll(f->pipeline,&completion)==1);
 CHECK(completion.kind==SM693_ACTIVE && completion.generation==1);
 CHECK(state(f).producer_blocked);
 /* The original ACTIVE is retried first. CLOSED is then held by capacity1. */
 CHECK(sm616_retry(&f->runtime,&f->result)==-2);
 CHECK(f->result.queued_checkpoints==1 && !f->runtime.pending);
 CHECK(f->runtime.capture.blocked && f->result.evidence.published==0);
 s=state(f);CHECK(s.producer_blocked && s.blocked_kind==SM693_CLOSED);
 CHECK(sm693_pipeline_drain(f->pipeline)==SM693_BUSY);
 CHECK(sm693_pipeline_poll(f->pipeline,&completion)==1);
 CHECK(completion.kind==SM693_ACTIVE && completion.generation==2);
 CHECK(sm616_retry(&f->runtime,&f->result)==0);
 CHECK(f->result.evidence.queued==1 && f->result.evidence.published==0);
 CHECK(sm693_pipeline_drain(f->pipeline)==0);
 CHECK(sm693_pipeline_poll(f->pipeline,&completion)==1);
 CHECK(completion.kind==SM693_CLOSED && completion.capture_id==1 && completion.confirmed);
 s=state(f);CHECK(!s.producer_blocked && !s.queue.occupied &&
    s.queue.submitted==3 && s.queue.durable==3);
 CHECK(sm693_pipeline_scan(f->pipeline,&closed_scan,&active_scan)==0);
 CHECK(closed_scan.complete_events==1 && active_scan.complete_events==2);
 CHECK(sm693_pipeline_destroy(f->pipeline)==0);f->pipeline=NULL;
 printf("PIPELINE693_ORDERED_STORE closed=1 active=2 ROOT=%s\n",f->root);
}
static void failed_index_retry(struct fixture *f,enum sm614_fault fault,unsigned index,enum sm693_kind kind) {
 char closed_dir[PATH_MAX],active_dir[PATH_MAX],tail[48];
 struct sm693_completion c;
 struct sm614_scan a,b;
 CHECK(snprintf(tail,sizeof tail,"/closed_fail%u_%u",index,(unsigned)kind)>0);join(closed_dir,f->root,tail);
 CHECK(snprintf(tail,sizeof tail,"/active_fail%u_%u",index,(unsigned)kind)>0);join(active_dir,f->root,tail);
 CHECK(mkdir(closed_dir,0700)==0 && mkdir(active_dir,0700)==0);
 f->pipeline=sm693_pipeline_create(closed_dir,active_dir,f->audit_dir,f->refs_dir,2);
 CHECK(f->pipeline!=NULL);
 CHECK(sm693_pipeline_template(f->pipeline,&f->template_model,&f->receipt)==0);
 CHECK(sm693_pipeline_inject(f->pipeline,kind,fault,0)==0);
 CHECK((kind==SM693_CLOSED?sm693_pipeline_closed(f->pipeline,&f->closed):
  sm693_pipeline_checkpoint(f->pipeline,&f->active_view))==SM613_SINK_QUEUED);
 CHECK(sm693_pipeline_drain(f->pipeline)==SM693_BLOCKED);
 struct sm693_pipeline_status s=state(f);
 CHECK(s.queue.failed==1 && s.queue.failed_seq==1 && s.queue.durable==0);
 CHECK(s.queue.writer_rc==(kind==SM693_CLOSED?SM693_CLOSED_ERROR_BASE:SM693_ACTIVE_ERROR_BASE)-SM614_IO);
 CHECK(sm693_pipeline_poll(f->pipeline,&c)==0);
 CHECK(sm693_pipeline_destroy(f->pipeline)==SM693_BUSY);
 CHECK(sm693_pipeline_template(f->pipeline,&f->template_model,&f->receipt)==SM693_BUSY);
 CHECK(sm693_pipeline_inject(f->pipeline,kind,SM614_FAULT_NONE,0)==0);
 CHECK(sm693_pipeline_retry_failed(f->pipeline)==0);
 CHECK(sm693_pipeline_drain(f->pipeline)==0);
 CHECK(sm693_pipeline_poll(f->pipeline,&c)==1 && c.seq==1 && c.kind==kind);
 CHECK(sm693_pipeline_poll(f->pipeline,&c)==0);
 s=state(f);CHECK(s.queue.submitted==1 && s.queue.durable==1 && !s.queue.occupied);
 CHECK(sm693_pipeline_scan(f->pipeline,&a,&b)==0 &&
  a.complete_events==(kind==SM693_CLOSED?1u:0u) && b.complete_events==(kind==SM693_ACTIVE?1u:0u));
 CHECK(sm693_pipeline_destroy(f->pipeline)==0);f->pipeline=NULL;
}
static void audit_corruption(struct fixture *f) {
 char closed_dir[PATH_MAX],active_dir[PATH_MAX],audit_file[PATH_MAX],suffix[SM655_AUDIT_NAME_MAX+2];
 struct sm693_completion c;
 join(closed_dir,f->root,"/closed_audit");join(active_dir,f->root,"/active_audit");
 CHECK(mkdir(closed_dir,0700)==0 && mkdir(active_dir,0700)==0);
 f->pipeline=sm693_pipeline_create(closed_dir,active_dir,f->audit_dir,f->refs_dir,2);
 CHECK(f->pipeline!=NULL);
 CHECK(sm693_pipeline_template(f->pipeline,&f->template_model,&f->receipt)==0);
 suffix[0]='/';memcpy(suffix+1,f->receipt.basename,strlen(f->receipt.basename)+1);
 join(audit_file,f->audit_dir,suffix);
 FILE *file=fopen(audit_file,"r+b");CHECK(file!=NULL);
 int byte=fgetc(file);CHECK(byte!=EOF && fseek(file,0,SEEK_SET)==0);
 CHECK(fputc(byte^1,file)!=EOF && fflush(file)==0 && fsync(fileno(file))==0);
 CHECK(sm693_pipeline_closed(f->pipeline,&f->closed)==SM613_SINK_QUEUED);
 CHECK(sm693_pipeline_drain(f->pipeline)==SM693_BLOCKED);
 CHECK(state(f).queue.writer_rc==SM693_AUDIT_CHECK_ERROR && state(f).queue.durable==0);
 CHECK(sm693_pipeline_poll(f->pipeline,&c)==0);
 CHECK(fseek(file,0,SEEK_SET)==0 && fputc(byte,file)!=EOF &&
  fflush(file)==0 && fsync(fileno(file))==0 && fclose(file)==0);
 CHECK(sm693_pipeline_retry_failed(f->pipeline)==0 && sm693_pipeline_drain(f->pipeline)==0);
 CHECK(sm693_pipeline_poll(f->pipeline,&c)==1 && c.seq==1 && c.kind==SM693_CLOSED);
 CHECK(sm693_pipeline_destroy(f->pipeline)==0);f->pipeline=NULL;
}
int main(void) {
 struct fixture *f=calloc(1,sizeof *f);CHECK(f!=NULL);
 setup(f);backpressure_and_real_store(f);
 const enum sm614_fault faults[]={SM614_FAULT_WRITE,SM614_FAULT_FILE_SYNC,
  SM614_FAULT_LINK,SM614_FAULT_DIR_SYNC,SM614_FAULT_INDEX_WRITE,
  SM614_FAULT_INDEX_SYNC,SM614_FAULT_INDEX_LINK,SM614_FAULT_INDEX_DIR_SYNC};
 for(unsigned i=0;i<sizeof faults/sizeof faults[0];i++) {
  failed_index_retry(f,faults[i],i,SM693_CLOSED);
  failed_index_retry(f,faults[i],i,SM693_ACTIVE);
 }
 audit_corruption(f);
 free(f);
 printf("PIPELINE693_PASS checks=%u REAL_AUDIT_FSYNC_INDEX_SYNTHETIC_SPECTRA_NO_HARDWARE\n",checks);
 return 0;
}
