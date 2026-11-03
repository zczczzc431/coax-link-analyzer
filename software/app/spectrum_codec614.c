#define _POSIX_C_SOURCE 200809L
#include "spectrum_codec614.h"
#include "spectrum_checkpoint615.h"
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

_Static_assert(CHAR_BIT==8 && sizeof(double)==8 && DBL_MANT_DIG==53 &&
               DBL_MAX_EXP==1024,"SM614 requires IEEE754 binary64");

static int boolean(int v) { return v==0 || v==1; }
static int identity_valid(const struct sm612_identity *id) {
    return id->session && isfinite(id->fs_hz) && id->fs_hz>0 &&
        id->format==SM612_FORMAT && id->fft_size>=8 &&
        id->fft_size<=SM613_MAX_SAMPLES && !(id->fft_size&(id->fft_size-1));
}
static int config_valid(const struct sm612_config *c,size_t bins) {
    if(c->first_bin<1 || c->first_bin>c->last_bin || c->last_bin>=bins ||
       c->learn_frames<2 || c->learn_frames>128 || c->enter_frames<1 ||
       c->enter_frames>32 || c->recover_frames<1 || c->recover_frames>32 ||
       !c->max_gap_us || !boolean(c->upper_enabled) || !boolean(c->lower_enabled) ||
       (!c->upper_enabled && !c->lower_enabled)) return 0;
    const double v[]={c->upper_margin_db,c->lower_margin_db,c->floor_db,
        c->lower_signal_margin_db,c->min_learn_peak_db,c->max_learn_spread_db,
        c->hysteresis_db};
    for(size_t i=0;i<sizeof(v)/sizeof(v[0]);i++) if(!isfinite(v[i])) return 0;
    if(c->upper_margin_db<0 || c->upper_margin_db>120 || c->lower_margin_db<0 ||
       c->lower_margin_db>120 || c->floor_db<-200 || c->floor_db>0 ||
       c->lower_signal_margin_db<0 || c->lower_signal_margin_db>120 ||
       c->min_learn_peak_db<-200 || c->min_learn_peak_db>0 ||
       c->max_learn_spread_db<=0 || c->max_learn_spread_db>120 ||
       c->hysteresis_db<0 || (c->upper_enabled && c->hysteresis_db>c->upper_margin_db) ||
       (c->lower_enabled && c->hysteresis_db>c->lower_margin_db)) return 0;
    unsigned included=0;
    for(size_t k=0;k<SM612_MAX_BINS;k++) {
        if(c->exclude[k]>1) return 0;
        if(k>=c->first_bin && k<=c->last_bin && !c->exclude[k]) included++;
    }
    return included!=0;
}
static int raw_valid(const struct sm613_raw *r,unsigned count,int good) {
    if(r->count!=count || !boolean(r->valid) || !boolean(r->overload) ||
       (good && (!r->valid || r->overload))) return 0;
    for(unsigned i=0;i<r->count;i++) if(r->sample[i]>16383u) return 0;
    return 1;
}
static int raw_same(const struct sm613_raw *a,const struct sm613_raw *b) {
    if(a->token!=b->token || a->timestamp_us!=b->timestamp_us || a->count!=b->count ||
       a->valid!=b->valid || a->overload!=b->overload) return 0;
    for(unsigned i=0;i<a->count;i++) if(a->sample[i]!=b->sample[i]) return 0;
    return 1;
}
static int follows(const struct sm613_raw *a,const struct sm613_raw *b,uint64_t gap) {
    return b->token-a->token==UINT32_C(1) && b->timestamp_us>a->timestamp_us &&
        b->timestamp_us-a->timestamp_us<=gap;
}
static int direction_valid(const struct sm613_template *t,unsigned bin,int direction) {
    const struct sm612_config *c=&t->config;
    if(bin<c->first_bin || bin>c->last_bin || c->exclude[bin]) return 0;
    return (direction==1 && c->upper_enabled) ||
        (direction==-1 && c->lower_enabled && t->lower_valid[bin]);
}
static int decision_valid(const struct sm613_event *e,const struct sm612_decision *d,
                          const struct sm613_raw *raw,int first) {
    const unsigned allowed=SM612_FIRST|SM612_CONFIRMED|SM612_WORST|SM612_GAP|
        SM612_INTERRUPTED|SM612_UNCONFIRMED;
    if((d->state!=SM612_CANDIDATE && d->state!=SM612_ALARM) ||
       (d->reason!=SM612_OK && d->reason!=SM612_SEQUENCE_GAP) ||
       (d->flags&~allowed) || !(d->flags&SM612_WORST) ||
       (first && !(d->flags&SM612_FIRST)) ||
       d->first_us!=e->first.timestamp_us || d->first_token!=e->first.token ||
       d->frame_token!=raw->token || d->worst_token!=raw->token ||
       d->timestamp_us!=raw->timestamp_us || d->worst_us!=raw->timestamp_us ||
       d->current_bin!=d->worst_bin || d->current_direction!=d->worst_direction ||
       !direction_valid(&e->limits,d->worst_bin,d->worst_direction) ||
       !isfinite(d->current_excess_db) || !isfinite(d->worst_excess_db) ||
       d->current_excess_db<=0 || d->current_excess_db!=d->worst_excess_db ||
       (d->event_id && (!e->confirmed || d->event_id!=e->event_id)) ||
       (d->state==SM612_ALARM)!=(d->event_id!=0) ||
       ((d->flags&SM612_CONFIRMED) && d->state!=SM612_ALARM) ||
       ((d->flags&(SM612_INTERRUPTED|SM612_UNCONFIRMED)) && !(d->flags&SM612_GAP)) ||
       ((d->flags&SM612_INTERRUPTED) && !d->ended_event_id) ||
       (d->ended_event_id && !(d->flags&SM612_INTERRUPTED)) ||
       ((d->flags&SM612_GAP)!=0)!=(d->reason==SM612_SEQUENCE_GAP)) return 0;
    return 1;
}
static int decision_same(const struct sm612_decision *a,const struct sm612_decision *b) {
    return a->state==b->state && a->reason==b->reason && a->flags==b->flags &&
        a->event_id==b->event_id && a->ended_event_id==b->ended_event_id &&
        a->first_us==b->first_us && a->worst_us==b->worst_us &&
        a->frame_token==b->frame_token && a->first_token==b->first_token &&
        a->worst_token==b->worst_token && a->timestamp_us==b->timestamp_us &&
        a->current_bin==b->current_bin && a->worst_bin==b->worst_bin &&
        a->current_direction==b->current_direction && a->worst_direction==b->worst_direction &&
        a->current_excess_db==b->current_excess_db && a->worst_excess_db==b->worst_excess_db;
}
static int close_valid(const struct sm613_event *e) {
    switch(e->end) {
    case SM613_END_RECOVERED:
        return e->confirmed && e->end_frame_present && e->end_reason==SM612_OK;
    case SM613_END_TRANSIENT:
        return !e->confirmed && e->end_frame_present && e->end_reason==SM612_OK;
    case SM613_END_GAP:
        return !e->end_frame_present && (e->end_reason==SM612_DUPLICATE ||
            e->end_reason==SM612_STALE_FRAME || e->end_reason==SM612_SEQUENCE_GAP ||
            e->end_reason==SM612_COUNTER_LIMIT);
    case SM613_END_QUALITY:
        return e->end_reason==SM612_BAD_FRAME || e->end_reason==SM612_OVERLOAD;
    case SM613_END_IDENTITY:
        return !e->end_frame_present && e->end_reason==SM612_IDENTITY_MISMATCH;
    case SM613_END_STOP:
        return !e->end_frame_present && e->end_reason==SM612_MANUAL_STOP;
    case SM613_END_SUSPEND:
    case SM613_END_TIMEOUT:
        return !e->end_frame_present && e->end_reason==SM612_OWNER_SUSPEND;
    default: return 0;
    }
}
static int event_valid(const struct sm613_event *e,int active,uint64_t through_us) {
    if(!e || !e->capture_id || !boolean(e->confirmed) ||
       !boolean(e->end_frame_present) || e->pre_count>SM613_PRE ||
       e->post_count>SM613_POST || !identity_valid(&e->limits.identity) ||
       e->limits.bins!=e->limits.identity.fft_size/2u+1u ||
       e->limits.bins>SM612_MAX_BINS ||
       !config_valid(&e->limits.config,e->limits.bins) ||
       ((e->event_id!=0)!=e->confirmed)) return 0;
    if(active) {
        if(e->closed!=0 || e->end!=SM613_END_NONE || e->end_reason!=SM612_OK ||
           e->end_us!=0 || e->end_frame_present!=0) return 0;
    } else if(e->closed!=1 || !close_valid(e)) return 0;
    const struct sm613_template *t=&e->limits;
    const struct sm612_config *c=&t->config;
    for(size_t k=0;k<t->bins;k++) {
        if(!isfinite(t->baseline_db[k]) || !isfinite(t->upper_db[k]) ||
           !isfinite(t->lower_db[k]) || t->lower_db[k]>t->baseline_db[k] ||
           t->upper_db[k]<c->floor_db ||
           t->lower_valid[k]>1 || !isfinite(e->first_code[k]) ||
           !isfinite(e->worst_code[k]) || e->first_code[k]<0 || e->worst_code[k]<0)
            return 0;
        double q=e->first_code[k]/8191.0,w=e->worst_code[k]/8191.0;
        if(!isfinite(q*q) || !isfinite(w*w)) return 0;
        if(t->lower_valid[k] && (!c->lower_enabled || k<c->first_bin ||
           k>c->last_bin || c->exclude[k] || t->lower_db[k]<c->floor_db ||
           t->baseline_db[k]<c->floor_db+c->lower_signal_margin_db)) return 0;
    }
    unsigned n=t->identity.fft_size;
    if(!raw_valid(&e->first,n,1) || !raw_valid(&e->worst,n,1) ||
       !decision_valid(e,&e->first_decision,&e->first,1) ||
       !decision_valid(e,&e->worst_decision,&e->worst,0) ||
       e->worst.timestamp_us<e->first.timestamp_us || through_us<e->worst.timestamp_us ||
       e->worst_decision.worst_excess_db<e->first_decision.worst_excess_db) return 0;
    if((c->enter_frames==1)!=(e->first_decision.state==SM612_ALARM) ||
       ((e->first_decision.flags&SM612_CONFIRMED)!=0)!=(c->enter_frames==1)) return 0;
    if(e->worst.timestamp_us==e->first.timestamp_us) {
        if(!raw_same(&e->first,&e->worst) ||
           !decision_same(&e->first_decision,&e->worst_decision)) return 0;
        for(size_t k=0;k<t->bins;k++) if(e->first_code[k]!=e->worst_code[k]) return 0;
    } else if((e->worst_decision.flags&SM612_FIRST) ||
              e->worst_decision.worst_excess_db<=e->first_decision.worst_excess_db)
        return 0;
    for(unsigned i=0;i<e->pre_count;i++) {
        if(!raw_valid(&e->pre[i],n,1) ||
           !follows(&e->pre[i],i+1u<e->pre_count?&e->pre[i+1u]:&e->first,c->max_gap_us))
            return 0;
    }
    const struct sm613_raw *last=&e->first;
    int worst_seen=e->worst.timestamp_us==e->first.timestamp_us;
    for(unsigned i=0;i<e->post_count;i++) {
        const struct sm613_raw *r=&e->post[i];
        if(!raw_valid(r,n,1) || !follows(last,r,c->max_gap_us) ||
           r->timestamp_us>through_us ||
           (r->timestamp_us==e->worst.timestamp_us && !raw_same(r,&e->worst))) return 0;
        if(r->timestamp_us==e->worst.timestamp_us) worst_seen=1;
        last=r;
    }
    if(!worst_seen && (e->post_count<SM613_POST || e->worst.timestamp_us<=last->timestamp_us))
        return 0;
    if(e->end_frame_present) {
        const struct sm613_raw *r=&e->end_frame;
        int good=e->end!=SM613_END_QUALITY;
        if(!raw_valid(r,n,good) || r->timestamp_us!=e->end_us ||
           r->timestamp_us<=e->first.timestamp_us || r->timestamp_us<last->timestamp_us ||
           (r->timestamp_us==last->timestamp_us && !raw_same(last,r)) ||
           (r->timestamp_us==e->worst.timestamp_us && !raw_same(&e->worst,r))) return 0;
        if(e->end==SM613_END_QUALITY && e->end_reason==SM612_OVERLOAD && !r->overload)
            return 0;
    }
    return 1;
}
int sm614_event_valid(const struct sm613_event *e) {
    return event_valid(e,0,e?e->end_us:0);
}
static int observed_after(const struct sm615_view *v,const struct sm613_raw *r) {
    return r->timestamp_us<=v->observed_us &&
        v->observed_token-r->token<=(uint32_t)INT32_MAX &&
        ((r->timestamp_us==v->observed_us)==(r->token==v->observed_token));
}
int sm615_view_valid(const struct sm615_view *v) {
    if(!v || !v->generation || v->snapshot_us<v->observed_us ||
       !event_valid(v->event,1,v->observed_us)) return 0;
    const struct sm613_event *e=v->event;
    if(!observed_after(v,&e->first) || !observed_after(v,&e->worst)) return 0;
    for(unsigned i=0;i<e->pre_count;i++) if(!observed_after(v,&e->pre[i])) return 0;
    for(unsigned i=0;i<e->post_count;i++) if(!observed_after(v,&e->post[i])) return 0;
    return 1;
}

