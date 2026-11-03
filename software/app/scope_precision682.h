#ifndef SCOPE_PRECISION682_H
#define SCOPE_PRECISION682_H
#include <math.h>
#include <stddef.h>
#include <stdint.h>

/* Display-only choice. Do not feed this value into the acquisition, monitor,
 * sweep, reflection, FFT, autoset or trend paths. period_ns already belongs
 * to the current measured RAW frame; there is no cross-frame smoothing. */
static inline double sp682_frequency_mhz(int scope_page,int legacy_valid,
                                       double legacy_mhz,double period_ns,
                                       int *used_fit) {
    if(used_fit) *used_fit=0;
    if(scope_page && legacy_valid && isfinite(legacy_mhz) && legacy_mhz>0 &&
       isfinite(period_ns) && period_ns>0) {
        double mhz=1000.0/period_ns;
        if(isfinite(mhz) && mhz>0) {
            if(used_fit) *used_fit=1;
            return mhz;
        }
    }
    return legacy_mhz;
}

/* Locate only the crossing beside the actual hardware trigger. The returned
 * offset translates plot coordinates, never samples or individual periods.
 * Reject ambiguous, forced, clipped, low-quality or near-rail triggers. */
static inline int sp682_edge_shift(const uint32_t *a,size_t n,unsigned flags,
                                  unsigned index,int level,int falling,
                                  unsigned lo,unsigned hi,int quality_ok,
                                  double *offset) {
    if(!offset) return 0;
    *offset=0;
    if(!a || n<16 || index<2 || (size_t)index+2>=n || !quality_ok ||
       (flags&3u)!=3u || (flags&4u) || lo<=4 || hi>=16379 || hi<=lo ||
       hi-lo<32 || (falling!=0 && falling!=1)) return 0;
    double span=(double)(hi-lo);
    if(level<=lo+0.15*span || level>=hi-0.15*span) return 0;
    double min_slope=fmax(16.0,0.05*span),found=0;
    int crossings=0,qualified=0;
    for(size_t i=(size_t)index-1;i<=(size_t)index+1;i++) {
        double p=a[i-1],q=a[i];
        if(p>16383 || q>16383) return 0;
        int crossing=falling ? (p>=level && q<level) : (p<level && q>=level);
        if(!crossing) continue;
        double edge=(double)(i-1)+(level-p)/(q-p);
        double delta=edge-(double)index;
        /* Any second same-direction crossing in the local search is
         * ambiguous, even when one is just beyond the shift budget. */
        crossings++;
        found=delta;
        qualified=isfinite(delta) && fabs(delta)<=1.25 && fabs(q-p)>=min_slope;
    }
    if(crossings!=1 || !qualified) return 0;
    *offset=found;
    return 1;
}

/* Keep every actual point and the original integer projection at offset=0.
 * plot_line() is responsible for the normal left/right clipping. */
static inline int sp682_plot_x(int left,int width,int visible,int sample,
                              double offset) {
    if(visible<=0 || width<=0 || !isfinite(offset)) return left;
    return left+(int)floor(((double)sample-offset)*(double)width/(double)visible);
}
#endif
