#ifndef FR_MANUAL_SESSION_H
#define FR_MANUAL_SESSION_H

#include "fr_manual_reflect.h"
#include <stddef.h>
#include <stdio.h>

#define FR_MANUAL_MAX_POINTS 64u
enum fr_manual_session_state {
    FR_MANUAL_IDLE = 0,
    FR_MANUAL_CAPTURE_FWD,
    FR_MANUAL_WAIT_REV_RECONNECT,
    FR_MANUAL_CAPTURE_REV,
    FR_MANUAL_COMPLETE,
    FR_MANUAL_ERROR
};
struct fr_manual_session {
    enum fr_manual_session_state state;
    unsigned points;
    double frequency_mhz[FR_MANUAL_MAX_POINTS];
    double forward_rms[FR_MANUAL_MAX_POINTS];
    double reverse_rms[FR_MANUAL_MAX_POINTS];
    struct fr_manual_reflect_result result[FR_MANUAL_MAX_POINTS];
    unsigned captured;
    int error;
};
int fr_manual_session_begin(struct fr_manual_session *s,
                            const double *frequency_mhz, unsigned points);
int fr_manual_session_forward_done(struct fr_manual_session *s,
                                   const double *forward_rms,
                                   unsigned points);
int fr_manual_session_confirm_reverse_connected(struct fr_manual_session *s);
int fr_manual_session_reverse_done(struct fr_manual_session *s,
                                   const double *reverse_rms,
                                   unsigned points,
                                   double min_forward_rms);
/* CSV is deliberately labelled scalar/manual; it never emits S11/VSWR/DTF. */
int fr_manual_session_write_csv(FILE *file, const struct fr_manual_session *s);
#endif
