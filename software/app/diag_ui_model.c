#include "diag_ui_model.h"
#include <string.h>

static const char *page_name(enum diag_ui_page p) {
    static const char *names[]={"MONITOR","SWEEP","REFLECT","DTF","REPORT","SELFTEST"};
    return p>=DIAG_PAGE_MONITOR && p<=DIAG_PAGE_SELFTEST ? names[p]:"MONITOR";
}
void diag_ui_init(struct diag_ui *ui) { memset(ui,0,sizeof(*ui)); }
int diag_ui_top_toggle_hit(int width,int x,int y) {
    (void)width; return x>=330 && x<470 && y>=4 && y<34;
}
int diag_ui_bottom_tab_hit(int width,int height,int x,int y) {
    if(y<height-47 || y>=height || x<20 || x>=width-20) return -1;
    int step=(width-40)/7, tab=(x-20)/step;
    return tab>=0 && tab<7 ? tab:-1;
}
int diag_ui_touch(struct diag_ui *ui,int width,int height,int x,int y) {
    if(diag_ui_top_toggle_hit(width,x,y)) {
        ui->mode=ui->mode==DIAG_UI_SCOPE?DIAG_UI_DIAG:DIAG_UI_SCOPE;
        return 1;
    }
    if(ui->mode!=DIAG_UI_DIAG) return 0;
    int tab=diag_ui_bottom_tab_hit(width,height,x,y);
    if(tab<0) return 0;
    if(tab==6) ui->mode=DIAG_UI_SCOPE;
    else ui->page=(enum diag_ui_page)tab;
    return 1;
}
struct diag_ui_snapshot diag_ui_make_snapshot(const struct diag_ui *ui,
    const struct diag_monitor *m,const struct diag_monitor_sample *current,
    const struct diag_monitor_event *last) {
    struct diag_ui_snapshot s; memset(&s,0,sizeof(s));
    s.page_label=page_name(ui?ui->page:DIAG_PAGE_MONITOR);
    if(!m) { s.state_label="UNAVAILABLE"; return s; }
    s.state_label=m->state==DIAG_MONITOR_NO_SIGNAL?"NO SIGNAL":
        m->state==DIAG_MONITOR_BASELINE?"BASELINE":
        m->state==DIAG_MONITOR_FAULT?(m->exit_count?"RECOVERING":"FAULT"):"NORMAL";
    s.baseline_rms=m->baseline_rms; s.event_count=m->event_sequence;
    if(current) { s.current_rms=current->rms_code; s.measurement_available=1; }
    if(last) { s.severity=last->severity; s.last_duration_ms=last->duration_ms; }
    if(m->state==DIAG_MONITOR_FAULT) s.severity=m->event_severity;
    return s;
}
