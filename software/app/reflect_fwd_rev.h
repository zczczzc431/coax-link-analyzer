#ifndef REFLECT_FWD_REV_H
#define REFLECT_FWD_REV_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FR_MAX_POINTS 64u

enum fr_switch_path {
    FR_PATH_FORWARD = 0,
    FR_PATH_REVERSE = 1
};

enum fr_standard {
    FR_STANDARD_LOAD = 0,
    FR_STANDARD_OPEN = 1,
    FR_STANDARD_SHORT = 2
};

enum fr_session_mode {
    FR_SESSION_MEASURE = 0,
    FR_SESSION_CAL_LOAD = 1,
    FR_SESSION_CAL_OPEN = 2,
    FR_SESSION_CAL_SHORT = 3
};

struct fr_point {
    double fwd_rms;
    double rev_rms;
    double ratio;
    int valid;
};

struct fr_calibration {
    unsigned points;
    struct fr_point standard[3][FR_MAX_POINTS];
    int have_standard[3];
    double min_fwd_rms;
};

struct fr_result {
    double ratio;
    double gamma_magnitude;
    double return_loss_db;
    double vswr;
    int valid;
};

struct fr_switch_backend {
    void *ctx;
    int (*select)(void *ctx, enum fr_switch_path path);
    unsigned settle_us;
};

struct fr_mmio_switch {
    volatile uint32_t *data_reg;
    uint32_t vc1_mask;
    uint32_t vc2_mask;
};

struct fr_session {
    int active;
    int completed;
    int error;
    enum fr_session_mode mode;
    unsigned points;
    unsigned point;
    unsigned settle_frames;
    unsigned settle_left;
    enum fr_switch_path path;
    struct fr_calibration *cal;
    struct fr_switch_backend backend;
    double fwd[FR_MAX_POINTS];
    double rev[FR_MAX_POINTS];
    struct fr_result result[FR_MAX_POINTS];
};

/* Safe numerical primitives.  No hardware access is performed. */
double fr_ratio(double rev_rms, double fwd_rms, double min_fwd_rms);
int fr_point_from_rms(double fwd_rms, double rev_rms, double min_fwd_rms,
                      struct fr_point *out);
int fr_calibration_ready(const struct fr_calibration *cal);
int fr_compute_result(const struct fr_calibration *cal, unsigned point,
                      double fwd_rms, double rev_rms, struct fr_result *out);

/* One call represents one completed ADC RMS frame. */
int fr_session_start(struct fr_session *session, enum fr_session_mode mode,
                     struct fr_calibration *cal, unsigned points,
                     struct fr_switch_backend backend, unsigned settle_frames);
int fr_session_step(struct fr_session *session, double rms);
void fr_session_abort(struct fr_session *session);

/* The caller must map the candidate AXI GPIO data register and must not call
 * this backend until the candidate PL bitstream has been validated. */
int fr_switch_mmio_select(void *ctx, enum fr_switch_path path);

/* A deliberately safe default backend: no GPIO is toggled. */
int fr_switch_backend_disabled(void *ctx, enum fr_switch_path path);

#ifdef __cplusplus
}
#endif

#endif
