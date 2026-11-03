#include "spectrum_async693.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

enum sm693_slot_state { SLOT_FREE, SLOT_PENDING, SLOT_INFLIGHT,
                        SLOT_FAILED, SLOT_DONE };
struct sm693_slot {
    enum sm693_slot_state state;
    int writer_rc;
    struct sm693_job job;
};
struct sm693_queue {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    pthread_t owner,worker;
    sm693_writer writer;
    void *user;
    unsigned capacity,head,occupied;
    int stopping;
    uint64_t next_seq,submitted,durable;
    struct sm693_slot slot[];
};

static int owned(const struct sm693_queue *q) {
    return q && pthread_equal(q->owner,pthread_self());
}
static struct sm693_slot *at(struct sm693_queue *q,unsigned offset) {
    return &q->slot[(q->head+offset)%q->capacity];
}
/* Scan only the bounded live FIFO. DONE may precede pending because poll is a
 * separate owner action. The first non-DONE slot controls writer ordering. */
static struct sm693_slot *next_work(struct sm693_queue *q) {
    unsigned i;
    for(i=0;i<q->occupied;i++) {
        struct sm693_slot *s=at(q,i);
        if(s->state!=SLOT_DONE) return s;
    }
    return NULL;
}
static int has_failed(struct sm693_queue *q) {
    unsigned i;
    for(i=0;i<q->occupied;i++)
        if(at(q,i)->state==SLOT_FAILED) return 1;
    return 0;
}
static void *work(void *arg) {
    struct sm693_queue *q=arg;
    (void)pthread_mutex_lock(&q->mutex);
    for(;;) {
        struct sm693_slot *s=next_work(q);
        int rc;
        if(q->stopping) break;
        if(!s || s->state!=SLOT_PENDING) {
            (void)pthread_cond_wait(&q->changed,&q->mutex);
            continue;
        }
        s->state=SLOT_INFLIGHT;
        (void)pthread_cond_broadcast(&q->changed);
        (void)pthread_mutex_unlock(&q->mutex);
        rc=q->writer(q->user,&s->job);
        (void)pthread_mutex_lock(&q->mutex);
        s->writer_rc=rc;
        if(rc==0) {
            s->state=SLOT_DONE;
            ++q->durable;
        } else s->state=SLOT_FAILED;
        (void)pthread_cond_broadcast(&q->changed);
    }
    (void)pthread_mutex_unlock(&q->mutex);
    return NULL;
}

struct sm693_queue *sm693_create(unsigned capacity,uint64_t first_seq,
                               sm693_writer writer,void *user) {
    struct sm693_queue *q;
    size_t bytes;
    if(!capacity || capacity>SM693_MAX_SLOTS || !first_seq || !writer)
        return NULL;
    if(sizeof(q->slot[0])>(SIZE_MAX-sizeof(*q))/(size_t)capacity) return NULL;
    bytes=sizeof(*q)+(size_t)capacity*sizeof(q->slot[0]);
    q=calloc(1,bytes);
    if(!q) return NULL;
    q->owner=pthread_self();
    q->capacity=capacity;q->next_seq=first_seq;q->writer=writer;q->user=user;
    if(pthread_mutex_init(&q->mutex,NULL)!=0) { free(q);return NULL; }
    if(pthread_cond_init(&q->changed,NULL)!=0) {
        (void)pthread_mutex_destroy(&q->mutex);free(q);return NULL;
    }
    if(pthread_create(&q->worker,NULL,work,q)!=0) {
        (void)pthread_cond_destroy(&q->changed);
        (void)pthread_mutex_destroy(&q->mutex);free(q);return NULL;
    }
    return q;
}

