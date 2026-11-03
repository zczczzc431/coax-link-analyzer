#ifndef DIAG_UI_MODEL_H
#define DIAG_UI_MODEL_H
#include "monitor_core.h"
enum diag_ui_mode { DIAG_UI_SCOPE=0, DIAG_UI_DIAG=1 };
enum diag_ui_page { DIAG_PAGE_MONITOR=0, DIAG_PAGE_SWEEP=1, DIAG_PAGE_REFLECT=2, DIAG_PAGE_DTF=3, DIAG_PAGE_REPORT=4, DIAG_PAGE_SELFTEST=5 };
struct diag_ui { enum diag_ui_mode mode; enum diag_ui_page page; };
struct diag_ui_snapshot { const char *state_label,*page_label; double baseline_rms,current_rms,severity; uint32_t event_count; uint64_t last_duration_ms; int measurement_available; };
void diag_ui_init(struct diag_ui *ui);
int diag_ui_top_toggle_hit(int width,int x,int y);
int diag_ui_bottom_tab_hit(int width,int height,int x,int y);
int diag_ui_touch(struct diag_ui *ui,int width,int height,int x,int y);
struct diag_ui_snapshot diag_ui_make_snapshot(const struct diag_ui *ui,const struct diag_monitor *monitor,const struct diag_monitor_sample *current,const struct diag_monitor_event *last_event);
#endif
