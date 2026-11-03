#include "fr_ps_adapter.h"
#include <math.h>
#include <string.h>

void fr_ps_init(struct fr_ps_adapter *a, struct fr_raw_capture *capture) {
    if (!a) return;
    memset(a,0,sizeof(*a)); a->capture=capture;
}
static int observe_clock(struct fr_ps_adapter *a,uint64_t now) {
    if(now<a->last_clock_us) { a->error=FR_PS_CLOCK; return -1; }
    a->last_clock_us=now; return 0;
}
static void release_rf(struct fr_ps_adapter *a) {
    if(a->output_pending) return;
    if(a->owned && !a->released) {
        if(a->io.release_rf(a->io.acquisition.ctx)!=0)
            a->safety_errors|=FR_PS_RELEASE_FAILED;
        a->released=1;
    }
}
int fr_ps_stop(struct fr_ps_adapter *a,enum fr_ps_reason why,uint64_t now) {
    if(!a || !a->capture) return FR_RAW_IGNORED;
    if(a->stopping) return a->safety_errors?FR_RAW_FAILED:FR_RAW_IGNORED;
    a->stopping=1; a->blocked=1; a->reason=why;
    if(why!=FR_PS_COMPLETE && a->capture->active) fr_raw_abort(a->capture);
    if(observe_clock(a,now)<0) a->safety_errors|=FR_PS_CLEANUP_CLOCK;
    if(a->owned) {
        int rc=a->io.dac_off(a->io.acquisition.ctx);
        if(rc==1 && a->output_poll) a->output_pending=1;
        else if(rc!=0) {
            a->safety_errors|=FR_PS_OFF_FAILED;
            if(a->output_poll) a->output_pending=1;
        }
    }
    if(a->pending) {
        if(a->owned && a->io.cancel_request(a->io.acquisition.ctx,a->pending_token)!=0)
            a->safety_errors|=FR_PS_CANCEL_FAILED;
        if(a->owned && observe_clock(a,a->io.acquisition.monotonic_us(
                                    a->io.acquisition.ctx))<0)
            a->safety_errors|=FR_PS_CLEANUP_CLOCK;
        if(FR_PS_CANCEL_US>UINT64_MAX-a->last_clock_us) {
            a->safety_errors|=FR_PS_CLEANUP_CLOCK;
            a->cancel_deadline_us=UINT64_MAX;
            release_rf(a); /* Keep pending/block even when deadline cannot form. */
        } else a->cancel_deadline_us=a->last_clock_us+FR_PS_CANCEL_US;
    } else release_rf(a);
    return a->safety_errors?FR_RAW_FAILED:FR_RAW_IGNORED;
}
static int fail(struct fr_ps_adapter *a,enum fr_ps_error error,uint64_t now) {
    a->error=error;
    (void)fr_ps_stop(a,FR_PS_ERROR,now);
    return FR_RAW_FAILED;
}
static int start(struct fr_ps_adapter *a,const struct fr_raw_config *cfg,
                struct fr_ps_io io,int permit,const struct fr_ps_admission *admit,
                int (*frequency_poll)(void *),int (*output_poll)(void *)) {
    if(!a || !a->capture || permit!=1 || a->pending || a->capture->active ||
       a->safety_errors || a->output_pending || (a->owned && !a->released) ||
       !io.acquisition.monotonic_us || !io.acquisition.set_frequency ||
       !io.acquisition.select_path || !io.dac_off || !io.cancel_request ||
       !io.release_rf) return FR_RAW_FAILED;
    a->io=io; a->owned=1; a->released=0; a->stopping=0; a->blocked=0;
    a->async_frequency=frequency_poll!=NULL; a->output_poll=output_poll;
    a->controls_seeded=admit!=NULL;
    if(admit) {
        a->control_generation=admit->control_generation;
        a->trigger_revision=admit->trigger_revision;
    }
    a->error=FR_PS_OK;
    uint64_t now=io.acquisition.monotonic_us(io.acquisition.ctx);
    if(observe_clock(a,now)<0) return fail(a,FR_PS_CLOCK,now);
    int rc=frequency_poll?fr_raw_start_async(a->capture,cfg,io.acquisition,
                                            a->sequence,frequency_poll):
                          fr_raw_start(a->capture,cfg,io.acquisition,a->sequence);
    if(rc<0) (void)fr_ps_stop(a,FR_PS_ERROR,
                           io.acquisition.monotonic_us(io.acquisition.ctx));
    return rc;
}
int fr_ps_start(struct fr_ps_adapter *a,const struct fr_raw_config *cfg,
                struct fr_ps_io io,int permit) {
    return start(a,cfg,io,permit,NULL,NULL,NULL);
}
int fr_ps_start_async(struct fr_ps_adapter *a,const struct fr_raw_config *cfg,
                      struct fr_ps_io io,int permit,const struct fr_ps_admission *p,
                      int (*freq_poll)(void *),int (*off_poll)(void *)) {
    if(!p || p->adc_lock_owned!=1 || p->mailbox_ready!=1 || p->writers_idle!=1 ||
       p->frontend_verified!=1 || !freq_poll || !off_poll) return FR_RAW_FAILED;
    return start(a,cfg,io,permit,p,freq_poll,off_poll);
}
int fr_ps_control_sync(struct fr_ps_adapter *a,uint32_t generation,
                       uint32_t revision,uint64_t now) {
    if(!a || !a->owned || a->stopping) return FR_RAW_IGNORED;
    if(!a->controls_seeded) {
        a->controls_seeded=1; a->control_generation=generation;
        a->trigger_revision=revision; return FR_RAW_IGNORED;
    }
    if(generation!=a->control_generation || revision!=a->trigger_revision)
        return fr_ps_stop(a,FR_PS_STOP,now);
    return FR_RAW_IGNORED;
}
int fr_ps_arm(struct fr_ps_adapter *a,uint32_t token,uint64_t now) {
    if(!a || !a->capture) return FR_RAW_FAILED;
    if(a->blocked || !a->capture->active) return FR_RAW_IGNORED;
    if(observe_clock(a,now)<0) return fail(a,FR_PS_CLOCK,now);
    if(a->async_frequency && !fr_raw_can_arm(a->capture,now))
        return FR_RAW_IGNORED;
    uint32_t delta=a->seeded?token-a->last_token:1;
    if(a->pending || !token || !delta || delta>=0x80000000u ||
       a->sequence>UINT64_MAX-delta)
        return fail(a,FR_PS_TOKEN,now);
    const struct fr_raw_capture *c=a->capture;
    if(c->point>=c->config.points || c->point>=FR_RAW_MAX_POINTS || !c->epoch)
        return fail(a,FR_PS_FRAME,now);
    a->sequence+=delta; a->last_token=token; a->seeded=1;
    a->pending_token=token; a->pending=1; a->armed_path=c->path;
    a->armed=(struct fr_raw_frame){.sequence=a->sequence,.epoch=c->epoch,
        .frequency_hz=c->config.frequency_hz[c->point],.started_us=now};
    return FR_RAW_PROGRESS;
}
int fr_ps_consume(struct fr_ps_adapter *a,const struct fr_ps_observation *o) {
    if(!a || !a->capture || !o) return FR_RAW_FAILED;
    if(!a->pending) return FR_RAW_IGNORED;
    if(observe_clock(a,o->observed_us)<0)
        return fail(a,FR_PS_CLOCK,o->observed_us);
    /* A foreign/stale DONE is not an acknowledgement of our outstanding ARM. */
    if(o->req_before!=a->pending_token || o->done_before!=a->pending_token ||
       o->req_after!=a->pending_token || o->done_after!=a->pending_token)
        return FR_RAW_IGNORED;
    if(o->flags_before!=o->flags_after ||
       (o->flags_after!=1 && o->flags_after!=3 && o->flags_after!=4))
        return fail(a,FR_PS_FRAME,o->observed_us);
    if(a->stopping) {
        a->pending=0; release_rf(a);
        return a->safety_errors?FR_RAW_FAILED:FR_RAW_IGNORED;
    }
    if(o->observed_us<a->armed.started_us || o->flags_after==4 ||
       !o->copy_stable || !o->samples || o->count!=FR_PS_SAMPLES ||
       o->index_before!=o->index_after || o->index_after>=FR_PS_SAMPLES)
        return fail(a,FR_PS_FRAME,o->observed_us);
    uint64_t sum=0,sq=0;
    unsigned lo=16383,hi=0;
    for(size_t i=0;i<o->count;i++) {
        uint32_t x=o->samples[i];
        if(x>16383) return fail(a,FR_PS_FRAME,o->observed_us);
        sum+=x; sq+=(uint64_t)x*x;
        if(x<lo) lo=x;
        if(x>hi) hi=x;
    }
    /* Integral variance numerator fits uint64 for 2048 14-bit samples.
     * This avoids cancellation when a weak signal sits on a large DC level. */
    uint64_t variance=(uint64_t)o->count*sq-sum*sum;
    struct fr_raw_frame frame=a->armed;
    frame.rms_ac_code=sqrt((double)variance)/o->count;
    frame.clipped=lo<=4 || hi>=16379;
    frame.completed_us=o->observed_us;
    a->pending=0;
    int rc=fr_raw_push(a->capture,&frame);
    if(rc==FR_RAW_COMPLETE) (void)fr_ps_stop(a,FR_PS_COMPLETE,o->observed_us);
    else if(rc<0) (void)fr_ps_stop(a,FR_PS_ERROR,o->observed_us);
    return a->safety_errors?FR_RAW_FAILED:rc;
}
int fr_ps_poll(struct fr_ps_adapter *a,uint64_t now) {
    if(!a || !a->capture) return FR_RAW_FAILED;
    if(observe_clock(a,now)<0) {
        a->safety_errors|=FR_PS_CLEANUP_CLOCK;
        return fail(a,FR_PS_CLOCK,now);
    }
    if(a->stopping) {
        if(a->output_pending) {
            int rc=a->output_poll(a->io.acquisition.ctx);
            if(rc==1) a->output_pending=0;
            else if(rc!=0) a->safety_errors|=FR_PS_OFF_FAILED;
            if(observe_clock(a,a->io.acquisition.monotonic_us(
                                        a->io.acquisition.ctx))<0)
                a->safety_errors|=FR_PS_CLEANUP_CLOCK;
        }
        if(!a->pending) release_rf(a);
        if(a->pending && a->last_clock_us>a->cancel_deadline_us) {
            a->safety_errors|=FR_PS_CANCEL_TIMEOUT;
            release_rf(a); /* Do NOT clear pending or report successful drain. */
        }
        return a->safety_errors?FR_RAW_FAILED:FR_RAW_IGNORED;
    }
    if(!a->capture->active) return FR_RAW_IGNORED;
    int rc=fr_raw_poll(a->capture);
    if(rc<0) (void)fr_ps_stop(a,FR_PS_ERROR,now);
    return rc;
}
int fr_ps_clean(const struct fr_ps_adapter *a) {
    return a && a->stopping && !a->pending && !a->output_pending && !a->safety_errors &&
           (!a->owned || a->released);
}

