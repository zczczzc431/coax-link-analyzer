#define _POSIX_C_SOURCE 200809L
#include "fr_vector_ui.h"
#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
void fr_vector_ui_init(struct fr_vector_ui *u) {
    if(!u)return;
    memset(u,0,sizeof(*u));u->initialized=1;u->velocity_factor=.66;
    u->window=FR_DTF_HANN;u->error=FR_VECTOR_NO_PHASE;
}
static int path(char out[440],const char *dir,const char *leaf) {
    if(!dir || dir[0]!='/' || strlen(dir)>360)return -1;
    int n=snprintf(out,440,"%s/%s",dir,leaf);return n>0 && n<440?0:-1;
}
static enum fr_vector_error load(const char *dir,const char *leaf,struct fr_vector_trace *t) {
    char name[440];if(path(name,dir,leaf)<0)return FR_VECTOR_FILE;
    int fd=open(name,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);struct stat s;
    if(fd<0)return FR_VECTOR_FILE;
    if(fstat(fd,&s)<0 || !S_ISREG(s.st_mode) || s.st_size>65536) {close(fd);return FR_VECTOR_FILE;}
    FILE *f=fdopen(fd,"r");if(!f){close(fd);return FR_VECTOR_FILE;}
    enum fr_vector_error e=fr_vector_read_s1p(f,t);
    if(fclose(f)!=0)e=FR_VECTOR_FILE;
    return e;
}
static int save(struct fr_vector_ui *u,int dtf,const char *dir) {
    char target[440],temp[480];
    if(path(target,dir,dtf?"dtf_result.csv":"vector_result.s1p")<0)return -1;
    int n=snprintf(temp,sizeof(temp),"%s.tmp.XXXXXX",target);
    if(n<0 || (size_t)n>=sizeof(temp))return -1;
    int d=open(dir,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(d<0)return -1;
    int fd=mkstemp(temp);if(fd<0){close(d);return -1;}
    FILE *f=fdopen(fd,"w");
    if(!f){close(fd);unlink(temp);close(d);return -1;}
    int ok=(dtf?fr_dtf_write_csv(f,&u->dtf):fr_vector_write_s1p(f,&u->trace))==0;
    if(ok)ok=fflush(f)==0;
    if(ok)ok=fsync(fileno(f))==0;
    if(fclose(f)!=0)ok=0;
    if(ok)ok=rename(temp,target)==0;
    if(ok)ok=fsync(d)==0;
    if(!ok)unlink(temp);
    close(d);return ok?0:-1;
}
int fr_vector_ui_touch(struct fr_vector_ui *u,int dtf,int x,int y,const char *dir) {
    if(!u)return 0;
    if(!u->initialized)fr_vector_ui_init(u);
    if(x>=650 && x<775 && y>=101 && y<125) {
        if(dtf) {u->exported=save(u,1,dir)==0;u->error=u->exported?FR_VECTOR_OK:FR_VECTOR_FILE;}
        else fr_vector_ui_init(u);
        return 1;
    }
    if(x>=75 && x<=775 && y>=205 && y<=318) {
        unsigned n=dtf?FR_DTF_BINS:u->trace.points;
        if((dtf && !u->dtf.ready) || (!dtf && !u->have_trace) || !n)return 0;
        if(dtf)u->cursor=(unsigned)lround((x-75)*(n-1)/700.0);
        else {
            double hz=u->trace.hz[0]+(x-75)*(u->trace.hz[n-1]-u->trace.hz[0])/700.0;
            unsigned nearest=0;
            for(unsigned i=1;i<n;i++)if(fabs(u->trace.hz[i]-hz)<fabs(u->trace.hz[nearest]-hz))nearest=i;
            u->cursor=nearest;
        }
        return 1;
    }
    if(y<382 || y>=421 || x<20 || x>=780 || (x-20)%152>=146)return 0;
    int key=(x-20)/152;u->exported=0;
    if(key==0) {
        struct fr_vector_trace t;
        enum fr_vector_error e=load(dir,"vector_input.s1p",&t);
        u->error=e;
        if(e==FR_VECTOR_OK) {u->trace=t;u->have_trace=1;u->cursor=0;memset(&u->dtf,0,sizeof(u->dtf));}
    } else if(dtf) {
        if(key==1 || key==2) {
            double vf=u->velocity_factor+(key==1?-.01:.01);
            u->velocity_factor=fmin(1.0,fmax(.1,round(vf*100)/100));
            memset(&u->dtf,0,sizeof(u->dtf));
        } else if(key==3) {
            u->window=u->window==FR_DTF_HANN?FR_DTF_RECT:FR_DTF_HANN;
            memset(&u->dtf,0,sizeof(u->dtf));
        } else if(key==4) {
            u->error=fr_dtf_compute(&u->trace,u->velocity_factor,u->window,&u->dtf);
            u->cursor=u->dtf.peak_bin<0?0:(unsigned)u->dtf.peak_bin;
        }
    } else {
        if(key==1) {
            struct fr_vector_trace l,o,s;struct fr_vector_cal c;
            enum fr_vector_error e=load(dir,"vector_load.s1p",&l);
            if(e==FR_VECTOR_OK)e=load(dir,"vector_open.s1p",&o);
            if(e==FR_VECTOR_OK)e=load(dir,"vector_short.s1p",&s);
            if(e==FR_VECTOR_OK)e=fr_vector_cal_build(&c,&l,&o,&s);
            u->error=e;
            if(e==FR_VECTOR_OK)u->calibration=c; /* failed reload preserves existing calibration */
        } else if(key==2) {
            if(u->trace.calibrated)u->error=FR_VECTOR_BAD_DATA; /* never apply calibration twice */
            else {
                u->error=fr_vector_cal_apply(&u->calibration,&u->trace,&u->trace);
                if(u->error==FR_VECTOR_OK)memset(&u->dtf,0,sizeof(u->dtf));
            }
        } else if(key==3) {
            u->exported=save(u,0,dir)==0;u->error=u->exported?FR_VECTOR_OK:FR_VECTOR_FILE;
        } else if(key==4)return 2;
    }
    return 1;
}
static void button(struct fr_ui_paint p,int x,int y,int width,int height,const char *label) {
    uint32_t c=0x00e0aa;
    p.line(p.ctx,x,y,x+width,y,c);p.line(p.ctx,x,y+height,x+width,y+height,c);
    p.line(p.ctx,x,y,x,y+height,c);p.line(p.ctx,x+width,y,x+width,y+height,c);
    p.text(p.ctx,x+8,y+12,label,1,c);
}
void fr_vector_ui_render(const struct fr_vector_ui *u,int dtf,struct fr_ui_paint p) {
    if(!u || !p.text || !p.line)return;
    uint32_t ink=0xe4edf5,accent=0xffb75e,grid=0x283b49;char b[150];
    p.text(p.ctx,25,105,dtf?"DTF - BANDPASS DISTANCE":"REFLECT - COMPLEX S11",2,ink);
    button(p,650,101,125,24,dtf?"EXPORT CSV":"CLEAR");
    p.text(p.ctx,25,133,u->trace.source==FR_VECTOR_SYNTHETIC?
        "OFFLINE SYNTHETIC - NOT BOARD MEASUREMENT":"IMPORTED DATA - PROVIDER PHASE / CAL DECLARATIONS",1,accent);
    enum fr_vector_error e=u->error;
    if(e==FR_VECTOR_OK && u->have_trace)e=fr_vector_validate(&u->trace,dtf?1:0);
    snprintf(b,sizeof(b),"%s%s",u->exported?"EXPORTED - ":"STATUS  ",fr_vector_error_name(e));
    p.text(p.ctx,25,153,b,1,accent);
    if(dtf)snprintf(b,sizeof(b),"VF %.2f  WINDOW %s  POINTS %u  CAL %s",u->velocity_factor,
        u->window==FR_DTF_HANN?"HANN":"RECT",u->trace.points,u->trace.calibrated?"DECLARED":"NONE");
    else snprintf(b,sizeof(b),"POINTS %u  IDEAL OSL %s  CAL %s",u->trace.points,
        u->calibration.ready?"LOADED":"NONE",u->trace.calibrated?"APPLIED / DECLARED":"NONE");
    p.text(p.ctx,25,175,b,1,ink);
    for(int y=205;y<=318;y+=28)p.line(p.ctx,75,y,775,y,grid);
    p.line(p.ctx,75,205,75,318,grid);p.line(p.ctx,775,205,775,318,grid);
    unsigned n=dtf?FR_DTF_BINS:u->trace.points;double peak=0;
    int ready=dtf?u->dtf.ready:u->have_trace;
    if(ready && n>1) {
        for(unsigned i=0;i<n;i++) {
            double a=dtf?u->dtf.magnitude[i]:cabs(u->trace.s11[i]);
            if(a>peak)peak=a;
        }
        int px=75,py=318;
        for(unsigned i=0;i<n;i++) {
            double a=dtf?u->dtf.magnitude[i]:cabs(u->trace.s11[i]);
            double fraction=dtf?(double)i/(n-1):
                (u->trace.hz[i]-u->trace.hz[0])/(u->trace.hz[n-1]-u->trace.hz[0]);
            int x=75+(int)lround(fraction*700.0),y=318-(int)lround(peak>0?a/peak*110:0);
            if(i)p.line(p.ctx,px,py,x,y,0x00e0aa);
            px=x;py=y;
        }
        unsigned cursor=u->cursor<n?u->cursor:0;
        double fraction=dtf?(double)cursor/(n-1):
            (u->trace.hz[cursor]-u->trace.hz[0])/(u->trace.hz[n-1]-u->trace.hz[0]);
        int x=75+(int)lround(fraction*700.0);p.line(p.ctx,x,205,x,318,accent);
        snprintf(b,sizeof(b),"Y AUTO 0..%.4g; X %s %.4g..%.4g %s",peak,dtf?"DISTANCE":"LINEAR",
            dtf?0:u->trace.hz[0]/1e6,dtf?(n-1)*u->dtf.distance_step_m:u->trace.hz[n-1]/1e6,dtf?"M":"MHZ");
        p.text(p.ctx,75,193,b,1,ink);
        if(dtf) {
            snprintf(b,sizeof(b),"CURSOR %.3f M  RESPONSE %.5g  PERIOD %.2f M",cursor*u->dtf.distance_step_m,
                u->dtf.magnitude[cursor],u->dtf.period_m);
            p.text(p.ctx,25,334,b,1,ink);
            snprintf(b,sizeof(b),"BANDWIDTH SCALE %.3f M; WINDOW BROADENS PEAKS; STRONGEST RESPONSE ONLY",
                u->dtf.bandwidth_scale_m);
        } else {
            double rl=NAN,swr=NAN;
            if(u->trace.calibrated && fr_vector_validate(&u->trace,1)==FR_VECTOR_OK)
                (void)fr_vector_metrics(u->trace.s11[cursor],&rl,&swr);
            snprintf(b,sizeof(b),"CURSOR %.3f MHZ  MAG %.5g  RL %.4g DB  VSWR %.4g",
                u->trace.hz[cursor]/1e6,cabs(u->trace.s11[cursor]),rl,swr);
            p.text(p.ctx,25,334,b,1,ink);
            snprintf(b,sizeof(b),"RL / VSWR REQUIRE CALIBRATED COMPLEX DATA; IMPORT IS NOT BOARD VERIFICATION");
        }
        p.text(p.ctx,25,355,b,1,accent);
    } else p.text(p.ctx,25,250,"LOAD COMPLEX S11; RMS-ONLY DATA CANNOT SUPPLY PHASE",1,accent);
    const char *a[]={"IMPORT S1P","CAL OSL","APPLY CAL","EXPORT S1P","RAW"};
    const char *d[]={"IMPORT S1P","VF -0.01","VF +0.01","WINDOW","RUN DTF"};
    for(int i=0;i<5;i++)button(p,20+i*152,382,146,38,dtf?d[i]:a[i]);
}
