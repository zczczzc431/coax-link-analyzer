#define _POSIX_C_SOURCE 200809L
#include "spectrum_pipeline693.h"
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct sm693_pipeline {
 pthread_t owner;
 struct sm693_queue *queue;
 struct sm614_store closed_store;
 struct sm615_store active_store;
 char audit_dir[PATH_MAX],refs_dir[PATH_MAX];
 struct sm612_model model;
 struct sm655_receipt receipt;
 int have_template,gate,last_rc;
 enum sm693_kind gate_kind;
 struct sm613_event gate_event;
 struct sm615_view gate_view;
#ifdef SM693_TESTING
 void (*test_hook)(void *,const struct sm693_job *);
 void *test_user;
#endif
};
static int owned_pipeline(const struct sm693_pipeline *p) {
 return p && pthread_equal(p->owner,pthread_self());
}
static int write_job(void *user,const struct sm693_job *job) {
 struct sm693_pipeline *p=user;
#ifdef SM693_TESTING
 if(p->test_hook)p->test_hook(p->test_user,job);
#endif
 if(sm655_audit_check(p->audit_dir,&job->audit_model,&job->receipt)!=0)return SM693_AUDIT_CHECK_ERROR;
 if(sm655_audit_bind(p->refs_dir,&job->event,&job->receipt)!=0)return SM693_AUDIT_BIND_ERROR;
 if(job->kind==SM693_CLOSED)return sm614_save(&p->closed_store,&job->event)==0?
  0:SM693_CLOSED_ERROR_BASE-(int)p->closed_store.status;
 if(job->kind==SM693_ACTIVE)return sm615_save(&p->active_store,&job->view)==0?
  0:SM693_ACTIVE_ERROR_BASE-(int)p->active_store.files.status;
 return -1;
}
static int copy_path(char *out,const char *path) {
 if(!path || path[0]!='/' || strlen(path)>=PATH_MAX)return -1;
 strcpy(out,path);return 0;
}
struct sm693_pipeline *sm693_pipeline_create(const char *closed_dir,const char *active_dir,
 const char *audit_dir,const char *refs_dir,unsigned capacity) {
 struct sm693_pipeline *p;
 struct sm614_scan scan;
 if(!closed_dir || !active_dir || !audit_dir || !refs_dir ||
    !strcmp(closed_dir,active_dir) || !strcmp(closed_dir,audit_dir) ||
    !strcmp(closed_dir,refs_dir) || !strcmp(active_dir,audit_dir) ||
    !strcmp(active_dir,refs_dir) || !strcmp(audit_dir,refs_dir))return NULL;
 p=calloc(1,sizeof *p);if(!p)return NULL;
 p->owner=pthread_self();
 p->closed_store.dirfd=p->closed_store.lockfd=-1;
 p->active_store.files.dirfd=p->active_store.files.lockfd=-1;
 if(copy_path(p->audit_dir,audit_dir)!=0 || copy_path(p->refs_dir,refs_dir)!=0 ||
    sm614_open(&p->closed_store,closed_dir,128,UINT64_C(33554432))!=0 ||
    sm615_open(&p->active_store,active_dir,64,UINT64_C(33554432))!=0 ||
    sm614_scan(&p->closed_store,&scan)!=0 || sm615_scan(&p->active_store,&scan)!=0)goto fail;
 p->queue=sm693_create(capacity,1,write_job,p);
 if(!p->queue)goto fail;
 return p;
fail:
 sm614_close(&p->closed_store);sm615_close(&p->active_store);free(p);return NULL;
}
int sm693_pipeline_status(struct sm693_pipeline *p,struct sm693_pipeline_status *out) {
 if(!owned_pipeline(p) || !out)return SM693_INVALID;
 memset(out,0,sizeof *out);
 int rc=sm693_get_status(p->queue,&out->queue);if(rc)return rc;
 out->producer_blocked=p->gate;out->blocked_kind=p->gate_kind;
 out->last_submit_rc=p->last_rc;out->have_template=p->have_template;return 0;
}
int sm693_pipeline_template(struct sm693_pipeline *p,const struct sm612_model *model,
 const struct sm655_receipt *receipt) {
 struct sm693_status status;
 if(!owned_pipeline(p) || !model || !receipt)return SM693_INVALID;
 if(sm693_get_status(p->queue,&status)!=0)return SM693_INVALID;
 if(status.occupied || p->gate)return SM693_BUSY;
 p->have_template=0;
 if(model->state!=SM612_READY || sm655_audit_check(p->audit_dir,model,receipt)!=0)return SM693_INVALID;
 memcpy(&p->model,model,sizeof *model);
 memcpy(&p->receipt,receipt,sizeof *receipt);p->have_template=1;return 0;
}
static int same_gate(const struct sm693_pipeline *p,enum sm693_kind kind,
 const struct sm613_event *event,const struct sm615_view *view) {
 if(!p->gate)return 1;
 if(p->gate_kind!=kind || memcmp(&p->gate_event,event,sizeof *event))return 0;
 if(kind==SM693_ACTIVE && (p->gate_view.generation!=view->generation ||
    p->gate_view.snapshot_us!=view->snapshot_us ||
    p->gate_view.observed_token!=view->observed_token ||
    p->gate_view.observed_us!=view->observed_us))return 0;
 return 1;
}
static void latch_gate(struct sm693_pipeline *p,enum sm693_kind kind,
 const struct sm613_event *event,const struct sm615_view *view) {
 if(p->gate)return;
 p->gate=1;p->gate_kind=kind;
 memcpy(&p->gate_event,event,sizeof *event);
 memset(&p->gate_view,0,sizeof p->gate_view);
 if(view){p->gate_view=*view;p->gate_view.event=&p->gate_event;}
}
static int enqueue(struct sm693_pipeline *p,enum sm693_kind kind,
 const struct sm613_event *event,const struct sm615_view *view) {
 uint64_t seq;
 if(!owned_pipeline(p))return -1;
 if(!p->have_template || !event ||
    (kind==SM693_CLOSED?!sm614_event_valid(event):!sm615_view_valid(view))) {
  p->last_rc=SM693_INVALID;return -1;
 }
 if(!same_gate(p,kind,event,view)){p->last_rc=SM693_BLOCKED;return -1;}
 int rc=kind==SM693_CLOSED?
  sm693_submit_closed(p->queue,event,&p->model,&p->receipt,&seq):
  sm693_submit_active(p->queue,view,&p->model,&p->receipt,&seq);
 p->last_rc=rc;
 if(rc!=0){latch_gate(p,kind,event,view);return -1;}
 p->gate=0;p->gate_kind=0;
 return SM613_SINK_QUEUED;
}
int sm693_pipeline_closed(void *user,const struct sm613_event *event) {
 return enqueue(user,SM693_CLOSED,event,NULL);
}
int sm693_pipeline_checkpoint(void *user,const struct sm615_view *view) {
 return enqueue(user,SM693_ACTIVE,view?view->event:NULL,view);
}
int sm693_pipeline_poll(struct sm693_pipeline *p,struct sm693_completion *out) {
 return owned_pipeline(p)?sm693_poll(p->queue,out):SM693_INVALID;
}
int sm693_pipeline_retry_failed(struct sm693_pipeline *p) {
 struct sm693_status s;
 if(!owned_pipeline(p) || sm693_get_status(p->queue,&s)!=0 || !s.failed)return SM693_INVALID;
 return sm693_retry(p->queue,s.failed_seq);
}
int sm693_pipeline_drain(struct sm693_pipeline *p) {
 if(!owned_pipeline(p))return SM693_INVALID;
 int rc=sm693_drain(p->queue);if(rc)return rc;
 return p->gate?SM693_BUSY:0;
}
int sm693_pipeline_destroy(struct sm693_pipeline *p) {
 if(!owned_pipeline(p))return SM693_INVALID;
 if(p->gate)return SM693_BUSY;
 int rc=sm693_destroy(p->queue);if(rc)return rc;
 sm614_close(&p->closed_store);sm615_close(&p->active_store);
 free(p);return 0;
}
int sm693_pipeline_scan(struct sm693_pipeline *p,struct sm614_scan *closed,struct sm614_scan *active) {
 struct sm693_status status;
 if(!owned_pipeline(p) || !closed || !active || sm693_get_status(p->queue,&status)!=0)return SM693_INVALID;
 if(status.pending || status.inflight)return SM693_BUSY;
 return sm614_scan(&p->closed_store,closed)!=0 || sm615_scan(&p->active_store,active)!=0?-1:0;
}
#ifdef SM693_TESTING
int sm693_pipeline_test_hook(struct sm693_pipeline *p,void (*hook)(void *,const struct sm693_job *),void *user) {
 struct sm693_status status;
 if(!owned_pipeline(p) || sm693_get_status(p->queue,&status)!=0)return SM693_INVALID;
 if(status.occupied)return SM693_BUSY;
 p->test_hook=hook;p->test_user=user;return 0;
}
int sm693_pipeline_inject(struct sm693_pipeline *p,enum sm693_kind kind,
 enum sm614_fault fault,uint64_t after) {
 struct sm693_status status;
 if(!owned_pipeline(p) || sm693_get_status(p->queue,&status)!=0 ||
    (kind!=SM693_CLOSED && kind!=SM693_ACTIVE))return SM693_INVALID;
 if(status.pending || status.inflight)return SM693_BUSY;
 if(kind==SM693_CLOSED)sm614_inject(&p->closed_store,fault,after);
 else sm615_inject(&p->active_store,fault,after);
 return 0;
}
#endif