void fr_ps_recovery_init(struct fr_ps_recovery *r,uint64_t now) {
    if(!r) return;
    memset(r,0,sizeof(*r)); r->last_us=now;
    if(now>UINT64_MAX-FR_PS_CANCEL_US) r->error=FR_PS_RECOVERY_CLOCK;
    else r->deadline_us=now+FR_PS_CANCEL_US;
}
static int recovery_fail(struct fr_ps_recovery *r,enum fr_ps_recovery_error e) {
    r->error=e; r->ready=0; return FR_PS_RECOVERY_FAILED;
}
int fr_ps_recovery_step(struct fr_ps_recovery *r,struct fr_ps_mailbox_state s,
                        int owner,uint64_t now) {
    if(!r || r->error) return FR_PS_RECOVERY_FAILED;
    if(owner!=1) return recovery_fail(r,FR_PS_RECOVERY_OWNER);
    if(now<r->last_us) return recovery_fail(r,FR_PS_RECOVERY_CLOCK);
    r->last_us=now;
    if(now>=r->deadline_us) return recovery_fail(r,FR_PS_RECOVERY_TIMEOUT);
    if(!s.stable) return FR_PS_RECOVERY_WAIT;
    if(s.flags!=0 && s.flags!=1 && s.flags!=3 && s.flags!=4 && s.flags!=8)
        return recovery_fail(r,FR_PS_RECOVERY_METADATA);
    if(r->cancel_sent && s.req!=r->token)
        return recovery_fail(r,FR_PS_RECOVERY_CONFLICT);
    if(r->ready && s.req!=r->token)
        return recovery_fail(r,FR_PS_RECOVERY_CONFLICT);
    if(s.req==s.done) {
        /* flags=0 only represents the reset/empty mailbox. A raced ordinary
         * completion (1/3) drains just as a cancellation (4) does. */
        if((!s.req && s.flags!=0) || (s.req && s.flags!=1 &&
                                                   s.flags!=3 && s.flags!=4))
            return recovery_fail(r,FR_PS_RECOVERY_METADATA);
        r->token=s.req; r->ready=1; return FR_PS_RECOVERY_READY;
    }
    if(!s.req) return recovery_fail(r,FR_PS_RECOVERY_METADATA);
    if(r->ready) return recovery_fail(r,FR_PS_RECOVERY_CONFLICT);
    if(!r->cancel_sent) {
        r->token=s.req; r->cancel_sent=1;
        return FR_PS_RECOVERY_CANCEL;
    }
    return FR_PS_RECOVERY_WAIT;
}
