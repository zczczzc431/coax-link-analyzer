#define _POSIX_C_SOURCE 200809L
#include "reflect_raw_ui.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned points(const struct fr_ui *u) {
    return u->capture.config.points ? u->capture.config.points : u->pending.points;
}
static const struct fr_raw_config *plot_config(const struct fr_ui *u) {
    return u->capture.config.points ? &u->capture.config : &u->pending;
}
void fr_ui_init(struct fr_ui *u) {
    if (!u) return;
    memset(u, 0, sizeof(*u));
    u->notice = FR_UI_WAIT_HARDWARE;
}
void fr_ui_set_manual_mode(struct fr_ui *u, int enabled) {
    if (!u) return;
    u->manual_mode = enabled ? 1 : 0;
    if (!u->manual_mode && (u->notice == FR_UI_MANUAL_WAIT_REV ||
                            u->notice == FR_UI_MANUAL_CAPTURE_REV))
        u->notice = FR_UI_STOPPED;
}
int fr_ui_prepare(struct fr_ui *u, const struct fr_raw_config *cfg, int logarithmic) {
    if (!u || !cfg || u->capture.active || cfg->points == 0 || cfg->points > 64 ||
        cfg->frames_per_path == 0 || cfg->frames_per_path > 256 ||
        (logarithmic != 0 && logarithmic != 1)) return -1;
    for (unsigned i = 0; i < cfg->points; ++i) {
        if (cfg->frequency_hz[i] < 1000000u || cfg->frequency_hz[i] > 180000000u ||
            (i && cfg->frequency_hz[i] <= cfg->frequency_hz[i - 1])) return -1;
    }
    /* Existing captured traces retain their own frequency configuration. */
    u->pending = *cfg;
    if (!u->capture.config.points) u->logarithmic = logarithmic;
    return 0;
}
static int has_data(const struct fr_ui *u) {
    unsigned n = u->capture.config.points;
    if (n > 64) return 0;
    for (unsigned i = 0; i < n; ++i)
        if (u->capture.trace[i].fwd_frames || u->capture.trace[i].rev_frames) return 1;
    return 0;
}
int fr_ui_export(struct fr_ui *u, const char *path) {
    if (!u) return -1;
    if (!has_data(u)) { u->notice = FR_UI_NO_DATA; return -1; }
    if ((u->capture_meta.known != 0 && u->capture_meta.known != 1) ||
        (u->capture_meta.known && (!isfinite(u->capture_meta.requested_amplitude_pct) ||
         u->capture_meta.requested_amplitude_pct < 0 ||
         u->capture_meta.requested_amplitude_pct > 50))) {
        u->notice = FR_UI_EXPORT_FAILED; return -1;
    }
    if (!path || path[0] != '/' || strlen(path) > 400) {
        u->notice = FR_UI_EXPORT_FAILED; return -1;
    }
    char tmp[440], parent[440];
    int n = snprintf(tmp, sizeof(tmp), "%s.tmp.XXXXXX", path);
    if (n < 0 || (size_t)n >= sizeof(tmp)) {
        u->notice = FR_UI_EXPORT_FAILED; return -1;
    }
    strcpy(parent, path);
    char *slash = strrchr(parent, '/');
    if (!slash) { u->notice = FR_UI_EXPORT_FAILED; return -1; }
    if (slash == parent) slash[1] = '\0'; else *slash = '\0';
    int directory = open(parent, O_RDONLY | O_DIRECTORY);
    if (directory < 0) { u->notice = FR_UI_EXPORT_FAILED; return -1; }
    int fd = mkstemp(tmp);
    if (fd < 0) { close(directory); u->notice = FR_UI_EXPORT_FAILED; return -1; }
    FILE *file = fdopen(fd, "w");
    if (!file) {
        close(fd); unlink(tmp); close(directory);
        u->notice = FR_UI_EXPORT_FAILED; return -1;
    }
    int ok = fprintf(file, "# source=%s\n", u->synthetic ?
                     "OFFLINE_SYNTHETIC_NOT_BOARD_DATA" : "RAW_CAPTURE_UNVERIFIED") > 0;
    if (ok && u->capture_meta.known) {
        ok = fprintf(file, "# app_capture_metadata_v1 requested_digital_amplitude_pct=%.17g\n"
                           "# amplitude_units=DIGITAL_PERCENT_NOT_VOLTS\n"
                           "# dac_off_command=%s ps_cleanup=%s\n",
                     u->capture_meta.requested_amplitude_pct,
                     u->capture_meta.output_off_confirmed == 1 ? "CONFIRMED" : "NOT_CONFIRMED",
                     u->capture_meta.cleanup_confirmed == 1 ? "CONFIRMED" : "NOT_CONFIRMED") > 0;
    } else if (ok) {
        ok = fprintf(file, "# app_capture_metadata_v1=UNKNOWN; "
                           "requested_digital_amplitude_pct=UNKNOWN; "
                           "dac_off_command=UNKNOWN; ps_cleanup=UNKNOWN\n") > 0;
    }
    if (ok) ok = fr_raw_write_csv(file, &u->capture) == 0;
    if (ok) ok = fflush(file) == 0;
    if (ok) ok = fsync(fileno(file)) == 0;
    if (fclose(file) != 0) ok = 0;
    if (ok) ok = rename(tmp, path) == 0;
    if (ok) ok = fsync(directory) == 0;
    if (!ok) unlink(tmp); /* Own unique temporary file only. */
    close(directory);
    u->notice = ok ? FR_UI_EXPORT_OK : FR_UI_EXPORT_FAILED;
    return ok ? 0 : -1;
}
static double x_fraction(const struct fr_ui *u, unsigned index) {
    const struct fr_raw_config *c = plot_config(u);
    if (c->points <= 1) return 0.5;
    double start = c->frequency_hz[0], end = c->frequency_hz[c->points - 1];
    if (end <= start || start <= 0) return 0;
    double f = c->frequency_hz[index];
    return u->logarithmic ? log(f / start) / log(end / start) : (f - start) / (end - start);
}
int fr_ui_touch(struct fr_ui *u, int x, int y, int w, int h, const char *csv_path) {
    if (!u || w != 800 || h != 480 || u->capture.config.points > 64 ||
        u->pending.points > 64) return 0;
    if (x >= 20 && x < w - 20 && y >= 382 && y < 421) {
        int step = (w - 40) / 5;
        if ((x - 20) % step >= step - 6) return 0;
        int key = (x - 20) / step;
        if (key == 0) {
            /* Intentionally no fr_raw_start or DAC/GPIO calls. */
            u->notice = FR_UI_WAIT_HARDWARE;
        } else if (key == 1) {
            if (u->capture.active) fr_raw_abort(&u->capture);
            u->notice = FR_UI_STOPPED;
        } else if (key == 2) {
            memset(&u->capture, 0, sizeof(u->capture));
            memset(&u->capture_meta, 0, sizeof(u->capture_meta));
            u->selected = 0; u->synthetic = 0;
            u->notice = FR_UI_CLEARED;
        } else if (key == 3) {
            (void)fr_ui_export(u, csv_path);
        } else if (key == 4) u->help = !u->help;
        return 1;
    }
    unsigned n = points(u);
    if (!u->help && n > 0 && x >= 80 && x <= 775 && y >= 185 && y <= 315) {
        double requested = (double)(x - 80) / 695.0, best = 2;
        unsigned closest = 0;
        for (unsigned i = 0; i < n; ++i) {
            double d = fabs(x_fraction(u, i) - requested);
            if (d < best) { best = d; closest = i; }
        }
        u->selected = closest;
        return 1;
    }
    return 0;
}
static const char *notice(const struct fr_ui *u) {
    switch (u->notice) {
    case FR_UI_WAIT_HARDWARE: return u->application_admitted ?
        "MANUAL READY - CONNECT FORWARD / TERMINATE REVERSE" :
        "HARDWARE NOT VERIFIED - START DISABLED";
    case FR_UI_STOPPED: return "STOPPED - RAW TRACES RETAINED";
    case FR_UI_CLEARED: return "RAW REFLECT CLEARED - OTHER PAGES UNCHANGED";
    case FR_UI_EXPORT_OK: return "CSV SAVED - RAW / UNCALIBRATED ONLY";
    case FR_UI_EXPORT_FAILED: return "CSV SAVE FAILED - CHECK ISOLATED STORAGE";
    case FR_UI_NO_DATA: return "NO RAW DATA TO EXPORT";
    case FR_UI_BAD_CONFIG: return "INVALID RANGE / ROUNDED FREQUENCY POINTS";
    case FR_UI_RESOURCE_BUSY: return "CAPTURE OR OTHER TEST BUSY - RETRY WHEN IDLE";
    case FR_UI_PREPARING: return "SETTING FREQUENCY - WAITING FOR CONFIRMATION";
    case FR_UI_ACQUIRING: return "ACQUIRING RAW FWD / REV - UNCALIBRATED";
    case FR_UI_STOPPING: return "STOPPING - WAITING FOR OUTPUT OFF / ADC ACK";
    case FR_UI_COMPLETE: return "RAW CAPTURE COMPLETE - OUTPUT OFF CONFIRMED";
    case FR_UI_CAPTURE_FAILED: return "CAPTURE FAILED - CHECK ERROR / CLEANUP STATUS";
    case FR_UI_MANUAL_WAIT_REV: return "RECONNECT COUPLED REVERSE TO ADC - PRESS START";
    case FR_UI_MANUAL_CAPTURE_REV: return "CAPTURING MANUAL REVERSE - UNCALIBRATED";
    default: return "UNAVAILABLE";
    }
}
static void box(struct fr_ui_paint p, int x, int y, int width, int height,
                 const char *label, uint32_t color) {
    p.line(p.ctx, x, y, x + width - 1, y, color);
    p.line(p.ctx, x, y + height - 1, x + width - 1, y + height - 1, color);
    p.line(p.ctx, x, y, x, y + height - 1, color);
    p.line(p.ctx, x + width - 1, y, x + width - 1, y + height - 1, color);
    p.text(p.ctx, x + (width - (int)strlen(label) * 12) / 2, y + 12, label, 2, color);
}
void fr_ui_render(const struct fr_ui *u, struct fr_ui_paint p, int w, int h) {
    if (!u || !p.line || !p.text || w != 800 || h != 480 ||
        u->capture.config.points > 64 || u->pending.points > 64) return;
    const uint32_t ink = 0xe4edf5, grid = 0x283b49, fwd = 0x00e0aa, rev = 0xffcf50;
    char b[160];
    p.text(p.ctx, 25, 105, "REFLECT RAW FWD / REV", 2, ink);
    p.text(p.ctx, 25, 127, u->synthetic ? "OFFLINE SYNTHETIC - NOT BOARD MEASUREMENT" :
           notice(u), 1, 0xffb75e);
    const struct fr_raw_config *c = plot_config(u);
    unsigned n = points(u);
    if (n > 64) return;
    if (n) snprintf(b, sizeof(b), "%.3f - %.3f MHZ  %u POINTS  %s  AVG %u",
                    c->frequency_hz[0] / 1e6, c->frequency_hz[n - 1] / 1e6,
                    n, u->logarithmic ? "LOG" : "LINEAR", c->frames_per_path);
    else snprintf(b, sizeof(b), "SET RANGE / POINTS / AVERAGE IN SWEEP OPTIONS");
    p.text(p.ctx, 25, 142, b, 1, ink);
    p.text(p.ctx, 25, 160, "FWD", 1, fwd);
    p.text(p.ctx, 105, 160, "REV", 1, rev);
    snprintf(b, sizeof(b), "PROGRESS %u/%u  PATH %s  ERROR %s", u->capture.point, n,
             u->capture.path == FR_RAW_REV ? "REV" : "FWD",
             fr_raw_error_name(u->capture.error));
    p.text(p.ctx, 220, 160, b, 1, ink);
    if (u->help) {
        p.text(p.ctx, 80, 205, "KEEP OUTPUT LOAD AND IDLE COUPLED PORT AT 50 OHM", 1, ink);
        p.text(p.ctx, 80, 235, "VERIFY RF SWITCH VOLTAGE AND UNUSED-PORT MATCHING", 1, ink);
        p.text(p.ctx, 80, 265, "RAW CODE CURVES ARE NOT CALIBRATED S11 / VSWR / DTF", 1, rev);
        p.text(p.ctx, 80, 295, "PRESS HELP AGAIN TO RETURN - NAVIGATION REMAINS OPEN", 1, ink);
    } else {
        double maximum = 1.0;
        for (unsigned i = 0; i < u->capture.config.points && i < 64; ++i) {
            const struct fr_raw_point *q = &u->capture.trace[i];
            if (q->fwd_frames && isfinite(q->fwd_rms) && q->fwd_rms > maximum) maximum = q->fwd_rms;
            if (q->rev_frames && isfinite(q->rev_rms) && q->rev_rms > maximum) maximum = q->rev_rms;
        }
        maximum *= 1.1;
        for (int i = 0; i <= 5; ++i) {
            p.line(p.ctx, 80, 185 + i * 26, 775, 185 + i * 26, grid);
            p.line(p.ctx, 80 + i * 139, 185, 80 + i * 139, 315, grid);
        }
        snprintf(b, sizeof(b), "%.1f", maximum);
        p.text(p.ctx, 25, 185, b, 1, ink);
        p.text(p.ctx, 25, 205, "CODE", 1, ink);
        p.text(p.ctx, 50, 307, "0", 1, ink);
        for (unsigned path = 0; path < 2; ++path) {
            int previous = 0, px = 0, py = 0;
            for (unsigned i = 0; i < u->capture.config.points && i < 64; ++i) {
                const struct fr_raw_point *q = &u->capture.trace[i];
                double value = path ? q->rev_rms : q->fwd_rms;
                unsigned count = path ? q->rev_frames : q->fwd_frames;
                if (!count || !isfinite(value) || value < 0 || value > FR_RAW_MAX_RMS_CODE) {
                    previous = 0; continue; /* Do not bridge missing points. */
                }
                int x = 80 + (int)lround(x_fraction(u, i) * 695);
                int y = 315 - (int)lround(value / maximum * 130);
                uint32_t color = path ? rev : fwd;
                if (previous) p.line(p.ctx, px, py, x, y, color);
                p.line(p.ctx, x - 1 < 80 ? 80 : x - 1, y,
                       x + 1 > 775 ? 775 : x + 1, y, color);
                previous = 1; px = x; py = y;
            }
        }
        if (!has_data(u)) p.text(p.ctx, 255, 243, "NO RAW DATA - HARDWARE NOT READY", 1, 0xffb75e);
        if (n) {
            snprintf(b, sizeof(b), "%.3f MHZ", c->frequency_hz[0] / 1e6);
            p.text(p.ctx, 80, 323, b, 1, ink);
            if (n > 1) {
                snprintf(b, sizeof(b), "%.3f MHZ", c->frequency_hz[n - 1] / 1e6);
                p.text(p.ctx, 700, 323, b, 1, ink);
            }
        }
    }
    unsigned selected = u->selected < n ? u->selected : 0;
    if (n && u->capture.config.points) {
        const struct fr_raw_point *q = &u->capture.trace[selected];
        char fv[20] = "--", rv[20] = "--";
        if (q->fwd_frames && isfinite(q->fwd_rms)) snprintf(fv, sizeof(fv), "%.1f", q->fwd_rms);
        if (q->rev_frames && isfinite(q->rev_rms)) snprintf(rv, sizeof(rv), "%.1f", q->rev_rms);
        snprintf(b, sizeof(b), "P%u %.3f MHZ  FWD %s  REV %s  %s", selected + 1,
                 q->frequency_hz / 1e6, fv, rv, fr_raw_quality_name(q));
    } else snprintf(b, sizeof(b), "TAP PLOT FOR POINT READOUT - NO CAPTURE YET");
    p.text(p.ctx, 25, 344, b, 1, ink);
    p.text(p.ctx, 25, 361, u->synthetic ? notice(u) :
           "UNCALIBRATED ADC CODE - NO RL / VSWR / DTF", 1, 0xffb75e);
    const char *keys[] = {u->manual_mode && u->notice == FR_UI_MANUAL_WAIT_REV ?
                              "CAPTURE REV" : "START", "STOP", "CLEAR", "EXPORT CSV", "HELP"};
    int step = (w - 40) / 5;
    for (int i = 0; i < 5; ++i) box(p, 20 + i * step, 382, step - 6, 39,
                                   keys[i], i == 0 ? (u->application_admitted ? fwd : grid) : ink);
}
