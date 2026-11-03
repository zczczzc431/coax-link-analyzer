#define _POSIX_C_SOURCE 200809L
#include "monitor_core.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

static struct diag_monitor_config defaults(void) {
    struct diag_monitor_config c={16,0.65,0.82,0.20,2,3,750,16.0};
    return c;
}

void diag_monitor_init(struct diag_monitor *m, struct diag_monitor_config cfg) {
    if(!cfg.baseline_frames) cfg=defaults();
    if(!(cfg.drop_enter_ratio>0 && cfg.drop_enter_ratio<1)) cfg.drop_enter_ratio=0.65;
    if(!(cfg.drop_exit_ratio>cfg.drop_enter_ratio && cfg.drop_exit_ratio<=1.0)) cfg.drop_exit_ratio=0.82;
    if(!(cfg.freq_tolerance_ratio>0 && cfg.freq_tolerance_ratio<1)) cfg.freq_tolerance_ratio=0.20;
    if(!cfg.enter_confirm_frames) cfg.enter_confirm_frames=2;
    if(!cfg.exit_confirm_frames) cfg.exit_confirm_frames=3;
    if(!cfg.recovery_hold_ms) cfg.recovery_hold_ms=750;
    if(!(cfg.min_valid_rms_code>0.0) || !isfinite(cfg.min_valid_rms_code)) cfg.min_valid_rms_code=16.0;
    memset(m,0,sizeof(*m)); m->cfg=cfg; m->state=DIAG_MONITOR_NO_SIGNAL;
}

static double safe_ratio(double a,double b) { return b>1e-9 ? a/b : 0.0; }

static double sample_severity(const struct diag_monitor *m,
                              const struct diag_monitor_sample *s) {
    double sev=1.0-safe_ratio(s->rms_code,m->baseline_rms);
    double vpp_sev=1.0-safe_ratio(s->vpp_code,m->baseline_vpp);
    if(vpp_sev>sev) sev=vpp_sev;
    if(s->frequency_valid && m->baseline_freq>1e-9) {
        double freq_sev=fabs(s->frequency_mhz-m->baseline_freq)/m->baseline_freq;
        if(freq_sev>sev) sev=freq_sev;
    }
    if(sev<0.0) sev=0.0;
    if(sev>1.0) sev=1.0;
    return sev;
}

static int abnormal(const struct diag_monitor *m,const struct diag_monitor_sample *s,double enter) {
    if(s->rms_code < m->baseline_rms*enter) return 1;
    if(s->vpp_code < m->baseline_vpp*enter) return 1;
    if(!s->frequency_valid || s->frequency_mhz<=0.0) return 1;
    if(m->baseline_freq>0 && fabs(s->frequency_mhz-m->baseline_freq)/m->baseline_freq>
       m->cfg.freq_tolerance_ratio) return 1;
    return 0;
}

static int valid_signal(const struct diag_monitor *m,const struct diag_monitor_sample *s) {
    return s->rms_code>=m->cfg.min_valid_rms_code;
}

static void baseline_add(struct diag_monitor *m,const struct diag_monitor_sample *s) {
    unsigned n=m->baseline_count;
    m->baseline_rms=(m->baseline_rms*n+s->rms_code)/(n+1);
    m->baseline_vpp=(m->baseline_vpp*n+s->vpp_code)/(n+1);
    if(s->frequency_valid) {
        unsigned fn=m->baseline_freq_count;
        m->baseline_freq=(m->baseline_freq*fn+s->frequency_mhz)/(fn+1);
        m->baseline_freq_count++;
    }
    m->baseline_count++;
}

