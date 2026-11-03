#ifndef FR_VECTOR_UI_H
#define FR_VECTOR_UI_H
#include "fr_vector_math.h"
#include "reflect_raw_ui.h"
struct fr_vector_ui {
    struct fr_vector_trace trace;
    struct fr_vector_cal calibration;
    struct fr_dtf_result dtf;
    enum fr_vector_error error;
    enum fr_dtf_window window;
    double velocity_factor;
    unsigned cursor;
    int initialized,have_trace,exported;
};
void fr_vector_ui_init(struct fr_vector_ui *);
/* Files and in-memory computation only. Returns 2 for REFLECT RAW navigation,
 * 1 handled, 0 elsewhere. Main application excludes active raw hardware work.
 * Fixed files under caller's isolated dir: vector_input/load/open/short.s1p. */
int fr_vector_ui_touch(struct fr_vector_ui *,int dtf,int x,int y,const char *dir);
void fr_vector_ui_render(const struct fr_vector_ui *,int dtf,struct fr_ui_paint);
#endif