static int basic_event(const struct sm613_event *e) {
    unsigned i;
    if(!e || e->pre_count>SM613_PRE || e->post_count>SM613_POST ||
       e->limits.bins>SM612_MAX_BINS || e->first.count>SM613_MAX_SAMPLES ||
       e->worst.count>SM613_MAX_SAMPLES || e->end_frame.count>SM613_MAX_SAMPLES)
        return 0;
    for(i=0;i<e->pre_count;i++)
        if(e->pre[i].count>SM613_MAX_SAMPLES) return 0;
    for(i=0;i<e->post_count;i++)
        if(e->post[i].count>SM613_MAX_SAMPLES) return 0;
    return 1;
}
static int submit(struct sm693_queue *q,enum sm693_kind kind,
                  const struct sm613_event *event,const struct sm615_view *view,
                  const struct sm612_model *model,
                  const struct sm655_receipt *receipt,uint64_t *seq) {
    struct sm693_slot *s;
    int rc=SM693_OK;
    if(!owned(q) || !basic_event(event) || !model || !receipt || !seq ||
       model->bins>SM612_MAX_BINS) return SM693_INVALID;
    if(kind==SM693_CLOSED && !event->closed) return SM693_INVALID;
    if(kind==SM693_ACTIVE && (!view || view->event!=event || event->closed ||
                             !view->generation)) return SM693_INVALID;
    (void)pthread_mutex_lock(&q->mutex);
    if(q->stopping) rc=SM693_BUSY;
    else if(has_failed(q)) rc=SM693_BLOCKED;
    else if(!q->next_seq || q->submitted==UINT64_MAX) rc=SM693_OVERFLOW;
    else if(q->occupied==q->capacity) rc=SM693_FULL;
    if(rc!=SM693_OK) {
        (void)pthread_mutex_unlock(&q->mutex);return rc;
    }
    s=at(q,q->occupied);
    s->job.seq=q->next_seq;s->job.kind=kind;
    memcpy(&s->job.event,event,sizeof(*event));
    memcpy(&s->job.audit_model,model,sizeof(*model));
    memcpy(&s->job.receipt,receipt,sizeof(*receipt));
    memset(&s->job.view,0,sizeof(s->job.view));
    if(view) {
        s->job.view.generation=view->generation;
        s->job.view.snapshot_us=view->snapshot_us;
        s->job.view.observed_token=view->observed_token;
        s->job.view.observed_us=view->observed_us;
        s->job.view.event=&s->job.event;
    }
    s->writer_rc=0;s->state=SLOT_PENDING;
    *seq=q->next_seq;
    q->next_seq=q->next_seq==UINT64_MAX?0:q->next_seq+1;
    ++q->submitted;++q->occupied;
    (void)pthread_cond_broadcast(&q->changed);
    (void)pthread_mutex_unlock(&q->mutex);
    return SM693_OK;
}
int sm693_submit_closed(struct sm693_queue *q,const struct sm613_event *event,
                       const struct sm612_model *model,
                       const struct sm655_receipt *receipt,uint64_t *seq) {
    return submit(q,SM693_CLOSED,event,NULL,model,receipt,seq);
}
int sm693_submit_active(struct sm693_queue *q,const struct sm615_view *view,
                       const struct sm612_model *model,
                       const struct sm655_receipt *receipt,uint64_t *seq) {
    return submit(q,SM693_ACTIVE,view?view->event:NULL,view,model,receipt,seq);
}
int sm693_get_status(struct sm693_queue *q,struct sm693_status *out) {
    unsigned i;
    if(!owned(q) || !out) return SM693_INVALID;
    (void)pthread_mutex_lock(&q->mutex);
    memset(out,0,sizeof(*out));
    out->capacity=q->capacity;out->occupied=q->occupied;
    out->submitted=q->submitted;out->durable=q->durable;
    out->next_seq=q->next_seq;
    for(i=0;i<q->occupied;i++) {
        const struct sm693_slot *s=at(q,i);
        switch(s->state) {
        case SLOT_PENDING:++out->pending;break;
        case SLOT_INFLIGHT:++out->inflight;break;
        case SLOT_FAILED:
            ++out->failed;out->failed_seq=s->job.seq;
            out->writer_rc=s->writer_rc;break;
        case SLOT_DONE:++out->completed;break;
        default:break;
        }
    }
    (void)pthread_mutex_unlock(&q->mutex);
    return SM693_OK;
}
int sm693_poll(struct sm693_queue *q,struct sm693_completion *out) {
    struct sm693_slot *s;
    if(!owned(q) || !out) return SM693_INVALID;
    (void)pthread_mutex_lock(&q->mutex);
    if(!q->occupied || at(q,0)->state!=SLOT_DONE) {
        (void)pthread_mutex_unlock(&q->mutex);return 0;
    }
    s=at(q,0);
    memset(out,0,sizeof(*out));
    out->seq=s->job.seq;out->kind=s->job.kind;
    out->session=s->job.event.limits.identity.session;
    out->capture_id=s->job.event.capture_id;out->event_id=s->job.event.event_id;
    out->confirmed=s->job.event.confirmed;
    out->generation=s->job.view.generation;
    out->first_token=s->job.event.first.token;
    out->worst_bin=s->job.event.worst_decision.worst_bin;
    out->worst_direction=s->job.event.worst_decision.worst_direction;
    out->end=(unsigned)s->job.event.end;out->end_us=s->job.event.end_us;
    s->state=SLOT_FREE;
    q->head=(q->head+1)%q->capacity;--q->occupied;
    (void)pthread_cond_broadcast(&q->changed);
    (void)pthread_mutex_unlock(&q->mutex);
    return 1;
}
int sm693_retry(struct sm693_queue *q,uint64_t failed_seq) {
    struct sm693_slot *s;
    if(!owned(q) || !failed_seq) return SM693_INVALID;
    (void)pthread_mutex_lock(&q->mutex);
    s=next_work(q);
    if(!s || s->state!=SLOT_FAILED || s->job.seq!=failed_seq) {
        (void)pthread_mutex_unlock(&q->mutex);return SM693_INVALID;
    }
    s->state=SLOT_PENDING;s->writer_rc=0;
    (void)pthread_cond_broadcast(&q->changed);
    (void)pthread_mutex_unlock(&q->mutex);
    return SM693_OK;
}
int sm693_drain(struct sm693_queue *q) {
    struct sm693_slot *s;
    if(!owned(q)) return SM693_INVALID;
    (void)pthread_mutex_lock(&q->mutex);
    while((s=next_work(q))!=NULL) {
        if(s->state==SLOT_FAILED) {
            (void)pthread_mutex_unlock(&q->mutex);return SM693_BLOCKED;
        }
        (void)pthread_cond_wait(&q->changed,&q->mutex);
    }
    (void)pthread_mutex_unlock(&q->mutex);
    return SM693_OK;
}
int sm693_destroy(struct sm693_queue *q) {
    if(!owned(q)) return SM693_INVALID;
    (void)pthread_mutex_lock(&q->mutex);
    if(q->occupied) {
        (void)pthread_mutex_unlock(&q->mutex);return SM693_BUSY;
    }
    q->stopping=1;
    (void)pthread_cond_broadcast(&q->changed);
    (void)pthread_mutex_unlock(&q->mutex);
    if(pthread_join(q->worker,NULL)!=0) return SM693_THREAD;
    (void)pthread_cond_destroy(&q->changed);
    (void)pthread_mutex_destroy(&q->mutex);
    free(q);
    return SM693_OK;
}
