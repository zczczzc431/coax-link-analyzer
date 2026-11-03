#include "fr_backend_context.h"
#include <math.h>
#include <string.h>

static uint64_t clock_us(void *p) {
    struct fr_backend_context *b=p;
    if(!b || !b->active || !b->resources.monotonic_us)return UINT64_MAX;
    return b->resources.monotonic_us(b->resources.clock_ctx);
}
static int frequency(void *p,uint32_t hz) {
    struct fr_backend_context *b=p;
    if(!b || !b->active)return -1;
    return fr_dac_owner_frequency(&b->dac,hz,clock_us(b));
}
int fr_backend_frequency_poll(void *p) {
    struct fr_backend_context *b=p;
    if(!b || !b->active)return -1;
    return fr_dac_owner_frequency_poll(&b->dac,clock_us(b));
}
static int select_path(void *p,enum fr_raw_path path) {
    struct fr_backend_context *b=p;
    if(!b || !b->active || b->rf_released || b->dac.state!=FR_OWNER_READY ||
       b->dac.errors || fr_dac_job_busy(&b->dac.job) ||
       (path!=FR_RAW_FWD && path!=FR_RAW_REV))return -1;
    if (b->manual_no_switch) return 0;
    int rc=b->resources.rf_select(b->resources.rf_ctx,path);
    if(rc!=0)b->errors|=FR_BACKEND_SWITCH;
    return rc==0?0:-1;
}
static int off(void *p) {
    struct fr_backend_context *b=p;
    if(!b || !b->active)return -1;
    return fr_dac_owner_off(&b->dac,clock_us(b));
}
int fr_backend_output_poll(void *p) {
    struct fr_backend_context *b=p;
    if(!b || !b->active)return -1;
    return fr_dac_owner_off_poll(&b->dac,clock_us(b));
}
static int cancel(void *p,uint32_t token) {
    struct fr_backend_context *b=p;
    if(!b || !b->active || !token || b->resources.adc_lock_owned!=1)return -1;
    volatile uint32_t *m=b->resources.mailbox;
    __sync_synchronize();
    if(m[2048]!=token) {b->errors|=FR_BACKEND_CANCEL;return -1;}
    /* A matching completed request needs an ACK drain, not another write. */
    uint32_t done=m[2049],flags=m[2060];
    __sync_synchronize();
    if(m[2048]!=token) {b->errors|=FR_BACKEND_CANCEL;return -1;}
    if(done==token && (flags==1 || flags==3 || flags==4))return 0;
    m[2054]=token;__sync_synchronize();
    if(m[2048]!=token) {b->errors|=FR_BACKEND_CANCEL;return -1;}
    return 0;
}
static int release(void *p) {
    struct fr_backend_context *b=p;
    if(!b || !b->active || !fr_dac_owner_off_confirmed(&b->dac))return -1;
    if(b->rf_released)return 0;
    if(b->resources.rf_release(b->resources.rf_ctx)!=0) {
        b->errors|=FR_BACKEND_RELEASE;return -1;
    }
    b->rf_released=1;b->active=0;
    /* ADC lock and mailbox remain owned by the main reader, never closed. */
    return 0;
}
struct fr_ps_io fr_backend_io(struct fr_backend_context *b) {
    return (struct fr_ps_io){.acquisition={b,frequency,select_path,clock_us},
        .dac_off=off,.cancel_request=cancel,.release_rf=release};
}
static int mailbox_ready(volatile uint32_t *m) {
    if(!m)return 0;
    __sync_synchronize();
    uint32_t req=m[2048],done=m[2049],magic=m[2050],count=m[2051];
    uint32_t trigger=m[2052],flags=m[2060];
    __sync_synchronize();
    if(req!=m[2048] || done!=m[2049] || magic!=m[2050] || count!=m[2051] ||
       trigger!=m[2052] || flags!=m[2060])return 0;
    return magic==0x534e5031u && count==FR_PS_SAMPLES &&
        (trigger==0x54524731u || trigger==0x54524732u) && req==done &&
        ((!req && !flags) || (req && (flags==1 || flags==3 || flags==4)));
}
int fr_backend_start(struct fr_backend_context *b,struct fr_ps_adapter *a,
                      const struct fr_raw_config *cfg,struct fr_backend_resources r,
                      int permit,double amp,uint32_t generation,uint32_t revision) {
    if(!b || !a || !a->capture || permit!=1 || b->active || a->pending ||
       a->capture->active || a->output_pending || a->safety_errors ||
       (a->owned && !a->released) || !fr_raw_config_valid(cfg) ||
       cfg->settle_us<RF_EMIO_SETTLE_US || !isfinite(amp) || amp<0 || amp>50 ||
       r.adc_lock_owned!=1 || r.dac_writers_idle!=1 || r.rf_handle_owned!=1 ||
       r.frontend_verified!=1 || r.dac_tool_verified!=1 ||
       !r.monotonic_us || !r.rf_select || !r.rf_release || !r.rf_ctx ||
       !mailbox_ready(r.mailbox))return FR_RAW_FAILED;
    for(unsigned i=0;i<cfg->points;i++)
        if(cfg->frequency_hz[i]%1000u)return FR_RAW_FAILED;
    uint64_t now=r.monotonic_us(r.clock_ctx);
    if(now<a->last_clock_us || now<b->dac.last_us || now==UINT64_MAX ||
       cfg->timeout_us>UINT64_MAX-now ||
       cfg->settle_us>UINT64_MAX-now-cfg->timeout_us)return FR_RAW_FAILED;
    /* Production requires the pinned C827 image, not a boolean alone.
     * Only explicitly fake-executable test builds retain the legacy fixture. */
#ifndef FR_DAC_EXEC_PATH
    if(!r.dac_image || strcmp(r.dac_image->sha256,FR_DAC_C827_SHA256))return FR_RAW_FAILED;
#endif
    if(r.dac_image) {
        if(fr_dac_owner_prepare_image(&b->dac,permit,amp,r.dac_image)<0)return FR_RAW_FAILED;
    } else if(fr_dac_owner_prepare(&b->dac,permit,amp)<0)return FR_RAW_FAILED;
    b->resources=r;b->active=1;b->rf_released=0;b->errors=0;
    struct fr_ps_admission p={1,1,1,1,generation,revision};
    /* Values above only follow the checked resource facts and mailbox, not
     * hardcoded production permission. Failure still uses normal OFF drain. */
    return fr_ps_start_async(a,cfg,fr_backend_io(b),permit,&p,
                             fr_backend_frequency_poll,fr_backend_output_poll);
}
int fr_backend_emio_select(void *p,enum fr_raw_path path) {
    if(path!=FR_RAW_FWD && path!=FR_RAW_REV)return -1;
    return rf_emio_select_nowait(p,path==FR_RAW_FWD?FR_PATH_FORWARD:FR_PATH_REVERSE);
}
int fr_backend_emio_release(void *p) {return rf_emio_close_checked(p);}
