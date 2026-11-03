#include "fr_vector_math.h"
#include <ctype.h>
#include <math.h>
#include <string.h>
#define PI 3.14159265358979323846
#define LIGHT_SPEED 299792458.0
static int finite_complex(double complex z) {return isfinite(creal(z)) && isfinite(cimag(z));}
int fr_vector_counter_phase(uint32_t counter,uint32_t ftw,double *phase) {
    if(!phase || !ftw || ftw>=0x80000000u)return -1;
    /* Integer modulo avoids precision loss from multiplying long-running
     * sample counts by floating-point frequency. Counter wrap is continuous
     * for a source with an exact Q0.32 per-sample phase increment. */
    uint32_t angle=(uint32_t)((uint64_t)counter*ftw);
    *phase=2*PI*(double)angle/4294967296.0;return 0;
}
const char *fr_vector_error_name(enum fr_vector_error e) {
    switch(e) {
    case FR_VECTOR_OK:return "READY";
    case FR_VECTOR_NO_PHASE:return "COHERENT PHASE REQUIRED";
    case FR_VECTOR_NO_CAL:return "S11 CALIBRATION REQUIRED";
    case FR_VECTOR_BAD_DATA:return "INVALID COMPLEX DATA";
    case FR_VECTOR_GRID:return "UNIFORM FREQUENCY GRID REQUIRED";
    case FR_VECTOR_SINGULAR:return "DEGENERATE CALIBRATION";
    default:return "FILE FORMAT / READ ERROR";
    }
}
enum fr_vector_error fr_vector_validate(const struct fr_vector_trace *t,int cal) {
    if(!t || t->points<3 || t->points>FR_VECTOR_POINTS ||
       t->source<FR_VECTOR_IMPORTED || t->source>FR_VECTOR_BOARD)return FR_VECTOR_BAD_DATA;
    for(unsigned i=0;i<t->points;i++)
        if(!isfinite(t->hz[i]) || t->hz[i]<=0 || (i && t->hz[i]<=t->hz[i-1]) ||
           !finite_complex(t->s11[i]) || cabs(t->s11[i])>1e6)return FR_VECTOR_BAD_DATA;
    if(t->phase_coherent!=1 || !t->reference_id)return FR_VECTOR_NO_PHASE;
    if(cal && t->calibrated!=1)return FR_VECTOR_NO_CAL;
    return FR_VECTOR_OK;
}
static int same_grid(const struct fr_vector_trace *a,const struct fr_vector_trace *b) {
    if(a->points!=b->points || a->reference_id!=b->reference_id || a->source!=b->source)return 0;
    for(unsigned i=0;i<a->points;i++)if(a->hz[i]!=b->hz[i])return 0;
    return 1;
}
enum fr_vector_error fr_vector_fit_frame(const struct fr_vector_frame *f,double complex *out,double *residual) {
    if(!f || !out || !residual || !f->samples || f->count<8 || f->count>2048 ||
       !isfinite(f->sample_rate_hz) || !isfinite(f->frequency_hz) ||
       f->frequency_hz<=0 || f->sample_rate_hz<=2*f->frequency_hz ||
       !isfinite(f->source_phase_at_first_sample) || f->source<FR_VECTOR_IMPORTED ||
       f->source>FR_VECTOR_BOARD)return FR_VECTOR_BAD_DATA;
    if(f->phase_anchor_verified!=1 || !f->reference_id)return FR_VECTOR_NO_PHASE;
    double a[3][4]={{0}};double step=2*PI*f->frequency_hz/f->sample_rate_hz;
    for(unsigned n=0;n<f->count;n++) {
        if(f->samples[n]<=4 || f->samples[n]>=16379)return FR_VECTOR_BAD_DATA;
        double v[3]={1,cos(step*n),sin(step*n)},y=(double)f->samples[n]-8192;
        for(unsigned i=0;i<3;i++) {
            for(unsigned j=0;j<3;j++)a[i][j]+=v[i]*v[j];
            a[i][3]+=v[i]*y;
        }
    }
    /* Small pivoted solve, no assumption of an integer number of periods. */
    for(unsigned k=0;k<3;k++) {
        unsigned p=k;
        for(unsigned i=k+1;i<3;i++)if(fabs(a[i][k])>fabs(a[p][k]))p=i;
        if(fabs(a[p][k])<1e-10*f->count)return FR_VECTOR_SINGULAR;
        for(unsigned j=k;j<4;j++){double tmp=a[k][j];a[k][j]=a[p][j];a[p][j]=tmp;}
        double pivot=a[k][k];for(unsigned j=k;j<4;j++)a[k][j]/=pivot;
        for(unsigned i=0;i<3;i++)if(i!=k) {
            double multiple=a[i][k];for(unsigned j=k;j<4;j++)a[i][j]-=multiple*a[k][j];
        }
    }
    double complex z=(a[1][3]-I*a[2][3])*cexp(-I*f->source_phase_at_first_sample);
    double sum=0;
    for(unsigned n=0;n<f->count;n++) {
        double prediction=a[0][3]+a[1][3]*cos(step*n)+a[2][3]*sin(step*n);
        double e=(double)f->samples[n]-8192-prediction;sum+=e*e;
    }
    if(!finite_complex(z) || !isfinite(sum))return FR_VECTOR_BAD_DATA;
    *out=z;*residual=sqrt(sum/f->count);return FR_VECTOR_OK;
}
enum fr_vector_error fr_vector_frame_ratio(const struct fr_vector_frame *f,const struct fr_vector_frame *r,
    double minimum,double complex *out) {
    if(!f || !r || !out || !isfinite(minimum) || minimum<=0 ||
       f->reference_id!=r->reference_id || f->source!=r->source ||
       f->frequency_hz!=r->frequency_hz || f->sample_rate_hz!=r->sample_rate_hz)return FR_VECTOR_BAD_DATA;
    double complex a,b;double residual;
    enum fr_vector_error e=fr_vector_fit_frame(f,&a,&residual);if(e!=FR_VECTOR_OK)return e;
    if((e=fr_vector_fit_frame(r,&b,&residual))!=FR_VECTOR_OK)return e;
    if(cabs(a)<minimum)return FR_VECTOR_BAD_DATA;
    double complex ratio=b/a;if(!finite_complex(ratio))return FR_VECTOR_BAD_DATA;
    *out=ratio;return FR_VECTOR_OK;
}
enum fr_vector_error fr_vector_frame_ratio_checked(const struct fr_vector_frame *f,
    const struct fr_vector_frame *r,const struct fr_vector_quality_limits *limits,
    double complex *out,struct fr_vector_quality_result *quality) {
    if(!f || !r || !limits || !out || !quality ||
       !isfinite(limits->min_fwd_peak_code) || limits->min_fwd_peak_code<=0 ||
       !isfinite(limits->absolute_residual_code) || limits->absolute_residual_code<0 ||
       !isfinite(limits->relative_residual_fraction) || limits->relative_residual_fraction<0 ||
       limits->relative_residual_fraction>1 ||
       f->reference_id!=r->reference_id || f->source!=r->source ||
       f->frequency_hz!=r->frequency_hz || f->sample_rate_hz!=r->sample_rate_hz)
        return FR_VECTOR_BAD_DATA;
    double complex a,b;struct fr_vector_quality_result q;
    enum fr_vector_error e=fr_vector_fit_frame(f,&a,&q.fwd_residual_code);
    if(e!=FR_VECTOR_OK)return e;
    if((e=fr_vector_fit_frame(r,&b,&q.rev_residual_code))!=FR_VECTOR_OK)return e;
    q.fwd_peak_code=cabs(a);q.rev_peak_code=cabs(b);
    double lf=limits->absolute_residual_code+limits->relative_residual_fraction*q.fwd_peak_code;
    double lr=limits->absolute_residual_code+limits->relative_residual_fraction*q.rev_peak_code;
    if(!isfinite(lf) || !isfinite(lr) || q.fwd_peak_code<limits->min_fwd_peak_code ||
       q.fwd_residual_code>lf || q.rev_residual_code>lr)return FR_VECTOR_BAD_DATA;
    double complex ratio=b/a;
    if(!finite_complex(ratio))return FR_VECTOR_BAD_DATA;
    *quality=q;*out=ratio;return FR_VECTOR_OK;
}

