#include "monitor_event_health.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static double clamp01(double x) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }

int monitor_event_health_summarize(const struct diag_monitor_event *e,
                                  struct monitor_event_health *o) {
    double drop, score;
    if (!e || !o) return -1;
    memset(o, 0, sizeof(*o));
    if (!isfinite(e->baseline_rms) || e->baseline_rms <= 0.0 ||
        !isfinite(e->minimum_rms) || e->minimum_rms < 0.0) {
        snprintf(o->event_health_label, sizeof(o->event_health_label), "INVALID");
        snprintf(o->severity, sizeof(o->severity), "UNKNOWN");
        snprintf(o->quality_reason, sizeof(o->quality_reason), "INVALID_RMS_BASELINE");
        return 0;
    }
    if (e->duration_ms == 0U) {
        snprintf(o->event_health_label, sizeof(o->event_health_label), "INVALID");
        snprintf(o->severity, sizeof(o->severity), "UNKNOWN");
        snprintf(o->quality_reason, sizeof(o->quality_reason), "ZERO_DURATION");
        return 0;
    }
    drop = clamp01(1.0 - e->minimum_rms / e->baseline_rms);
    score = fmax(drop, isfinite(e->severity) ? clamp01(e->severity) : 0.0);
    o->severity_score = score;
    snprintf(o->severity, sizeof(o->severity), "%s", score >= 0.60 ? "HIGH" : (score >= 0.30 ? "MEDIUM" : "LOW"));
    snprintf(o->event_health_label, sizeof(o->event_health_label), "%s", score >= 0.60 ? "FAIL" : (score >= 0.30 ? "WARNING" : "NORMAL"));
    snprintf(o->quality_reason, sizeof(o->quality_reason), "%s", e->minimum_rms <= 0.0 ? "RMS_COLLAPSE" : (drop >= 0.30 ? "RMS_DROP" : "EVENT_WITHIN_TOLERANCE"));
    return 0;
}
