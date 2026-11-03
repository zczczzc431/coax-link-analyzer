#include "fr_manual_session.h"
#include <math.h>
#include <string.h>

static int valid_axis(const double *axis, unsigned points) {
    if (!axis || points == 0 || points > FR_MANUAL_MAX_POINTS) return 0;
    for (unsigned i = 0; i < points; ++i) {
        if (!isfinite(axis[i]) || axis[i] <= 0.0 ||
            (i && axis[i] <= axis[i - 1])) return 0;
    }
    return 1;
}

int fr_manual_session_begin(struct fr_manual_session *s,
                            const double *frequency_mhz, unsigned points) {
    if (!s || !valid_axis(frequency_mhz, points)) return -1;
    memset(s, 0, sizeof(*s));
    s->points = points;
    memcpy(s->frequency_mhz, frequency_mhz, points * sizeof(double));
    s->state = FR_MANUAL_CAPTURE_FWD;
    return 0;
}

int fr_manual_session_forward_done(struct fr_manual_session *s,
                                   const double *forward_rms,
                                   unsigned points) {
    if (!s || s->state != FR_MANUAL_CAPTURE_FWD ||
        !forward_rms || points != s->points) return -1;
    for (unsigned i = 0; i < points; ++i) {
        if (!isfinite(forward_rms[i]) || forward_rms[i] < 0.0) {
            s->state = FR_MANUAL_ERROR; s->error = -1; return -1;
        }
    }
    memcpy(s->forward_rms, forward_rms, points * sizeof(double));
    s->captured = 0;
    s->state = FR_MANUAL_WAIT_REV_RECONNECT;
    return 0;
}

int fr_manual_session_confirm_reverse_connected(struct fr_manual_session *s) {
    if (!s || s->state != FR_MANUAL_WAIT_REV_RECONNECT) return -1;
    s->captured = 0;
    s->state = FR_MANUAL_CAPTURE_REV;
    return 0;
}

int fr_manual_session_reverse_done(struct fr_manual_session *s,
                                   const double *reverse_rms,
                                   unsigned points,
                                   double min_forward_rms) {
    if (!s || s->state != FR_MANUAL_CAPTURE_REV ||
        !reverse_rms || points != s->points ||
        !isfinite(min_forward_rms) || min_forward_rms <= 0.0) return -1;
    for (unsigned i = 0; i < points; ++i) {
        if (!isfinite(reverse_rms[i]) || reverse_rms[i] < 0.0 ||
            fr_manual_reflect_compute(s->forward_rms[i], reverse_rms[i],
                                      min_forward_rms, &s->result[i]) !=
                FR_MANUAL_REFLECT_OK) {
            s->state = FR_MANUAL_ERROR; s->error = -1; return -1;
        }
    }
    memcpy(s->reverse_rms, reverse_rms, points * sizeof(double));
    s->captured = points;
    s->state = FR_MANUAL_COMPLETE;
    return 0;
}

int fr_manual_session_write_csv(FILE *file, const struct fr_manual_session *s) {
    if (!file || !s || s->state != FR_MANUAL_COMPLETE ||
        s->points == 0 || s->points > FR_MANUAL_MAX_POINTS) return -1;
    if (fprintf(file, "# MANUAL_REFLECT_SCALAR_V1\n"
                     "# workflow=FWD_CAPTURE_THEN_MANUAL_RECONNECT_THEN_REV_CAPTURE\n"
                     "# units=ADC_AC_RMS_CODE; relative_db=20log10(REV/FWD)\n"
                     "# not_S11_not_RETURN_LOSS_not_VSWR_not_DTF\n"
                     "frequency_mhz,forward_rms_code,reverse_rms_code,"
                     "relative_reflection_magnitude,relative_db\n") < 0) return -1;
    for (unsigned i = 0; i < s->points; ++i) {
        const struct fr_manual_reflect_result *r = &s->result[i];
        if (fprintf(file, "%.9g,%.9g,%.9g,%.9g,%.9g\n",
                    s->frequency_mhz[i], r->forward_rms_code,
                    r->reverse_rms_code,
                    r->relative_reflection_magnitude, r->relative_db) < 0)
            return -1;
    }
    return ferror(file) ? -1 : 0;
}
