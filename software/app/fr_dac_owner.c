#include "fr_dac_owner.h"
#include <math.h>

static int lost(struct fr_dac_owner *o) {
    if(o->job.pid<0) {
        o->errors|=FR_OWNER_LOST_CHILD;o->state=FR_OWNER_FAULT;return 1;
    }
    return 0;
}
static int clock_observe(struct fr_dac_owner *o,uint64_t now) {
    if(now<o->last_us || now==UINT64_MAX) {
        o->errors|=FR_OWNER_CLOCK;
        if(o->job.pid>0) (void)fr_dac_job_cancel(&o->job,now);
        o->state=FR_OWNER_FAULT;return -1;
    }
    o->last_us=now;return 0;
}
int fr_dac_owner_prepare(struct fr_dac_owner *o,int permit,double amp) {
    if(!o || o->permitted || permit!=1 || !isfinite(amp) || amp<0 || amp>50 ||
       fr_dac_job_busy(&o->job) ||
       (o->state!=FR_OWNER_IDLE && o->state!=FR_OWNER_CLOSED))return -1;
    o->amplitude_pct=amp;o->permitted=1;o->state=FR_OWNER_IDLE;
    o->errors=o->frequency_job_errors=o->off_job_errors=0;
    o->use_image=0;
    return 0;
}
int fr_dac_owner_prepare_image(struct fr_dac_owner *o,int permit,double amp,
                                const struct fr_dac_image *image) {
    if(fr_dac_image_check(image)<0 || fr_dac_owner_prepare(o,permit,amp)<0)return -1;
    o->image=*image;o->use_image=1;return 0;
}
static int start_job(struct fr_dac_owner *o,struct fr_dac_command c,int permit,uint64_t now) {
    return o->use_image?fr_dac_job_start_image(&o->job,c,permit,now,&o->image):
        fr_dac_job_start(&o->job,c,permit,now);
}
int fr_dac_owner_frequency(struct fr_dac_owner *o,uint32_t hz,uint64_t now) {
    if(!o || o->permitted!=1 || o->errors || fr_dac_job_busy(&o->job) ||
       (o->state!=FR_OWNER_IDLE && o->state!=FR_OWNER_READY))return -1;
    if(clock_observe(o,now)<0)return -1;
    if(start_job(o,(struct fr_dac_command){1,hz,o->amplitude_pct},
                        o->permitted,now)<0) {
        o->errors|=FR_OWNER_FREQUENCY_FAILED;
        o->frequency_job_errors|=o->job.errors;return -1;
    }
    o->state=FR_OWNER_FREQUENCY;return 1;
}
int fr_dac_owner_frequency_poll(struct fr_dac_owner *o,uint64_t now) {
    if(!o || clock_observe(o,now)<0 || lost(o))return -1;
    if(o->state==FR_OWNER_READY)return 1;
    if(o->state!=FR_OWNER_FREQUENCY)return -1;
    int rc=fr_dac_job_poll(&o->job,now);
    o->frequency_job_errors|=o->job.errors;
    if(lost(o))return -1;
    if(rc==1 && !fr_dac_job_busy(&o->job)) {o->state=FR_OWNER_READY;return 1;}
    if(rc<0) {o->errors|=FR_OWNER_FREQUENCY_FAILED;return -1;}
    return 0;
}
static int start_off(struct fr_dac_owner *o,uint64_t now) {
    if(lost(o) || fr_dac_job_busy(&o->job))return -1;
    o->frequency_job_errors|=o->job.errors;
    /* Permission was established at prepare; shutdown closes new ON access
     * but preserves the existing session's authority for this single OFF. */
    if(start_job(o,(struct fr_dac_command){0,0,0},1,now)<0) {
        o->errors|=FR_OWNER_OFF_FAILED;o->off_job_errors|=o->job.errors;
        o->state=FR_OWNER_FAULT;return -1;
    }
    o->state=FR_OWNER_OFF;return 1;
}
int fr_dac_owner_off(struct fr_dac_owner *o,uint64_t now) {
    if(!o || clock_observe(o,now)<0 || lost(o))return -1;
    if(o->state==FR_OWNER_CLOSED)return 0;
    if(o->state==FR_OWNER_DRAIN || o->state==FR_OWNER_OFF)return 1;
    if(o->state==FR_OWNER_FAULT || o->permitted!=1)return -1;
    o->permitted=0;
    if(fr_dac_job_busy(&o->job)) {
        (void)fr_dac_job_cancel(&o->job,now);
        o->frequency_job_errors|=o->job.errors;
        if(lost(o))return -1;
        if(fr_dac_job_busy(&o->job)) {o->state=FR_OWNER_DRAIN;return 1;}
    }
    return start_off(o,now);
}
int fr_dac_owner_off_poll(struct fr_dac_owner *o,uint64_t now) {
    if(!o)return -1;
    if(clock_observe(o,now)<0)return -1;
    if(o->state==FR_OWNER_FAULT) {
        /* Error return is not permission to abandon a still-waitable child. */
        if(o->job.pid>0) (void)fr_dac_job_poll(&o->job,now);
        (void)lost(o);return -1;
    }
    if(lost(o))return -1;
    if(o->state==FR_OWNER_CLOSED)return 1;
    if(o->state!=FR_OWNER_DRAIN && o->state!=FR_OWNER_OFF)return -1;
    int rc=fr_dac_job_poll(&o->job,now);
    if(lost(o))return -1;
    if(o->state==FR_OWNER_DRAIN) {
        o->frequency_job_errors|=o->job.errors;
        if(fr_dac_job_busy(&o->job))return 0;
        /* Prior command failure/cancellation is diagnostic, not OFF success. */
        return start_off(o,now)<0?-1:0;
    }
    o->off_job_errors|=o->job.errors;
    if(fr_dac_job_busy(&o->job))return 0;
    if(rc==1) {o->state=FR_OWNER_CLOSED;return 1;}
    o->errors|=FR_OWNER_OFF_FAILED;o->state=FR_OWNER_FAULT;return -1;
}
int fr_dac_owner_off_confirmed(const struct fr_dac_owner *o) {
    return o && o->state==FR_OWNER_CLOSED && !fr_dac_job_busy(&o->job) &&
           !(o->errors&(FR_OWNER_OFF_FAILED|FR_OWNER_LOST_CHILD|FR_OWNER_CLOCK));
}
