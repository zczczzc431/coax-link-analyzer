#include "signal_trends.h"
#include <math.h>
#include <string.h>
int signal_timing_measure(const uint32_t *a,size_t n,double fs,struct signal_timing *o) {
    if(!o) return -1;
    memset(o,0,sizeof(*o));o->reason="INVALID_INPUT";
    o->frequency_hz=o->period_ns=o->jitter_rms_ns=o->jitter_pp_ns=NAN;
    if(!a || n<16 || !isfinite(fs) || fs<=0) return -1;
    double mean=0;unsigned lo=16383,hi=0;
    for(size_t i=0;i<n;i++) {
        if(a[i]>16383) return -1;
        mean+=a[i];if(a[i]<lo)lo=a[i];if(a[i]>hi)hi=a[i];
    }
    mean/=n;
    if(lo<=4 || hi>=16379){o->reason="CLIPPED";return 0;}
    if(hi-lo<32){o->reason="LOW_SIGNAL";return 0;}
    double hysteresis=fmax(16.0,(hi-lo)*.08),pmin=HUGE_VAL,pmax=0;
    double edges[2048]; size_t edge_count=0;
    int armed=0;
    for(size_t i=0;i<n;i++) {
        if(a[i]<=mean-hysteresis)armed=1;
        if(armed && i && a[i-1]<mean+hysteresis && a[i]>=mean+hysteresis) {
            double edge=(double)(i-1)+(mean+hysteresis-a[i-1])/((double)a[i]-a[i-1]);
            if(edge_count<sizeof(edges)/sizeof(edges[0])) edges[edge_count++]=edge;
            armed=0;
        }
    }
    if(edge_count<4){o->reason="TOO_FEW_PERIODS";return 0;}
    o->periods=(unsigned)(edge_count-1U);
    double sx=0,sy=0,sxx=0,sxy=0;
    for(size_t i=0;i<edge_count;i++) {
        double x=(double)i,y=edges[i];
        sx+=x;sy+=y;sxx+=x*x;sxy+=x*y;
    }
    double den=(double)edge_count*sxx-sx*sx;
    double period=(den>0)?((double)edge_count*sxy-sx*sy)/den:0;
    if(!isfinite(period) || period<8){o->reason="UNDERSAMPLED_TIMING";return 0;}
    double intercept=(sy-period*sx)/(double)edge_count;
    double fit_ss=0;
    for(size_t i=0;i<edge_count;i++) {
        double err=edges[i]-(intercept+period*(double)i);
        fit_ss+=err*err;
    }
    double fit_var=fit_ss/(double)(edge_count-2U);
    if(!isfinite(fit_var) || fit_var>period*period*.01){o->reason="UNSTABLE_PERIOD";return 0;}
    double mean_period=0,m2=0;
    for(size_t i=1;i<edge_count;i++) {
        double p=edges[i]-edges[i-1];
        if(p<pmin)pmin=p;
        if(p>pmax)pmax=p;
        double d=p-mean_period;
        mean_period+=d/(double)i;
        m2+=d*(p-mean_period);
    }
    o->frequency_hz=fs/period;o->period_ns=period*1e9/fs;
    o->jitter_rms_ns=sqrt(fmax(0,m2/(double)o->periods))*1e9/fs;
    o->jitter_pp_ns=(pmax-pmin)*1e9/fs;
    o->valid=1;o->reason="FIT_PERIOD_ESTIMATE";return 0;
}
void signal_trend_reset(struct signal_trend *s){if(s)memset(s,0,sizeof(*s));}
int signal_trend_push(struct signal_trend *s,const struct signal_frame *f) {
    if(!s || !f || !isfinite(f->rms_code) || f->rms_code<0 ||
       (f->frequency_valid && (!isfinite(f->frequency_hz)||f->frequency_hz<=0))) return -1;
    if(s->count) {
        const struct signal_frame *last=&s->frame[s->count-1];
        if(last->token==f->token)return 0;
        if(f->observed_us<=last->observed_us || f->observed_us-last->observed_us>1000000)
            signal_trend_reset(s);
    }
    if(s->count==SIGNAL_TREND_FRAMES) {
        memmove(s->frame,s->frame+1,(SIGNAL_TREND_FRAMES-1)*sizeof(*s->frame));s->count--;
    }
    s->frame[s->count++]=*f;return 1;
}
int signal_trend_summarize(const struct signal_trend *s,double threshold,struct signal_trend_result *o) {
    if(!s || !o || s->count>SIGNAL_TREND_FRAMES || !isfinite(threshold) || threshold<0)return -1;
    memset(o,0,sizeof(*o));o->frames=s->count;
    o->amplitude_index_pct=o->low_high_ratio_pct=NAN;
    o->frequency_min_hz=o->frequency_max_hz=o->frequency_span_hz=NAN;
    if(!s->count)return 0;
    double amin=HUGE_VAL,amax=0,fmin=HUGE_VAL,fmax=0;
    for(unsigned i=0;i<s->count;i++) {
        const struct signal_frame *f=&s->frame[i];
        if(f->rms_code<amin)amin=f->rms_code;
        if(f->rms_code>amax)amax=f->rms_code;
        if(i) {
            double gap=(f->observed_us-s->frame[i-1].observed_us)/1000.0;
            if(gap>o->max_gap_ms)o->max_gap_ms=gap;
            if(f->frequency_valid && s->frame[i-1].frequency_valid &&
               fabs(f->frequency_hz-s->frame[i-1].frequency_hz)>threshold)o->observed_changes++;
        }
        if(f->frequency_valid) {
            o->frequency_frames++;if(f->frequency_hz<fmin)fmin=f->frequency_hz;
            if(f->frequency_hz>fmax)fmax=f->frequency_hz;
        }
    }
    o->span_ms=(s->frame[s->count-1].observed_us-s->frame[0].observed_us)/1000.0;
    if(s->count>=8 && amax>=16) {
        o->amplitude_valid=1;o->amplitude_index_pct=100*(amax-amin)/(amax+amin);
        o->low_high_ratio_pct=100*amin/amax;
    }
    if(o->frequency_frames>=8) {
        o->frequency_valid=1;o->frequency_min_hz=fmin;o->frequency_max_hz=fmax;
        o->frequency_span_hz=fmax-fmin;
    }
    return 0;
}