enum fr_vector_error fr_vector_cal_build(struct fr_vector_cal *out,const struct fr_vector_trace *l,
    const struct fr_vector_trace *o,const struct fr_vector_trace *s) {
    if(!out)return FR_VECTOR_BAD_DATA;
    /* Failure invalidates this requested calibration. No partially ready set. */
    memset(out,0,sizeof(*out));
    enum fr_vector_error e=fr_vector_validate(l,0);
    if(e!=FR_VECTOR_OK)return e;
    if((e=fr_vector_validate(o,0))!=FR_VECTOR_OK || (e=fr_vector_validate(s,0))!=FR_VECTOR_OK)return e;
    if(!same_grid(l,o) || !same_grid(l,s))return FR_VECTOR_GRID;
    struct fr_vector_cal c={0};c.points=l->points;c.reference_id=l->reference_id;c.source=l->source;
    for(unsigned i=0;i<c.points;i++) {
        double complex a=o->s11[i]-l->s11[i],b=s->s11[i]-l->s11[i];
        double scale=cabs(a)+cabs(b);
        if(scale<1e-12 || cabs(a-b)<1e-12*scale)return FR_VECTOR_SINGULAR;
        c.directivity[i]=l->s11[i];c.source_match[i]=(a+b)/(a-b);
        c.tracking[i]=a*(1.0-c.source_match[i]);c.hz[i]=l->hz[i];
        if(!finite_complex(c.source_match[i]) || !finite_complex(c.tracking[i]) ||
           cabs(c.tracking[i])<1e-12*scale)return FR_VECTOR_SINGULAR;
    }
    c.ready=1;*out=c;return FR_VECTOR_OK;
}
enum fr_vector_error fr_vector_cal_apply(const struct fr_vector_cal *c,const struct fr_vector_trace *raw,
    struct fr_vector_trace *out) {
    if(!out || !c || c->ready!=1)return FR_VECTOR_NO_CAL;
    enum fr_vector_error e=fr_vector_validate(raw,0);if(e!=FR_VECTOR_OK)return e;
    if(c->points!=raw->points || c->reference_id!=raw->reference_id || c->source!=raw->source)
        return FR_VECTOR_GRID;
    struct fr_vector_trace t=*raw;
    for(unsigned i=0;i<t.points;i++) {
        if(c->hz[i]!=raw->hz[i])return FR_VECTOR_GRID;
        double complex delta=raw->s11[i]-c->directivity[i];
        double complex denominator=c->tracking[i]+c->source_match[i]*delta;
        double scale=cabs(c->tracking[i])+cabs(c->source_match[i]*delta);
        if(!finite_complex(denominator) || scale<1e-12 || cabs(denominator)<1e-12*scale)
            return FR_VECTOR_SINGULAR;
        t.s11[i]=delta/denominator;
        if(!finite_complex(t.s11[i]) || cabs(t.s11[i])>1e6)return FR_VECTOR_BAD_DATA;
    }
    t.calibrated=1;*out=t;return FR_VECTOR_OK;
}
int fr_vector_metrics(double complex gamma,double *rl,double *swr) {
    if(!rl || !swr || !finite_complex(gamma))return -1;
    double a=cabs(gamma);
    *rl=a==0?INFINITY:-20*log10(a);
    *swr=a<1?(1+a)/(1-a):a==1?INFINITY:NAN;
    return 0;
}
static void inverse_fft(double complex z[FR_DTF_BINS]) {
    for(unsigned i=1,j=0;i<FR_DTF_BINS;i++) {
        unsigned bit=FR_DTF_BINS>>1;
        for(;j&bit;bit>>=1)j^=bit;
        j^=bit;
        if(i<j){double complex t=z[i];z[i]=z[j];z[j]=t;}
    }
    for(unsigned len=2;len<=FR_DTF_BINS;len<<=1) {
        double complex step=cexp(I*2*PI/len);
        for(unsigned start=0;start<FR_DTF_BINS;start+=len) {
            double complex w=1;
            for(unsigned j=0;j<len/2;j++) {
                double complex a=z[start+j],b=z[start+j+len/2]*w;
                z[start+j]=a+b;z[start+j+len/2]=a-b;w*=step;
            }
        }
    }
    for(unsigned i=0;i<FR_DTF_BINS;i++)z[i]/=FR_DTF_BINS;
}
enum fr_vector_error fr_dtf_compute(const struct fr_vector_trace *t,double vf,
    enum fr_dtf_window window,struct fr_dtf_result *out) {
    if(!out)return FR_VECTOR_BAD_DATA;
    memset(out,0,sizeof(*out));out->peak_bin=-1;
    enum fr_vector_error e=fr_vector_validate(t,1);
    if(e!=FR_VECTOR_OK){out->error=e;return e;}
    if(!isfinite(vf) || vf<0.1 || vf>1 || (window!=FR_DTF_RECT && window!=FR_DTF_HANN)) {
        out->error=FR_VECTOR_BAD_DATA;return out->error;
    }
    double df=(t->hz[t->points-1]-t->hz[0])/(t->points-1);
    for(unsigned i=1;i<t->points;i++)
        if(fabs(t->hz[i]-(t->hz[0]+i*df))>fmax(1e-3,df*1e-9)) {
            out->error=FR_VECTOR_GRID;return out->error;
        }
    if(df<1e-3 || !isfinite(df)){out->error=FR_VECTOR_GRID;return out->error;}
    double complex z[FR_DTF_BINS]={0};double weight_sum=0;
    for(unsigned i=0;i<t->points;i++) {
        double w=window==FR_DTF_RECT?1.0:0.5*(1-cos(2*PI*i/(t->points-1)));
        z[i]=w*t->s11[i];weight_sum+=w;
    }
    inverse_fft(z);
    double peak=1e-12;
    for(unsigned i=0;i<FR_DTF_BINS;i++) {
        out->magnitude[i]=cabs(z[i])*FR_DTF_BINS/weight_sum;
        if(out->magnitude[i]>peak){peak=out->magnitude[i];out->peak_bin=(int)i;}
    }
    out->distance_step_m=LIGHT_SPEED*vf/(2*FR_DTF_BINS*df);
    out->period_m=LIGHT_SPEED*vf/(2*df);
    out->bandwidth_scale_m=LIGHT_SPEED*vf/(2*(t->hz[t->points-1]-t->hz[0]));
    out->velocity_factor=vf;out->window=window;out->points=t->points;
    out->source=t->source;out->ready=1;return FR_VECTOR_OK;
}
enum fr_vector_error fr_vector_read_s1p(FILE *f,struct fr_vector_trace *out) {
    if(!f || !out)return FR_VECTOR_FILE;
    struct fr_vector_trace t={0};char line[512];int header=0,metadata=0;double factor=1;char fmt[8]="";
    while(fgets(line,sizeof(line),f)) {
        if(!strchr(line,'\n') && !feof(f))return FR_VECTOR_FILE;
        char *p=line;while(isspace((unsigned char)*p))p++;
        if(!*p)continue;
        if(*p=='!') {
            if(!strncmp(p,"! zynq_vector_v1 ",17)) {
                char source[20],phase[20],cal[20],tail;unsigned long long ref;
                if(metadata || sscanf(p,"! zynq_vector_v1 source=%19s phase=%19s calibration=%19s reference_id=%llu %c",
                    source,phase,cal,&ref,&tail)!=4)return FR_VECTOR_FILE;
                if(!strcmp(source,"IMPORTED"))t.source=FR_VECTOR_IMPORTED;
                else if(!strcmp(source,"SYNTHETIC"))t.source=FR_VECTOR_SYNTHETIC;
                else return FR_VECTOR_FILE;
                if(strcmp(phase,"COHERENT") && strcmp(phase,"UNKNOWN"))return FR_VECTOR_FILE;
                if(strcmp(cal,"APPLIED") && strcmp(cal,"NONE"))return FR_VECTOR_FILE;
                t.phase_coherent=!strcmp(phase,"COHERENT");t.calibrated=!strcmp(cal,"APPLIED");
                t.reference_id=ref;metadata=1;
            }
            continue;
        }
        char *comment=strchr(p,'!');if(comment)*comment=0;
        if(*p=='#') {
            char unit[8],param[8],r[8],tail;double impedance;
            if(header || t.points || sscanf(p,"# %7s %7s %7s %7s %lf %c",unit,param,fmt,r,&impedance,&tail)!=5 ||
                strcmp(param,"S") || strcmp(r,"R") || impedance!=50)return FR_VECTOR_FILE;
            if(!strcmp(unit,"Hz"))factor=1;else if(!strcmp(unit,"kHz"))factor=1e3;
            else if(!strcmp(unit,"MHz"))factor=1e6;else if(!strcmp(unit,"GHz"))factor=1e9;
            else return FR_VECTOR_FILE;
            if(strcmp(fmt,"RI") && strcmp(fmt,"MA") && strcmp(fmt,"DB"))return FR_VECTOR_FILE;
            header=1;continue;
        }
        double hz,a,b;char tail;
        if(!header || t.points>=FR_VECTOR_POINTS || sscanf(p,"%lf %lf %lf %c",&hz,&a,&b,&tail)!=3 ||
           !isfinite(hz) || !isfinite(a) || !isfinite(b))return FR_VECTOR_FILE;
        double complex value;
        if(!strcmp(fmt,"RI"))value=a+I*b;
        else {
            double mag=!strcmp(fmt,"MA")?a:pow(10,a/20);
            if(mag<0 || !isfinite(mag))return FR_VECTOR_FILE;
            value=mag*cexp(I*(b*PI/180));
        }
        hz*=factor;
        if(!isfinite(hz) || hz<=0 || (t.points && hz<=t.hz[t.points-1]) ||
           !finite_complex(value) || cabs(value)>1e6)return FR_VECTOR_FILE;
        t.hz[t.points]=hz;t.s11[t.points++]=value;
    }
    if(ferror(f) || t.points<3)return FR_VECTOR_FILE;
    if(!metadata)t.source=FR_VECTOR_IMPORTED; /* usable to inspect; compute still rejects */
    *out=t;return FR_VECTOR_OK;
}
int fr_vector_write_s1p(FILE *f,const struct fr_vector_trace *t) {
    if(!f || fr_vector_validate(t,0)!=FR_VECTOR_OK)return -1;
    const char *source=t->source==FR_VECTOR_SYNTHETIC?"SYNTHETIC":"IMPORTED";
    if(fprintf(f,"! zynq_vector_v1 source=%s phase=COHERENT calibration=%s reference_id=%llu\n"
        "! exported_source_enum=%d; file declarations are not board verification\n# Hz S RI R 50\n",
        source,t->calibrated?"APPLIED":"NONE",(unsigned long long)t->reference_id,t->source)<0)return -1;
    for(unsigned i=0;i<t->points;i++)
        if(fprintf(f,"%.17g %.17g %.17g\n",t->hz[i],creal(t->s11[i]),cimag(t->s11[i]))<0)return -1;
    return ferror(f)?-1:0;
}
int fr_dtf_write_csv(FILE *f,const struct fr_dtf_result *r) {
    if(!f || !r || !r->ready || r->error!=FR_VECTOR_OK)return -1;
    if(fprintf(f,"# DTF_BANDPASS_ENVELOPE; source=%d; zero_padding_is_not_resolution\n"
        "# vf=%.17g window=%s points=%u bins=%u period_m=%.17g bandwidth_scale_m=%.17g\n"
        "# strongest_peak_bin=%d; strongest_response_is_not_verified_fault\n"
        "distance_m,response_magnitude\n",r->source,r->velocity_factor,
        r->window==FR_DTF_RECT?"RECT":"HANN",r->points,FR_DTF_BINS,r->period_m,
        r->bandwidth_scale_m,r->peak_bin)<0)return -1;
    for(unsigned i=0;i<FR_DTF_BINS;i++)
        if(fprintf(f,"%.17g,%.17g\n",i*r->distance_step_m,r->magnitude[i])<0)return -1;
    return ferror(f)?-1:0;
}
