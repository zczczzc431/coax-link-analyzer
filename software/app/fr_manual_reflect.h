#ifndef FR_MANUAL_REFLECT_H
#define FR_MANUAL_REFLECT_H

#include <stddef.h>

/* Manual single-ADC REFLECT math.
 * The user captures the forward sample first, then reconnects the reverse
 * coupled port to the same ADC path.  The result is a scalar amplitude ratio;
 * it is deliberately not a complex reflection coefficient, return loss, VSWR,
 * or DTF value. */
enum fr_manual_reflect_status {
    FR_MANUAL_REFLECT_OK = 0,
    FR_MANUAL_REFLECT_BAD_ARGUMENT = -1,
    FR_MANUAL_REFLECT_LOW_FORWARD = -2,
    FR_MANUAL_REFLECT_INVALID_REVERSE = -3
};

struct fr_manual_reflect_result {
    double forward_rms_code;
    double reverse_rms_code;
    double relative_reflection_magnitude;
    double relative_db;
};

int fr_manual_reflect_compute(double forward_rms_code,
                              double reverse_rms_code,
                              double min_forward_rms_code,
                              struct fr_manual_reflect_result *out);

#endif
