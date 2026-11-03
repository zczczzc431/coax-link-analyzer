#include "spectrum_async693.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static void check(int ok,const char *label) {
    ++checks;
    if(!ok) { fprintf(stderr,"ASYNC693_FAIL %s\n",label);exit(1); }
}
struct writer_test {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    pthread_t worker,owner;
    unsigned entered,returned,release_calls,running,max_running;
    int have_worker,bad,fail_call;
    uint64_t order[16];
    const struct sm693_job *expected;
    unsigned expected_count;
};
static void init_writer(struct writer_test *w,const struct sm693_job *expected,
                        unsigned count) {
    memset(w,0,sizeof(*w));w->expected=expected;w->expected_count=count;
    w->owner=pthread_self();
    check(pthread_mutex_init(&w->mutex,NULL)==0,"writer mutex");
    check(pthread_cond_init(&w->changed,NULL)==0,"writer condition");
}
static void close_writer(struct writer_test *w) {
    check(pthread_cond_destroy(&w->changed)==0,"writer condition destroy");
    check(pthread_mutex_destroy(&w->mutex)==0,"writer mutex destroy");
}
static int writer(void *user,const struct sm693_job *job) {
    struct writer_test *w=user;
    const struct sm693_job *e=NULL;
    unsigned call,i;
    int rc;
    (void)pthread_mutex_lock(&w->mutex);
    call=++w->entered;++w->running;
    if(w->running>w->max_running) w->max_running=w->running;
    if(call>16) w->bad=1;
    else w->order[call-1]=job->seq;
    if(pthread_equal(pthread_self(),w->owner)) w->bad=1;
    if(w->have_worker && !pthread_equal(w->worker,pthread_self())) w->bad=1;
    w->worker=pthread_self();w->have_worker=1;
    (void)pthread_cond_broadcast(&w->changed);
    while(call>w->release_calls)
        (void)pthread_cond_wait(&w->changed,&w->mutex);
    for(i=0;i<w->expected_count;i++)
        if(w->expected[i].seq==job->seq) { e=&w->expected[i];break; }
    if(!e || e->kind!=job->kind ||
       memcmp(&e->event,&job->event,sizeof(job->event)) ||
       memcmp(&e->audit_model,&job->audit_model,sizeof(job->audit_model)) ||
       memcmp(&e->receipt,&job->receipt,sizeof(job->receipt))) w->bad=1;
    if(e && job->kind==SM693_ACTIVE) {
        if(job->view.event!=&job->event ||
           job->view.event==e->view.event ||
           job->view.generation!=e->view.generation ||
           job->view.snapshot_us!=e->view.snapshot_us ||
           job->view.observed_token!=e->view.observed_token ||
           job->view.observed_us!=e->view.observed_us) w->bad=1;
    } else if(job->view.event || job->view.generation || job->view.snapshot_us ||
              job->view.observed_token || job->view.observed_us) w->bad=1;
    rc=(int)call==w->fail_call?-97:0;
    --w->running;++w->returned;
    (void)pthread_cond_broadcast(&w->changed);
    (void)pthread_mutex_unlock(&w->mutex);
    return rc;
}
static void await_entered(struct writer_test *w,unsigned count) {
    (void)pthread_mutex_lock(&w->mutex);
    while(w->entered<count)
        (void)pthread_cond_wait(&w->changed,&w->mutex);
    (void)pthread_mutex_unlock(&w->mutex);
}
static void release_writer(struct writer_test *w,unsigned count) {
    (void)pthread_mutex_lock(&w->mutex);
    w->release_calls=count;
    (void)pthread_cond_broadcast(&w->changed);
    (void)pthread_mutex_unlock(&w->mutex);
}
static struct sm693_status status(struct sm693_queue *q) {
    struct sm693_status s;
    check(sm693_get_status(q,&s)==0,"status readable during blocked IO");
    check(s.occupied==s.pending+s.inflight+s.failed+s.completed,
          "status slot accounting");
    return s;
}
static void fill(struct sm693_job *job,uint64_t seq,int active) {
    unsigned i;
    memset(job,0,sizeof(*job));job->seq=seq;
    job->kind=active?SM693_ACTIVE:SM693_CLOSED;
    job->event.closed=!active;job->event.capture_id=100+seq;
    job->event.event_id=200+seq;job->event.confirmed=1;
    job->event.pre_count=SM613_PRE;job->event.post_count=SM613_POST;
    job->event.limits.identity.session=77;
    job->event.first.count=SM613_MAX_SAMPLES;
    for(i=0;i<SM613_MAX_SAMPLES;i++) {
        job->event.first.sample[i]=(uint32_t)(i+seq);
        job->event.pre[SM613_PRE-1].sample[i]=(uint32_t)(i+seq+17);
        job->event.post[SM613_POST-1].sample[i]=(uint32_t)(i+seq+33);
    }
    for(i=0;i<SM612_MAX_BINS;i++) {
        job->event.first_code[i]=(double)i+0.25;
        job->audit_model.baseline_db[i]=(double)i+0.5;
        job->audit_model.lower_db[i]=(double)i-20.0;
    }
    job->receipt.valid=1;job->receipt.format=SM655_AUDIT_FORMAT;
    (void)strcpy(job->receipt.basename,"synthetic_audit_693");
    if(active) {
        job->view.event=&job->event;job->view.generation=seq+7;
        job->view.snapshot_us=1000;job->view.observed_token=123;
        job->view.observed_us=999;
    }
}
static int submit_job(struct sm693_queue *q,const struct sm693_job *j,uint64_t *seq) {
    if(j->kind==SM693_ACTIVE)
        return sm693_submit_active(q,&j->view,&j->audit_model,&j->receipt,seq);
    return sm693_submit_closed(q,&j->event,&j->audit_model,&j->receipt,seq);
}
static void test_fifo_copy(void) {
    struct sm693_job *expected=calloc(5,sizeof(*expected));
    struct sm693_job *source=calloc(1,sizeof(*source));
    struct writer_test w;
    struct sm693_queue *q;
    struct sm693_status s;
    struct sm693_completion c;
    uint64_t seq=999;
    unsigned i;
    check(expected && source,"heap fixtures");
    for(i=0;i<5;i++) fill(&expected[i],(uint64_t)i+1,(int)(i%2));
    init_writer(&w,expected,5);q=sm693_create(4,1,writer,&w);
    check(q!=NULL,"create bounded FIFO");
    for(i=0;i<4;i++) {
        memcpy(source,&expected[i],sizeof(*source));
        if(source->kind==SM693_ACTIVE) source->view.event=&source->event;
        check(submit_job(q,source,&seq)==0 && seq==(uint64_t)i+1,"submit queued");
        if(i==0) await_entered(&w,1);
        memset(source,0xA5,sizeof(*source));
    }
    s=status(q);
    check(s.occupied==4 && s.inflight==1 && s.pending==3 && !s.durable &&
          s.submitted==4,"queued is not durable");
    check(sm693_poll(q,&c)==0,"no premature completion");
    seq=999;
    check(submit_job(q,&expected[4],&seq)==SM693_FULL && seq==999,"full preserves output");
    check(sm693_destroy(q)==SM693_BUSY,"inflight destroy refused");
    release_writer(&w,4);
    check(sm693_drain(q)==0,"drain waits all writer calls");
    s=status(q);
    check(s.completed==4 && s.durable==4 && !s.pending && !s.inflight,
          "successful writer produces durable completion");
    check(sm693_destroy(q)==SM693_BUSY,"drain does not reclaim completions");
    check(submit_job(q,&expected[4],&seq)==SM693_FULL,"DONE still consumes capacity");
    for(i=0;i<4;i++) {
        check(sm693_poll(q,&c)==1 && c.seq==(uint64_t)i+1 &&
              c.session==77 && c.capture_id==101+i && c.event_id==201+i &&
              c.confirmed && c.kind==expected[i].kind &&
              c.generation==(i%2?(uint64_t)i+8:0),"ordered small completion");
    }
    check(sm693_poll(q,&c)==0,"no duplicate completion");
    check(submit_job(q,&expected[4],&seq)==0 && seq==5,"reclaimed ring slot reused");
    await_entered(&w,5);release_writer(&w,5);
    check(sm693_drain(q)==0 && sm693_poll(q,&c)==1 && c.seq==5,"wrapped FIFO completion");
    check(sm693_destroy(q)==0,"empty queue joins worker");
    check(!w.bad && w.max_running==1 && w.entered==5 && w.returned==5,
          "deep copies rebound and one non-owner worker");
    for(i=0;i<5;i++) check(w.order[i]==(uint64_t)i+1,"FIFO writer order");
    close_writer(&w);free(source);free(expected);
}
static void test_failure(void) {
    struct sm693_job *e=calloc(3,sizeof(*e));
    struct writer_test w;
    struct sm693_queue *q;
    struct sm693_status s;
    struct sm693_completion c;
    uint64_t seq;
    unsigned i;
    check(e!=NULL,"failure fixtures");
    for(i=0;i<3;i++) fill(&e[i],(uint64_t)i+10,i==0);
    init_writer(&w,e,3);w.fail_call=1;
    q=sm693_create(4,10,writer,&w);check(q!=NULL,"failure queue");
    check(submit_job(q,&e[0],&seq)==0,"first failure submit");
    await_entered(&w,1);
    check(submit_job(q,&e[1],&seq)==0,"later FIFO submit before failure");
    release_writer(&w,1);
    check(sm693_drain(q)==SM693_BLOCKED,"drain exposes failure");
    s=status(q);
    check(s.failed==1 && s.failed_seq==10 && s.writer_rc==-97 &&
          s.pending==1 && !s.durable && s.submitted==2 && s.occupied==2,
          "failed job preserved and later writer paused");
    check(sm693_poll(q,&c)==0,"failure cannot look saved");
    seq=888;
    check(submit_job(q,&e[2],&seq)==SM693_BLOCKED && seq==888,"failure blocks submission");
    check(sm693_destroy(q)==SM693_BUSY,"failure destroy retains job");
    check(sm693_retry(q,11)==SM693_INVALID,"wrong retry sequence rejected");
    check(sm693_retry(q,10)==0,"explicit exact retry");
    await_entered(&w,2);
    s=status(q);
    check(s.inflight==1 && s.pending==1 && !s.failed && !s.durable &&
          s.submitted==2,"retry not counted as new submit or durable");
    release_writer(&w,3);
    check(sm693_drain(q)==0,"retry resumes original FIFO");
    check(sm693_poll(q,&c)==1 && c.seq==10 && c.kind==SM693_ACTIVE,"retry original completion");
    check(sm693_poll(q,&c)==1 && c.seq==11,"later completion follows retry");
    s=status(q);check(s.submitted==2 && s.durable==2 && !s.occupied,"durable exactly once");
    check(sm693_retry(q,10)==SM693_INVALID,"durable retry rejected");
    check(sm693_destroy(q)==0,"failure queue clean destroy");
    check(!w.bad && w.max_running==1 && w.entered==3 &&
          w.order[0]==10 && w.order[1]==10 && w.order[2]==11,
          "retry preserves bytes key and FIFO");
    close_writer(&w);free(e);
}
static void test_arguments_overflow(void) {
    struct sm693_job *e=calloc(1,sizeof(*e));
    struct writer_test w;
    struct sm693_queue *q;
    struct sm693_completion c;
    struct sm693_status s;
    uint64_t seq=123;
    check(e!=NULL,"overflow fixture");fill(e,UINT64_MAX,0);
    init_writer(&w,e,1);
    check(!sm693_create(0,1,writer,&w) && !sm693_create(5,1,writer,&w) &&
          !sm693_create(1,0,writer,&w) && !sm693_create(1,1,NULL,&w),
          "invalid create rejected");
    check(sm693_drain(NULL)==SM693_INVALID && sm693_destroy(NULL)==SM693_INVALID &&
          sm693_poll(NULL,&c)==SM693_INVALID,"null queue rejected");
    q=sm693_create(1,UINT64_MAX,writer,&w);check(q!=NULL,"last sequence queue");
    check(sm693_submit_closed(q,NULL,&e->audit_model,&e->receipt,&seq)==SM693_INVALID &&
          seq==123,"null input preserves output");
    check(sm693_submit_active(q,&e->view,&e->audit_model,&e->receipt,&seq)==SM693_INVALID,
          "invalid ACTIVE view rejected");
    e->event.pre_count=SM613_PRE+1;
    check(submit_job(q,e,&seq)==SM693_INVALID,"invalid raw bounds rejected");
    e->event.pre_count=SM613_PRE;
    check(sm693_get_status(q,NULL)==SM693_INVALID && sm693_poll(q,NULL)==SM693_INVALID,
          "null output rejected");
    check(submit_job(q,e,&seq)==0 && seq==UINT64_MAX,"last sequence accepted");
    await_entered(&w,1);release_writer(&w,1);
    check(sm693_drain(q)==0 && sm693_poll(q,&c)==1 && c.seq==UINT64_MAX,
          "last sequence durable");
    seq=123;
    check(submit_job(q,e,&seq)==SM693_OVERFLOW && seq==123,"sequence never wraps");
    s=status(q);check(!s.next_seq && s.submitted==1 && s.durable==1 && !s.occupied,
                     "overflow no state mutation");
    check(sm693_destroy(q)==0 && !w.bad,"overflow queue destroy");
    close_writer(&w);free(e);
}
struct wrong_owner {struct sm693_queue *q;const struct sm693_job *job;int ok;};
static void *nonowner(void *arg) {
    struct wrong_owner *x=arg;
    struct sm693_status s;
    struct sm693_completion c;
    uint64_t seq=88;
    x->ok=sm693_get_status(x->q,&s)==SM693_INVALID &&
      sm693_poll(x->q,&c)==SM693_INVALID &&
      submit_job(x->q,x->job,&seq)==SM693_INVALID && seq==88 &&
      sm693_retry(x->q,1)==SM693_INVALID &&
      sm693_drain(x->q)==SM693_INVALID &&
      sm693_destroy(x->q)==SM693_INVALID;
    return NULL;
}
static void test_live_ring_reuse(void) {
    struct sm693_job *e=calloc(3,sizeof *e);
    struct writer_test w;
    struct sm693_completion c;
    pthread_t other;
    uint64_t seq;
    check(e!=NULL,"live ring fixtures");
    for(unsigned i=0;i<3;i++)fill(&e[i],i+1,0);
    init_writer(&w,e,3);
    struct sm693_queue *q=sm693_create(2,1,writer,&w);
    check(q!=NULL,"live ring create");
    struct wrong_owner wrong={q,&e[0],0};
    check(pthread_create(&other,NULL,nonowner,&wrong)==0,"non-owner create");
    check(pthread_join(other,NULL)==0 && wrong.ok,"all non-owner APIs rejected");
    check(status(q).occupied==0 && status(q).submitted==0,"non-owner leaves state");
    check(submit_job(q,&e[0],&seq)==0,"live first submit");
    await_entered(&w,1);
    check(submit_job(q,&e[1],&seq)==0,"live second submit");
    release_writer(&w,1);await_entered(&w,2);
    check(sm693_poll(q,&c)==1 && c.seq==1,"DONE polled beside INFLIGHT");
    check(submit_job(q,&e[2],&seq)==0 && seq==3,"ring tail reused beside IO");
    check(status(q).inflight==1 && status(q).pending==1,"live slot remains in flight");
    release_writer(&w,3);
    check(sm693_drain(q)==0,"live ring drain");
    check(sm693_poll(q,&c)==1 && c.seq==2,"inflight bytes preserved");
    check(sm693_poll(q,&c)==1 && c.seq==3,"reused tail stays FIFO");
    check(!w.bad && w.max_running==1 && w.order[0]==1 &&
      w.order[1]==2 && w.order[2]==3,"live pointer stable");
    check(sm693_destroy(q)==0,"live ring destroy");
    close_writer(&w);free(e);
}
int main(void) {
    test_fifo_copy();test_failure();test_arguments_overflow();test_live_ring_reuse();
    printf("ASYNC693_MEMORY JOB_BYTES=%zu MAX_JOB_BYTES=%zu\n",
      sizeof(struct sm693_job),(size_t)SM693_MAX_SLOTS*sizeof(struct sm693_job));
    printf("ASYNC693_PASS checks=%u synthetic_no_hardware\n",checks);
    return 0;
}
