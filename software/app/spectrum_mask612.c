#include "spectrum_mask612.h"
#ifdef SPECTRUM_QUALIFICATION654
#include "spectrum_qualification654.h"
#endif
#include <float.h>
#include <math.h>
#include <string.h>

static int identity_valid(const struct sm612_identity *id) {
    return id && id->session && isfinite(id->fs_hz) && id->fs_hz>0 &&
        id->format==SM612_FORMAT && id->fft_size>=8 && id->fft_size<=2048 &&
        !(id->fft_size & (id->fft_size-1));
}
int sm612_identity_equal(const struct sm612_identity *a,const struct sm612_identity *b) {
    return identity_valid(a) && identity_valid(b) && a->session==b->session &&
        a->input_signature==b->input_signature && a->source_signature==b->source_signature &&
        a->fs_hz==b->fs_hz && a->fft_size==b->fft_size && a->format==b->format;
}
void sm612_defaults(struct sm612_config *c) {
    if(!c) return;
    memset(c,0,sizeof(*c));
    c->first_bin=1; c->last_bin=614; /* About 120 MHz at production Fs/N. */
    /* Mean subtraction before symmetric Hann leaks the removed finite-frame
     * mean into bin1. Do not learn a phase-dependent DC-neighbour pedestal.
     * Explicit guard is retained in config and must be visible in the UI. */
    c->exclude[1]=1;
    c->learn_frames=32; c->enter_frames=2; c->recover_frames=3;
    c->upper_margin_db=6; c->lower_margin_db=6; c->floor_db=-100;
    c->lower_signal_margin_db=12; c->min_learn_peak_db=-60;
    c->max_learn_spread_db=3; c->hysteresis_db=2; c->max_gap_us=1000000;
    c->upper_enabled=1; c->lower_enabled=1;
}
void sm612_init(struct sm612_model *m) {
    if(m) { memset(m,0,sizeof(*m)); m->state=SM612_NO_TEMPLATE; }
}
static int active(const struct sm612_model *m) {
    return m->state==SM612_ARMED || m->state==SM612_CANDIDATE || m->state==SM612_ALARM;
}
static int config_valid(const struct sm612_config *c,size_t bins) {
    if(!c || c->first_bin<1 || c->last_bin>=bins || c->first_bin>c->last_bin ||
       c->learn_frames<2 || c->learn_frames>128 || c->enter_frames<1 || c->enter_frames>32 ||
       c->recover_frames<1 || c->recover_frames>32 || !c->max_gap_us ||
       (c->upper_enabled!=0 && c->upper_enabled!=1) ||
       (c->lower_enabled!=0 && c->lower_enabled!=1) ||
       (!c->upper_enabled && !c->lower_enabled)) return 0;
    const double values[]={c->upper_margin_db,c->lower_margin_db,c->floor_db,
        c->lower_signal_margin_db,c->min_learn_peak_db,c->max_learn_spread_db,c->hysteresis_db};
    for(size_t i=0;i<sizeof(values)/sizeof(values[0]);i++) if(!isfinite(values[i])) return 0;
    if(c->upper_margin_db<0 || c->upper_margin_db>120 ||
       c->lower_margin_db<0 || c->lower_margin_db>120 ||
       c->floor_db<-200 || c->floor_db>0 || c->lower_signal_margin_db<0 ||
       c->lower_signal_margin_db>120 || c->min_learn_peak_db<-200 ||
       c->min_learn_peak_db>0 || c->max_learn_spread_db<=0 ||
       c->max_learn_spread_db>120 || c->hysteresis_db<0 ||
       (c->upper_enabled && c->hysteresis_db>c->upper_margin_db) ||
       (c->lower_enabled && c->hysteresis_db>c->lower_margin_db)) return 0;
    unsigned included=0;
    for(size_t k=0;k<SM612_MAX_BINS;k++) {
        if(c->exclude[k]>1) return 0;
        if(k>=c->first_bin && k<=c->last_bin && !c->exclude[k]) included++;
    }
    return included!=0;
}
static void clear_event(struct sm612_model *m) {
    m->enter_count=m->recover_count=0; m->event_id=0;
    m->first_token=m->worst_token=0; m->first_us=m->worst_us=0;
    m->worst_bin=0; m->worst_direction=0; m->worst_excess_db=0;
}
static void clear_learning(struct sm612_model *m) {
    m->learned=0;
    memset(m->sum_power,0,sizeof(m->sum_power));
    for(size_t k=0;k<m->bins;k++) { m->min_db[k]=DBL_MAX; m->max_db[k]=-DBL_MAX; }
}
static void publish(const struct sm612_model *m,struct sm612_decision *out) {
    out->state=m->state; out->reason=m->reason; out->event_id=m->event_id;
    out->first_token=m->first_token; out->first_us=m->first_us;
    out->worst_token=m->worst_token; out->worst_us=m->worst_us;
    out->worst_bin=m->worst_bin; out->worst_direction=m->worst_direction;
    out->worst_excess_db=m->worst_excess_db;
}
static void break_continuity(struct sm612_model *m,struct sm612_decision *out) {
    if(m->state==SM612_ALARM) {
        out->flags|=SM612_INTERRUPTED; out->ended_event_id=m->event_id;
    } else if(m->state==SM612_CANDIDATE) out->flags|=SM612_UNCONFIRMED;
    if(m->state==SM612_LEARNING) clear_learning(m);
    else if(active(m)) m->state=SM612_ARMED;
    clear_event(m);
}
int sm612_begin_learn(struct sm612_model *m,const struct sm612_config *c,
                     const struct sm612_identity *id) {
    if(!m) return -1;
    if(active(m)) return -1;
    if(!identity_valid(id)) { m->reason=SM612_BAD_IDENTITY; return -1; }
    size_t bins=id->fft_size/2+1;
    if(!config_valid(c,bins)) { m->reason=SM612_BAD_CONFIG; return -1; }
    /* Preserve the anti-replay watermark when relearning in the same session/settings. */
    int have=m->have_frame && sm612_identity_equal(&m->identity,id);
    uint32_t token=m->last_token; uint64_t time=m->last_us,next=m->next_event;
    struct sm612_identity saved_identity=*id;
    struct sm612_config saved_config=*c;
    sm612_init(m); m->identity=saved_identity; m->config=saved_config; m->bins=bins; m->next_event=next;
    m->have_frame=have; if(have) { m->last_token=token; m->last_us=time; }
    m->state=SM612_LEARNING; clear_learning(m); return 0;
}
int sm612_arm(struct sm612_model *m,const struct sm612_identity *id) {
    if(!m) return -1;
    if(!identity_valid(id) || !sm612_identity_equal(&m->identity,id)) {
        /* Cannot silently abandon an active event; caller must use push/stop. */
        if(active(m)) return -1;
        m->template_valid=0; m->state=SM612_INVALID; m->reason=SM612_IDENTITY_MISMATCH;
        return -1;
    }
    if(!m->template_valid || (m->state!=SM612_READY && m->state!=SM612_SUSPENDED)) return -1;
    clear_event(m); m->state=SM612_ARMED; m->reason=SM612_OK; return 0;
}
void sm612_stop(struct sm612_model *m,int suspended,struct sm612_decision *out) {
    if(!m || !out) return;
    memset(out,0,sizeof(*out)); publish(m,out);
    if(m->state==SM612_ALARM) {
        out->flags|=SM612_INTERRUPTED; out->ended_event_id=m->event_id;
    } else if(m->state==SM612_CANDIDATE) out->flags|=SM612_UNCONFIRMED;
    if(m->state==SM612_LEARNING) { clear_learning(m); m->template_valid=0; }
    clear_event(m);
    m->state=m->template_valid?(suspended?SM612_SUSPENDED:SM612_READY):SM612_NO_TEMPLATE;
    m->reason=suspended?SM612_OWNER_SUSPEND:SM612_MANUAL_STOP;
    out->state=m->state; out->reason=m->reason;
}
static double power_db(double p) { return p>0 ? 10.0*log10(p) : -300.0; }
static int amplitudes_valid(const struct sm612_model *m,const struct sm612_frame *f) {
    if(!f->valid || !f->code || f->bins!=m->bins) return 0;
    for(size_t k=0;k<m->bins;k++) {
        double v=f->code[k];
        if(!isfinite(v) || v<0) return 0;
        double q=v/8191.0,p=q*q;
        if(!isfinite(p) || (m->state==SM612_LEARNING && !isfinite(m->sum_power[k]+p))) return 0;
    }
    return 1;
}
static int finish_learning(struct sm612_model *m) {
    double peak=-DBL_MAX; unsigned lower_count=0;
    const struct sm612_config *c=&m->config;
#ifdef SPECTRUM_QUALIFICATION654
    struct sm654_policy policy654;struct sm654_selection selection654;
    double mean654[SM612_MAX_BINS];
    sm654_defaults(&policy654);
    for(size_t k=0;k<m->bins;k++) mean654[k]=m->sum_power[k]/m->learned;
    if(sm654_select(c,&policy654,mean654,m->bins,&selection654)!=0) {
        m->reason=SM612_BAD_CONFIG;return -1;
    }
    /* New OPT-IN dual-limit policy: never silently become upper-only.
     * Insufficient reference on a one-bin band is deliberately not legacy612.
     */
    if(c->lower_enabled && (!selection654.qualified ||
       selection654.coverage[selection654.dominant_bin]!=SM654_SUPPORTED)) {
        m->reason=SM612_NO_SIGNAL;return -1;
    }
#endif
    for(size_t k=0;k<m->bins;k++) {
        double b=power_db(m->sum_power[k]/m->learned);
        /* Preserve the power-mean reference. In unsupported noise-only bins,
         * use the observed learning envelope for the upper bound: coherent
         * quantization products can vary by more than mean+6 dB with phase.
         * Strong qualified bins still use the ordinary mean +/- margins. */
        int supported=b>=c->floor_db+c->lower_signal_margin_db;
#ifdef SPECTRUM_QUALIFICATION654
        supported=selection654.coverage[k]==SM654_SUPPORTED;
#endif
        double upper_reference=supported ? b : m->max_db[k];
        m->baseline_db[k]=b;
        m->upper_db[k]=fmax(upper_reference+c->upper_margin_db,c->floor_db);
        m->lower_db[k]=b-c->lower_margin_db;
        m->lower_valid[k]=0;
        if(k<c->first_bin || k>c->last_bin || c->exclude[k]) continue;
        if(b>peak) peak=b;
        if(supported) {
            if(m->max_db[k]-m->min_db[k]>c->max_learn_spread_db) {
                m->reason=SM612_UNSTABLE_TEMPLATE; return -1;
            }
            if(c->lower_enabled && m->lower_db[k]>=c->floor_db) {
                m->lower_valid[k]=1; lower_count++;
            }
        }
    }
    if(peak<c->min_learn_peak_db || (!c->upper_enabled && !lower_count)
#ifdef SPECTRUM_QUALIFICATION654
       || (c->lower_enabled && !lower_count)
#endif
       ) {
        m->reason=SM612_NO_SIGNAL; return -1;
    }
    m->template_valid=1; m->state=SM612_READY; return 0;
}
static int reject_frame(struct sm612_model *m,struct sm612_decision *out,enum sm612_reason reason) {
    out->flags|=SM612_REJECTED|SM612_GAP;
    break_continuity(m,out); m->reason=reason; publish(m,out); return -1;
}
int sm612_push(struct sm612_model *m,const struct sm612_frame *f,struct sm612_decision *out) {
    if(!m || !out) return -1;
    memset(out,0,sizeof(*out));
    if(f) { out->frame_token=f->token; out->timestamp_us=f->timestamp_us; }
    if(m->state!=SM612_LEARNING && !active(m)) {
        m->reason=SM612_NOT_ARMED; publish(m,out); return 0;
    }
    if(!f) return reject_frame(m,out,SM612_BAD_FRAME);
    if(!sm612_identity_equal(&m->identity,&f->identity)) {
        reject_frame(m,out,SM612_IDENTITY_MISMATCH);
        m->template_valid=0; m->state=SM612_INVALID; publish(m,out); return -1;
    }
    int gap=0;
    if(m->have_frame) {
        uint32_t distance=f->token-m->last_token;
        if(!distance) return reject_frame(m,out,SM612_DUPLICATE);
        if(distance>=UINT32_C(0x80000000) || f->timestamp_us<=m->last_us)
            return reject_frame(m,out,SM612_STALE_FRAME);
        gap=distance!=1 || f->timestamp_us-m->last_us>m->config.max_gap_us;
    }
    m->have_frame=1; m->last_token=f->token; m->last_us=f->timestamp_us;
    if(gap) { out->flags|=SM612_GAP; break_continuity(m,out); }
    m->reason=gap?SM612_SEQUENCE_GAP:SM612_OK;
    if(f->overload) return reject_frame(m,out,SM612_OVERLOAD);
    if(!amplitudes_valid(m,f)) return reject_frame(m,out,SM612_BAD_FRAME);
    if(m->state==SM612_LEARNING) {
        for(size_t k=0;k<m->bins;k++) {
            double q=f->code[k]/8191.0,p=q*q,db=power_db(p);
            m->sum_power[k]+=p;
            if(db<m->min_db[k]) m->min_db[k]=db;
            if(db>m->max_db[k]) m->max_db[k]=db;
        }
        m->learned++;
        if(m->learned==m->config.learn_frames) {
            if(finish_learning(m)<0) {
                m->template_valid=0; m->state=SM612_INVALID; out->flags|=SM612_REJECTED;
                publish(m,out); return -1;
            }
            out->flags|=SM612_LEARNED;
        }
        publish(m,out); return 0;
    }
    int bad=0,safe=1;
    const struct sm612_config *c=&m->config;
    for(unsigned k=c->first_bin;k<=c->last_bin;k++) {
        if(c->exclude[k]) continue;
        double q=f->code[k]/8191.0,d=power_db(q*q),excess=0;
        int direction=0;
        if(c->upper_enabled) {
            if(d>m->upper_db[k]) { excess=d-m->upper_db[k]; direction=1; }
            /* floor_db is the declared minimum reliable upper-limit level;
             * hysteresis must not demand evidence below that level. */
            if(d>fmax(c->floor_db,m->upper_db[k]-c->hysteresis_db)) safe=0;
        }
        if(m->lower_valid[k]) {
            if(d<m->lower_db[k] && m->lower_db[k]-d>excess) {
                excess=m->lower_db[k]-d; direction=-1;
            }
            if(d<m->lower_db[k]+c->hysteresis_db) safe=0;
        }
        if(direction) {
            bad=1;
            if(excess>out->current_excess_db) {
                out->current_excess_db=excess; out->current_bin=k; out->current_direction=direction;
            }
        }
    }
    if(bad) {
        if(m->state==SM612_ARMED) {
            m->state=SM612_CANDIDATE; m->first_token=f->token; m->first_us=f->timestamp_us;
            out->flags|=SM612_FIRST;
        }
        m->recover_count=0;
        if(out->current_excess_db>m->worst_excess_db) {
            m->worst_excess_db=out->current_excess_db; m->worst_bin=out->current_bin;
            m->worst_direction=out->current_direction; m->worst_token=f->token;
            m->worst_us=f->timestamp_us; out->flags|=SM612_WORST;
        }
        if(m->state==SM612_CANDIDATE && ++m->enter_count>=c->enter_frames) {
            if(m->next_event==UINT64_MAX) {
                reject_frame(m,out,SM612_COUNTER_LIMIT);
                m->template_valid=0; m->state=SM612_INVALID; publish(m,out); return -1;
            }
            m->event_id=++m->next_event; m->state=SM612_ALARM; out->flags|=SM612_CONFIRMED;
        }
    } else if(m->state==SM612_CANDIDATE) {
        out->flags|=SM612_UNCONFIRMED; clear_event(m); m->state=SM612_ARMED;
    } else if(m->state==SM612_ALARM) {
        if(safe) m->recover_count++; else m->recover_count=0;
        if(m->recover_count>=c->recover_frames) {
            out->flags|=SM612_RECOVERED; out->ended_event_id=m->event_id;
            publish(m,out); clear_event(m); m->state=SM612_ARMED; out->state=m->state;
            return 0;
        }
    }
    publish(m,out); return 0;
}
