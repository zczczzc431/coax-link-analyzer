#include "spectrum_evidence613.h"
#include <string.h>

static int monitoring(const struct sm613_context *c) {
    return c->core.state==SM612_ARMED || c->core.state==SM612_CANDIDATE || c->core.state==SM612_ALARM;
}
static void history_clear(struct sm613_context *c) {
    c->history_count=0; c->history_next=0;
}
static void result_init(const struct sm613_context *c,struct sm613_result *r) {
    memset(r,0,sizeof(*r));
    if(c) { r->measurement.state=c->core.state; r->measurement.reason=c->core.reason; }
}
static int fail_blocked(struct sm613_context *c,struct sm613_result *r) {
    r->status=(enum sm613_status)c->blocked; return -2;
}
static void suspend_core(struct sm613_context *c) {
    struct sm612_decision ignored;
    sm612_stop(&c->core,1,&ignored);
    c->watching=0; history_clear(c);
}
static int raw_valid(const struct sm612_frame *f,const uint32_t *raw,size_t n) {
    if(!f || !raw || n!=f->identity.fft_size || n<8 || n>SM613_MAX_SAMPLES) return 0;
    for(size_t i=0;i<n;i++) if(raw[i]>16383u) return 0;
    return 1;
}
static void copy_raw(struct sm613_raw *to,const struct sm612_frame *f,const uint32_t *raw,size_t n) {
    to->token=f->token; to->timestamp_us=f->timestamp_us; to->count=(unsigned)n;
    to->valid=f->valid; to->overload=f->overload;
    memcpy(to->sample,raw,n*sizeof(*raw));
}
static void history_add(struct sm613_context *c,const struct sm612_frame *f,const uint32_t *raw,size_t n) {
    copy_raw(&c->history[c->history_next],f,raw,n);
    c->history_next=(c->history_next+1u)%SM613_PRE;
    if(c->history_count<SM613_PRE) c->history_count++;
}
static void unretained(struct sm613_context *c,const struct sm612_decision *d) {
    if(!(d->flags&SM612_FIRST)) return;
    c->unretained_first=1; c->unretained_first_token=d->first_token;
    c->unretained_event_id=d->event_id;
}
static int close_capture(struct sm613_context *c,enum sm613_end end,enum sm612_reason reason,
                         uint64_t now,const struct sm612_frame *f,const uint32_t *raw,size_t n,
                         struct sm613_result *r) {
    if(!c->active) return 0;
    c->event.closed=1; c->event.end=end; c->event.end_reason=reason; c->event.end_us=now;
    c->event.end_frame_present=f!=NULL;
    if(f) copy_raw(&c->event.end_frame,f,raw,n);
    int sink_rc=c->sink(c->sink_user,&c->event);
    if(sink_rc!=0 && sink_rc!=SM613_SINK_QUEUED) {
        c->blocked=SM613_STORE_ERROR;
        suspend_core(c); r->status=SM613_STORE_ERROR; return -2;
    }
    c->active=0;
    if(sink_rc==0) r->published++; else r->queued++;
    return 0;
}
static int begin_capture(struct sm613_context *c,const struct sm612_frame *f,
                         const uint32_t *raw,size_t n,const struct sm612_decision *d,
                         struct sm613_result *r) {
    if(c->next_capture==UINT64_MAX) {
        unretained(c,d); c->blocked=SM613_CAPTURE_LIMIT;
        suspend_core(c); r->status=SM613_CAPTURE_LIMIT; return -2;
    }
    struct sm613_event *e=&c->event;
    memset(e,0,sizeof(*e));
    e->capture_id=++c->next_capture;
    e->pre_count=c->history_count;
    unsigned start=(c->history_next+SM613_PRE-c->history_count)%SM613_PRE;
    for(unsigned i=0;i<c->history_count;i++) e->pre[i]=c->history[(start+i)%SM613_PRE];
    copy_raw(&e->first,f,raw,n); e->worst=e->first;
    memcpy(e->first_code,f->code,f->bins*sizeof(double));
    memcpy(e->worst_code,f->code,f->bins*sizeof(double));
    e->first_decision=e->worst_decision=*d;
    e->limits.identity=c->core.identity; e->limits.config=c->core.config; e->limits.bins=c->core.bins;
    memcpy(e->limits.baseline_db,c->core.baseline_db,c->core.bins*sizeof(double));
    memcpy(e->limits.upper_db,c->core.upper_db,c->core.bins*sizeof(double));
    memcpy(e->limits.lower_db,c->core.lower_db,c->core.bins*sizeof(double));
    memcpy(e->limits.lower_valid,c->core.lower_valid,c->core.bins);
    c->active=1; return 0;
}
static enum sm613_end interruption_kind(const struct sm612_decision *d) {
    if(d->reason==SM612_IDENTITY_MISMATCH) return SM613_END_IDENTITY;
    if(d->reason==SM612_BAD_FRAME || d->reason==SM612_OVERLOAD) return SM613_END_QUALITY;
    return SM613_END_GAP;
}
static int time_valid(const struct sm613_context *c,uint64_t now) {
    return now>=c->clock_us && now>=c->watch_us &&
        (!c->core.have_frame || now>=c->core.last_us);
}
int sm613_init(struct sm613_context *c,sm613_sink sink,void *user) {
    if(!c || !sink) return -1;
    memset(c,0,sizeof(*c)); sm612_init(&c->core);
    c->sink=sink; c->sink_user=user; return 0;
}
int sm613_learn(struct sm613_context *c,const struct sm612_config *config,
                const struct sm612_identity *identity) {
    if(!c || !c->sink || c->blocked || c->active) return -1;
    if(sm612_begin_learn(&c->core,config,identity)!=0) return -1;
    history_clear(c); c->watching=0; c->watch_us=0;
    return 0;
}
int sm613_arm(struct sm613_context *c,const struct sm612_identity *identity,uint64_t now) {
    if(!c || !c->sink || c->blocked || c->active || !time_valid(c,now)) return -1;
    if(sm612_arm(&c->core,identity)!=0) return -1;
    c->clock_us=now; c->watch_us=now; c->watching=1; return 0;
}
int sm613_feed(struct sm613_context *c,const struct sm612_frame *f,
               const uint32_t *raw,size_t n,struct sm613_result *r) {
    if(!r) return -1;
    result_init(c,r);
    if(!c || !c->sink || !f) { r->status=SM613_BAD_ARGUMENT; return -1; }
    if(c->blocked) return fail_blocked(c,r);
    int engaged=monitoring(c) || c->core.state==SM612_LEARNING;
    if(!engaged) {
        r->core_rc=sm612_push(&c->core,f,&r->measurement);
        return r->core_rc; /* Explicit NOT_ARMED, no raw history mutation. */
    }
    /* A genuinely old/duplicate core frame must still break continuity through
     * the core. A NEW observation predating ARM/control time is not consumed:
     * otherwise ARM could move the watchdog ahead, then feed move it back. */
    if(f->timestamp_us<c->clock_us &&
       (!c->core.have_frame || f->timestamp_us>c->core.last_us)) {
        r->status=SM613_BAD_TIME; return -1;
    }
    if(f->timestamp_us>c->clock_us) c->clock_us=f->timestamp_us;
    int good_raw=raw_valid(f,raw,n);
    struct sm612_frame checked=*f;
    if(!good_raw) checked.valid=0;
    r->core_rc=sm612_push(&c->core,&checked,&r->measurement);
    /* Rejections can leave INVALID after identity/learning failure. Stop the
     * watchdog before any early return so poll cannot overwrite that cause. */
    c->watching=monitoring(c) || c->core.state==SM612_LEARNING;
    const struct sm612_decision *d=&r->measurement;
    int rejected=(d->flags&SM612_REJECTED)!=0;
    if(rejected) r->status=good_raw?SM613_CORE_REJECTED:SM613_BAD_RAW;
    int boundary=(d->flags&(SM612_GAP|SM612_REJECTED|SM612_INTERRUPTED))!=0;
    if(boundary) {
        enum sm613_end end=interruption_kind(d);
        int attach=end==SM613_END_QUALITY && good_raw &&
            sm612_identity_equal(&c->core.identity,&f->identity);
        uint64_t observed=c->clock_us;
        history_clear(c);
        if(close_capture(c,end,d->reason,observed,attach?f:NULL,raw,n,r)!=0) {
            unretained(c,d); return -2;
        }
    }
    if(rejected) return -1;
    c->watch_us=f->timestamp_us;
    if(d->flags&SM612_FIRST) {
        if(begin_capture(c,f,raw,n,d,r)!=0) return -2;
    } else if(c->active && c->event.post_count<SM613_POST) {
        copy_raw(&c->event.post[c->event.post_count++],f,raw,n);
    }
    if(c->active) {
        if(d->flags&SM612_WORST) {
            copy_raw(&c->event.worst,f,raw,n);
            memcpy(c->event.worst_code,f->code,f->bins*sizeof(double));
            c->event.worst_decision=*d;
        }
        if(d->flags&SM612_CONFIRMED) {
            c->event.confirmed=1; c->event.event_id=d->event_id;
        }
        if(d->flags&(SM612_RECOVERED|SM612_UNCONFIRMED)) {
            enum sm613_end end=(d->flags&SM612_RECOVERED)?SM613_END_RECOVERED:SM613_END_TRANSIENT;
            if(close_capture(c,end,d->reason,f->timestamp_us,f,raw,n,r)!=0) return -2;
        }
    }
    if(monitoring(c)) history_add(c,f,raw,n);
    return r->core_rc;
}
int sm613_stop(struct sm613_context *c,int suspended,uint64_t now,struct sm613_result *r) {
    if(!r) return -1;
    result_init(c,r);
    if(!c || !c->sink) { r->status=SM613_BAD_ARGUMENT; return -1; }
    if(c->blocked) return fail_blocked(c,r);
    if(!time_valid(c,now)) { r->status=SM613_BAD_TIME; return -1; }
    c->clock_us=now;
    sm612_stop(&c->core,suspended,&r->measurement);
    c->watching=0; history_clear(c);
    return close_capture(c,suspended?SM613_END_SUSPEND:SM613_END_STOP,
                         r->measurement.reason,now,NULL,NULL,0,r);
}
int sm613_poll(struct sm613_context *c,uint64_t now,struct sm613_result *r) {
    if(!r) return -1;
    result_init(c,r);
    if(!c || !c->sink) { r->status=SM613_BAD_ARGUMENT; return -1; }
    if(c->blocked) return fail_blocked(c,r);
    if(!time_valid(c,now)) { r->status=SM613_BAD_TIME; return -1; }
    c->clock_us=now;
    if(!c->watching || now-c->watch_us<=c->core.config.max_gap_us) return 0;
    sm612_stop(&c->core,1,&r->measurement);
    r->measurement.flags|=SM612_GAP;
    c->watching=0; history_clear(c);
    if(close_capture(c,SM613_END_TIMEOUT,r->measurement.reason,now,NULL,NULL,0,r)!=0) return -2;
    r->status=SM613_TIMEOUT; return 1;
}
int sm613_retry(struct sm613_context *c,struct sm613_result *r) {
    if(!r) return -1;
    result_init(c,r);
    if(!c || !c->sink) { r->status=SM613_BAD_ARGUMENT; return -1; }
    if(!c->blocked) { r->status=SM613_BUSY; return -1; }
    if(c->blocked!=SM613_STORE_ERROR || !c->active || !c->event.closed) return fail_blocked(c,r);
    int sink_rc=c->sink(c->sink_user,&c->event);
    if(sink_rc!=0 && sink_rc!=SM613_SINK_QUEUED) return fail_blocked(c,r);
    c->active=0; c->blocked=0;
    if(sink_rc==0) r->published=1; else r->queued=1;
    return 0;
}
