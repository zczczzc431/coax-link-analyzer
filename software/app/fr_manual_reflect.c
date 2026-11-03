#include "fr_manual_reflect.h"
#include <math.h>

int fr_manual_reflect_compute(double forward_rms_code,
                              double reverse_rms_code,
                              double min_forward_rms_code,
                              struct fr_manual_reflect_result *out) {
    if (!out || !isfinite(forward_rms_code) || !isfinite(reverse_rms_code) ||
        !isfinite(min_forward_rms_code) || min_forward_rms_code <= 0.0)
        return FR_MANUAL_REFLECT_BAD_ARGUMENT;
    if (forward_rms_code < min_forward_rms_code)
        return FR_MANUAL_REFLECT_LOW_FORWARD;
    if (reverse_rms_code < 0.0)
        return FR_MANUAL_REFLECT_INVALID_REVERSE;

    const double ratio = reverse_rms_code / forward_rms_code;
    if (!isfinite(ratio) || ratio < 0.0)
        return FR_MANUAL_REFLECT_INVALID_REVERSE;
    out->forward_rms_code = forward_rms_code;
    out->reverse_rms_code = reverse_rms_code;
    out->relative_reflection_magnitude = ratio;
    /* This is only a logarithmic display of the scalar ratio.  It is not
     * Return Loss because no calibrated reflection coefficient is available. */
    out->relative_db = ratio > 0.0 ? 20.0 * log10(ratio) : -INFINITY;
    return FR_MANUAL_REFLECT_OK;
}