/* Small bounded stream buffer; no event/raw copies or giant stack arrays. */
struct output { sm614_emit emit; void *user; size_t total,used,max; unsigned char data[2048]; };
static int flush(struct output *o) {
    if(o->used && o->emit(o->user,o->data,o->used)!=0) return -1;
    o->used=0; return 0;
}
static int bytes(struct output *o,const void *data,size_t n) {
    const unsigned char *p=data;
    if(n>o->max-o->total) return -1;
    o->total+=n;
    while(n) {
        size_t take=sizeof(o->data)-o->used;
        if(take>n) take=n;
        memcpy(o->data+o->used,p,take); o->used+=take; p+=take; n-=take;
        if(o->used==sizeof(o->data) && flush(o)!=0) return -1;
    }
    return 0;
}
static int u32(struct output *o,uint32_t v) {
    unsigned char b[4];
    for(unsigned i=0;i<4;i++) b[i]=(unsigned char)(v>>(i*8));
    return bytes(o,b,sizeof(b));
}
static int u64(struct output *o,uint64_t v) {
    unsigned char b[8];
    for(unsigned i=0;i<8;i++) b[i]=(unsigned char)(v>>(i*8));
    return bytes(o,b,sizeof(b));
}
static int f64(struct output *o,double v) {
    uint64_t bits;
    memcpy(&bits,&v,sizeof(bits)); return u64(o,bits);
}
static int decision_bin(struct output *o,const struct sm612_decision *d) {
    return u32(o,(uint32_t)d->state) || u32(o,(uint32_t)d->reason) || u32(o,d->flags) ||
        u64(o,d->event_id) || u64(o,d->ended_event_id) || u64(o,d->first_us) ||
        u64(o,d->worst_us) || u32(o,d->frame_token) || u32(o,d->first_token) ||
        u32(o,d->worst_token) || u64(o,d->timestamp_us) || u32(o,d->current_bin) ||
        u32(o,d->worst_bin) || u32(o,(uint32_t)(int32_t)d->current_direction) ||
        u32(o,(uint32_t)(int32_t)d->worst_direction) || f64(o,d->current_excess_db) ||
        f64(o,d->worst_excess_db) ? -1:0;
}
static int raw_bin(struct output *o,const struct sm613_raw *r) {
    if(u32(o,r->token) || u64(o,r->timestamp_us) || u32(o,r->count) ||
       u32(o,(uint32_t)r->valid) || u32(o,(uint32_t)r->overload)) return -1;
    for(unsigned i=0;i<r->count;i++) if(u32(o,r->sample[i])) return -1;
    return 0;
}
static int event_bin(struct output *o,const struct sm613_event *e) {
    const struct sm613_template *t=&e->limits;
    const struct sm612_identity *id=&t->identity;
    const struct sm612_config *c=&t->config;
    if(u64(o,e->capture_id) ||
       u64(o,e->event_id) || u32(o,(uint32_t)e->confirmed) || u32(o,(uint32_t)e->closed) ||
       u32(o,(uint32_t)e->end) || u32(o,(uint32_t)e->end_reason) || u64(o,e->end_us) ||
       u32(o,e->pre_count) || u32(o,e->post_count) || u32(o,(uint32_t)e->end_frame_present) ||
       u32(o,(uint32_t)t->bins) || u64(o,id->session) || u64(o,id->input_signature) ||
       u64(o,id->source_signature) || f64(o,id->fs_hz) || u32(o,id->fft_size) ||
       u32(o,id->format) || u32(o,c->first_bin) || u32(o,c->last_bin) ||
       u32(o,c->learn_frames) || u32(o,c->enter_frames) || u32(o,c->recover_frames) ||
       f64(o,c->upper_margin_db) || f64(o,c->lower_margin_db) || f64(o,c->floor_db) ||
       f64(o,c->lower_signal_margin_db) || f64(o,c->min_learn_peak_db) ||
       f64(o,c->max_learn_spread_db) || f64(o,c->hysteresis_db) || u64(o,c->max_gap_us) ||
       u32(o,(uint32_t)c->upper_enabled) || u32(o,(uint32_t)c->lower_enabled) ||
       bytes(o,c->exclude,SM612_MAX_BINS) || decision_bin(o,&e->first_decision) ||
       decision_bin(o,&e->worst_decision)) return -1;
    for(size_t k=0;k<t->bins;k++) {
        if(f64(o,t->baseline_db[k]) || f64(o,t->upper_db[k]) || f64(o,t->lower_db[k]) ||
           bytes(o,&t->lower_valid[k],1) || f64(o,e->first_code[k]) || f64(o,e->worst_code[k]))
            return -1;
    }
    for(unsigned i=0;i<e->pre_count;i++) if(raw_bin(o,&e->pre[i])) return -1;
    if(raw_bin(o,&e->first) || raw_bin(o,&e->worst)) return -1;
    for(unsigned i=0;i<e->post_count;i++) if(raw_bin(o,&e->post[i])) return -1;
    if(e->end_frame_present && raw_bin(o,&e->end_frame)) return -1;
    return flush(o);
}
int sm614_encode_bin(const struct sm613_event *e,sm614_emit emit,void *user) {
    if(!emit || !sm614_event_valid(e)) return -1;
    struct output o={emit,user,0,0,SM614_BIN_MAX,{0}};
    static const unsigned char magic[8]={'S','M','6','1','4','B','1',0};
    if(bytes(&o,magic,sizeof(magic)) || u32(&o,1)) return -1;
    return event_bin(&o,e);
}
int sm615_encode_bin(const struct sm615_view *v,sm614_emit emit,void *user) {
    if(!emit || !sm615_view_valid(v)) return -1;
    struct output o={emit,user,0,0,SM614_BIN_MAX,{0}};
    static const unsigned char magic[8]={'S','M','6','1','5','A','1',0};
    if(bytes(&o,magic,sizeof(magic)) || u32(&o,1) || u64(&o,v->generation) ||
       u64(&o,v->snapshot_us) || u32(&o,v->observed_token) || u64(&o,v->observed_us)) return -1;
    return event_bin(&o,v->event);
}