int diag_monitor_update(struct diag_monitor *m, const struct diag_monitor_sample *s,
                        struct diag_monitor_event *completed) {
    if(!m || !s) return -1;
    if(completed) memset(completed,0,sizeof(*completed));
    /* A bad ADC/FFT result must never be allowed to silently become a
     * normal frame.  Reject NaN/Inf as malformed input. */
    if(!isfinite(s->rms_code) || !isfinite(s->vpp_code) ||
       (s->frequency_valid && !isfinite(s->frequency_mhz)) ||
       s->rms_code<0 || s->vpp_code<0)
        return -1;
    if(m->state==DIAG_MONITOR_NO_SIGNAL) {
        if(!valid_signal(m,s)) return 0;
        m->state=DIAG_MONITOR_BASELINE;
        baseline_add(m,s);
        if(m->baseline_count>=m->cfg.baseline_frames) m->state=DIAG_MONITOR_NORMAL;
        return 0;
    }
    if(m->state==DIAG_MONITOR_BASELINE) {
        if(!valid_signal(m,s)) {
            m->baseline_count=0; m->baseline_freq_count=0;
            m->baseline_rms=0.0; m->baseline_vpp=0.0; m->baseline_freq=0.0;
            m->state=DIAG_MONITOR_NO_SIGNAL;
            return 0;
        }
        baseline_add(m,s);
        if(m->baseline_count>=m->cfg.baseline_frames) m->state=DIAG_MONITOR_NORMAL;
        return 0;
    }
    int bad=abnormal(m,s,m->cfg.drop_enter_ratio);
    if(m->state==DIAG_MONITOR_NORMAL) {
        m->normal_frames++;
        if(bad) {
            double severity=sample_severity(m,s);
            if(!m->enter_count) {
                m->candidate_start_ms=s->timestamp_ms;
                m->candidate_min_rms=s->rms_code;
                m->candidate_min_vpp=s->vpp_code;
                m->candidate_severity=severity;
            }
            if(s->rms_code<m->candidate_min_rms) m->candidate_min_rms=s->rms_code;
            if(s->vpp_code<m->candidate_min_vpp) m->candidate_min_vpp=s->vpp_code;
            if(severity>m->candidate_severity) m->candidate_severity=severity;
            m->enter_count++;
        } else m->enter_count=0;
        /* Follow slow gain/temperature drift only while the link is healthy.
         * The 1/64 update is intentionally much slower than event entry and
         * therefore cannot absorb a short fault. */
        if(!bad) {
            m->baseline_rms += (s->rms_code-m->baseline_rms)/64.0;
            m->baseline_vpp += (s->vpp_code-m->baseline_vpp)/64.0;
            if(s->frequency_valid && m->baseline_freq>0.0)
                m->baseline_freq += (s->frequency_mhz-m->baseline_freq)/64.0;
        }
        if(m->enter_count>=m->cfg.enter_confirm_frames) {
            m->state=DIAG_MONITOR_FAULT; m->fault_frames=0;
            /* Confirmation gates detection, not the observed fault onset. */
            m->event_start_ms=m->candidate_start_ms;
            m->event_min_rms=m->candidate_min_rms; m->event_min_vpp=m->candidate_min_vpp;
            m->event_severity=m->candidate_severity;
            m->event_sequence++;
            m->exit_count=0; m->recovery_start_ms=0;
        }
        return 0;
    }
    m->fault_frames++;
    if(s->rms_code<m->event_min_rms) m->event_min_rms=s->rms_code;
    if(s->vpp_code<m->event_min_vpp) m->event_min_vpp=s->vpp_code;
    double sev=sample_severity(m,s);
    if(sev>m->event_severity) m->event_severity=sev;
    int recovered=!abnormal(m,s,m->cfg.drop_exit_ratio);
    if(recovered) {
        if(!m->exit_count) m->recovery_start_ms=s->timestamp_ms;
        m->exit_count++;
    } else {
        m->exit_count=0;
        m->recovery_start_ms=0;
    }
    uint64_t recovery_elapsed=(m->exit_count && s->timestamp_ms>=m->recovery_start_ms) ?
        s->timestamp_ms-m->recovery_start_ms:0;
    if(m->exit_count>=m->cfg.exit_confirm_frames &&
       recovery_elapsed>=m->cfg.recovery_hold_ms) {
        m->state=DIAG_MONITOR_NORMAL; m->enter_count=0;
        if(completed) {
            completed->sequence=m->event_sequence;
            /* Wait for a sustained recovery before closing the event, but
             * report the first healthy sample as the physical fault end. */
            completed->start_ms=m->event_start_ms; completed->end_ms=m->recovery_start_ms;
            completed->duration_ms=m->recovery_start_ms>=m->event_start_ms ?
                m->recovery_start_ms-m->event_start_ms:0;
            completed->severity=m->event_severity;
            completed->baseline_rms=m->baseline_rms; completed->minimum_rms=m->event_min_rms;
            completed->baseline_vpp=m->baseline_vpp; completed->minimum_vpp=m->event_min_vpp;
            completed->pre_frames=m->cfg.baseline_frames; completed->post_frames=m->exit_count;
        }
        m->exit_count=0; m->recovery_start_ms=0;
        return completed ? 1:0;
    }
    return 0;
}

int diag_monitor_write_all(int fd,const void *data,size_t size) {
    if(size>INT_MAX) return -1;
    const char *p=data;
    size_t done=0;
    while(done<size) {
        ssize_t n=write(fd,p+done,size-done);
        if(n<0 && errno==EINTR) continue;
        if(n<=0) return -1;
        done+=(size_t)n;
    }
    return (int)done;
}

int diag_monitor_write_csv_header(int fd) {
    static const char h[]="sequence,start_ms,end_ms,duration_ms,severity,baseline_rms,minimum_rms,baseline_vpp,minimum_vpp,pre_frames,post_frames\n";
    return diag_monitor_write_all(fd,h,sizeof(h)-1);
}

int diag_monitor_write_csv_event(int fd, const struct diag_monitor_event *e) {
    char b[320];
    int n=snprintf(b,sizeof(b),"%u,%llu,%llu,%llu,%.6f,%.3f,%.3f,%.3f,%.3f,%u,%u\n",
        e->sequence,(unsigned long long)e->start_ms,(unsigned long long)e->end_ms,
        (unsigned long long)e->duration_ms,e->severity,e->baseline_rms,e->minimum_rms,
        e->baseline_vpp,e->minimum_vpp,e->pre_frames,e->post_frames);
    return n>0 && n<(int)sizeof(b) ? diag_monitor_write_all(fd,b,(size_t)n):-1;
}
