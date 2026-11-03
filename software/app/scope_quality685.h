#ifndef SCOPE_QUALITY685_H
#define SCOPE_QUALITY685_H
#include <math.h>
#include <string.h>

/* Ordinary SCOPE readout policy only; not an estimator or calibration.
 * Never promote an explicit fit rejection to an apparently valid fallback.
 * Limited-coverage legacy estimates remain usable, but must be labelled EDGE
 * and must not claim fitted-period jitter. No RAW or other consumer changes. */
enum sp685_quality { SP685_INVALID=0, SP685_FIT, SP685_EDGE_EST };
static inline enum sp685_quality sp685_classify(int legacy_valid,double legacy_mhz,
        double period_ns,int timing_valid,const char *reason,int rail,int low_signal) {
    if(legacy_valid!=1 || rail || low_signal || !isfinite(legacy_mhz) ||
       legacy_mhz<=0 || !reason) return SP685_INVALID;
    if(timing_valid==1 && !strcmp(reason,"FIT_PERIOD_ESTIMATE") &&
       isfinite(period_ns) && period_ns>0) return SP685_FIT;
    if(timing_valid==0 && isnan(period_ns) &&
       (!strcmp(reason,"TOO_FEW_PERIODS") || !strcmp(reason,"UNDERSAMPLED_TIMING")))
        return SP685_EDGE_EST;
    return SP685_INVALID;
}
#endif
