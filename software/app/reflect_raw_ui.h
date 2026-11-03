#ifndef REFLECT_RAW_UI_H
#define REFLECT_RAW_UI_H
#include "fr_raw_capture.h"

enum fr_ui_notice {
    FR_UI_WAIT_HARDWARE, FR_UI_STOPPED, FR_UI_CLEARED,
    FR_UI_EXPORT_OK, FR_UI_EXPORT_FAILED, FR_UI_NO_DATA, FR_UI_BAD_CONFIG,
    FR_UI_RESOURCE_BUSY, FR_UI_PREPARING, FR_UI_ACQUIRING,
    FR_UI_STOPPING, FR_UI_COMPLETE, FR_UI_CAPTURE_FAILED,
    FR_UI_MANUAL_WAIT_REV, FR_UI_MANUAL_CAPTURE_REV
};
struct fr_ui_capture_meta {
    /* Application snapshot for the retained capture, not pending UI settings.
     * Digital percent is a request, not measured voltage. Confirmation flags
     * describe command/adapter cleanup, not analog zero or physical RF state. */
    double requested_amplitude_pct;
    int known, output_off_confirmed, cleanup_confirmed;
};
struct fr_ui {
    struct fr_raw_capture capture;
    struct fr_raw_config pending;
    enum fr_ui_notice notice;
    struct fr_ui_capture_meta capture_meta;
    unsigned selected;
    int logarithmic, help, synthetic, manual_mode, application_admitted;
};
struct fr_ui_paint {
    void *ctx;
    void (*line)(void *, int, int, int, int, uint32_t);
    void (*text)(void *, int, int, const char *, int, uint32_t);
};

void fr_ui_init(struct fr_ui *u);
int fr_ui_prepare(struct fr_ui *u, const struct fr_raw_config *cfg, int logarithmic);
/* This renderer never calls hardware. Its standalone START is blocked; the
 * application intercepts START through its separately admitted backend.
 * CLEAR only clears raw REFLECT state, never MONITOR/SWEEP evidence. */
int fr_ui_touch(struct fr_ui *u, int x, int y, int w, int h, const char *csv_path);
int fr_ui_export(struct fr_ui *u, const char *path);
void fr_ui_render(const struct fr_ui *u, struct fr_ui_paint p, int w, int h);
void fr_ui_set_manual_mode(struct fr_ui *u, int enabled);
/* Tests may inject synthetic traces into this model directly. A synthetic
 * flag and explicit watermark/export label are mandatory for such tests. */
#endif
