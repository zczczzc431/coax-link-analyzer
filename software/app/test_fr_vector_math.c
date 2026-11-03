#include "fr_vector_math.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define PI 3.14159265358979323846
static unsigned checks;
static void ck(int x,const char *msg) {checks++;if(!x){fprintf(stderr,"VECTOR_FAIL %s\n",msg);exit(1);}}
static struct fr_vector_trace trace(void) {
    struct fr_vector_trace t={.points=64,.source=FR_VECTOR_SYNTHETIC,.phase_coherent=1,
        .calibrated=1,.reference_id=7};
    for(unsigned k=0;k<t.points;k++)t.hz[k]=1000000+1000000*k;
    return t;
}
static void frames(void) {
    uint32_t a[2048],b[2048];
    const double rate=400e6,hz=1.123e6,anchor_a=.7,anchor_b=-1.2;
    double complex gamma=-.2+I*.15;
    for(unsigned n=0;n<2048;n++) {
        a[n]=(uint32_t)lround(8200+500*cos(2*PI*hz*n/rate+anchor_a));
        b[n]=(uint32_t)lround(8100+creal(500*gamma*cexp(I*(2*PI*hz*n/rate+anchor_b))));
    }
    struct fr_vector_frame f={a,2048,rate,hz,anchor_a,7,FR_VECTOR_SYNTHETIC,1},r=f;
    r.samples=b;r.source_phase_at_first_sample=anchor_b;
    double complex z=123;double residual=-1;
    ck(fr_vector_fit_frame(&f,&z,&residual)==FR_VECTOR_OK && cabs(z-500)<.05 && residual<.5,
        "non-integer period fit removes DC and anchored source phase");
    ck(fr_vector_frame_ratio(&f,&r,8,&z)==FR_VECTOR_OK && cabs(z-gamma)<.0002,
        "different frame origins correctly referred to common phase");
    f.phase_anchor_verified=0;z=123;
    ck(fr_vector_frame_ratio(&f,&r,8,&z)==FR_VECTOR_NO_PHASE && z==123,
        "Linux timing or local phase without anchor cannot invent complex ratio");
    f.phase_anchor_verified=1;r.reference_id=8;
    ck(fr_vector_frame_ratio(&f,&r,8,&z)==FR_VECTOR_BAD_DATA,"different reference IDs refused");
    r.reference_id=7;a[8]=16383;
    ck(fr_vector_fit_frame(&f,&z,&residual)==FR_VECTOR_BAD_DATA,"clipped raw samples refused");
    a[8]=8192;f.frequency_hz=rate/2;
    ck(fr_vector_fit_frame(&f,&z,&residual)==FR_VECTOR_BAD_DATA,"Nyquist phase degeneracy refused");
}
static void calibration(void) {
    struct fr_vector_trace l=trace(),o=l,s=l,d=l,out={0};
    struct fr_vector_cal c={0};
    l.calibrated=o.calibrated=s.calibrated=d.calibrated=0;
    for(unsigned k=0;k<64;k++) {
        double complex ed=0.02+I*0.03,er=0.7-I*0.12,es=0.07+I*0.02;
        double complex gamma=0.5*cexp(-I*2*PI*d.hz[k]*43e-9);
        l.s11[k]=ed;o.s11[k]=ed+er/(1-es);s.s11[k]=ed-er/(1+es);
        d.s11[k]=ed+er*gamma/(1-es*gamma);
    }
    ck(fr_vector_cal_build(&c,&l,&o,&s)==FR_VECTOR_OK && c.ready,"build complex ideal OSL");
    ck(fr_vector_cal_apply(&c,&d,&out)==FR_VECTOR_OK && out.calibrated,"apply complex OSL");
    for(unsigned k=0;k<64;k++)ck(cabs(out.s11[k]-0.5*cexp(-I*2*PI*d.hz[k]*43e-9))<1e-12,
        "remove complex directivity tracking and source match");
    d.reference_id=8;ck(fr_vector_cal_apply(&c,&d,&out)==FR_VECTOR_GRID,"different phase setup blocked");
    ck(out.reference_id==7,"failed apply retains previous result");
    d.reference_id=7;d.hz[2]+=1;ck(fr_vector_cal_apply(&c,&d,&out)==FR_VECTOR_GRID,"calibration grid mismatch");
    s=o;ck(fr_vector_cal_build(&c,&l,&o,&s)==FR_VECTOR_SINGULAR && !c.ready,"identical standards refused");
    l.phase_coherent=0;ck(fr_vector_cal_build(&c,&l,&o,&s)==FR_VECTOR_NO_PHASE,"RMS-only calibration refused");
    double rl,swr;
    ck(fr_vector_metrics(0.5,&rl,&swr)==0 && fabs(rl-6.02059991328)<1e-10 && swr==3,
        "known return loss and VSWR");
    ck(fr_vector_metrics(0,&rl,&swr)==0 && isinf(rl) && swr==1,"matched limit not fake 60 dB");
    ck(fr_vector_metrics(1,&rl,&swr)==0 && rl==0 && isinf(swr),"open limit preserved");
    ck(fr_vector_metrics(1.2,&rl,&swr)==0 && rl<0 && isnan(swr),"above-one never clamped");
}
static void transform(void) {
    struct fr_vector_trace t=trace();struct fr_dtf_result r;
    const int bin=37;double tau=(double)bin/(256e6);
    for(unsigned k=0;k<t.points;k++)t.s11[k]=0.6*cexp(-I*2*PI*t.hz[k]*tau);
    ck(fr_dtf_compute(&t,0.66,FR_DTF_RECT,&r)==FR_VECTOR_OK && r.ready,"bandpass transform");
    ck(r.peak_bin==bin && fabs(r.magnitude[bin]-0.6)<1e-12,"delay sign and coherent window gain");
    ck(fabs(r.distance_step_m*bin-299792458*0.66*tau/2)<1e-12,"one-way distance factor two");
    ck(fabs(r.period_m-299792458*0.66/(2e6))<1e-12,"unambiguous periodic range");
    ck(fabs(r.bandwidth_scale_m-299792458*0.66/(2*63e6))<1e-12,"bandwidth not zero-pad resolution");
    ck(fr_dtf_compute(&t,0.8,FR_DTF_HANN,&r)==FR_VECTOR_OK && r.peak_bin==bin &&
        fabs(r.magnitude[bin]-0.6)<1e-12,"Hann gain and peak");
    /* Independent O(N*M) inverse DFT checks every output bin of the radix-2 FFT. */
    for(unsigned n=0;n<FR_DTF_BINS;n++) {
        double complex z=0;double sum=0;
        for(unsigned k=0;k<t.points;k++) {
            double w=0.5*(1-cos(2*PI*k/(t.points-1)));sum+=w;
            z+=w*t.s11[k]*cexp(I*2*PI*k*n/FR_DTF_BINS);
        }
        ck(fabs(r.magnitude[n]-cabs(z)/sum)<1e-12,"FFT agrees with independent direct DFT");
    }
    t.calibrated=0;ck(fr_dtf_compute(&t,.66,FR_DTF_RECT,&r)==FR_VECTOR_NO_CAL && !r.ready,
        "uncalibrated distance blocked");
    t.calibrated=1;t.phase_coherent=0;
    ck(fr_dtf_compute(&t,.66,FR_DTF_RECT,&r)==FR_VECTOR_NO_PHASE && !r.ready,"amplitude-only blocked");
    t.phase_coherent=1;t.hz[31]+=1000;
    ck(fr_dtf_compute(&t,.66,FR_DTF_RECT,&r)==FR_VECTOR_GRID,"rounded or logarithmic grid refused");
    t=trace();ck(fr_dtf_compute(&t,1.1,FR_DTF_RECT,&r)==FR_VECTOR_BAD_DATA,"bad VF refused");
    ck(fr_dtf_compute(&t,.66,FR_DTF_RECT,&r)==FR_VECTOR_OK && r.peak_bin==-1,"zero data has no fault marker");
    t=trace();
    for(unsigned k=0;k<64;k++)t.s11[k]=0.6*cexp(-I*2*PI*t.hz[k]*37/(256e6))+
        0.25*cexp(-I*2*PI*t.hz[k]*150/(256e6));
    ck(fr_dtf_compute(&t,.66,FR_DTF_HANN,&r)==FR_VECTOR_OK && r.peak_bin==37 &&
        fabs(r.magnitude[150]-.25)<1e-5,"two separated reflectors retained");
    FILE *f=tmpfile();ck(f!=NULL && fr_dtf_write_csv(f,&r)==0,"distance export");
    rewind(f);char text[512]={0};ck(fread(text,1,511,f)>0 && strstr(text,"zero_padding_is_not_resolution"),
        "CSV physical limitation retained");fclose(f);
}
static enum fr_vector_error parse(const char *text,struct fr_vector_trace *t) {
    FILE *f=tmpfile();ck(f!=NULL,"parse fixture");ck(fputs(text,f)>=0,"write fixture");rewind(f);
    enum fr_vector_error e=fr_vector_read_s1p(f,t);fclose(f);return e;
}
static void files(void) {
    struct fr_vector_trace t=trace(),out;
    for(unsigned k=0;k<64;k++)t.s11[k]=.2+I*.1;
    FILE *f=tmpfile();ck(f!=NULL && fr_vector_write_s1p(f,&t)==0,"Touchstone write");rewind(f);
    ck(fr_vector_read_s1p(f,&out)==FR_VECTOR_OK && out.source==FR_VECTOR_SYNTHETIC && out.calibrated &&
        out.reference_id==7,"synthetic provenance survives round trip");fclose(f);
    for(unsigned k=0;k<64;k++)ck(t.hz[k]==out.hz[k] && cabs(t.s11[k]-out.s11[k])<1e-15,"S1P round trip");
    ck(parse("# MHz S MA R 50\n1 .5 90\n2 .5 90\n3 .5 90\n",&out)==FR_VECTOR_OK &&
        cabs(out.s11[0]-I*.5)<1e-12 && out.hz[0]==1e6,"MHz magnitude-angle conversion");
    ck(fr_vector_validate(&out,1)==FR_VECTOR_NO_PHASE,"plain S1P does not invent coherence or calibration");
    ck(parse("# GHz S DB R 50\n.001 -6.02059991328 180\n.002 -6.02059991328 180\n.003 -6.02059991328 180\n",&out)==FR_VECTOR_OK &&
        cabs(out.s11[0]+.5)<1e-12,"dB degrees conversion");
    struct fr_vector_trace previous=out;
    ck(parse("# Hz S RI R 50\n1 0 0 5\n2 0 0\n3 0 0\n",&out)==FR_VECTOR_FILE &&
        !memcmp(&out,&previous,sizeof(out)),"bad/multiport input preserves prior trace");
    ck(parse("# Hz S RI R 50\n1 nan 0\n2 0 0\n3 0 0\n",&out)==FR_VECTOR_FILE,"NaN input blocked");
    ck(parse("# Hz S RI R 75\n1 0 0\n2 0 0\n3 0 0\n",&out)==FR_VECTOR_FILE,"other impedance blocked");
    ck(parse("# Hz S RI R 50\n2 0 0\n1 0 0\n3 0 0\n",&out)==FR_VECTOR_FILE,"unordered input blocked");
}
static void counter_reference(void) {
    double phase=0,previous;
    uint32_t ftw=0x20000004u; /* 4x NCO FTW=0x08000001, only a synthetic model. */
    ck(fr_vector_counter_phase(0,ftw,&phase)==0 && phase==0,"zero counter phase");
    ck(fr_vector_counter_phase(UINT32_MAX,ftw,&previous)==0 &&
        cabs(cexp(I*(previous+2*PI*ftw/4294967296.0))-1)<1e-12,"counter wrap phase continuity");
    ck(fr_vector_counter_phase(1,0,&phase)<0 && fr_vector_counter_phase(1,0x80000000u,&phase)<0 &&
        fr_vector_counter_phase(1,ftw,NULL)<0,"zero step and aliased reference rejected");
    uint32_t fwd[129],rev[129];double pf,pr;
    ck(fr_vector_counter_phase(UINT32_MAX-79,ftw,&pf)==0 &&
       fr_vector_counter_phase(29,ftw,&pr)==0,"different first-sample counters across wrap");
    double step=2*PI*ftw/4294967296.0;
    for(unsigned i=0;i<129;i++) {
        fwd[i]=(uint32_t)lround(8192+51+1000*cos(pf+3.04+step*i));
        rev[i]=(uint32_t)lround(8192-32+250*cos(pr+3.04-1.1+step*i));
    }
    struct fr_vector_frame a={.samples=fwd,.count=129,.sample_rate_hz=400e6,
        .frequency_hz=400e6*ftw/4294967296.0,.source_phase_at_first_sample=pf,
        .source=FR_VECTOR_SYNTHETIC,.phase_anchor_verified=1,.reference_id=211};
    struct fr_vector_frame b=a;b.samples=rev;b.source_phase_at_first_sample=pr;
    double complex ratio;
    ck(fr_vector_frame_ratio(&a,&b,10,&ratio)==FR_VECTOR_OK &&
        cabs(ratio-.25*cexp(-I*1.1))<.001,"single ADC reference cancels unknown common source phase");
    b.phase_anchor_verified=0;
    ck(fr_vector_frame_ratio(&a,&b,10,&ratio)==FR_VECTOR_NO_PHASE,"computed phase is not hardware verification");
}
static void quality_gate(void) {
    uint32_t a[512],b[512];
    struct fr_vector_frame f={.samples=a,.count=512,.sample_rate_hz=400e6,
        .frequency_hz=12.5e6,.source_phase_at_first_sample=0,
        .source=FR_VECTOR_SYNTHETIC,.phase_anchor_verified=1,.reference_id=212},r=f;
    r.samples=b;
    struct fr_vector_quality_limits limits={10,1,.01};
    struct fr_vector_quality_result q={0},before;
    double complex ratio=99;
    for(unsigned n=0;n<512;n++) {
        a[n]=(uint32_t)lround(8192+500*cos(2*PI*n/32));
        b[n]=(uint32_t)lround(8192+100*cos(2*PI*n/32+.4));
    }
    ck(fr_vector_frame_ratio_checked(&f,&r,&limits,&ratio,&q)==FR_VECTOR_OK &&
        cabs(ratio-.2*cexp(I*.4))<.001 && q.fwd_residual_code<.5 && q.rev_residual_code<.5,
        "quality gate accepts clean quantized phasors");
    before=q;
    for(unsigned n=0;n<512;n++)b[n]+=n%2?30:(uint32_t)-30;
    ratio=99;
    ck(fr_vector_frame_ratio_checked(&f,&r,&limits,&ratio,&q)==FR_VECTOR_BAD_DATA &&
        ratio==99 && !memcmp(&q,&before,sizeof(q)),"REV residual rejected atomically");
    for(unsigned n=0;n<512;n++)b[n]=8192;
    ck(fr_vector_frame_ratio_checked(&f,&r,&limits,&ratio,&q)==FR_VECTOR_OK &&
        cabs(ratio)<1e-10,"matched zero REV is not a low-signal failure");
    for(unsigned n=0;n<512;n++)a[n]+=n%2?30:(uint32_t)-30;
    ck(fr_vector_frame_ratio_checked(&f,&r,&limits,&ratio,&q)==FR_VECTOR_BAD_DATA,
        "FWD residual also rejected");
    for(unsigned n=0;n<512;n++)a[n]=(uint32_t)lround(8192+5*cos(2*PI*n/32));
    ck(fr_vector_frame_ratio_checked(&f,&r,&limits,&ratio,&q)==FR_VECTOR_BAD_DATA,
        "weak forward denominator blocked");
    limits.relative_residual_fraction=NAN;
    ck(fr_vector_frame_ratio_checked(&f,&r,&limits,&ratio,&q)==FR_VECTOR_BAD_DATA,"NaN quality limit");
    limits.relative_residual_fraction=1.1;
    ck(fr_vector_frame_ratio_checked(&f,&r,&limits,&ratio,&q)==FR_VECTOR_BAD_DATA,"unbounded relative limit");
    limits.relative_residual_fraction=.01;limits.absolute_residual_code=-1;
    ck(fr_vector_frame_ratio_checked(&f,&r,&limits,&ratio,&q)==FR_VECTOR_BAD_DATA,"negative absolute limit");
    limits.absolute_residual_code=1;limits.min_fwd_peak_code=INFINITY;
    ck(fr_vector_frame_ratio_checked(&f,&r,&limits,&ratio,&q)==FR_VECTOR_BAD_DATA,"infinite minimum");
    limits.min_fwd_peak_code=10;
    ck(fr_vector_frame_ratio_checked(&f,&r,&limits,&ratio,NULL)==FR_VECTOR_BAD_DATA,"missing quality output");
    for(unsigned n=0;n<512;n++)a[n]=(uint32_t)lround(8192+500*cos(2*PI*n/32));
    r.phase_anchor_verified=0;
    ck(fr_vector_frame_ratio_checked(&f,&r,&limits,&ratio,&q)==FR_VECTOR_NO_PHASE,
        "residual check cannot grant missing phase");
    r.phase_anchor_verified=1;r.reference_id++;
    ck(fr_vector_frame_ratio_checked(&f,&r,&limits,&ratio,&q)==FR_VECTOR_BAD_DATA,
        "different source epoch blocked");
}
int main(void) {
    frames();counter_reference();quality_gate();calibration();transform();files();
    printf("VECTOR_MATH_PASS checks=%u synthetic_only_no_board\n",checks);return 0;
}