static int line(struct output *o,const char *format,...) {
    char b[1024];
    va_list ap;
    va_start(ap,format); int n=vsnprintf(b,sizeof(b),format,ap); va_end(ap);
    if(n<0 || (size_t)n>=sizeof(b)) return -1;
    return bytes(o,b,(size_t)n);
}
static int decision_csv(struct output *o,const char *role,const struct sm612_decision *d) {
    return line(o,"DECISION,%s,%u,%u,%u,%" PRIu64 ",%" PRIu64 ",%" PRIu64
        ",%" PRIu64 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu64
        ",%u,%u,%d,%d,%.17g,%.17g\n",role,(unsigned)d->state,(unsigned)d->reason,d->flags,
        d->event_id,d->ended_event_id,d->first_us,d->worst_us,d->frame_token,d->first_token,
        d->worst_token,d->timestamp_us,d->current_bin,d->worst_bin,d->current_direction,
        d->worst_direction,d->current_excess_db,d->worst_excess_db);
}
static int raw_csv(struct output *o,const char *role,unsigned index,const struct sm613_raw *r) {
    return line(o,"FRAME,%s,%u,%" PRIu32 ",%" PRIu64 ",%u,%d,%d\n",role,index,
        r->token,r->timestamp_us,r->count,r->valid,r->overload);
}
static int csv_body(struct output *o,const struct sm613_event *e,const struct sm615_view *v) {
    const struct sm613_template *t=&e->limits;
    const struct sm612_identity *id=&t->identity;
    const struct sm612_config *c=&t->config;
    if(v) {
        if(line(o,"SM615_CSV,1\n") ||
           line(o,"CHECKPOINT_HEADER,generation,snapshot_host_us,observed_token,observed_host_us\n") ||
           line(o,"CHECKPOINT,%" PRIu64 ",%" PRIu64 ",%" PRIu32 ",%" PRIu64 "\n",
                v->generation,v->snapshot_us,v->observed_token,v->observed_us) ||
           line(o,"NOTICE,state=ACTIVE_CHECKPOINT_NOT_CLOSED_NO_RESUME\n")) return -1;
    } else if(line(o,"SM614_CSV,1\n")) return -1;
    if(line(o,"NOTICE,ADC_samples=decoded_unsigned_14bit_CODE_in_binary_only\n") ||
       line(o,"NOTICE,FFT=single_sided_peak_amplitude_CODE_not_PSD\n") ||
       line(o,"NOTICE,template_unit=dBFS_CODE_full_scale_8191_uncalibrated_amplitude\n") ||
       line(o,"NOTICE,sparse_frames_not_continuous_capture_first_8_trigger_post_frames\n") ||
       line(o,"NOTICE,timestamps=host_monotonic_observation_us_not_ADC_epoch_or_physical_fault_duration\n") ||
       line(o,"EVENT_HEADER,capture_id,event_id,confirmed,closed,end,end_reason,end_host_us,pre_count,post_count,end_frame_present,bins\n") ||
       line(o,"EVENT,%" PRIu64 ",%" PRIu64 ",%d,%d,%u,%u,%" PRIu64 ",%u,%u,%d,%zu\n",
            e->capture_id,e->event_id,e->confirmed,e->closed,(unsigned)e->end,
            (unsigned)e->end_reason,e->end_us,e->pre_count,e->post_count,e->end_frame_present,t->bins) ||
       line(o,"IDENTITY_HEADER,session,input_signature,source_signature,fs_hz,fft_size,format\n") ||
       line(o,"IDENTITY,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.17g,%" PRIu32 ",%" PRIu32 "\n",
            id->session,id->input_signature,id->source_signature,id->fs_hz,id->fft_size,id->format) ||
       line(o,"CONFIG_HEADER,first_bin,last_bin,learn_frames,enter_frames,recover_frames,upper_margin_db,lower_margin_db,floor_dbFS,lower_signal_margin_db,min_learn_peak_dbFS,max_learn_spread_db,hysteresis_db,max_gap_us,upper_enabled,lower_enabled\n") ||
       line(o,"CONFIG,%u,%u,%u,%u,%u,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%" PRIu64 ",%d,%d\n",
            c->first_bin,c->last_bin,c->learn_frames,c->enter_frames,c->recover_frames,
            c->upper_margin_db,c->lower_margin_db,c->floor_db,c->lower_signal_margin_db,
            c->min_learn_peak_db,c->max_learn_spread_db,c->hysteresis_db,c->max_gap_us,
            c->upper_enabled,c->lower_enabled) ||
       line(o,"EXCLUDE_HEADER,bin,protected\n")) return -1;
    for(size_t k=0;k<SM612_MAX_BINS;k++)
        if(line(o,"EXCLUDE,%zu,%u\n",k,(unsigned)c->exclude[k])) return -1;
    if(line(o,"DECISION_HEADER,role,state,reason,flags,event_id,ended_event_id,first_host_us,worst_host_us,frame_token,first_token,worst_token,host_timestamp_us,current_bin,worst_bin,current_direction,worst_direction,current_excess_db,worst_excess_db\n") ||
       decision_csv(o,"FIRST",&e->first_decision) || decision_csv(o,"WORST",&e->worst_decision) ||
       line(o,"BIN_HEADER,bin,frequency_hz,baseline_dbFS,upper_dbFS,lower_dbFS,lower_valid,FIRST_peak_CODE,WORST_peak_CODE\n")) return -1;
    for(size_t k=0;k<t->bins;k++) {
        double frequency=id->fs_hz*((double)k/(double)id->fft_size);
        if(line(o,"BIN,%zu,%.17g,%.17g,%.17g,%.17g,%u,%.17g,%.17g\n",k,frequency,
            t->baseline_db[k],t->upper_db[k],t->lower_db[k],(unsigned)t->lower_valid[k],
            e->first_code[k],e->worst_code[k])) return -1;
    }
    if(line(o,"FRAME_HEADER,role,index,token,host_timestamp_us,sample_count,valid,overload\n")) return -1;
    for(unsigned i=0;i<e->pre_count;i++) if(raw_csv(o,"PRE",i,&e->pre[i])) return -1;
    if(raw_csv(o,"FIRST",0,&e->first) || raw_csv(o,"WORST",0,&e->worst)) return -1;
    for(unsigned i=0;i<e->post_count;i++) if(raw_csv(o,"POST",i,&e->post[i])) return -1;
    if(e->end_frame_present && raw_csv(o,"END",0,&e->end_frame)) return -1;
    return flush(o);
}
static int event_csv(const struct sm613_event *e,const struct sm615_view *v,
                     sm614_emit emit,void *user) {
    locale_t c_locale=newlocale(LC_NUMERIC_MASK,"C",(locale_t)0);
    if(!c_locale) return -1;
    locale_t previous=uselocale(c_locale);
    if(!previous) { freelocale(c_locale); return -1; }
    struct output o={emit,user,0,0,SM614_CSV_MAX,{0}};
    int rc=csv_body(&o,e,v);
    /* Never mutate the process-global locale, even on an emitter failure. */
    if(!uselocale(previous)) {
        /* Do not free a locale which might remain installed in this thread. */
        return -1;
    }
    freelocale(c_locale); return rc;
}
int sm614_encode_csv(const struct sm613_event *e,sm614_emit emit,void *user) {
    if(!emit || !sm614_event_valid(e)) return -1;
    return event_csv(e,NULL,emit,user);
}
int sm615_encode_csv(const struct sm615_view *v,sm614_emit emit,void *user) {
    if(!emit || !sm615_view_valid(v)) return -1;
    return event_csv(v->event,v,emit,user);
}
