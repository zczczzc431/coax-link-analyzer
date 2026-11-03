#ifndef REFLECT_TREND_QUALITY_H
#define REFLECT_TREND_QUALITY_H
#include <math.h>
/* Empirical display index, NOT a calibrated reflection coefficient. */
static inline double reflect_relative(double load,double open,double shorted,double measured) {
    if(!isfinite(load)||!isfinite(open)||!isfinite(shorted)||!isfinite(measured)||
       load<0||open<0||shorted<0||measured<0) return NAN;
    double span=fmax(open,shorted)-load;
    if(!isfinite(span)||span<8.0) return NAN;
    double g=(measured-load)/span;
    if(!isfinite(g)) return NAN;
    /* Preserve excursions outside the standards for diagnostic evidence. */
    return g;
}
static inline const char *reflect_trend_quality(double measured,double index) {
    if(!isfinite(measured)||measured<0) return "INVALID_DATA";
    if(measured<8.0) return "LOW_SIGNAL";
    if(!isfinite(index)) return "INVALID_CAL";
    return "VALID_TREND";
}
#endif
