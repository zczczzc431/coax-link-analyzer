#ifndef MONITOR_EVENT_HEALTH_H
#define MONITOR_EVENT_HEALTH_H

#include "monitor_core.h"

struct monitor_event_health {
    char event_health_label[16];
    char severity[16];
    char quality_reason[48];
    double severity_score;
};

/* Event-only summary.  It never consumes SWEEP relative_db or health_fingerprint. */
int monitor_event_health_summarize(const struct diag_monitor_event *event,
                                  struct monitor_event_health *out);

#endif
