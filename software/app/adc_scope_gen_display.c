#define _POSIX_C_SOURCE 200809L
/* This is an isolated, undeployed candidate, not the production source. */
#ifndef DIAG_STORAGE_DIR
#error "REFLECT candidate requires an explicit isolated DIAG_STORAGE_DIR"
#endif
#ifndef SCOPE_VIEW_PATH
#error "REFLECT candidate requires an explicit isolated SCOPE_VIEW_PATH"
#endif
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "diag_ui_model.h"
#include "reflect_raw_ui.h"
#include "fr_ps_adapter.h"
#include "fr_backend_context.h"
#include "fr_platform_files.h"
#include "fr_vector_ui.h"
#include "fr_writer_probe.h"
#include "fr_manual_session.h"
#include "health_fingerprint.h"
#include "monitor_event_health.h"
#include "rf_metrics.h"
#include "signal_trends.h"
#include "measurement_quality.h"
#include "measurement_quality_report.h"
#include "spectrum_runtime616.h"
#include "monitor_fast686.h"
#include "monitor_profile690.h"
#include "framebuffer_pack691.h"
static int packed_flush691;
#include "lowfreq681.h"
#include "scope_precision682.h"
#include "scope_stats683.h"
#include "scope_quality685.h"
#include "custom_ui_model.h"
#include "custom_wave/package/custom_wavegen_package.h"
#include "custom_ui_backend.h"
/* CUSTOM is a software-only, isolated preview.  No action in this panel may
 * call the DAC, RF GPIO or MMIO backend before real hardware admission.  The
 * backend object is nevertheless wired at this boundary so the transaction
 * order is exercised by offline tests before any deployment decision. */
static struct cui_model custom_ui;
static int custom_ui_initialized;
static struct custom_ui_backend custom_backend;
static int custom_panel;
static int custom_edit_field; /* 1=expression, 2=integer Hz, 3=amp, 4=offset */
static char custom_edit_buf[CUI_MAX_EXPR + 1u];
static int custom_package_ready;
static int custom_apply_refused;
static char custom_status[112]="PREVIEW ONLY - ORIGINAL GEN OUTPUT UNCHANGED";
static const char *const custom_formula_keys[5][6]={
    {"sin(","cos(","abs(","min(","max(","clamp("},
    {"t","pi","(",")",",","BKSP"},
    {"7","8","9","/","+","CLR"},
    {"4","5","6","*","-","CANCEL"},
    {"1","2","3",".","0","OK"}
};
static const char *const custom_number_keys[4][4]={
    {"1","2","3","BKSP"},{"4","5","6","CLR"},
    {"7","8","9","-"},{"0","OK","CANCEL",""}
};
static struct rf_metrics_result rf_metrics_cache;
static int rf_metrics_cache_valid;
static struct signal_trend_result signal_trend_cache;
static struct measurement_quality_result measurement_quality_cache;
static int measurement_quality_cache_valid;
/* Only a freshly evaluated frame may populate this cache.  In particular,
 * an earlier PASS must not leak into a report after its reference expires. */
static struct measurement_quality_config measurement_quality_cfg;
static struct measurement_quality_input measurement_quality_last_input;
static uint64_t measurement_quality_last_timestamp_ms;
#ifndef FR_PLATFORM_FILES_OPEN
#define FR_PLATFORM_FILES_OPEN fr_platform_files_open
#endif
#ifndef FR_PLATFORM_FILES_CHECK
#define FR_PLATFORM_FILES_CHECK fr_platform_files_check
#endif
#ifndef FR_PLATFORM_WRITER_PROBE
#define FR_PLATFORM_WRITER_PROBE fr_writer_probe
#endif
#ifndef FR_PLATFORM_RF_OPEN
#define FR_PLATFORM_RF_OPEN rf_emio_open
#endif
#ifndef FR_PLATFORM_RF_SELECT
#define FR_PLATFORM_RF_SELECT fr_backend_emio_select
#endif
#ifndef FR_PLATFORM_RF_RELEASE
#define FR_PLATFORM_RF_RELEASE fr_backend_emio_release
#endif

#define N 2048
#define ADC_SAMPLE_RATE_HZ 400000000.0
/* FFT bin spacing is Fs/N.  The displayed FFT uses a Hann window; its
 * equivalent noise bandwidth is computed for the finite symmetric window. Keep both quantities explicit so
 * the UI does not call bin spacing an RBW. */
#define FFT_BIN_HZ (ADC_SAMPLE_RATE_HZ/(double)N)
#define FFT_HANN_ENBW_BINS (N*(3.0*(N-1)/8.0)/(((N-1)/2.0)*((N-1)/2.0)))
#define FFT_HANN_ENBW_HZ (FFT_HANN_ENBW_BINS*FFT_BIN_HZ)
#define REQ 2048
#define DONE 2049
#define MAGIC 2050
#define COUNT 2051
#ifndef SCOPE_VIEW_PATH
#define SCOPE_VIEW_PATH "/run/adc_scope.view"
#endif
static volatile sig_atomic_t stopped;
static volatile sig_atomic_t paused,single_pending;
static volatile sig_atomic_t control_generation;
/* A persistent readout reset epoch survives consumption of SINGLE/AUTO
 * between the main-loop context check and the next render. */
static volatile sig_atomic_t scope_stats_invalidation683;
struct trigger_config { int mode,level,falling,pre; };
static struct trigger_config trigger={2,8192,0,512};
static unsigned trigger_revision;
static int capture_waiting;
static unsigned last_flags,last_trigger_index;
static int last_trigger_level=8192,tool_page,pan_samples,cursor_enabled,cursor_selected;
static int last_trigger_falling682;
static int last_frame_raw_owned682;
static int cursor_index[2]={512,552};
static int fine_drag_x=-1;
static int view_drag,cursor_auto,preview_level=8192,preview_pre=512,trigger_drag;
static int fine_pretrigger;
static int power_dialog,shutdown_requested;
#ifdef MONITOR_ASYNC694
static int mask694_selected,mask694_exit_hold,mask694_exit_retry,mask694_power_after_drain;
#endif
static volatile sig_atomic_t autoset_pending;
/* Diagnostic: keep triggering captures but leave the panel showing a static
 * black field, so LCD data-line coupling can be told apart from the rest. */
static int static_screen;
/* Generator settings are requests, not calibrated analog volts or MMIO readback. */
/* Generator frequency is kept in kHz so the UI and dacctl command preserve
 * 0.001 MHz (1 kHz) resolution end to end. */
static int gen_freq_khz=1000,gen_amp_pct=20,gen_wave,gen_verified=-1;
static int gen_active_freq_khz,gen_active_amp_pct,gen_active_wave;
static int gen_applied; /* 0 unknown, 1 active, 2 digitally off, 3 command error */
static int gen_pending=1;
/* GEN numeric editor: field 1=frequency in MHz, field 2=digital amplitude %. */
static int gen_edit_field;
static char gen_edit_buf[24];
static struct diag_ui diag_ui_state;
static struct lf681_ring lf_history681;
static int lf_scope_active681(void) {
    return diag_ui_state.mode==DIAG_UI_SCOPE && tool_page==5;
}
static int lf_capture_mode681(int mode,int single,int raw_owner) {
    return lf_scope_active681() && !single && !raw_owner ? 0 : mode;
}
static struct diag_monitor diag_monitor_state;
static struct diag_monitor_sample diag_current_sample;
static struct diag_monitor_event diag_last_event;
static int diag_have_sample,diag_have_event;
static int diag_csv_fd=-1;
#define DIAG_EVENT_HISTORY 8
static struct diag_monitor_event diag_event_history[DIAG_EVENT_HISTORY];
static unsigned diag_event_history_count;
static unsigned diag_selected_event_index;
/* Number of completed events in the current user-defined test session.  This
 * is intentionally separate from event_sequence, which is a persistent,
 * monotonically increasing evidence identifier. */
static uint32_t diag_session_event_count;
static int diag_baseline_learning;
static uint64_t diag_baseline_started_ms;
static uint64_t diag_baseline_completed_ms;
static double diag_new_test_last_press=-1.0;
static double seconds(void);
static void measurement_quality_refresh_age(void);
static void measurement_quality_mark_invalid(const char *reason);
static uint32_t diag_replay_frame[N];
static int diag_replay_valid;
enum diag_replay_phase { REPLAY_PRE,REPLAY_ONSET,REPLAY_WORST,REPLAY_RECOVERY,REPLAY_LIVE };
static enum diag_replay_phase diag_replay_phase=REPLAY_WORST;
static uint64_t diag_replay_timestamp;
static unsigned diag_replay_index;
static int diag_replay_legacy;
static int diag_replay_unavailable;
#ifndef DIAG_STORAGE_DIR
#define DIAG_STORAGE_DIR "/home/root/diag_monitor"
#endif
#define DIAG_RING_FRAMES 32
static uint32_t diag_frame_ring[DIAG_RING_FRAMES][N];
static unsigned diag_ring_next,diag_ring_count;
static int diag_freeze_ready;
static unsigned diag_freeze_sequence;
static unsigned diag_freeze_frames;
static int diag_storage_error;
/* Sparse evidence: up to 16 pre-onset frames, onset, worst, then the
 * first 8 recovery frames. It is not a contiguous high-speed recording. */
struct diag_evidence_header {
    uint32_t magic,sequence,frames,samples,selected,pre,post,reserved;
    uint64_t system_wall_start_ms;
    uint64_t timestamps[DIAG_RING_FRAMES];
};
static struct diag_evidence_header diag_evidence;
static uint32_t diag_evidence_frames[DIAG_RING_FRAMES][N];
static uint64_t diag_ring_times[DIAG_RING_FRAMES];
static int diag_evidence_active;
static double diag_evidence_severity;
static void diag_event_date(uint32_t sequence,char *date,size_t size);
static int diag_archive_manual_event(void);
#define SWEEP_MAX_POINTS 64
#define SWEEP_MIN_POINTS 8
#define SWEEP_MAX_MHZ 180.0
#define SWEEP_CONFIG_PATH DIAG_STORAGE_DIR "/sweep_config.csv"
#define SWEEP_REFERENCE_PATH DIAG_STORAGE_DIR "/sweep_reference.csv"
#define SWEEP_FLAT_PATH DIAG_STORAGE_DIR "/sweep_flatness.csv"
#define SWEEP_RESULT_PATH DIAG_STORAGE_DIR "/sweep_latest.csv"
#define SWEEP_SCALAR_PATH DIAG_STORAGE_DIR "/sweep_scalar.csv"
#ifndef REFLECT_STORAGE_DIR
#define REFLECT_STORAGE_DIR DIAG_STORAGE_DIR
#endif
#define REFLECT_CAL_PATH REFLECT_STORAGE_DIR "/reflect_scalar_cal.csv"
#define REFLECT_RESULT_PATH REFLECT_STORAGE_DIR "/reflect_scalar_latest.csv"
#define REFLECT_LOW_SIGNAL_RMS 8.0
#define MONITOR_CONFIG_PATH DIAG_STORAGE_DIR "/monitor_config.csv"
#define REPORT_RESULT_PATH DIAG_STORAGE_DIR "/diagnostic_report.csv"
#define SWEEP_PASS_THRESHOLD_DB (-3.0)
struct sweep_state {
    int active;
    int completed;
    int error;
    int mode; /* 0=measurement, 1=reference calibration, 2=source flatness */
    int calibrated;
    unsigned point;
    unsigned settle_frames;
    unsigned accum_frames;
    unsigned reject_frames;
    unsigned retry_count;
    unsigned points;
    unsigned average_frames;
    unsigned max_retries;
    int logarithmic;
    double accum_rms;
    double accum_vpp;
    double accum_clip_max;
    double start_mhz;
    double stop_mhz;
    double pass_threshold_db;
    double active_drive_pct;
    double freq_mhz[SWEEP_MAX_POINTS];
    double rms_code[SWEEP_MAX_POINTS];
    double vpp_code[SWEEP_MAX_POINTS];
    double reference_rms[SWEEP_MAX_POINTS];
    double relative_db[SWEEP_MAX_POINTS];
    unsigned pass_points;
    unsigned valid_points;
    unsigned invalid_points;
    unsigned reference_points;
};
static struct sweep_state sweep;
struct sweep_flatness_state {
    int calibrated;
    unsigned points;
    double target_rms;
    double freq_mhz[SWEEP_MAX_POINTS];
    double raw_rms[SWEEP_MAX_POINTS];
    double drive_pct[SWEEP_MAX_POINTS];
};
static struct sweep_flatness_state sweep_flatness;
static int sweep_options_panel;
static unsigned sweep_cursor_point[2];
static unsigned sweep_cursor_selected;
enum reflect_mode { REFLECT_MEASURE=0, REFLECT_CAL_LOAD=1, REFLECT_CAL_OPEN=2, REFLECT_CAL_SHORT=3 };
struct reflect_state {
    int active,completed,error,mode;
    int off_unconfirmed;
    int have_load,have_open,have_short;
    unsigned point,settle_frames,accum_frames,reject_frames,retry_count;
    unsigned valid_points,invalid_points;
    double accum_rms,accum_vpp;
    double freq_mhz[SWEEP_MAX_POINTS];
    double load_rms[SWEEP_MAX_POINTS];
    double open_rms[SWEEP_MAX_POINTS];
    double short_rms[SWEEP_MAX_POINTS];
    double measured_rms[SWEEP_MAX_POINTS];
    double gamma[SWEEP_MAX_POINTS];
    unsigned low_signal_points;
};
static struct reflect_state reflect;
/* Isolated PS candidate only. Hardware acquisition stays disabled until
 * physical RF matching, pin limits and candidate PL have been verified. */
/* The current board prototype exposes the scalar relative-trend page.  The
 * raw FWD/REV backend remains compiled and safety-gated for later hardware
 * integration, but must not mask the user-visible trend prototype. */
#ifndef REFLECT_RAW_PAGE
#define REFLECT_RAW_PAGE 0
#endif
static const int reflect_raw_candidate_enabled=REFLECT_RAW_PAGE;
static struct fr_ui reflect_raw_ui;
static int reflect_raw_ui_initialized;
static struct fr_vector_ui reflect_vector_ui;
static int reflect_vector_view;
static struct fr_ps_adapter reflect_ps_adapter;
static struct fr_manual_session reflect_manual_session;
static int reflect_manual_mode;
static int reflect_manual_only;
static struct fr_raw_point reflect_manual_forward_trace[FR_MANUAL_MAX_POINTS];
static int reflect_manual_phase; /* 0=off, 1=FWD run, 2=wait reconnect, 3=REV run */
static int reflect_manual_export(const char *path) {
    if (!path || !reflect_manual_session.points ||
        reflect_manual_session.state != FR_MANUAL_COMPLETE) return -1;
    for(unsigned i=0;i<reflect_manual_session.points;i++)
        if(reflect_raw_ui.capture.trace[i].quality & FR_RAW_CLIPPED) return -1;
    char tmp[420], parent[420];
    if (snprintf(tmp,sizeof(tmp),"%s.manual.tmp.%ld",path,(long)getpid())<0 ||
        strlen(tmp)>=sizeof(tmp) || snprintf(parent,sizeof(parent),"%s",path)<0) return -1;
    char *slash=strrchr(parent,'/'); if(!slash)return -1;
    if(slash==parent)slash[1]='\0'; else *slash='\0';
    int dir=open(parent,O_RDONLY|O_DIRECTORY|O_CLOEXEC); if(dir<0)return -1;
    FILE *f=fopen(tmp,"w"); if(!f){close(dir);return -1;}
    int ok=fr_manual_session_write_csv(f,&reflect_manual_session)==0 &&
           fflush(f)==0 && fsync(fileno(f))==0;
    if(ok && fprintf(f,"# quality_bits=1:LOW_FWD,2:LOW_REV,4:CLIPPED; sequence=monotonic_capture_sequence_not_hardware_token\n")<0)ok=0;
    for(unsigned i=0;ok && i<reflect_manual_session.points;i++) {
        const struct fr_raw_point *q=&reflect_raw_ui.capture.trace[i];
        if(fprintf(f,"# point_%u quality=%u fwd_sequence=%llu:%llu rev_sequence=%llu:%llu\n",
                   i+1,q->quality,(unsigned long long)q->fwd_first_seq,
                   (unsigned long long)q->fwd_last_seq,(unsigned long long)q->rev_first_seq,
                   (unsigned long long)q->rev_last_seq)<0)ok=0;
    }
    if(ok && (fflush(f)!=0 || fsync(fileno(f))!=0))ok=0;
    if(fclose(f)!=0)ok=0;
    if(!ok){unlink(tmp);close(dir);return -1;}
    if(rename(tmp,path)<0 || fsync(dir)<0){unlink(tmp);close(dir);return -1;}
    close(dir); return 0;
}
static int reflect_manual_prepare_session(void) {
    struct fr_raw_config *c=&reflect_raw_ui.pending;
    if(!c->points || c->points>FR_MANUAL_MAX_POINTS)return -1;
    double f[FR_MANUAL_MAX_POINTS];
    for(unsigned i=0;i<c->points;i++)f[i]=(double)c->frequency_hz[i]/1e6;
    return fr_manual_session_begin(&reflect_manual_session,f,c->points);
}
/* Platform admission is deliberately unbound. No command-line/UI setting
 * supplies these facts. Offline tests inject RAM/fake-exec resources; a future
 * platform setup must establish every fact and keep it valid for the session. */
static struct fr_backend_context reflect_backend;
static struct fr_backend_resources reflect_resources;
static struct fr_platform_files reflect_files;
static int reflect_backend_permit;
/* Only the isolated candidate accepts an explicit hardware-verification
 * attestation. It is NOT PL detection, phase verification or S11 permission. */
static int reflect_frontend_confirmed,reflect_admission_requested;
static int reflect_platform_adc_owned,reflect_platform_reader_fd=-1;
static volatile uint32_t *reflect_platform_mailbox;
static struct rf_emio_gpio reflect_platform_rf={.handle_fd=-1};
static struct fr_writer_watch reflect_platform_watch;
static uint64_t reflect_platform_next_probe;
static uint32_t reflect_platform_generation,reflect_platform_revision;
static void reflect_platform_tick(int pending);
static void reflect_platform_abort(void);
static double reflect_backend_amplitude=10.0;
static int reflect_clear_requested;
/* Ownership includes unconfirmed cleanup, not merely an active curve. A
 * successful finished session keeps owned=1 for audit, so owned alone must
 * not permanently block ordinary GEN/SWEEP. */
static int reflect_ps_exclusive(void) {
    return reflect_admission_requested || reflect_raw_ui.capture.active || reflect_ps_adapter.pending ||
           (reflect_ps_adapter.owned && !fr_ps_clean(&reflect_ps_adapter));
}
static int reflect_capture_plan(int *single,int *mode) {
    if(reflect_admission_requested)return 0; /* Drain SCOPE; never relabel it. */
    if(reflect_ps_exclusive()) {
        if(!reflect_ps_adapter.owned || reflect_ps_adapter.stopping ||
           reflect_ps_adapter.blocked || !reflect_raw_ui.capture.active ||
           reflect_raw_ui.capture.frequency_pending) return 0;
        *single=0; *mode=2; /* AUTO: fresh frames, not NORMAL's held waveform. */
        return 1;
    }
    *single=single_pending!=0; *mode=trigger.mode;
    return !paused || single_pending;
}
static void reflect_raw_prepare_ui(void);
#define REFLECT_RAW_RESULT_PATH DIAG_STORAGE_DIR "/reflect_raw_latest.csv"
static int gen_control_exec(int on,int freq_khz,int wave,int amp_pct);
#ifndef SWEEP_CONTROL_EXEC
static int sweep_gen_control_exec(int on,int freq_khz,double amp_pct);
#define SWEEP_CONTROL_EXEC sweep_gen_control_exec
#define SWEEP_CONTROL_EXEC_DEFAULT 1
#endif
static int sweep_save_config(void);

static void diag_apply_sensitivity(struct diag_monitor_config *c, unsigned level) {
    /* Level 0 is conservative; level 2 is intended for small, repeatable
     * contact disturbances.  The baseline and confirmation counts remain
     * explicit so a more sensitive setting does not become a noisy trigger. */
    if(!c) return;
    if(level==0) { c->drop_enter_ratio=0.65; c->drop_exit_ratio=0.82; c->freq_tolerance_ratio=0.20; c->enter_confirm_frames=3; c->exit_confirm_frames=4; }
    else if(level==1) { c->drop_enter_ratio=0.78; c->drop_exit_ratio=0.90; c->freq_tolerance_ratio=0.12; c->enter_confirm_frames=2; c->exit_confirm_frames=3; }
    else { c->drop_enter_ratio=0.88; c->drop_exit_ratio=0.95; c->freq_tolerance_ratio=0.08; c->enter_confirm_frames=2; c->exit_confirm_frames=2; }
}
static unsigned diag_sensitivity_level(const struct diag_monitor_config *c) {
    if(!c) return 0;
    if(c->drop_enter_ratio>=0.86) return 2;
    if(c->drop_enter_ratio>=0.72) return 1;
    return 0;
}
#define MONITOR_HOLD_MIN_MS 10u
#define MONITOR_HOLD_MAX_MS 5000u
static uint64_t diag_next_recovery_hold_ms(uint64_t current) {
    static const uint64_t presets[]={10,20,50,100,250,500,750,1000,1500,2000,3000,5000};
    for(unsigned i=0;i<sizeof(presets)/sizeof(presets[0]);i++)
        if(current<presets[i]) return presets[i];
    return MONITOR_HOLD_MIN_MS;
}
/* Directory fsync is required after rename for durable metadata. Failure may
 * occur AFTER the destination was replaced, so it is not a rollback promise. */
static int sync_parent_directory(const char *path) {
    char parent[PATH_MAX];
    int n=snprintf(parent,sizeof(parent),"%s",path);
    if(n<0 || (size_t)n>=sizeof(parent)) return -1;
    char *slash=strrchr(parent,'/');
    if(!slash) return -1;
    if(slash==parent) slash[1]=0; else *slash=0;
    int fd=open(parent,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(fd<0) return -1;
    int rc=fsync(fd); if(close(fd)<0) rc=-1;
    return rc;
}
static int diag_sync_directory(void) { return sync_parent_directory(MONITOR_CONFIG_PATH); }
static void diag_load_monitor_config(struct diag_monitor_config *c) {
    if(!c) return;
    FILE *f=fopen(MONITOR_CONFIG_PATH,"r");
    if(!f) return;
    struct diag_monitor_config next=*c;
    char line[120],key[64],value[64]; unsigned level=diag_sensitivity_level(c); int valid=1;
    while(fgets(line,sizeof(line),f)) {
        char *p=line; while(*p==' '||*p=='\t') p++;
        if(*p=='#'||*p=='\n'||*p=='\r'||!*p) continue;
        if(sscanf(p,"%63[^=]=%63s",key,value)!=2) { valid=0; break; }
        if(!strcmp(key,"sensitivity")) { char *e=NULL; errno=0; unsigned long v=strtoul(value,&e,10); if(errno||e==value||*e||v>2) valid=0; else level=(unsigned)v; }
        else if(!strcmp(key,"recovery_hold_ms")) {
            char *e=NULL; errno=0; unsigned long v=strtoul(value,&e,10);
            if(errno||e==value||*e||v<MONITOR_HOLD_MIN_MS||v>MONITOR_HOLD_MAX_MS) valid=0; else next.recovery_hold_ms=(uint64_t)v;
        }
        else { valid=0; break; }
    }
    if(ferror(f)) valid=0;
    if(fclose(f)!=0) valid=0;
    if(valid) { diag_apply_sensitivity(&next,level); *c=next; printf("MONITOR_CONFIG_LOADED SENSITIVITY=%u RECOVERY_HOLD_MS=%llu PATH=%s\n",
        level,(unsigned long long)c->recovery_hold_ms,MONITOR_CONFIG_PATH); }
}
static int diag_save_monitor_config(const struct diag_monitor_config *c) {
    char tmp[PATH_MAX]; snprintf(tmp,sizeof(tmp),"%s.tmp.%ld",MONITOR_CONFIG_PATH,(long)getpid());
    FILE *f=fopen(tmp,"w"); if(!f) return -1;
    int ok=fprintf(f,"# diag monitor config v2\nsensitivity=%u\nrecovery_hold_ms=%llu\n",
                   diag_sensitivity_level(c),(unsigned long long)c->recovery_hold_ms)>0;
    if(fflush(f)!=0 || fsync(fileno(f))<0) ok=0;
    if(fclose(f)!=0) ok=0;
    f=NULL;
    if(ok) {
        if(rename(tmp,MONITOR_CONFIG_PATH)==0) return diag_sync_directory();
    }
    if(f) fclose(f);
    unlink(tmp);
    return -1;
}
static void diag_reset_monitor_baseline(struct diag_monitor_config c) {
    if(diag_monitor_state.state==DIAG_MONITOR_FAULT) return;
    /* Re-arm the live detector without erasing evidence already captured.
     * Event numbering must remain monotonic across a manual reset or a
     * sensitivity change; history, replay and CSV files are intentionally
     * left untouched. */
    uint32_t sequence=diag_monitor_state.event_sequence;
    diag_monitor_init(&diag_monitor_state,c);
    /* Keep the intentional user-facing re-arm label for the next frame;
     * monitor_core still rejects a low-signal frame and moves to NO SIGNAL. */
    diag_monitor_state.state=DIAG_MONITOR_BASELINE;
    diag_monitor_state.event_sequence=sequence;
    diag_have_sample=0;
    diag_baseline_learning=0;
    diag_baseline_started_ms=0;
    measurement_quality_mark_invalid("BASELINE_RESET");
}
static int diag_start_new_test(struct diag_monitor_config c) {
    if(diag_csv_fd<0) return -1;
    /* NEW TEST is an explicit user request, including a changed stimulus.
     * Archive an active fault as interrupted, never as recovered. */
    if(diag_monitor_state.state==DIAG_MONITOR_FAULT && diag_archive_manual_event()<0)
        return -1;
    /* Commit the session boundary before changing the in-memory view.  Old
     * event rows and frozen waveform files remain immutable evidence. */
    if(diag_csv_fd>=0) {
        char marker[96];
        int n=snprintf(marker,sizeof(marker),"# SESSION_START,%lld\n",(long long)time(NULL));
        if(n<=0 || n>=(int)sizeof(marker) || diag_monitor_write_all(diag_csv_fd,marker,(size_t)n)!=n ||
           fsync(diag_csv_fd)<0) return -1;
    }
    uint32_t sequence=diag_monitor_state.event_sequence;
    memset(diag_event_history,0,sizeof(diag_event_history));
    diag_event_history_count=0;
    diag_selected_event_index=0;
    diag_session_event_count=0;
    memset(&diag_last_event,0,sizeof(diag_last_event));
    diag_have_event=0;
    memset(diag_replay_frame,0,sizeof(diag_replay_frame));
    diag_replay_valid=0;
    diag_replay_phase=REPLAY_LIVE;
    diag_replay_unavailable=0; diag_replay_legacy=0;
    diag_replay_timestamp=0; diag_replay_index=0;
    diag_freeze_ready=0;
    diag_freeze_sequence=0;
    diag_freeze_frames=0;
    diag_evidence_active=0;
    memset(&diag_evidence,0,sizeof(diag_evidence));
    diag_ring_next=0;
    diag_ring_count=0;
    memset(diag_frame_ring,0,sizeof(diag_frame_ring));
    diag_monitor_init(&diag_monitor_state,c);
    diag_monitor_state.state=DIAG_MONITOR_BASELINE;
    diag_monitor_state.event_sequence=sequence;
    diag_have_sample=0;
    diag_baseline_learning=1;
    diag_baseline_started_ms=0;
    diag_baseline_completed_ms=0;
    diag_storage_error=0;
    measurement_quality_mark_invalid("NEW_TEST_BASELINE_PENDING");
    return 0;
}
static int diag_update_live_monitor(const struct diag_monitor_sample *sample,
                                    struct diag_monitor_event *completed) {
    if(!diag_baseline_learning)
        return diag_monitor_update(&diag_monitor_state,sample,completed);
    unsigned target=diag_monitor_state.cfg.baseline_frames;
    /* Collect a real two-second baseline instead of merely displaying a
     * delayed NORMAL label.  Keep the configured minimum frame count. */
    diag_monitor_state.cfg.baseline_frames=UINT_MAX;
    int rc=diag_monitor_update(&diag_monitor_state,sample,completed);
    diag_monitor_state.cfg.baseline_frames=target;
    if(rc<0) return rc;
    if(diag_monitor_state.state==DIAG_MONITOR_NO_SIGNAL) {
        diag_baseline_started_ms=0;
        return rc;
    }
    if(!diag_baseline_started_ms) diag_baseline_started_ms=sample->timestamp_ms;
    if(diag_monitor_state.baseline_count>=target &&
       sample->timestamp_ms>=diag_baseline_started_ms &&
       sample->timestamp_ms-diag_baseline_started_ms>=2000) {
        diag_monitor_state.state=DIAG_MONITOR_NORMAL;
        diag_baseline_learning=0;
        diag_baseline_completed_ms=sample->timestamp_ms;
        printf("MONITOR_BASELINE_READY FRAMES=%u RMS=%.3f VPP=%.3f FREQ=%.6f\n",
               diag_monitor_state.baseline_count,diag_monitor_state.baseline_rms,
               diag_monitor_state.baseline_vpp,diag_monitor_state.baseline_freq);
    }
    return rc;
}
/* Only a finite positive signal and a usable reference define a dB ratio.
 * The quiet-baseline branch elsewhere is an absolute-code comparison. */
static const char *sweep_export_quality(unsigned i) {
    if(i>=SWEEP_MAX_POINTS || !isfinite(sweep.freq_mhz[i]) || sweep.freq_mhz[i]<=0 ||
       !isfinite(sweep.rms_code[i]) || sweep.rms_code[i]<0 ||
       !isfinite(sweep.vpp_code[i]) || sweep.vpp_code[i]<0) return "INVALID_DATA";
    if(sweep.rms_code[i]==0) return "LOW_SIGNAL";
    if(sweep.mode!=0) return "CALIBRATION_SAMPLE";
    if(!sweep.calibrated) return "NO_REFERENCE";
    if(!isfinite(sweep.reference_rms[i]) || sweep.reference_rms[i]<0) return "INVALID_REFERENCE";
    if(sweep.reference_rms[i]<=32.0) return "ABSOLUTE_BASELINE_ONLY";
    return isfinite(sweep.relative_db[i]) ? "VALID_SCALAR" : "INVALID_DATA";
}
static enum hf_point_quality sweep_health_quality(unsigned i) {
    const char *q=sweep_export_quality(i);
    if(!strcmp(q,"VALID_SCALAR")) return HF_POINT_VALID;
    if(!strcmp(q,"LOW_SIGNAL")) return HF_POINT_LOW_SIGNAL;
    if(!strcmp(q,"CLIPPED")) return HF_POINT_CLIPPED;
    if(!strcmp(q,"RETRY_EXHAUSTED")) return HF_POINT_RETRY_EXHAUSTED;
    if(!strcmp(q,"STALE")) return HF_POINT_STALE;
    return HF_POINT_CONFIG_MISMATCH;
}
static int sweep_health_summary(struct hf_result *result) {
    if(!result || sweep.valid_points==0 || sweep.valid_points>SWEEP_MAX_POINTS)
        return -1;
    struct hf_point points[SWEEP_MAX_POINTS];
    for(unsigned i=0;i<sweep.valid_points;i++) {
        points[i].frequency_mhz=sweep.freq_mhz[i];
        points[i].measured_db=sweep.relative_db[i];
        points[i].reference_db=0.0;
        points[i].quality=sweep_health_quality(i);
    }
    struct hf_config cfg=hf_config_default();
    return hf_evaluate(&cfg,points,sweep.valid_points,result);
}
static int diag_export_report(void) {
    char tmp[PATH_MAX]; snprintf(tmp,sizeof(tmp),"%s.tmp.%ld",REPORT_RESULT_PATH,(long)getpid());
    FILE *f=fopen(tmp,"w"); if(!f) return -1;
    time_t now=time(NULL); struct tm tm_now; char iso[40]="unknown";
    if(localtime_r(&now,&tm_now)) strftime(iso,sizeof(iso),"%Y-%m-%dT%H:%M:%S%z",&tm_now);
    int ok=fprintf(f,"# diagnostic report v3\n# clock_source=SYSTEM_CLOCK_UNVERIFIED; legacy_event_date=unknown; monitor=periodic_sparse_capture\n# monitor_rows=latest_%u_of_%u_session_events\n# unix_time=%lld date=%s sensitivity=%u\n",
                   diag_event_history_count,diag_session_event_count,
                   (long long)now,iso,diag_sensitivity_level(&diag_monitor_state.cfg))>0;
    ok=ok && fprintf(f,"section,run_metadata\n")>0;
    ok=ok && fprintf(f,"field,value\n")>0;
    ok=ok && fprintf(f,"report_schema,monitor_event_health_v1\n")>0;
    ok=ok && fprintf(f,"build_variant,scope_persistent261_monitor_report\n")>0;
    ok=ok && fprintf(f,"storage_dir,%s\n",DIAG_STORAGE_DIR)>0;
    ok=ok && fprintf(f,"monitor_sensitivity,%u\n",diag_sensitivity_level(&diag_monitor_state.cfg))>0;
    ok=ok && fprintf(f,"recovery_hold_ms,%llu\nrecovery_hold_min_ms,%u\nrecovery_hold_max_ms,%u\nrecovery_hold_semantics,RMS_VPP_FREQUENCY_RECOVERY_NOT_SAMPLE_PERIOD\n",
                    (unsigned long long)diag_monitor_state.cfg.recovery_hold_ms,MONITOR_HOLD_MIN_MS,MONITOR_HOLD_MAX_MS)>0;
    ok=ok && fprintf(f,"config_signature,sens=%u|hold_ms=%llu|history=%u\n",
                     diag_sensitivity_level(&diag_monitor_state.cfg),
                     (unsigned long long)diag_monitor_state.cfg.recovery_hold_ms,
                     DIAG_EVENT_HISTORY)>0;
    ok=ok && fprintf(f,"section,sequence,date,start_ms,end_ms,duration_ms,severity,baseline_rms,minimum_rms,baseline_vpp,minimum_vpp,frozen_file,evidence_status,event_health_label,event_severity,event_quality_reason,event_health_score\n")>0;
    for(unsigned i=0;i<diag_event_history_count && ok;i++) {
        const struct diag_monitor_event *e=&diag_event_history[i];
        char event_date[40];
        diag_event_date(e->sequence,event_date,sizeof(event_date));
        char evidence[PATH_MAX];
        snprintf(evidence,sizeof(evidence),"%s/diag_monitor_event_%04u.bin",DIAG_STORAGE_DIR,e->sequence);
        int present=access(evidence,R_OK)==0;
        struct monitor_event_health event_health;
        if(monitor_event_health_summarize(e,&event_health)!=0) {
            memset(&event_health,0,sizeof(event_health));
            snprintf(event_health.event_health_label,sizeof(event_health.event_health_label),"INVALID");
            snprintf(event_health.severity,sizeof(event_health.severity),"UNKNOWN");
            snprintf(event_health.quality_reason,sizeof(event_health.quality_reason),"SUMMARY_ERROR");
        }
        ok=fprintf(f,"monitor,%u,%s,%llu,%llu,%llu,%.6f,%.6f,%.6f,%.6f,%.6f,%s,%s,%s,%s,%s,%.6f\n",
                   e->sequence,event_date,(unsigned long long)e->start_ms,(unsigned long long)e->end_ms,
                   (unsigned long long)e->duration_ms,e->severity,e->baseline_rms,e->minimum_rms,
                   e->baseline_vpp,e->minimum_vpp,present?evidence:"",present?"AVAILABLE":"MISSING",
                   event_health.event_health_label,event_health.severity,event_health.quality_reason,
                   event_health.severity_score)>0;
    }
    ok=ok && fprintf(f,"section,sweep_config\n")>0;
    ok=ok && fprintf(f,"sweep_config,start_mhz,stop_mhz,points,scale,average_frames,retries,pass_threshold_db\n")>0;
    ok=ok && fprintf(f,"sweep_config,%.6f,%.6f,%u,%s,%u,%u,%.6f\n",
                     sweep.start_mhz,sweep.stop_mhz,sweep.points,
                     sweep.logarithmic?"log":"linear",sweep.average_frames,
                     sweep.max_retries,sweep.pass_threshold_db)>0;
    ok=ok && fprintf(f,"source_flatness,calibrated,points,target_rms,path\n")>0;
    ok=ok && fprintf(f,"source_flatness,%d,%u,%.6f,%s\n",sweep_flatness.calibrated,
                     sweep_flatness.points,sweep_flatness.target_rms,SWEEP_FLAT_PATH)>0;
    ok=ok && fprintf(f,"section,sweep_points\n")>0;
    ok=ok && fprintf(f,"index,frequency_mhz,rms_code,vpp_code,relative_db,valid,status\n")>0;
    for(unsigned i=0;i<sweep.valid_points && i<SWEEP_MAX_POINTS && ok;i++) {
        const char *quality=sweep_export_quality(i);
        int valid=!strcmp(quality,"VALID_SCALAR");
        const char *status=valid?(sweep.relative_db[i]>=sweep.pass_threshold_db?"PASS":"FAIL"):quality;
        ok=fprintf(f,"%u,%.6f,%.6f,%.6f,%.6f,%d,%s\n",i+1,
                   sweep.freq_mhz[i],sweep.rms_code[i],sweep.vpp_code[i],
                   valid?sweep.relative_db[i]:NAN,valid,status)>0;
    }
    struct hf_result health;
    int health_ok=sweep_health_summary(&health)==0;
    ok=ok && fprintf(f,"section,health_fingerprint\n")>0;
    ok=ok && fprintf(f,"label,health_score,valid_points,total_points,valid_ratio,pass_ratio,max_abs_delta_db,mean_abs_delta_db,p95_abs_delta_db,reason\n")>0;
    if(health_ok) {
        ok=ok && fprintf(f,"%s,%.6f,%u,%u,%.6f,%.6f,%.6f,%.6f,%.6f,%s\n",
                         hf_label_name(health.label),health.health_score,
                         health.valid_points,health.total_points,health.valid_ratio,
                         health.pass_ratio,health.max_abs_delta_db,
                         health.mean_abs_delta_db,health.p95_abs_delta_db,
                         health.reason)>0;
    } else {
        ok=ok && fprintf(f,"INVALID_BASELINE,0,0,%u,0,0,0,0,0,NO_VALID_SWEEP_POINTS\n",
                         sweep.valid_points)>0;
    }
    measurement_quality_refresh_age();
    if(!measurement_quality_cache_valid) {
        memset(&measurement_quality_cache,0,sizeof measurement_quality_cache);
        measurement_quality_cache.label=MQ_INVALID;
        snprintf(measurement_quality_cache.reason,sizeof measurement_quality_cache.reason,"NO_RUNTIME_QUALITY_INPUT");
    }
    ok=ok && measurement_quality_csv(f,&measurement_quality_cache)==0;
    ok=ok && fprintf(f,"# occupied_bw_hz is the contiguous -20 dBc threshold width around the fundamental; it is not integrated occupied bandwidth\n")>0;
    ok=ok && fprintf(f,"section,rf_metrics\n")>0;
    ok=ok && fprintf(f,"valid,fundamental_bin,hd2_db,hd3_db,thd_percent,sfdr_db,snr_db,occupied_bw_hz,noise_floor_db\n")>0;
    if(rf_metrics_cache_valid)
        ok=ok && fprintf(f,"1,%u,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                         rf_metrics_cache.fundamental_bin,rf_metrics_cache.hd2_db,
                         rf_metrics_cache.hd3_db,rf_metrics_cache.thd_percent,
                         rf_metrics_cache.sfdr_db,rf_metrics_cache.snr_db,
                         rf_metrics_cache.occupied_bw_hz,rf_metrics_cache.noise_floor_db)>0;
    else ok=ok && fprintf(f,"0,0,nan,nan,nan,nan,nan,nan,nan\n")>0;
    ok=ok && fprintf(f,"section,signal_trend\n")>0;
    ok=ok && fprintf(f,"frames,frequency_frames,amplitude_index_percent,frequency_min_hz,frequency_max_hz,frequency_span_hz,observed_changes,max_gap_ms\n")>0;
    ok=ok && fprintf(f,"%u,%u,%.6f,%.6f,%.6f,%.6f,%u,%.6f\n",
                     signal_trend_cache.frames,signal_trend_cache.frequency_frames,
                     signal_trend_cache.amplitude_index_pct,
                     signal_trend_cache.frequency_min_hz,signal_trend_cache.frequency_max_hz,
                     signal_trend_cache.frequency_span_hz,signal_trend_cache.observed_changes,
                     signal_trend_cache.max_gap_ms)>0;
    ok=ok && fprintf(f,"section,artifacts\n")>0;
    ok=ok && fprintf(f,"artifact,path,availability_at_export\n")>0;
    ok=ok && fprintf(f,"sweep_latest_csv,%s,%s\n",SWEEP_RESULT_PATH,access(SWEEP_RESULT_PATH,R_OK)==0?"AVAILABLE":"MISSING")>0;
    ok=ok && fprintf(f,"sweep_scalar_csv,%s,%s\n",SWEEP_SCALAR_PATH,access(SWEEP_SCALAR_PATH,R_OK)==0?"AVAILABLE":"MISSING")>0;
    ok=ok && fprintf(f,"sweep_flatness_csv,%s,%s\n",SWEEP_FLAT_PATH,access(SWEEP_FLAT_PATH,R_OK)==0?"AVAILABLE":"MISSING")>0;
    if(fflush(f)!=0 || fsync(fileno(f))<0) ok=0;
    if(fclose(f)!=0) ok=0;
    f=NULL;
    if(ok) {
        if(rename(tmp,REPORT_RESULT_PATH)==0) {
            if(sync_parent_directory(REPORT_RESULT_PATH)<0) return -1;
            printf("DIAG_REPORT_EXPORTED PATH=%s EVENTS=%u\n",REPORT_RESULT_PATH,diag_event_history_count); return 0;
        }
    }
    if(f) fclose(f);
    unlink(tmp);
    return -1;
}

/* The configurable SWEEP revision keeps the existing screen/measurement path
 * unchanged.  A small key=value file
 * makes the range and pass limit reproducible without adding fragile keypad
 * code to the touchscreen.  Invalid or incomplete files fall back to the
 * known-good defaults. */
static void sweep_defaults(void) {
    sweep.start_mhz=1.0;
    sweep.stop_mhz=SWEEP_MAX_MHZ;
    sweep.pass_threshold_db=SWEEP_PASS_THRESHOLD_DB;
    sweep.points=16;
    sweep.average_frames=4;
    sweep.max_retries=2;
    sweep.logarithmic=0;
}
static void sweep_load_config(void) {
    sweep_defaults();
    FILE *f=fopen(SWEEP_CONFIG_PATH,"r");
    if(!f) return;
    double start=sweep.start_mhz, stop=sweep.stop_mhz, pass=sweep.pass_threshold_db;
    unsigned points=sweep.points, average=sweep.average_frames, retries=sweep.max_retries;
    int logarithmic=sweep.logarithmic;
    int have_start=0,have_stop=0,have_pass=0,have_points=0,valid=1;
    char line[160],key[64],value[64];
    while(fgets(line,sizeof(line),f)) {
        char *p=line;
        while(*p==' '||*p=='\t') p++;
        if(*p=='#'||*p=='\n'||*p=='\r'||!*p) continue;
        if(sscanf(p,"%63[^=]=%63s",key,value)!=2) { valid=0; break; }
        char *k=key; while(*k) { if(*k>='A'&&*k<='Z') *k=(char)(*k-'A'+'a'); k++; }
        /* SCALE is symbolic (linear/log), so do not pass it through
         * strtod().  The previous parser rejected every persisted LOG file
         * and silently restored defaults after restart. */
        if(!strcmp(key,"scale") || !strcmp(key,"sweep_type")) {
            if(!strcmp(value,"log") || !strcmp(value,"LOG") || !strcmp(value,"1")) logarithmic=1;
            else if(!strcmp(value,"linear") || !strcmp(value,"LINEAR") || !strcmp(value,"0")) logarithmic=0;
            else valid=0;
            continue;
        }
        char *end=NULL; errno=0; double x=strtod(value,&end);
        if(errno || end==value || (*end!='\0'&&*end!='\n'&&*end!='\r')) { valid=0; break; }
        if(!isfinite(x)) { valid=0; break; }
        if(!strcmp(key,"start_mhz")) { start=x; have_start=1; }
        else if(!strcmp(key,"stop_mhz")) { stop=x; have_stop=1; }
        else if(!strcmp(key,"pass_threshold_db")) { pass=x; have_pass=1; }
        else if(!strcmp(key,"points")) { if(x<8||x>64||x!=floor(x)) valid=0; else {points=(unsigned)x; have_points=1;} }
        else if(!strcmp(key,"average") || !strcmp(key,"average_frames")) { if(x<1||x>16||x!=floor(x)) valid=0; else average=(unsigned)x; }
        else if(!strcmp(key,"retries") || !strcmp(key,"max_retries")) { if(x<0||x>5||x!=floor(x)) valid=0; else retries=(unsigned)x; }
        else { valid=0; break; }
    }
    if(ferror(f)) valid=0;
    if(fclose(f)!=0) valid=0;
    /* Clip out-of-range persisted profiles to the supported diagnostic
     * ceiling while preserving all other user-selected sweep options. */
    if(valid && have_stop && isfinite(stop) && stop>SWEEP_MAX_MHZ) {
        printf("SWEEP_CONFIG_STOP_CLAMPED FROM=%.3f TO=%.3f\n",stop,SWEEP_MAX_MHZ);
        stop=SWEEP_MAX_MHZ;
    }
    if(!valid || !have_start || !have_stop || !have_pass || !have_points ||
       !isfinite(start) || !isfinite(stop) || !isfinite(pass) ||
       start<0.3 || stop>SWEEP_MAX_MHZ || stop<=start || pass>0.0 || pass<-60.0 ||
       points<SWEEP_MIN_POINTS || points>SWEEP_MAX_POINTS ||
       (points!=8 && points!=16 && points!=32 && points!=64) ||
       average<1 || average>16 || retries>5 || (logarithmic && start<=0.0)) {
        printf("SWEEP_CONFIG_INVALID USING_DEFAULTS PATH=%s\n",SWEEP_CONFIG_PATH);
        sweep_defaults();
        return;
    }
    sweep.start_mhz=start; sweep.stop_mhz=stop; sweep.pass_threshold_db=pass;
    sweep.points=points; sweep.average_frames=average; sweep.max_retries=retries;
    sweep.logarithmic=logarithmic;
    printf("SWEEP_CONFIG_LOADED START=%.3f STOP=%.3f PASS_DB=%.3f POINTS=%u SCALE=%s AVG=%u RETRIES=%u PATH=%s\n",
           start,stop,pass,points,logarithmic?"LOG":"LINEAR",average,retries,SWEEP_CONFIG_PATH);
}

static void diag_history_add(const struct diag_monitor_event *e) {
    if(!e || !e->sequence) return;
    if(diag_event_history_count < DIAG_EVENT_HISTORY) {
        diag_event_history[diag_event_history_count++] = *e;
    } else {
        memmove(&diag_event_history[0],&diag_event_history[1],
                (DIAG_EVENT_HISTORY-1)*sizeof(diag_event_history[0]));
        diag_event_history[DIAG_EVENT_HISTORY-1] = *e;
    }
    diag_selected_event_index=diag_event_history_count-1;
}

static int diag_load_replay_phase(uint32_t sequence,enum diag_replay_phase phase) {
    diag_replay_valid=0; diag_freeze_ready=0; diag_freeze_frames=0;
    diag_replay_phase=phase; diag_replay_timestamp=0; diag_replay_index=0;
    diag_replay_legacy=0; diag_replay_unavailable=0;
    if(phase==REPLAY_LIVE) return 0;
    char path[160];
    int fd=-1;
    const char *roots[]={DIAG_STORAGE_DIR,"/var/log","/tmp"};
    for(unsigned i=0;i<sizeof(roots)/sizeof(roots[0]) && fd<0;i++) {
        snprintf(path,sizeof(path),"%s/diag_monitor_event_%04u.bin",roots[i],sequence);
        fd=open(path,O_RDONLY);
    }
    if(fd<0) { diag_replay_unavailable=1; return -1; }
    struct { uint32_t magic,sequence,frames,samples; } header={0};
    int ok=read(fd,&header,sizeof(header))==(ssize_t)sizeof(header) &&
           (header.magic==0x31474d44U || header.magic==0x32474d44U) && header.sequence==sequence &&
           header.frames>0 && header.frames<=DIAG_RING_FRAMES && header.samples==N;
    off_t offset=(off_t)sizeof(header)+(off_t)(header.frames-1)*N*sizeof(uint32_t);
    off_t expected=(off_t)sizeof(header)+(off_t)header.frames*N*sizeof(uint32_t);
    if(ok && header.magic==0x32474d44U) {
        struct diag_evidence_header h;
        ok=lseek(fd,0,SEEK_SET)==0 && read(fd,&h,sizeof(h))==(ssize_t)sizeof(h) &&
           h.selected<h.frames && h.pre<=16 && h.post<=8 && h.pre+2+h.post==h.frames;
        if(ok) {
            unsigned index=h.pre+1;
            if(phase==REPLAY_PRE) { if(!h.pre) ok=0; else index=h.pre-1; }
            else if(phase==REPLAY_ONSET) index=h.pre;
            else if(phase==REPLAY_RECOVERY) { if(!h.post) ok=0; else index=h.pre+2; }
            if(!ok) diag_replay_unavailable=1;
            diag_replay_index=index; diag_replay_timestamp=h.timestamps[index];
            offset=(off_t)sizeof(h)+(off_t)index*N*sizeof(uint32_t);
            expected=(off_t)sizeof(h)+(off_t)h.frames*N*sizeof(uint32_t);
        }
    } else if(ok) {
        diag_replay_legacy=1; diag_replay_index=header.frames-1;
        /* Legacy records have no trustworthy phase map or timestamps. */
        if(phase!=REPLAY_WORST) { ok=0; diag_replay_unavailable=1; }
    }
    struct stat st;
    if(ok && (fstat(fd,&st)<0 || st.st_size!=expected)) ok=0;
    if(ok) {
        if(lseek(fd,offset,SEEK_SET)<0 || read(fd,diag_replay_frame,sizeof(diag_replay_frame))!=(ssize_t)sizeof(diag_replay_frame)) ok=0;
    }
    close(fd);
    diag_replay_valid=ok;
    if(!ok) diag_replay_unavailable=1;
    if(ok) { diag_freeze_ready=1; diag_freeze_sequence=sequence; diag_freeze_frames=header.frames; }
    return ok?0:-1;
}
static int diag_load_replay_event(uint32_t sequence) {
    return diag_load_replay_phase(sequence,REPLAY_WORST);
}
static int diag_report_select_event(int delta) {
    if(!diag_event_history_count) return 0;
    int next=(int)diag_selected_event_index+delta;
    if(next<0) next=0;
    if(next>=(int)diag_event_history_count) next=(int)diag_event_history_count-1;
    diag_selected_event_index=(unsigned)next;
    diag_last_event=diag_event_history[diag_selected_event_index];
    diag_have_event=1;
    (void)diag_load_replay_event(diag_last_event.sequence);
    printf("DIAG_REPORT_EVENT_SELECT INDEX=%u SEQ=%u REPLAY=%d\n",
           diag_selected_event_index,diag_last_event.sequence,diag_replay_valid);
    return 1;
}
static int diag_replay_touch(int x,int y) {
    if(diag_ui_state.mode!=DIAG_UI_DIAG || diag_ui_state.page!=DIAG_PAGE_MONITOR ||
       !diag_have_event || x<25 || x>=410 || y<305 || y>=330) return 0;
    unsigned choice=(unsigned)(x-25)/77;
    (void)diag_load_replay_phase(diag_last_event.sequence,(enum diag_replay_phase)choice);
    return 1;
}

static void diag_event_date(uint32_t sequence,char *date,size_t size) {
    snprintf(date,size,"unknown");
    char path[180]; snprintf(path,sizeof(path),"%s/diag_monitor_event_%04u.bin",DIAG_STORAGE_DIR,sequence);
    FILE *f=fopen(path,"rb"); if(!f) return;
    struct diag_evidence_header h;
    int ok=fread(&h,1,sizeof(h),f)==sizeof(h) && h.magic==0x32474d44U &&
           h.sequence==sequence && h.system_wall_start_ms>0;
    fclose(f);
    if(ok) {
        time_t t=(time_t)(h.system_wall_start_ms/1000ULL); struct tm tm_event;
        if(localtime_r(&t,&tm_event)) strftime(date,size,"%Y-%m-%dT%H:%M:%S%z",&tm_event);
    }
}

static void diag_capture_evidence(const uint32_t *a,unsigned old_enter,
                                  enum diag_monitor_state old_state) {
    const struct diag_monitor *m=&diag_monitor_state;
    if(old_state==DIAG_MONITOR_NORMAL && !old_enter && m->enter_count) {
        memset(&diag_evidence,0,sizeof(diag_evidence));
        diag_evidence.magic=0x32474d44U; diag_evidence.samples=N;
        unsigned pre=diag_ring_count?diag_ring_count-1:0;
        if(pre>16) pre=16;
        diag_evidence.pre=pre; diag_evidence.frames=pre+2;
        diag_evidence.selected=pre+1;
        struct timespec wall;
        if(clock_gettime(CLOCK_REALTIME,&wall)==0)
            diag_evidence.system_wall_start_ms=(uint64_t)wall.tv_sec*1000+wall.tv_nsec/1000000;
        unsigned first=(diag_ring_next+DIAG_RING_FRAMES-pre-1)%DIAG_RING_FRAMES;
        for(unsigned i=0;i<pre;i++) {
            unsigned slot=(first+i)%DIAG_RING_FRAMES;
            memcpy(diag_evidence_frames[i],diag_frame_ring[slot],sizeof(diag_evidence_frames[i]));
            diag_evidence.timestamps[i]=diag_ring_times[slot];
        }
        memcpy(diag_evidence_frames[pre],a,sizeof(diag_evidence_frames[pre]));
        memcpy(diag_evidence_frames[pre+1],a,sizeof(diag_evidence_frames[pre+1]));
        diag_evidence.timestamps[pre]=diag_evidence.timestamps[pre+1]=diag_current_sample.timestamp_ms;
        diag_evidence_severity=m->candidate_severity;
        diag_evidence_active=1;
    }
    if(!diag_evidence_active) return;
    if(m->state==DIAG_MONITOR_NORMAL && old_state!=DIAG_MONITOR_FAULT && !m->enter_count) {
        diag_evidence_active=0; return; /* Unconfirmed candidate, not an event. */
    }
    double severity=m->state==DIAG_MONITOR_FAULT?m->event_severity:m->candidate_severity;
    if(severity>diag_evidence_severity) {
        unsigned worst=diag_evidence.pre+1;
        memcpy(diag_evidence_frames[worst],a,sizeof(diag_evidence_frames[worst]));
        diag_evidence.timestamps[worst]=diag_current_sample.timestamp_ms;
        diag_evidence_severity=severity;
    }
    if(m->state==DIAG_MONITOR_FAULT) {
        diag_evidence.sequence=m->event_sequence;
        if(!m->exit_count) { diag_evidence.post=0; diag_evidence.frames=diag_evidence.pre+2; }
        else if(diag_evidence.post<8) {
            unsigned slot=diag_evidence.frames++;
            memcpy(diag_evidence_frames[slot],a,sizeof(diag_evidence_frames[slot]));
            diag_evidence.timestamps[slot]=diag_current_sample.timestamp_ms;
            diag_evidence.post++;
        }
    }
    /* On completion exit_count is cleared by the core: include final frame
     * when short recovery holds complete before all eight post slots. */
    if(old_state==DIAG_MONITOR_FAULT && m->state==DIAG_MONITOR_NORMAL && diag_evidence.post<8) {
        unsigned slot=diag_evidence.frames++;
        memcpy(diag_evidence_frames[slot],a,sizeof(diag_evidence_frames[slot]));
        diag_evidence.timestamps[slot]=diag_current_sample.timestamp_ms;
        diag_evidence.post++;
    }
}

static void diag_load_event_history(const char *csv_path) {
    FILE *fp=fopen(csv_path,"r");
    if(!fp) return;
    char line[512];
    uint32_t max_sequence=0;
    diag_event_history_count=0;
    diag_selected_event_index=0;
    diag_session_event_count=0;
    diag_have_event=0;
    diag_replay_valid=0;
    while(fgets(line,sizeof(line),fp)) {
        unsigned reserved_sequence;
        if(sscanf(line,"# EVENT_RESERVED,%u",&reserved_sequence)==1) {
            if(reserved_sequence>max_sequence) max_sequence=reserved_sequence;
            continue;
        }
        if(!strncmp(line,"# SESSION_START,",16)) {
            /* Only the most recent session belongs in the live UI.  Earlier
             * rows stay in the CSV and still contribute to max_sequence. */
            memset(diag_event_history,0,sizeof(diag_event_history));
            diag_event_history_count=0;
            diag_selected_event_index=0;
            diag_session_event_count=0;
            diag_have_event=0;
            diag_replay_valid=0;
            continue;
        }
        struct diag_monitor_event e; unsigned long long start,end,duration;
        memset(&e,0,sizeof(e));
        if(sscanf(line,"%u,%llu,%llu,%llu,%lf,%lf,%lf,%lf,%lf,%u,%u",
                  &e.sequence,&start,&end,&duration,&e.severity,
                  &e.baseline_rms,&e.minimum_rms,&e.baseline_vpp,
                  &e.minimum_vpp,&e.pre_frames,&e.post_frames)==11) {
            e.start_ms=start; e.end_ms=end; e.duration_ms=duration;
            /* Older CSV files may contain an unclamped severity.  Sanitize
             * historical records so the UI never displays impossible values. */
            if(!isfinite(e.severity) || e.severity<0.0) e.severity=0.0;
            if(e.severity>1.0) e.severity=1.0;
            if(e.sequence>max_sequence) max_sequence=e.sequence;
            diag_history_add(&e);
            diag_session_event_count++;
        }
    }
    fclose(fp);
    diag_monitor_state.event_sequence=max_sequence;
    if(diag_event_history_count) {
        diag_last_event=diag_event_history[diag_event_history_count-1];
        diag_have_event=1;
        diag_load_replay_event(diag_last_event.sequence);
    }
}

static int diag_save_evidence_file(const char *suffix) {
    char path[180],tmp[220];
    snprintf(path,sizeof(path),"%s/diag_monitor_event_%04u.%s",DIAG_STORAGE_DIR,diag_evidence.sequence,suffix);
    snprintf(tmp,sizeof(tmp),"%s.tmp.%ld",path,(long)getpid());
    /* Never overwrite evidence or silently fall back to volatile /tmp. */
    int fd=open(tmp,O_CREAT|O_EXCL|O_WRONLY,0644);
    if(fd<0) return -1;
    int ok=diag_monitor_write_all(fd,&diag_evidence,sizeof(diag_evidence))==(int)sizeof(diag_evidence);
    for(unsigned i=0;ok && i<diag_evidence.frames;i++) {
        size_t bytes=(size_t)N*sizeof(uint32_t);
        ok=diag_monitor_write_all(fd,diag_evidence_frames[i],bytes)==(int)bytes;
    }
    if(fsync(fd)<0) ok=0;
    if(close(fd)<0) ok=0;
    /* link publishes only a fully written file and fails if the target
     * already exists. A directory fsync makes publication durable. */
    if(ok && link(tmp,path)<0) ok=0;
    if(unlink(tmp)<0) ok=0;
    int dir_fd=open(DIAG_STORAGE_DIR,O_RDONLY|O_DIRECTORY);
    if(dir_fd<0) ok=0;
    else { if(fsync(dir_fd)<0) ok=0; close(dir_fd); }
    return ok?0:-1;
}

static int diag_reserve_active_event(void) {
    if(!diag_evidence_active || !diag_evidence.sequence || diag_csv_fd<0) return -1;
    char marker[96];
    int n=snprintf(marker,sizeof(marker),"# EVENT_RESERVED,%u\n",diag_evidence.sequence);
    if(n<=0 || n>=(int)sizeof(marker) || diag_monitor_write_all(diag_csv_fd,marker,(size_t)n)!=n || fsync(diag_csv_fd)<0)
        return -1;
    /* Kept even after completion: an immutable onset snapshot. A .open file
     * without matching completed CSV row means an interrupted event. */
    return diag_save_evidence_file("open");
}

static int diag_archive_manual_event(void) {
    if(!diag_evidence_active || !diag_evidence.sequence || diag_csv_fd<0) return -1;
    if(diag_save_evidence_file("manual")<0) {
        /* A prior attempt may have saved the snapshot but failed to commit
         * its session marker. Accept only an intact snapshot of this event. */
        char path[180];
        snprintf(path,sizeof(path),"%s/diag_monitor_event_%04u.manual",DIAG_STORAGE_DIR,diag_evidence.sequence);
        FILE *f=fopen(path,"rb"); if(!f) return -1;
        struct diag_evidence_header h; struct stat st;
        int ok=fread(&h,1,sizeof(h),f)==sizeof(h) && h.magic==0x32474d44U &&
               h.sequence==diag_evidence.sequence && h.frames>0 && h.frames<=DIAG_RING_FRAMES &&
               h.samples==N && h.pre<=16 && h.post<=8 && h.pre+2+h.post==h.frames &&
               h.selected<h.frames && h.system_wall_start_ms==diag_evidence.system_wall_start_ms &&
               fstat(fileno(f),&st)==0 && st.st_size==(off_t)(sizeof(h)+(size_t)h.frames*N*sizeof(uint32_t));
        fclose(f); if(!ok) return -1;
    }
    char marker[200];
    int n=snprintf(marker,sizeof(marker),"# EVENT_INTERRUPTED,%u,%llu,%llu,%.6f,MANUAL_NEW_TEST\n",
                   diag_evidence.sequence,(unsigned long long)diag_monitor_state.event_start_ms,
                   (unsigned long long)diag_current_sample.timestamp_ms,diag_monitor_state.event_severity);
    if(n<=0 || n>=(int)sizeof(marker) || diag_monitor_write_all(diag_csv_fd,marker,(size_t)n)!=n || fsync(diag_csv_fd)<0)
        return -1;
    printf("MONITOR_EVENT_INTERRUPTED SEQ=%u REASON=MANUAL_NEW_TEST\n",diag_evidence.sequence);
    return 0;
}

static int diag_write_frozen_event(const struct diag_monitor_event *e) {
    if(!e || !e->sequence || !diag_evidence_active || diag_evidence.sequence!=e->sequence) return -1;
    int ok=diag_save_evidence_file("bin")==0;
    if(ok) {
        diag_freeze_ready=1; diag_freeze_sequence=e->sequence;
        diag_freeze_frames=diag_evidence.frames;
        printf("MONITOR_FREEZE_FILE=%s/diag_monitor_event_%04u.bin FRAMES=%u PRE=%u POST=%u SELECTED=WORST SAMPLES=%d\n",
               DIAG_STORAGE_DIR,e->sequence,diag_evidence.frames,diag_evidence.pre,diag_evidence.post,N);
    }
    return ok?0:-1;
}
static int diag_select_history_row(int x,int y) {
    if(diag_ui_state.mode!=DIAG_UI_DIAG || diag_ui_state.page!=DIAG_PAGE_MONITOR ||
       x<420 || y<360 || y>=420 || diag_event_history_count==0) return 0;
    unsigned row=(unsigned)(y-360)/18;
    if(row>=3 || row>=diag_event_history_count) return 0;
    diag_selected_event_index=diag_event_history_count-1-row;
    diag_last_event=diag_event_history[diag_selected_event_index];
    diag_have_event=1;
    diag_replay_valid=diag_load_replay_event(diag_last_event.sequence)==0;
    return 1;
}
struct metrics;
static void sweep_stop(void);
static void sweep_begin(int calibration);
static void sweep_update(const struct metrics *v);
static void measurement_quality_update_frame(const struct metrics *v);
static void measurement_quality_update_sweep(const struct metrics *v,
                                             unsigned point);
static void measurement_quality_refresh_age(void);
static void sweep_load_reference(void);
static int sweep_save_reference(void);
static int sweep_save_result(void);
static int sweep_pass_band(double *lo_mhz,double *hi_mhz);
static int gen_verify(void);
struct screen;
static void render_gen_keypad(struct screen *s);
static void render_custom_panel(struct screen *s);
#ifndef SCOPE_POWEROFF_EXEC
static int launch_system_poweroff(void) {
    pid_t pid=fork();
    if(pid<0) return -1;
    if(pid==0) { execl("/sbin/poweroff","poweroff",(char *)NULL); _exit(127); }
    int status;
    while(waitpid(pid,&status,0)<0) { if(errno!=EINTR) return -1; }
    return WIFEXITED(status) && WEXITSTATUS(status)==0 ? 0 : -1;
}
#define SCOPE_POWEROFF_EXEC launch_system_poweroff
#endif
static int power_hit(int w,int h,int x,int y) {
    if(!power_dialog) return x>=w-260 && x<w-110 && y>=4 && y<34 ? 1 : 0;
    if(power_dialog==2) return 0;
    int left=w/2-210,top=h/2+55;
    if(y<top || y>=top+55) return 0;
    if(x>=left && x<left+180) return 2;
    if(x>=left+240 && x<left+420) return 3;
    return 0;
}
static void power_action(int action) {
    if(action==1 && !power_dialog) { power_dialog=1; printf("POWER_DIALOG_OPEN\n"); }
    else if(action==2 && power_dialog!=2) { power_dialog=0; printf("POWER_DIALOG_CANCEL\n"); }
    else if(action==3 && (power_dialog==1 || power_dialog==3)) {
        power_dialog=2; shutdown_requested=1; printf("POWER_OFF_CONFIRMED\n");
    }
}
static void power_tick(void) {
    if(!shutdown_requested) return;
    shutdown_requested=0;
#ifdef MONITOR_ASYNC694
    if(mask694_selected) {
        mask694_power_after_drain=1;stopped=1;
        printf("POWER_OFF_DEFERRED694 UNTIL_OUTPUT_OFF_AND_DURABLE_DRAIN\n");return;
    }
#endif
    fflush(stdout);
    if(SCOPE_POWEROFF_EXEC()==0) { stopped=1; printf("POWER_OFF_REQUEST_ACCEPTED\n"); }
    else { power_dialog=3; fprintf(stderr,"POWER_OFF_FAILED; device remains powered; retry or cancel\n"); }
}
static int window_start(int visible,unsigned position,int pan) {
    int start=(int)position-(int)position*visible/N+pan;
    if(start<0) start=0;
    if(start>N-visible) start=N-visible;
    return start;
}
struct view_config { int ns,codes,offset; };
static struct view_config view={100,2048,0};
static int view_dirty=1;
static const int view_times[]={10,20,50,100,200,500};
static const int view_scales[]={256,512,1024,2048,4096,8192,0};
#define VIEW_TIME_COUNT ((int)(sizeof(view_times)/sizeof(view_times[0])))
#define VIEW_SCALE_COUNT ((int)(sizeof(view_scales)/sizeof(view_scales[0])))
static int detent_index(const int *values,int count,int value) {
    for(int i=0;i<count;i++) if(values[i]==value) return i;
    return 0;
}
static int detent_contains(const int *values,int count,int value) {
    for(int i=0;i<count;i++) if(values[i]==value) return 1;
    return 0;
}
static int valid_view(struct view_config v) {
    return detent_contains(view_times,VIEW_TIME_COUNT,v.ns) &&
           detent_contains(view_scales,VIEW_SCALE_COUNT,v.codes) &&
           v.offset>=-8192 && v.offset<=8192;
}
static int visible_samples(struct view_config v) { return v.ns*4; }
static void read_view(void) {
    if(view_drag) return;
    FILE *f=fopen(SCOPE_VIEW_PATH,"r"); if(!f) return;
    struct view_config next; char extra;
    int n=fscanf(f,"%d %d %d %c",&next.ns,&next.codes,&next.offset,&extra);
    fclose(f);
    if(n!=3 || !valid_view(next)) return;
    if(next.ns!=view.ns || next.codes!=view.codes || next.offset!=view.offset) {
        view=next; view_dirty=1;
    }
}
static struct view_config view_action(struct view_config v,int b) {
    if(b==3) v.ns=view_times[(detent_index(view_times,VIEW_TIME_COUNT,v.ns)+1)%VIEW_TIME_COUNT];
    else if(b==4) v.codes=view_scales[(detent_index(view_scales,VIEW_SCALE_COUNT,v.codes)+1)%VIEW_SCALE_COUNT];
    else if(b==5) { v.offset-=512; if(v.offset<-8192) v.offset=-8192; }
    else if(b==6) { v.offset+=512; if(v.offset>8192) v.offset=8192; }
    else if(b==7) v.offset=0;
    else if(b==8) { v.codes=0; v.offset=0; }
    return v;
}
static void save_view(struct view_config next) {
    if(!valid_view(next)) return;
    char path[]=SCOPE_VIEW_PATH ".touch.XXXXXX";
    int fd=mkstemp(path); if(fd<0) { perror("Touch view create"); return; }
    FILE *f=fdopen(fd,"w");
    if(!f) { close(fd); unlink(path); return; }
    int ok=fprintf(f,"%d %d %d\n",next.ns,next.codes,next.offset)>0;
    if(fflush(f)!=0 || fsync(fileno(f))<0) ok=0;
    if(fclose(f)!=0) ok=0;
    if(!ok || rename(path,SCOPE_VIEW_PATH)<0) { unlink(path); fprintf(stderr,"Touch view write failed\n"); return; }
    if(sync_parent_directory(SCOPE_VIEW_PATH)<0) fprintf(stderr,"Touch view directory sync failed; rename already completed\n");
    view=next; view_dirty=1;
}
static void apply_view_button(int b) { save_view(view_action(view,b)); }
static void stop_handler(int sig) { (void)sig; stopped=1; }
static void control_handler(int sig) {
    control_generation=control_generation==INT_MAX ? 0 : control_generation+1;
    if(sig!=SIGUSR1)
        scope_stats_invalidation683=scope_stats_invalidation683==INT_MAX ? 0 : scope_stats_invalidation683+1;
    if(sig==SIGUSR1) paused=1;
    else if(sig==SIGHUP) autoset_pending=1;
    else if(sig==SIGCONT) { paused=0; single_pending=0; }
    else if(sig==SIGUSR2) { paused=1; single_pending=1; }
}
static void set_handler(int sig,void (*handler)(int)) {
    struct sigaction sa; memset(&sa,0,sizeof(sa));
    sa.sa_handler=handler; sigemptyset(&sa.sa_mask);
    if(sigaction(sig,&sa,NULL)<0) { perror("sigaction"); exit(1); }
}
static void barrier(void) {
#if defined(__arm__)
    __asm__ volatile("dsb sy" ::: "memory");
#else
    __sync_synchronize();
#endif
}
static double seconds(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return t.tv_sec+t.tv_nsec/1e9;
}
static void pause_ns(long ns) {
    struct timespec t={ns/1000000000L,ns%1000000000L};
    while(nanosleep(&t,&t)<0 && errno==EINTR && !stopped) {}
}
struct metrics { unsigned lo,hi; double mean,rms_ac,mhz,period_ns,jitter_rms_ns,jitter_pp_ns; double clipped_fraction; int freq_valid,low_signal,rail; int timing_valid685; const char *timing_reason685; };
/* Optional readout statistics only: never feed the mean back into measure,
 * triggering, RAW, FFT or diagnostic consumers. Context sync cannot add N. */
static struct ss683_state scope_stats683;
static int scope_stats_enabled683,scope_stats_context_live683;
static unsigned scope_stats_revision683;
static sig_atomic_t scope_stats_generation683;
static sig_atomic_t scope_stats_invalidation_seen683;
static struct view_config scope_stats_view683;
static void scope_stats_context683(void) {
    /* Save exactly the epochs tested below: a signal arriving later must
     * remain visible to the next call, not be silently acknowledged here. */
    sig_atomic_t generation=control_generation;
    sig_atomic_t invalidation=scope_stats_invalidation683;
    if(!scope_stats_enabled683) {
        ss683_reset(&scope_stats683,"OFF");scope_stats_context_live683=0;return;
    }
    int eligible=diag_ui_state.mode==DIAG_UI_SCOPE && tool_page>=0 && tool_page<=2 &&
                 !reflect_ps_exclusive();
    if(!eligible) {
        ss683_reset(&scope_stats683,"PAGE/OWNER");scope_stats_context_live683=0;return;
    }
    if(!scope_stats_context_live683 || scope_stats_revision683!=trigger_revision ||
       scope_stats_view683.ns!=view.ns || scope_stats_view683.codes!=view.codes ||
       scope_stats_view683.offset!=view.offset ||
       scope_stats_invalidation_seen683!=invalidation ||
       (scope_stats_generation683!=generation && !paused))
        ss683_reset(&scope_stats683,"CONTEXT");
    scope_stats_revision683=trigger_revision;
    scope_stats_generation683=generation;
    scope_stats_invalidation_seen683=invalidation;
    scope_stats_view683=view;scope_stats_context_live683=1;
}
static void scope_stats_frame683(const struct metrics *v,uint32_t token,
                                 uint64_t observed_us,int raw_owned) {
    scope_stats_context683();
    if(!scope_stats_enabled683 || !scope_stats_context_live683)return;
    int fit=0;
    double mhz=sp682_frequency_mhz(1,v->freq_valid,v->mhz,v->period_ns,&fit);
    /* Keep clear of the existing >=20MHz HF rendering route. No new tone
     * reconstruction or estimator is admitted by this optional statistic. */
    if(raw_owned || last_frame_raw_owned682) {ss683_reset(&scope_stats683,"RAW OWNER");return;}
    if((isfinite(mhz) && mhz>=19.0) || (isfinite(v->mhz) && v->mhz>=19.0)) {
        ss683_reset(&scope_stats683,"HF/RANGE");return;
    }
    int valid=fit && !v->rail && !v->low_signal && (last_flags&7u)==3u;
    (void)ss683_push(&scope_stats683,token,observed_us,mhz*1e6,valid);
}
static void scope_stats_expire683(uint64_t now_us) {
    if(!scope_stats_enabled683 || paused || !scope_stats683.n)return;
    unsigned last=(scope_stats683.head+scope_stats683.n-1u)%SS683_CAP;
    uint64_t observed_us=scope_stats683.sample[last].observed_us;
    if(now_us>observed_us && now_us-observed_us>SS683_MAX_GAP_US) {
        ss683_reset(&scope_stats683,"STALE/WAIT");view_dirty=1;
    }
}
static double sweep_frequency(unsigned point) {
    unsigned points=sweep.points?sweep.points:16;
    if(points<=1 || point>=points) return sweep.start_mhz;
    double t=(double)point/(double)(points-1);
    if(sweep.logarithmic && sweep.start_mhz>0.0 && sweep.stop_mhz>0.0)
        return exp(log(sweep.start_mhz)+t*(log(sweep.stop_mhz)-log(sweep.start_mhz)));
    return sweep.start_mhz+(sweep.stop_mhz-sweep.start_mhz)*t;
}
static const char *sweep_mode_name(int mode) {
    if(mode==1) return "CAL_REF";
    if(mode==2) return "FLAT_CAL";
    return "MEASURE";
}
static double sweep_flat_drive_pct(double freq_mhz) {
    if(!sweep_flatness.calibrated || sweep_flatness.points<2 || !isfinite(freq_mhz)) return 25.0;
    if(freq_mhz<=sweep_flatness.freq_mhz[0]) return sweep_flatness.drive_pct[0];
    unsigned last=sweep_flatness.points-1;
    if(freq_mhz>=sweep_flatness.freq_mhz[last]) return sweep_flatness.drive_pct[last];
    for(unsigned i=1;i<sweep_flatness.points;i++) {
        if(freq_mhz<=sweep_flatness.freq_mhz[i]) {
            double f0=sweep_flatness.freq_mhz[i-1],f1=sweep_flatness.freq_mhz[i];
            double x=freq_mhz,t;
            if(f0>0.0 && f1>f0) t=(log(x)-log(f0))/(log(f1)-log(f0));
            else t=(x-f0)/(f1-f0);
            if(t<0.0) t=0.0;
            if(t>1.0) t=1.0;
            return sweep_flatness.drive_pct[i-1]+t*(sweep_flatness.drive_pct[i]-sweep_flatness.drive_pct[i-1]);
        }
    }
    return 25.0;
}
static int sweep_save_flatness(void) {
    char tmp[PATH_MAX];
    int n=snprintf(tmp,sizeof(tmp),"%s.tmp.%ld",SWEEP_FLAT_PATH,(long)getpid());
    if(n<0 || (size_t)n>=sizeof(tmp)) return -1;
    FILE *f=fopen(tmp,"w");
    if(!f) return -1;
    int ok=fprintf(f,"# diag source flatness v1\n# target_rms=%.9f\nfrequency_mhz,raw_rms_code,drive_percent\n",
                   sweep_flatness.target_rms)>0;
    for(unsigned i=0;i<sweep_flatness.points && ok;i++)
        ok=fprintf(f,"%.9f,%.9f,%.9f\n",sweep_flatness.freq_mhz[i],
                   sweep_flatness.raw_rms[i],sweep_flatness.drive_pct[i])>0;
    if(!ok || fflush(f)!=0 || fsync(fileno(f))!=0) {
        fclose(f); unlink(tmp); return -1;
    }
    if(fclose(f)!=0) { unlink(tmp); return -1; }
    if(rename(tmp,SWEEP_FLAT_PATH)==0) {
        if(sync_parent_directory(SWEEP_FLAT_PATH)<0) return -1;
        printf("SWEEP_FLATNESS_SAVED PATH=%s POINTS=%u TARGET_RMS=%.3f\n",
               SWEEP_FLAT_PATH,sweep_flatness.points,sweep_flatness.target_rms);
        return 0;
    }
    unlink(tmp);
    return -1;
}
static void sweep_load_flatness(void) {
    memset(&sweep_flatness,0,sizeof(sweep_flatness));
    FILE *f=fopen(SWEEP_FLAT_PATH,"r");
    if(!f) return;
    char line[192]; unsigned count=0; double target=0.0;
    while(fgets(line,sizeof(line),f)) {
        if(!strncmp(line,"# target_rms=",13)) {
            target=strtod(line+13,NULL);
            continue;
        }
        if(line[0]=='#' || line[0]=='\n' || line[0]=='\r' || strstr(line,"frequency_mhz")) continue;
        double freq=0.0,raw=0.0,drive=0.0;
        if(count>=SWEEP_MAX_POINTS || sscanf(line,"%lf,%lf,%lf",&freq,&raw,&drive)!=3 ||
           !isfinite(freq) || !isfinite(raw) || !isfinite(drive) || freq<0.3 || raw<=32.0 ||
           drive<1.0 || drive>50.0 || (count && freq<=sweep_flatness.freq_mhz[count-1])) {
            fclose(f); memset(&sweep_flatness,0,sizeof(sweep_flatness)); return;
        }
        sweep_flatness.freq_mhz[count]=freq;
        sweep_flatness.raw_rms[count]=raw;
        sweep_flatness.drive_pct[count]=drive;
        count++;
    }
    fclose(f);
    if(count>=2 && isfinite(target) && target>32.0) {
        sweep_flatness.points=count;
        sweep_flatness.target_rms=target;
        sweep_flatness.calibrated=1;
        printf("SWEEP_FLATNESS_LOADED PATH=%s POINTS=%u TARGET_RMS=%.3f\n",
               SWEEP_FLAT_PATH,count,target);
    } else memset(&sweep_flatness,0,sizeof(sweep_flatness));
}
static int sweep_build_flatness(void) {
    double target=HUGE_VAL;
    for(unsigned i=0;i<sweep.points;i++) {
        if(!isfinite(sweep.rms_code[i]) || sweep.rms_code[i]<=32.0) return -1;
        if(sweep.rms_code[i]<target) target=sweep.rms_code[i];
    }
    if(!isfinite(target) || target<=32.0) return -1;
    memset(&sweep_flatness,0,sizeof(sweep_flatness));
    sweep_flatness.points=sweep.points;
    sweep_flatness.target_rms=target;
    for(unsigned i=0;i<sweep.points;i++) {
        double drive=25.0*target/sweep.rms_code[i];
        if(drive<1.0) drive=1.0;
        if(drive>50.0) drive=50.0;
        sweep_flatness.freq_mhz[i]=sweep.freq_mhz[i];
        sweep_flatness.raw_rms[i]=sweep.rms_code[i];
        sweep_flatness.drive_pct[i]=drive;
    }
    sweep_flatness.calibrated=1;
    if(sweep_save_flatness()<0) { memset(&sweep_flatness,0,sizeof(sweep_flatness)); return -1; }
    return 0;
}
static int sweep_save_reference(void) {
    char tmp[PATH_MAX];
    int n=snprintf(tmp,sizeof(tmp),"%s.tmp.%ld",SWEEP_REFERENCE_PATH,(long)getpid());
    if(n<0 || (size_t)n>=sizeof(tmp)) return -1;
    FILE *f=fopen(tmp,"w");
    if(!f) return -1;
    if(fprintf(f,"# diag sweep reference v1\nfrequency_mhz,reference_rms_code\n")<0) {
        fclose(f); unlink(tmp); return -1;
    }
    for(unsigned i=0;i<sweep.points && i<SWEEP_MAX_POINTS;i++) {
        if(fprintf(f,"%.6f,%.9f\n",sweep.freq_mhz[i],sweep.reference_rms[i])<0) {
            fclose(f); unlink(tmp); return -1;
        }
    }
    if(fflush(f)!=0) { fclose(f); unlink(tmp); return -1; }
    if(fsync(fileno(f))<0) { fclose(f); unlink(tmp); return -1; }
    if(fclose(f)!=0 || rename(tmp,SWEEP_REFERENCE_PATH)<0) { unlink(tmp); return -1; }
    return sync_parent_directory(SWEEP_REFERENCE_PATH);
}
/* Persist the most recent sweep with file and directory synchronization.
 * Rename is atomic; durability still depends on filesystem/storage behavior.
 * Invalid/low-signal points remain in the file explicitly;
 * they must not be silently dropped when judging a cable. */
static int sweep_save_result(void) {
    char tmp[PATH_MAX];
    int n=snprintf(tmp,sizeof(tmp),"%s.tmp.%ld",SWEEP_RESULT_PATH,(long)getpid());
    if(n<0 || (size_t)n>=sizeof(tmp)) return -1;
    FILE *f=fopen(tmp,"w");
    if(!f) return -1;
    time_t now=time(NULL);
    if(fprintf(f,"# diag sweep result v2\n# unix_time=%lld mode=%s start_mhz=%.6f stop_mhz=%.6f points=%u scale=%s average=%u retries=%u\n",
               (long long)now,sweep_mode_name(sweep.mode),sweep.start_mhz,
               sweep.stop_mhz,sweep.points,sweep.logarithmic?"log":"linear",
               sweep.average_frames,sweep.max_retries)<0 ||
       fprintf(f,"# relative amplitude only; quiet baseline uses absolute comparison, not dB\nindex,frequency_mhz,rms_code,vpp_code,relative_db,valid,status\n")<0) {
        fclose(f); unlink(tmp); return -1;
    }
    for(unsigned i=0;i<sweep.valid_points && i<SWEEP_MAX_POINTS;i++) {
        const char *quality=sweep_export_quality(i);
        int valid=!strcmp(quality,"VALID_SCALAR");
        const char *status=valid?(sweep.relative_db[i]>=sweep.pass_threshold_db?"PASS":"FAIL"):quality;
        if(fprintf(f,"%u,%.6f,%.6f,%.6f,%.6f,%d,%s\n",i+1,sweep.freq_mhz[i],
                   sweep.rms_code[i],sweep.vpp_code[i],valid?sweep.relative_db[i]:NAN,valid,status)<0) {
            fclose(f); unlink(tmp); return -1;
        }
    }
    int ok=fflush(f)==0;
    if(fsync(fileno(f))<0) ok=0;
    if(fclose(f)!=0) ok=0;
    if(!ok) {
        unlink(tmp); return -1;
    }
    f=NULL;
    if(rename(tmp,SWEEP_RESULT_PATH)<0) {
        unlink(tmp); return -1;
    }
    if(sync_parent_directory(SWEEP_RESULT_PATH)<0) return -1;
    printf("SWEEP_CSV_SAVED PATH=%s POINTS=%u\n",SWEEP_RESULT_PATH,sweep.valid_points);
    return 0;
}
/* Scalar amplitude data is not a one-port S parameter. Do not invent phase
 * or disguise invalid values as a finite noise-floor measurement. */
static int sweep_save_scalar(void) {
    char tmp[PATH_MAX];
    int n=snprintf(tmp,sizeof(tmp),"%s.tmp.%ld",SWEEP_SCALAR_PATH,(long)getpid());
    if(n<0 || (size_t)n>=sizeof(tmp)) return -1;
    FILE *f=fopen(tmp,"w");
    if(!f) return -1;
    int ok=fprintf(f,"# scalar relative amplitude only; not S11/S21 Touchstone; phase not measured\n"
                     "frequency_mhz,relative_amplitude_db,quality\n")>0;
    for(unsigned i=0;i<sweep.valid_points && i<SWEEP_MAX_POINTS && ok;i++) {
        double db=sweep.relative_db[i];
        const char *quality=sweep_export_quality(i);
        if(strcmp(quality,"VALID_SCALAR")) db=NAN;
        ok=fprintf(f,"%.9f,%.9f,%s\n",sweep.freq_mhz[i],db,quality)>0;
    }
    if(fflush(f)!=0 || fsync(fileno(f))<0) ok=0;
    if(fclose(f)!=0) ok=0;
    f=NULL;
    if(ok) {
        if(rename(tmp,SWEEP_SCALAR_PATH)==0 && sync_parent_directory(SWEEP_SCALAR_PATH)==0) {
            printf("SWEEP_SCALAR_SAVED PATH=%s POINTS=%u NO_PHASE=1\n",SWEEP_SCALAR_PATH,sweep.valid_points);
            return 0;
        }
    }
    if(f) fclose(f);
    unlink(tmp);
    return -1;
}
static void sweep_load_reference(void) {
    FILE *f=fopen(SWEEP_REFERENCE_PATH,"r");
    if(!f) return;
    char line[160]; double freq=0.0,ref=0.0; unsigned count=0;
    while(fgets(line,sizeof(line),f)) {
        if(line[0]=='#' || line[0]=='\n' || line[0]=='\r') continue;
        if(strstr(line,"frequency_mhz")) continue;
        /* A user may deliberately define a disconnected/quiet state as the
         * comparison baseline.  Zero is therefore a valid persisted RMS
         * value; negative and non-finite values still indicate corruption. */
        if(count>=SWEEP_MAX_POINTS || sscanf(line,"%lf,%lf",&freq,&ref)!=2 ||
           !isfinite(freq) || !isfinite(ref) || ref<0.0) { fclose(f); return; }
        double expected=sweep_frequency(count);
        if(fabs(freq-expected)>0.05) {
            fclose(f); return;
        }
        sweep.freq_mhz[count]=freq;
        sweep.reference_rms[count]=ref;
        count++;
    }
    fclose(f);
    if(count==sweep.points) {
        sweep.calibrated=1;
        sweep.reference_points=count;
        printf("SWEEP_REFERENCE_LOADED POINTS=%u PATH=%s\n",count,SWEEP_REFERENCE_PATH);
    }
}
static int sweep_pass_band(double *lo_mhz,double *hi_mhz) {
    if(!sweep.calibrated || !sweep.valid_points) return 0;
    int best_first=-1,best_last=-1,current_first=-1;
    for(unsigned i=0;i<sweep.valid_points;i++) {
        if(sweep.relative_db[i]>=sweep.pass_threshold_db) {
            if(current_first<0) current_first=(int)i;
        } else if(current_first>=0) {
            if(best_first<0 || (int)i-current_first>best_last-best_first+1) {
                best_first=current_first; best_last=(int)i-1;
            }
            current_first=-1;
        }
    }
    if(current_first>=0 && (best_first<0 || (int)sweep.valid_points-current_first>best_last-best_first+1)) {
        best_first=current_first; best_last=(int)sweep.valid_points-1;
    }
    if(best_first<0) return 0;
    if(lo_mhz) *lo_mhz=sweep.freq_mhz[best_first];
    if(hi_mhz) *hi_mhz=sweep.freq_mhz[best_last];
    return best_last-best_first+1;
}
static const char *sweep_result_label(void) {
    if(sweep.mode==2) {
        if(sweep.active) return "FLAT CAL RUNNING";
        if(sweep.completed && !sweep.error) return "FLATNESS SAVED";
    }
    if(sweep.mode==1) {
        if(sweep.active) return "CAL REF RUNNING";
        if(sweep.completed && !sweep.error) return "REFERENCE SAVED";
    }
    if(!sweep.calibrated && !sweep.valid_points) return "NO BASELINE";
    if(sweep.active) return "RUNNING";
    if(sweep.invalid_points) return "LOW_SIGNAL";
    if(sweep.valid_points && sweep.pass_points==sweep.valid_points) return "PASS";
    if(sweep.valid_points) return "FAIL";
    return "READY";
}
static void sweep_stop(void) {
    if(reflect_ps_exclusive()) {
        printf("SWEEP_STOP_REFUSED REFLECT_OWNS_DAC; USE GEN_OFF_OR_REFLECT_STOP\n");
        view_dirty=1; return;
    }
    if(sweep.active) gen_control_exec(0,0,0,0);
    sweep.active=0;
    view_dirty=1;
    printf("SWEEP_STOP POINTS=%u\n",sweep.valid_points);
}
static void sweep_begin(int mode) {
    if(reflect_ps_exclusive()) {
        printf("SWEEP_START_REFUSED REFLECT_OWNS_DAC\n");view_dirty=1;return;
    }
    int old_calibrated=sweep.calibrated;
    measurement_quality_mark_invalid("SWEEP_POINT_PENDING");
    double configured_start=sweep.start_mhz, configured_stop=sweep.stop_mhz;
    double configured_pass=sweep.pass_threshold_db;
    unsigned points=sweep.points?sweep.points:16;
    unsigned average_frames=sweep.average_frames?sweep.average_frames:4;
    unsigned max_retries=sweep.max_retries;
    int logarithmic=sweep.logarithmic;
    unsigned old_reference_points=sweep.reference_points;
    double old_reference[SWEEP_MAX_POINTS];
    double old_freq[SWEEP_MAX_POINTS];
    memcpy(old_reference,sweep.reference_rms,sizeof(old_reference));
    memcpy(old_freq,sweep.freq_mhz,sizeof(old_freq));
    memset(&sweep,0,sizeof(sweep));
    /* Preserve the loaded range and threshold across each run. */
    sweep.start_mhz=configured_start; sweep.stop_mhz=configured_stop;
    sweep.pass_threshold_db=configured_pass;
    sweep.points=points;
    sweep.average_frames=average_frames;
    sweep.max_retries=max_retries;
    sweep.logarithmic=logarithmic;
    sweep.point=0; sweep.settle_frames=4; sweep.active=1; sweep.mode=mode;
    sweep.calibrated=old_calibrated;
    sweep.reference_points=old_reference_points;
    memcpy(sweep.reference_rms,old_reference,sizeof(old_reference));
    memcpy(sweep.freq_mhz,old_freq,sizeof(old_freq));
    int freq_khz=(int)llround(sweep_frequency(0)*1000.0);
    sweep.active_drive_pct=mode==2?25.0:sweep_flat_drive_pct(sweep_frequency(0));
    if(SWEEP_CONTROL_EXEC(1,freq_khz,sweep.active_drive_pct)<0) {
        sweep.active=0; sweep.error=1;
        printf("SWEEP_START_ERROR DAC_CONTROL MODE=%s\n",sweep_mode_name(mode));
        return;
    }
    printf("SWEEP_START MODE=%s START=%.1fMHZ STOP=%.1fMHZ POINTS=%u SCALE=%s AVG=%u RETRIES=%u DRIVE=%.4fPCT FLAT=%d\n",
           sweep_mode_name(mode),sweep.start_mhz,sweep.stop_mhz,sweep.points,
           sweep.logarithmic?"LOG":"LINEAR",sweep.average_frames,sweep.max_retries,
           sweep.active_drive_pct,sweep_flatness.calibrated);
    view_dirty=1;
}
static void sweep_update(const struct metrics *v) {
    if(reflect_ps_exclusive()) return;
    if(!sweep.active || !v) return;
    if(sweep.settle_frames) { sweep.settle_frames--; return; }
    /* SWEEP measures amplitude response.  At the upper points the ADC
     * frequency estimator can lose lock even though the sampled amplitude is
     * usable, so frequency_valid must not gate a sweep point. */
    int low_signal=(v->hi-v->lo<32 || v->rms_ac<=0);
    if(low_signal) {
        /* A disconnected input must not hold the sweep forever.  Require
         * four consecutive low-signal frames (the same count as the normal
         * average) and then record an invalid point and advance. */
        sweep.reject_frames++;
        if(sweep.reject_frames<sweep.average_frames) return;
        if(sweep.retry_count<sweep.max_retries) {
            sweep.retry_count++;
            sweep.reject_frames=0;
            sweep.settle_frames=4;
            printf("SWEEP_RETRY POINT=%u/%u RETRY=%u/%u FREQ=%.3fMHZ\n",
                   sweep.point+1,sweep.points,sweep.retry_count,sweep.max_retries,
                   sweep_frequency(sweep.point));
            view_dirty=1;
            return;
        }
    } else {
        sweep.reject_frames=0;
        sweep.accum_clip_max=fmax(sweep.accum_clip_max,v->clipped_fraction);
        sweep.accum_rms+=v->rms_ac;
        sweep.accum_vpp+=(double)(v->hi-v->lo);
        sweep.accum_frames++;
        if(sweep.accum_frames<sweep.average_frames) return;
    }
    sweep.freq_mhz[sweep.point]=sweep_frequency(sweep.point);
    if(low_signal) {
        sweep.rms_code[sweep.point]=0.0;
        sweep.vpp_code[sweep.point]=0.0;
        sweep.invalid_points++;
        /* -60 dB is the display floor; it is safely below the -3 dB pass
         * threshold while avoiding -INFINITY in the LCD formatter. */
        if(sweep.mode==1 || sweep.mode==2) {
            /* CAL REF is user-authoritative: even a disconnected or
             * deliberately low-level state may be the requested baseline. */
            sweep.relative_db[sweep.point]=0.0;
        } else if(sweep.calibrated && sweep.reference_rms[sweep.point]<=32.0) {
            /* A near-zero user baseline cannot use a logarithmic ratio.
             * Compare absolute code distance instead. */
            double diff=fabs(sweep.rms_code[sweep.point]-sweep.reference_rms[sweep.point]);
            sweep.relative_db[sweep.point]=(diff<=32.0)?0.0:-60.0;
            if(sweep.relative_db[sweep.point]>=sweep.pass_threshold_db) sweep.pass_points++;
        } else {
            sweep.relative_db[sweep.point]=sweep.calibrated?-60.0:0.0;
        }
    } else {
        sweep.rms_code[sweep.point]=sweep.accum_rms/sweep.accum_frames;
        sweep.vpp_code[sweep.point]=sweep.accum_vpp/sweep.accum_frames;
        if(sweep.mode==1 || sweep.mode==2) {
            sweep.relative_db[sweep.point]=0.0;
        } else if(sweep.calibrated && sweep.reference_rms[sweep.point]<=32.0) {
            double diff=fabs(sweep.rms_code[sweep.point]-sweep.reference_rms[sweep.point]);
            sweep.relative_db[sweep.point]=(diff<=32.0)?0.0:-60.0;
            if(sweep.relative_db[sweep.point]>=sweep.pass_threshold_db) sweep.pass_points++;
        } else if(sweep.calibrated && sweep.reference_rms[sweep.point]>32.0) {
            double ratio=sweep.rms_code[sweep.point]/sweep.reference_rms[sweep.point];
            sweep.relative_db[sweep.point]=20.0*log10(ratio>1e-12?ratio:1e-12);
            if(sweep.relative_db[sweep.point]>=sweep.pass_threshold_db) sweep.pass_points++;
        }
    }
    if(low_signal) {
        printf("SWEEP_POINT INDEX=%u/%u FREQ=%.1fMHZ RMS=0.000 S21_DB=%.3f MODE=%s VALID=0 STATUS=LOW_SIGNAL\n",
               sweep.point+1,sweep.points,sweep.freq_mhz[sweep.point],
               sweep.relative_db[sweep.point],sweep_mode_name(sweep.mode));
    } else {
        printf("SWEEP_POINT INDEX=%u/%u FREQ=%.1fMHZ RMS=%.3f S21_DB=%.3f MODE=%s VALID=1\n",
               sweep.point+1,sweep.points,sweep.freq_mhz[sweep.point],
               sweep.rms_code[sweep.point],sweep.relative_db[sweep.point],
               sweep_mode_name(sweep.mode));
    }
    measurement_quality_update_sweep(v,sweep.point);
    sweep.accum_rms=sweep.accum_vpp=0; sweep.accum_frames=0; sweep.reject_frames=0;
    sweep.accum_clip_max=0;
    sweep.valid_points=sweep.point+1;
    /* CAL REF deliberately accepts every user-selected state, including
     * LOW_SIGNAL.  The saved reference is the user's definition of normal. */
    if(sweep.point+1>=sweep.points) {
        if(sweep.mode==1) {
            for(unsigned i=0;i<sweep.points;i++)
                sweep.reference_rms[i]=sweep.rms_code[i];
            if(sweep_save_reference()<0) {
                sweep.error=1;
                fprintf(stderr,"SWEEP_REFERENCE_SAVE_FAILED PATH=%s\n",SWEEP_REFERENCE_PATH);
            } else {
                sweep.calibrated=1;
                sweep.reference_points=sweep.points;
                printf("SWEEP_REFERENCE_SAVED POINTS=%u PATH=%s\n",sweep.points,SWEEP_REFERENCE_PATH);
            }
        } else if(sweep.mode==2) {
            if(sweep.invalid_points || sweep_build_flatness()<0) {
                sweep.error=1;
                fprintf(stderr,"SWEEP_FLATNESS_SAVE_FAILED PATH=%s\n",SWEEP_FLAT_PATH);
            } else {
                /* A reference captured with the previous source-amplitude
                 * profile is no longer comparable.  Require a fresh CAL REF
                 * rather than silently mixing two source calibrations. */
                sweep.calibrated=0;
                sweep.reference_points=0;
                memset(sweep.reference_rms,0,sizeof(sweep.reference_rms));
                if(unlink(SWEEP_REFERENCE_PATH)<0 && errno!=ENOENT)
                    fprintf(stderr,"SWEEP_REFERENCE_INVALIDATE_FAILED PATH=%s\n",SWEEP_REFERENCE_PATH);
                else
                    printf("SWEEP_REFERENCE_INVALIDATED REASON=FLATNESS_CHANGED\n");
            }
        }
        if(sweep_save_result()<0) {
            sweep.error=1;
            fprintf(stderr,"SWEEP_CSV_SAVE_FAILED PATH=%s\n",SWEEP_RESULT_PATH);
        }
        if(!sweep.error && sweep_save_scalar()<0)
            fprintf(stderr,"SWEEP_SCALAR_SAVE_FAILED PATH=%s\n",SWEEP_SCALAR_PATH);
        sweep_stop(); sweep.completed=sweep.error?0:1;
        if(sweep.completed && sweep.valid_points) {
            sweep_cursor_point[0]=0;
            sweep_cursor_point[1]=sweep.valid_points-1;
            sweep_cursor_selected=0;
        }
        if(!sweep.error) diag_export_report();
        return;
    }
    sweep.point++;
    sweep.retry_count=0;
    sweep.settle_frames=4;
    int freq_khz=(int)llround(sweep_frequency(sweep.point)*1000.0);
    sweep.active_drive_pct=sweep.mode==2?25.0:sweep_flat_drive_pct(sweep_frequency(sweep.point));
    if(SWEEP_CONTROL_EXEC(1,freq_khz,sweep.active_drive_pct)<0) {
        sweep.error=1; sweep_stop();
    }
    view_dirty=1;
}

/* Single-ADC scalar reflection prototype.  The directional coupler is used in
 * reverse so IN1 sees the reflected sample.  OPEN/SHORT/LOAD are acquired in
 * separate sweeps; without a simultaneous phase reference these values are
 * deliberately labelled approximate rather than complex S11. */
static unsigned reflect_points(void) { return sweep.points?sweep.points:16; }
static double reflect_start_mhz(void) {
    double x=sweep.start_mhz;
    return x>=5.0 && x<SWEEP_MAX_MHZ?x:10.0;
}
static double reflect_stop_mhz(void) {
    double start=reflect_start_mhz(),x=sweep.stop_mhz;
    return x>start && x<=SWEEP_MAX_MHZ?x:SWEEP_MAX_MHZ;
}
static double reflect_frequency(unsigned point) {
    unsigned points=reflect_points();
    if(points<=1 || point>=points) return reflect_start_mhz();
    double start=reflect_start_mhz(),stop=reflect_stop_mhz();
    double t=(double)point/(double)(points-1);
    if(sweep.logarithmic) return exp(log(start)+t*(log(stop)-log(start)));
    return start+(stop-start)*t;
}
static const char *reflect_mode_name(int mode) {
    return mode==REFLECT_CAL_LOAD?"CAL LOAD":mode==REFLECT_CAL_OPEN?"CAL OPEN":
           mode==REFLECT_CAL_SHORT?"CAL SHORT":"MEASURE";
}
static int reflect_ready(void) {
    return reflect.have_load && reflect.have_open && reflect.have_short;
}
static void reflect_invalidate_calibration(void) {
    reflect.have_load=reflect.have_open=reflect.have_short=0;
    reflect.completed=reflect.error=0; reflect.valid_points=reflect.invalid_points=0;
    memset(reflect.load_rms,0,sizeof(reflect.load_rms));
    memset(reflect.open_rms,0,sizeof(reflect.open_rms));
    memset(reflect.short_rms,0,sizeof(reflect.short_rms));
}
#include "reflect_trend_quality.h"
static int reflect_sync_directory(void) {
    int fd=open(REFLECT_STORAGE_DIR,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(fd<0) return -1;
    int rc=fsync(fd);if(close(fd)<0)rc=-1;return rc;
}
static int reflect_save_calibration(void) {
    char tmp[PATH_MAX];
    int n=snprintf(tmp,sizeof(tmp),"%s.tmp.%ld",REFLECT_CAL_PATH,(long)getpid());
    if(n<0 || (size_t)n>=sizeof(tmp)) return -1;
    FILE *f=fopen(tmp,"w"); if(!f) return -1;
    unsigned points=reflect_points();
    int ok=fprintf(f,"# relative amplitude trend calibration v2; not OSL/S11\n")>0 &&
        fprintf(f,"# start_mhz=%.6f stop_mhz=%.6f points=%u scale=%s have_load=%d have_open=%d have_short=%d drive_pct=10\n",
                reflect_start_mhz(),reflect_stop_mhz(),points,sweep.logarithmic?"log":"linear",
                reflect.have_load,reflect.have_open,reflect.have_short)>0 &&
        fprintf(f,"frequency_mhz,load_rms_code,open_rms_code,short_rms_code\n")>0;
    for(unsigned i=0;ok && i<points;i++)
        ok=fprintf(f,"%.6f,%.9f,%.9f,%.9f\n",reflect_frequency(i),reflect.load_rms[i],
                   reflect.open_rms[i],reflect.short_rms[i])>0;
    if(!ok || fflush(f)!=0 || fsync(fileno(f))<0) {
        fclose(f); unlink(tmp); return -1;
    }
    if(fclose(f)!=0 || rename(tmp,REFLECT_CAL_PATH)<0) { unlink(tmp); return -1; }
    if(reflect_sync_directory()<0) return -1;
    printf("REFLECT_CAL_SAVED PATH=%s L=%d O=%d S=%d\n",REFLECT_CAL_PATH,
           reflect.have_load,reflect.have_open,reflect.have_short);
    return 0;
}
static void reflect_load_calibration(void) {
    FILE *f=fopen(REFLECT_CAL_PATH,"r"); if(!f) return;
    char line[256],scale[16];unsigned count=0,points=0;
    double start=0,stop=0,drive=0;
    int l=0,o=0,s=0,end=0,valid=1;
    double freqs[SWEEP_MAX_POINTS],loads[SWEEP_MAX_POINTS],opens[SWEEP_MAX_POINTS],shorts[SWEEP_MAX_POINTS];
    if(!fgets(line,sizeof(line),f)||strcmp(line,"# relative amplitude trend calibration v2; not OSL/S11\n")||
       !fgets(line,sizeof(line),f)||
       sscanf(line,"# start_mhz=%lf stop_mhz=%lf points=%u scale=%15s have_load=%d have_open=%d have_short=%d drive_pct=%lf %n",
              &start,&stop,&points,scale,&l,&o,&s,&drive,&end)!=8||!end||line[end]||
       !isfinite(start)||!isfinite(stop)||!isfinite(drive)||drive!=10||
       points<2||points>SWEEP_MAX_POINTS||points!=reflect_points()||
       fabs(start-reflect_start_mhz())>1e-6||fabs(stop-reflect_stop_mhz())>1e-6||
       strcmp(scale,sweep.logarithmic?"log":"linear")||
       (l!=0&&l!=1)||(o!=0&&o!=1)||(s!=0&&s!=1)||
       !fgets(line,sizeof(line),f)||strcmp(line,"frequency_mhz,load_rms_code,open_rms_code,short_rms_code\n")) valid=0;
    while(valid&&fgets(line,sizeof(line),f)) {
        double freq,load,open,shorted;
        end=0;
        if(count>=points || sscanf(line,"%lf,%lf,%lf,%lf %n",&freq,&load,&open,&shorted,&end)!=4 || !end || line[end] ||
           !isfinite(freq) || !isfinite(load) || !isfinite(open) || !isfinite(shorted) ||
           load<0 || open<0 || shorted<0 ||fabs(freq-reflect_frequency(count))>1e-6) { valid=0; break; }
        freqs[count]=freq;loads[count]=load;opens[count]=open;shorts[count]=shorted;
        count++;
    }
    if(ferror(f))valid=0;
    if(fclose(f)!=0)valid=0;
    if(valid&&count==points) {
        memcpy(reflect.freq_mhz,freqs,count*sizeof(double));memcpy(reflect.load_rms,loads,count*sizeof(double));
        memcpy(reflect.open_rms,opens,count*sizeof(double));memcpy(reflect.short_rms,shorts,count*sizeof(double));
        reflect.have_load=l;reflect.have_open=o;reflect.have_short=s;
        printf("REFLECT_CAL_LOADED POINTS=%u L=%d O=%d S=%d\n",count,
               reflect.have_load,reflect.have_open,reflect.have_short);
    } else printf("REFLECT_CAL_REJECT FORMAT_OR_CONDITIONS; PREVIOUS_STATE_PRESERVED\n");
}
static int reflect_save_result(void) {
    char tmp[PATH_MAX];
    int n=snprintf(tmp,sizeof(tmp),"%s.tmp.%ld",REFLECT_RESULT_PATH,(long)getpid());
    if(n<0 || (size_t)n>=sizeof(tmp)) return -1;
    FILE *f=fopen(tmp,"w"); if(!f) return -1;
    int ok=fprintf(f,"# single-adc relative reflection trend v2 (no complex phase; not S11/VSWR/DTF)\n")>0 &&
        fprintf(f,"frequency_mhz,measured_rms_code,relative_reflection_magnitude,quality\n")>0;
    for(unsigned i=0;ok && i<reflect.valid_points;i++) {
        const char *status=reflect_trend_quality(reflect.measured_rms[i],reflect.gamma[i]);
        ok=fprintf(f,"%.6f,%.9f,%.9f,%s\n",reflect.freq_mhz[i],
                   reflect.measured_rms[i],reflect.gamma[i],status)>0;
    }
    if(!ok || fflush(f)!=0 || fsync(fileno(f))<0) {
        fclose(f); unlink(tmp); return -1;
    }
    if(fclose(f)!=0 || rename(tmp,REFLECT_RESULT_PATH)<0) { unlink(tmp); return -1; }
    if(reflect_sync_directory()<0) return -1;
    printf("REFLECT_RESULT_SAVED PATH=%s POINTS=%u\n",REFLECT_RESULT_PATH,reflect.valid_points);
    return 0;
}
static void reflect_stop(void) {
    if(reflect_ps_exclusive()) return;
    if(reflect.active || reflect.off_unconfirmed) {
        reflect.off_unconfirmed=gen_control_exec(0,0,0,0)<0;
        if(reflect.off_unconfirmed) {
            reflect.error=1;reflect.completed=0;
            printf("REFLECT_OFF_UNCONFIRMED RESTART_BLOCKED\n");
        }
    }
    reflect.active=0; view_dirty=1;
}
static void reflect_begin(int mode) {
    if(reflect_ps_exclusive()) return;
    if(sweep.active) return;
    if(reflect.active || reflect.off_unconfirmed) {
        printf("REFLECT_START_REJECT ACTIVE_OR_OFF_UNCONFIRMED\n");return;
    }
    if(mode==REFLECT_MEASURE && !reflect_ready()) {
        printf("REFLECT_START_REJECT CAL_REQUIRED L=%d O=%d S=%d\n",
               reflect.have_load,reflect.have_open,reflect.have_short);
        return;
    }
    int have_load=reflect.have_load,have_open=reflect.have_open,have_short=reflect.have_short;
    double load[SWEEP_MAX_POINTS],open[SWEEP_MAX_POINTS],shorted[SWEEP_MAX_POINTS];
    memcpy(load,reflect.load_rms,sizeof(load)); memcpy(open,reflect.open_rms,sizeof(open));
    memcpy(shorted,reflect.short_rms,sizeof(shorted));
    memset(&reflect,0,sizeof(reflect));
    reflect.have_load=have_load; reflect.have_open=have_open; reflect.have_short=have_short;
    memcpy(reflect.load_rms,load,sizeof(load)); memcpy(reflect.open_rms,open,sizeof(open));
    memcpy(reflect.short_rms,shorted,sizeof(shorted));
    if(mode==REFLECT_CAL_LOAD)reflect.have_load=0;
    else if(mode==REFLECT_CAL_OPEN)reflect.have_open=0;
    else if(mode==REFLECT_CAL_SHORT)reflect.have_short=0;
    reflect.active=1; reflect.mode=mode; reflect.settle_frames=4;
    int freq_khz=(int)llround(reflect_frequency(0)*1000.0);
    if(gen_control_exec(1,freq_khz,0,10)<0) { reflect.error=1; reflect_stop(); return; }
    printf("REFLECT_START MODE=%s START=%.3f STOP=%.3f POINTS=%u AMP=10%%\n",
           reflect_mode_name(mode),reflect_start_mhz(),reflect_stop_mhz(),reflect_points());
    view_dirty=1;
}
static void reflect_update(const struct metrics *v) {
    if(reflect_ps_exclusive()) return;
    if(!reflect.active || !v) return;
    if(reflect.settle_frames) { reflect.settle_frames--; return; }
    if(!isfinite(v->rms_ac) || v->rail) {
        reflect.reject_frames++;
        if(reflect.reject_frames<(sweep.average_frames?sweep.average_frames:4)) return;
        if(reflect.retry_count<sweep.max_retries) {
            reflect.retry_count++; reflect.reject_frames=0; reflect.settle_frames=4;
            reflect.accum_rms=reflect.accum_vpp=0;reflect.accum_frames=0;return;
        }
        reflect.error=1; reflect_stop(); return;
    }
    reflect.accum_rms+=v->rms_ac; reflect.accum_vpp+=(double)(v->hi-v->lo);
    reflect.accum_frames++;
    unsigned average=sweep.average_frames?sweep.average_frames:4;
    if(reflect.accum_frames<average) return;
    unsigned p=reflect.point; double rms=reflect.accum_rms/reflect.accum_frames;
    reflect.freq_mhz[p]=reflect_frequency(p);
    if(reflect.mode==REFLECT_CAL_LOAD) reflect.load_rms[p]=rms;
    else if(reflect.mode==REFLECT_CAL_OPEN) reflect.open_rms[p]=rms;
    else if(reflect.mode==REFLECT_CAL_SHORT) reflect.short_rms[p]=rms;
    else {
        reflect.measured_rms[p]=rms;
        if(rms<REFLECT_LOW_SIGNAL_RMS) {
            reflect.low_signal_points++;
            reflect.gamma[p]=NAN;
        } else {
            /* Keep the normalized amplitude only as a relative trend.  The
             * single ADC has no phase reference, so this is deliberately not
             * rendered or exported as RL, VSWR, or complex S11. */
            reflect.gamma[p]=reflect_relative(reflect.load_rms[p],reflect.open_rms[p],reflect.short_rms[p],rms);
        }
        if(strcmp(reflect_trend_quality(rms,reflect.gamma[p]),"VALID_TREND")) reflect.invalid_points++;
    }
    reflect.valid_points=p+1;
    printf("REFLECT_POINT MODE=%s INDEX=%u/%u FREQ=%.3f RMS=%.3f\n",
           reflect_mode_name(reflect.mode),p+1,reflect_points(),reflect.freq_mhz[p],rms);
    reflect.accum_rms=reflect.accum_vpp=0; reflect.accum_frames=reflect.reject_frames=0;
    if(p+1>=reflect_points()) {
        if(reflect.mode==REFLECT_CAL_LOAD) reflect.have_load=1;
        else if(reflect.mode==REFLECT_CAL_OPEN) reflect.have_open=1;
        else if(reflect.mode==REFLECT_CAL_SHORT) reflect.have_short=1;
        if(reflect.mode==REFLECT_MEASURE) {
            if(reflect_save_result()<0) reflect.error=1;
        } else if(reflect_save_calibration()<0) reflect.error=1;
        reflect_stop(); reflect.completed=!reflect.error; return;
    }
    reflect.point++; reflect.retry_count=0; reflect.settle_frames=4;
    int freq_khz=(int)llround(reflect_frequency(reflect.point)*1000.0);
    if(gen_control_exec(1,freq_khz,0,10)<0) { reflect.error=1; reflect_stop(); }
    view_dirty=1;
}
struct spectrum { double db[N/2+1],code[N/2+1],peak_mhz,peak_code,spur_db; int peak_bin,peak_valid; };
struct hf_model { double mhz,mean,amp,phase; int valid; };
static struct spectrum spectrum_cache;
static double spectrum_peak_hold[N/2+1];
static int spectrum_peak_hold_valid;
static unsigned spectrum_history_frames;
#define SPECTRUM_BIN_COUNT (N/2+1)
#define SPECTRUM_MAX_MHZ 200.0
#define SPECTRUM_CSV_PATH DIAG_STORAGE_DIR "/spectrum_latest.csv"
/* The production FFT already covers 0..Fs/2.  These controls only select the
 * displayed window and post-process complete frames; they do not change the
 * ADC capture or claim a narrower physical RBW. */
static double spectrum_start_mhz;
static double spectrum_stop_mhz=SPECTRUM_MAX_MHZ;
static unsigned spectrum_span_preset;
static int spectrum_marker_bin[2]={-1,-1};
static unsigned spectrum_marker_selected;
static unsigned spectrum_average_target=4;
static unsigned spectrum_average_count;
static unsigned spectrum_average_next_slot;
static double spectrum_average_power[8][SPECTRUM_BIN_COUNT];
static double spectrum_average_db[SPECTRUM_BIN_COUNT];
static int spectrum_average_valid;
#define SPECTRUM_WATERFALL_ROWS 32
static unsigned char spectrum_waterfall[SPECTRUM_WATERFALL_ROWS][N/2+1];
static unsigned spectrum_waterfall_head;
static uint32_t spectrum_cache_token;
static int spectrum_cache_valid;
static unsigned spectrum_compute_count;
/* An export result must remain visible on the LCD; serial logs alone do not
 * tell the operator whether the CSV was durably saved. */
enum spectrum_csv_result { SPECTRUM_CSV_NONE, SPECTRUM_CSV_SAVED,
                           SPECTRUM_CSV_FAILED, SPECTRUM_CSV_NO_FRAME };
static enum spectrum_csv_result spectrum_csv_status;
static void spectrum_trace_reset(void) {
    memset(spectrum_peak_hold,0,sizeof(spectrum_peak_hold));
    memset(spectrum_waterfall,0,sizeof(spectrum_waterfall));
    memset(spectrum_average_power,0,sizeof(spectrum_average_power));
    memset(spectrum_average_db,0,sizeof(spectrum_average_db));
    spectrum_peak_hold_valid=0; spectrum_history_frames=0;
    spectrum_average_count=0; spectrum_average_next_slot=0;
    spectrum_average_valid=0;
    spectrum_waterfall_head=0;
    spectrum_csv_status=SPECTRUM_CSV_NONE;
}
static void spectrum_history_reset(void) {
    spectrum_trace_reset();
    spectrum_start_mhz=0.0; spectrum_stop_mhz=SPECTRUM_MAX_MHZ;
    spectrum_span_preset=0; spectrum_marker_bin[0]=spectrum_marker_bin[1]=-1;
    spectrum_marker_selected=0;
}
static void spectrum_span_apply(unsigned preset) {
    static const double starts[]={0.0,0.0,0.0,50.0,100.0};
    static const double stops []={200.0,100.0,50.0,150.0,200.0};
    spectrum_span_preset=preset%5u;
    spectrum_start_mhz=starts[spectrum_span_preset];
    spectrum_stop_mhz=stops[spectrum_span_preset];
}
static void spectrum_span_next(void) { spectrum_span_apply(spectrum_span_preset+1u); }
static void spectrum_average_set(unsigned target) {
    if(target!=1u && target!=4u && target!=8u) target=4u;
    spectrum_average_target=target;
    memset(spectrum_average_power,0,sizeof(spectrum_average_power));
    memset(spectrum_average_db,0,sizeof(spectrum_average_db));
    spectrum_average_count=0; spectrum_average_next_slot=0;
    spectrum_average_valid=0;
}
static void spectrum_average_next(void) {
    unsigned next=spectrum_average_target==1u?4u:spectrum_average_target==4u?8u:1u;
    spectrum_average_set(next);
}
static int spectrum_display_range(int *k0,int *k1) {
    if(!k0 || !k1 || !(spectrum_stop_mhz>spectrum_start_mhz) ||
       spectrum_start_mhz<0.0 || spectrum_stop_mhz>SPECTRUM_MAX_MHZ) return -1;
    int lo=(int)floor(spectrum_start_mhz*1e6/FFT_BIN_HZ+0.5);
    int hi=(int)floor(spectrum_stop_mhz*1e6/FFT_BIN_HZ+0.5);
    if(lo<0) lo=0;
    if(hi>N/2) hi=N/2;
    if(hi<=lo) return -1;
    *k0=lo; *k1=hi; return 0;
}
static double spectrum_display_db(const struct spectrum *s,int k) {
    if(k<0 || k>N/2 || !s) return -100.0;
    return spectrum_average_valid?spectrum_average_db[k]:s->db[k];
}
static double spectrum_marker_mhz(int marker) {
    int k=(marker>=0 && marker<2)?spectrum_marker_bin[marker]:-1;
    return k>=0 ? k*FFT_BIN_HZ/1e6 : NAN;
}
static int spectrum_marker_set_from_x(int marker,int x,int left,int right) {
    int k0,k1;
    if(marker<0 || marker>1 || right<=left || spectrum_display_range(&k0,&k1)<0) return -1;
    if(x<left) x=left;
    if(x>right) x=right;
    int k=k0+(int)((int64_t)(x-left)*(k1-k0)+(right-left)/2)/(right-left);
    if(k<k0) k=k0;
    if(k>k1) k=k1;
    spectrum_marker_bin[marker]=k; return k;
}
static int spectrum_export_csv(void) {
    char tmp[PATH_MAX];
    int n=snprintf(tmp,sizeof(tmp),"%s.tmp.%ld",SPECTRUM_CSV_PATH,(long)getpid());
    if(!spectrum_cache_valid || n<=0 || n>=(int)sizeof(tmp)) return -1;
    FILE *f=fopen(tmp,"w"); if(!f) return -1;
    int ok=0,renamed=0;
    if(fprintf(f,"# sample_rate_hz,%.0f\n# fft_size,%d\n# window,HANN\n# bin_spacing_hz,%.6f\n# hann_enbw_hz,%.6f\n# start_mhz,%.6f\n# stop_mhz,%.6f\n# average_target,%u\n# average_complete,%d\n# peak_hold_valid,%d\n# marker1_mhz,%.9f\n# marker2_mhz,%.9f\nfrequency_hz,raw_dbfs,display_dbfs,peak_hold_dbfs\n",
                ADC_SAMPLE_RATE_HZ,N,FFT_BIN_HZ,FFT_HANN_ENBW_HZ,
                spectrum_start_mhz,spectrum_stop_mhz,spectrum_average_target,
                spectrum_average_valid,spectrum_peak_hold_valid,
                spectrum_marker_mhz(0),spectrum_marker_mhz(1))<0) goto done;
    for(int k=0;k<=N/2;k++) {
        int rc=spectrum_peak_hold_valid ?
            fprintf(f,"%.6f,%.6f,%.6f,%.6f\n",k*FFT_BIN_HZ,spectrum_cache.db[k],
                    spectrum_display_db(&spectrum_cache,k),spectrum_peak_hold[k]) :
            fprintf(f,"%.6f,%.6f,%.6f,\n",k*FFT_BIN_HZ,spectrum_cache.db[k],
                    spectrum_display_db(&spectrum_cache,k));
        if(rc<0) goto done;
    }
    if(fflush(f)<0 || fsync(fileno(f))<0) goto done;
    ok=1;
done:
    if(fclose(f)<0) ok=0;
    if(ok) {
        if(rename(tmp,SPECTRUM_CSV_PATH)<0) ok=0;
        else { renamed=1; if(sync_parent_directory(SPECTRUM_CSV_PATH)<0) ok=0; }
    }
    if(!renamed) unlink(tmp);
    if(ok) printf("SPECTRUM_CSV_SAVED PATH=%s POINTS=%d AVG_TARGET=%u\n",SPECTRUM_CSV_PATH,N/2+1,spectrum_average_target);
    else fprintf(stderr,"SPECTRUM_CSV_SAVE_FAILED PATH=%s RENAMED=%d DURABILITY_UNCONFIRMED=%d\n",
                 SPECTRUM_CSV_PATH,renamed,renamed);
    return ok?0:-1;
}
static struct hf_model hf_smooth;
static uint32_t hf_smooth_token;
/* Hysteretic single-tone lock for the display fit.  Measured on the loopback,
 * a sine keeps its strongest out-of-skirt component below -30 dB while square
 * and triangle wavetables sit at -9..-19 dB, so the two are easy to separate.
 * Hysteresis keeps a marginal frame (50 MHz once measured -25.1 dB) from
 * dropping a working lock. */
static int hf_tone_lock;
/* Pure decision so it can be tested directly: engage below -30 dB, release
 * only above -24 dB.  Returns 1 when a single tone is locked. */
static int hf_tone_gate(double spur_db) {
    if(!hf_tone_lock && spur_db<=-30.0) hf_tone_lock=1;
    else if(hf_tone_lock && spur_db>-24.0) hf_tone_lock=0;
    return hf_tone_lock;
}
static int measure(const uint32_t *a,struct metrics *v) {
    v->lo=16383; v->hi=0; v->mean=0; unsigned clipped=0;
    for(int i=0;i<N;i++) {
        if(a[i]>16383) return -1;
        if(a[i]<v->lo) v->lo=a[i];
        if(a[i]>v->hi) v->hi=a[i];
        if(a[i]<=4 || a[i]>=16379) clipped++;
        v->mean+=a[i];
    }
    v->mean/=N; double sq=0,first=0,last=0,period_sum=0,period_sq=0; int crossings=0;
    for(int i=0;i<N;i++) {
        double d=a[i]-v->mean; sq+=d*d;
    }
    v->rms_ac=sqrt(sq/N);
    v->low_signal=v->hi-v->lo<32;
    v->rail=v->lo<=4 || v->hi>=16379;
    v->clipped_fraction=(double)clipped/(double)N;
    /* Hysteretic rising edges suppress multiple noise crossings near mean.
     * Raw ADC samples and VPP/RMS are unchanged; only FREQ detection differs. */
    double hysteresis=fmax(16.0,(v->hi-v->lo)*0.08);
    double low=v->mean-hysteresis,high=v->mean+hysteresis;
    int armed=0;
    for(int i=0;i<N;i++) {
        if(a[i]<=low) armed=1;
        if(armed && i && a[i-1]<high && a[i]>=high) {
            double p=i-1+(high-a[i-1])/(a[i]-a[i-1]);
            if(!crossings) first=p;
            else { double period=p-last; period_sum+=period; period_sq+=period*period; }
            last=p; crossings++;
            armed=0;
        }
    }
    double avg=crossings>=3?period_sum/(crossings-1):0;
    double variance=crossings>=3?period_sq/(crossings-1)-avg*avg:0;
    v->freq_valid=crossings>=3 && !v->low_signal && !v->rail && avg>=4 && variance<=avg*avg*0.01;
    v->mhz=v->freq_valid ? (crossings-1)*400.0/(last-first) : 0;
    v->period_ns=NAN; v->jitter_rms_ns=NAN; v->jitter_pp_ns=NAN;
    struct signal_timing timing;
    int timing_rc685=signal_timing_measure(a,N,ADC_SAMPLE_RATE_HZ,&timing);
    /* Preserve existing numeric fields/consumers; keep the result metadata so
     * ordinary SCOPE cannot silently discard an explicit fit rejection. */
    v->timing_valid685=timing_rc685==0 && timing.valid;
    v->timing_reason685=timing.reason;
    if(v->timing_valid685) {
        v->period_ns=timing.period_ns; v->jitter_rms_ns=timing.jitter_rms_ns;
        v->jitter_pp_ns=timing.jitter_pp_ns;
    }
    return 0;
}
static void measurement_quality_mark_invalid(const char *reason) {
    memset(&measurement_quality_cache,0,sizeof measurement_quality_cache);
    measurement_quality_cache.label=MQ_INVALID;
    snprintf(measurement_quality_cache.reason,sizeof measurement_quality_cache.reason,
             "%s",reason?reason:"QUALITY_EVALUATION_ERROR");
    measurement_quality_cache_valid=1;
    measurement_quality_last_timestamp_ms=0;
}
static void measurement_quality_store(const struct measurement_quality_input *q) {
    measurement_quality_last_input=*q;
    measurement_quality_last_timestamp_ms=(uint64_t)llround(seconds()*1000.0);
}
static void measurement_quality_refresh_age(void) {
    if(!measurement_quality_cache_valid || !measurement_quality_last_timestamp_ms) return;
    uint64_t now=(uint64_t)llround(seconds()*1000.0);
    struct measurement_quality_input q=measurement_quality_last_input;
    q.age_ms=now>=measurement_quality_last_timestamp_ms ?
              now-measurement_quality_last_timestamp_ms : 0;
    measurement_quality_cfg=measurement_quality_default();
    if(measurement_quality_evaluate(&measurement_quality_cfg,&q,
                                    &measurement_quality_cache)<0)
        measurement_quality_mark_invalid("QUALITY_EVALUATION_ERROR");
}
/* Live MONITOR quality uses the locked, completed RMS baseline.  A frame
 * without that baseline remains INVALID; we never treat an event severity or
 * an uncalibrated amplitude as a dB measurement. */
static void measurement_quality_update_frame(const struct metrics *v) {
    if(!v) { measurement_quality_mark_invalid("NO_FRAME"); return; }
    measurement_quality_cfg=measurement_quality_default();
    struct measurement_quality_input q={0};
    q.rms=v->rms_ac;
    q.clipped_fraction=v->clipped_fraction;
    q.frequency_mhz=v->freq_valid?v->mhz:NAN;
    q.age_ms=0;
    q.baseline_valid=0;
    q.delta_db=0.0;
    if(!diag_baseline_learning &&
       (diag_monitor_state.state==DIAG_MONITOR_NORMAL ||
        diag_monitor_state.state==DIAG_MONITOR_FAULT) &&
       diag_monitor_state.baseline_count>=
       diag_monitor_state.cfg.baseline_frames &&
       isfinite(diag_monitor_state.baseline_rms) &&
       diag_monitor_state.baseline_rms>0.0 && v->rms_ac>0.0) {
        q.baseline_valid=1;
        q.delta_db=20.0*log10(v->rms_ac/diag_monitor_state.baseline_rms);
    }
    if(measurement_quality_evaluate(&measurement_quality_cfg,&q,
                                    &measurement_quality_cache)<0)
        measurement_quality_mark_invalid("QUALITY_EVALUATION_ERROR");
    else { measurement_quality_cache_valid=1; measurement_quality_store(&q); }
}
/* SWEEP quality is updated only after a point has been fully averaged and
 * assigned a relative value.  CAL REF and uncalibrated MEASURE therefore
 * remain BASELINE_MISSING/INVALID by construction. */
static void measurement_quality_update_sweep(const struct metrics *v,
                                             unsigned point) {
    if(!v || point>=SWEEP_MAX_POINTS) {
        measurement_quality_mark_invalid("NO_SWEEP_POINT"); return;
    }
    measurement_quality_cfg=measurement_quality_default();
    struct measurement_quality_input q={0};
    q.rms=sweep.rms_code[point];
    q.clipped_fraction=fmax(sweep.accum_clip_max,v->clipped_fraction);
    q.frequency_mhz=sweep.freq_mhz[point];
    q.age_ms=0;
    q.baseline_valid=(sweep.mode==0 && sweep.calibrated &&
                      isfinite(sweep.reference_rms[point]) &&
                      sweep.reference_rms[point]>32.0 &&
                      isfinite(sweep.relative_db[point]));
    q.delta_db=q.baseline_valid?sweep.relative_db[point]:0.0;
    if(measurement_quality_evaluate(&measurement_quality_cfg,&q,
                                    &measurement_quality_cache)<0)
        measurement_quality_mark_invalid("QUALITY_EVALUATION_ERROR");
    else { measurement_quality_cache_valid=1; measurement_quality_store(&q); }
}
/* AUTO SET picks the timebase that puts about four periods across the ten
 * horizontal divisions and switches the vertical scale to automatic. The
 * nearest-detent choice is made in log space because the timebase steps are
 * a 1-2-5 sequence.  The 10 ns/div floor is still forty real 400 MSPS
 * samples across the plot, so it improves high-frequency inspection without
 * inventing interpolated acquisition data. */
static struct metrics latest_metrics;
static int latest_metrics_valid;
static struct signal_trend signal_frame_history;
static const char *diag_quality_label(const struct metrics *v,int available) {
    if(!available) return "NO DATA";
    if(v->rail) return "SATURATED";
    if(v->low_signal) return "LOW SIGNAL";
    if(!v->freq_valid) return "FREQ UNRELIABLE";
    return "VALID";
}
static unsigned autoset_count;
static int autoset_ns_div(double mhz) {
    if(!(mhz>0)) return 0;
    double target=1000.0/mhz*0.4;
    int best=view_times[0]; double best_err=1e18;
    for(int i=0;i<VIEW_TIME_COUNT;i++) {
        double err=fabs(log(view_times[i]/target));
        if(err<best_err) { best_err=err; best=view_times[i]; }
    }
    return best;
}
static struct view_config autoset_target(int *have_signal) {
    int have_freq=latest_metrics_valid && latest_metrics.freq_valid && latest_metrics.mhz>0;
    struct view_config next=view;
    if(have_freq) next.ns=autoset_ns_div(latest_metrics.mhz);
    next.codes=0; next.offset=0;
    if(have_signal) *have_signal=have_freq;
    return next;
}
static void autoset(void) {
    int have_freq=0;
    struct view_config next=autoset_target(&have_freq);
    if(!valid_view(next)) return;
    save_view(next); autoset_count++;
    if(have_freq) {
        double period_ns=1000.0/latest_metrics.mhz;
        printf("AUTO_SET APPLIED NS_DIV=%d CODE_DIV=AUTO OFFSET=0 FREQ_MHZ=%.6f VPP_CODE=%u PERIODS_ON_SCREEN=%.2f\n",
               next.ns,latest_metrics.mhz,latest_metrics.hi-latest_metrics.lo,next.ns*10/period_ns);
    } else {
        printf("AUTO_SET NO_VALID_FREQUENCY NS_DIV=%d CODE_DIV=AUTO OFFSET=0; APPLY A SIGNAL THEN RETRY\n",
               next.ns);
    }
}
static int spectrum_measure(const uint32_t *a,struct spectrum *s) {
    static double window[N],window_sum;
    if(window_sum==0) for(int i=0;i<N;i++) {
        window[i]=0.5-0.5*cos(2.0*3.141592653589793*i/(N-1));
        window_sum+=window[i];
    }
    /* Remove the true mean, not a hard-wired 8192.  The ADC mid-scale is only
     * nominal: a healthy frame measures 8227 instead of 8192, and the 35-code
     * residual used to leak through the Hann main lobe into bins 1-2, where it
     * looked like a -27 dB "spur" and permanently blocked the HF-fit gate on
     * 20-50 MHz sine.  Subtracting the real mean drops that leakage to noise. */
    double mean=0;
    for(int i=0;i<N;i++) mean+=a[i];
    mean/=N;
    double re[N],im[N];
    for(int i=0;i<N;i++) {
        re[i]=((double)a[i]-mean)*window[i]; im[i]=0;
    }
    for(int i=1,j=0;i<N;i++) {
        int bit=N>>1;
        for(;j&bit;bit>>=1) j^=bit;
        j^=bit;
        if(i<j) { double q=re[i]; re[i]=re[j]; re[j]=q; q=im[i]; im[i]=im[j]; im[j]=q; }
    }
    for(int len=2;len<=N;len<<=1) {
        double angle=-2.0*3.141592653589793/len,wr0=cos(angle),wi0=sin(angle);
        for(int base=0;base<N;base+=len) {
            double wr=1,wi=0;
            for(int j=0;j<len/2;j++) {
                int p=base+j,q=p+len/2;
                double tr=wr*re[q]-wi*im[q],ti=wr*im[q]+wi*re[q];
                re[q]=re[p]-tr; im[q]=im[p]-ti; re[p]+=tr; im[p]+=ti;
                double nwr=wr*wr0-wi*wi0; wi=wr*wi0+wi*wr0; wr=nwr;
            }
        }
    }
    s->peak_bin=1; s->peak_code=0;
    for(int k=0;k<=N/2;k++) {
        double code=(k==0 || k==N/2 ? 1.0:2.0)*hypot(re[k],im[k])/window_sum;
        s->code[k]=code;
        s->db[k]=20.0*log10(code/8191.0+1e-12);
        if(s->db[k]<-100) s->db[k]=-100;
        if(k && code>s->peak_code) { s->peak_code=code; s->peak_bin=k; }
    }
    /* Rolling power average.  Averaging |X|^2 is appropriate for a noise
     * floor; averaging dB values would bias it low.  The raw frame still
     * drives peak and RF quality metrics. */
    unsigned slot=spectrum_average_next_slot;
    for(int k=0;k<=N/2;k++) {
        double normalized=s->code[k]/8191.0;
        spectrum_average_power[slot][k]=normalized*normalized;
    }
    spectrum_average_next_slot=(slot+1u)%spectrum_average_target;
    if(spectrum_average_count<spectrum_average_target) spectrum_average_count++;
    for(int k=0;k<=N/2;k++) {
        double p=0.0;
        for(unsigned j=0;j<spectrum_average_count;j++) p+=spectrum_average_power[j][k];
        p/=(double)spectrum_average_count;
        spectrum_average_db[k]=10.0*log10(p>1e-10?p:1e-10);
    }
    spectrum_average_valid=spectrum_average_count>=spectrum_average_target;
    double bin=s->peak_bin;
    if(s->peak_bin>1 && s->peak_bin<N/2) {
        double a=s->db[s->peak_bin-1],b=s->db[s->peak_bin],c=s->db[s->peak_bin+1];
        double den=a-2*b+c;
        /* A selected local maximum must have negative curvature.  Reject an
         * upward-curving/noise triplet instead of applying a misleading
         * sub-bin shift. */
        if(den < -1e-9) { double d=0.5*(a-c)/den; if(fabs(d)<=1) bin+=d; }
    }
    s->peak_valid=s->peak_code>=16;
    s->peak_mhz=s->peak_valid ? bin*ADC_SAMPLE_RATE_HZ/N/1e6 : 0;
    /* Strongest component that is neither the fundamental nor its immediate
     * leakage skirt, relative to the fundamental.  A clean sinusoid lands far
     * below -25 dB; square and triangle wavetables keep their odd harmonics
     * well above that, so they stay on the raw trace. */
    double spur=0;
    for(int k=4;k<=N/2;k++) {
        if(k>=s->peak_bin-3 && k<=s->peak_bin+3) continue;
        if(s->code[k]>spur) spur=s->code[k];
    }
    s->spur_db=s->peak_code>0 ? 20.0*log10((spur+1e-9)/s->peak_code) : 0.0;
    rf_metrics_cache_valid=rf_metrics_analyze(s->code,N/2+1,ADC_SAMPLE_RATE_HZ,
                                              (unsigned)s->peak_bin,&rf_metrics_cache)==0;
    for(int k=0;k<=N/2;k++) {
        if(!spectrum_peak_hold_valid || s->db[k]>spectrum_peak_hold[k]) spectrum_peak_hold[k]=s->db[k];
        double level=s->db[k]+100.0;
        if(level<0) level=0;
        if(level>100) level=100;
        spectrum_waterfall[spectrum_waterfall_head][k]=(unsigned char)lround(level*2.55);
    }
    spectrum_waterfall_head=(spectrum_waterfall_head+1)%SPECTRUM_WATERFALL_ROWS;
    spectrum_peak_hold_valid=1; spectrum_history_frames++;
    spectrum_compute_count++;
    return 0;
}
static unsigned spectrum_compute_count;
static int spectrum_ensure(const uint32_t *raw,uint32_t token) {
    if(!spectrum_cache_valid || spectrum_cache_token!=token) {
        if(spectrum_measure(raw,&spectrum_cache)!=0) return -1;
        spectrum_cache_token=token;spectrum_cache_valid=1;spectrum_compute_count++;
    }
    return 0;
}
#include "spectrum_mask617.inc"
/* At 80 MHz and above a 400 MSPS display has five or fewer raw points per
 * period.  In particular 100 MHz is exactly Fs/4, so tiny aperture/trigger
 * errors look like large point-to-point movement.  Fit the dominant tone and
 * phase-lock only the rendered trace to the hardware trigger.  Measurements,
 * cursors, FFT and the captured sample array remain raw. */
static void hf_model_reset(void) { memset(&hf_smooth,0,sizeof(hf_smooth)); hf_smooth_token=0; }
static double hf_model_value(const struct hf_model *m,double sample_index) {
    return m->mean+m->amp*sin(2.0*3.141592653589793*m->mhz*sample_index/400.0+m->phase);
}
/* Keep sub-pixel estimator noise from moving an otherwise stationary trace.
 * The dead band freezes tiny changes; the slew limit rejects a single noisy
 * frame; the low-pass term still follows a genuine setting change. */
static double hf_stable_step(double old,double sample,double deadband,double max_step,double alpha) {
    double delta=sample-old;
    if(fabs(delta)<=deadband) return old;
    if(delta>max_step) delta=max_step;
    if(delta<-max_step) delta=-max_step;
    return old+alpha*delta;
}
static int hf_model_update(const uint32_t *a,const struct metrics *v,uint32_t token,
                           unsigned trigger_index,int level,int falling,struct hf_model *out) {
    if(v->low_signal || v->rail || !(last_flags&2)) { hf_model_reset(); return 0; }
    if(!spectrum_cache_valid || spectrum_cache_token!=token) {
        spectrum_measure(a,&spectrum_cache);
        spectrum_cache_token=token; spectrum_cache_valid=1;
    }
    double mhz=spectrum_cache.peak_mhz;
    double amp=v->rms_ac*1.4142135623730951;
    /* A 400 MSPS display has 20 or fewer raw points per period at 20 MHz and
     * above, so the frame-to-frame sample phase makes the raw VPP readout move
     * far more than the analog amplitude really does.  Phase-lock only a clean
     * single tone; square/triangle harmonics and noisy external signals keep
     * the raw trace and the conservative 80 MHz floor.  The lock is
     * hysteretic: engage below -30 dB, release only above -24 dB. */
    double min_mhz=hf_tone_gate(spectrum_cache.spur_db)?20.0:80.0;
    if(!spectrum_cache.peak_valid || mhz<min_mhz || mhz>180.0 || amp<32.0) {
        hf_model_reset(); return 0;
    }
    int sequential=hf_smooth.valid && token==hf_smooth_token+1 &&
                   fabs(mhz-hf_smooth.mhz)<5.0 && amp/hf_smooth.amp>0.5 && amp/hf_smooth.amp<2.0;
    if(!sequential) {
        hf_smooth.mhz=mhz; hf_smooth.mean=v->mean; hf_smooth.amp=amp; hf_smooth.valid=1;
    } else {
        hf_smooth.mhz=hf_stable_step(hf_smooth.mhz,mhz,0.001,0.20,0.06);
        hf_smooth.mean=hf_stable_step(hf_smooth.mean,v->mean,2.0,32.0,0.06);
        hf_smooth.amp=hf_stable_step(hf_smooth.amp,amp,
            hf_smooth.amp*0.005,hf_smooth.amp*0.08,0.06);
    }
    hf_smooth_token=token;
    double q=(level-hf_smooth.mean)/hf_smooth.amp;
    if(q>1) q=1;
    if(q<-1) q=-1;
    double angle=asin(q);
    if(falling) angle=3.141592653589793-angle;
    hf_smooth.phase=angle-2.0*3.141592653589793*hf_smooth.mhz*trigger_index/400.0;
    *out=hf_smooth;
    return 1;
}
static int copy_completed(volatile uint32_t *m,uint32_t *a,uint32_t token) {
    barrier();
    if(m[REQ]!=token || m[DONE]!=token || !(m[2060]&1)) return -1;
    barrier(); for(int i=0;i<N;i++) a[i]=m[i];
    pause_ns(1000000); barrier();
    for(int i=0;i<N;i++) if(m[i]!=a[i]) {
        fprintf(stderr,"Completed frame changed\n"); return -1;
    }
    if(m[REQ]!=token || m[DONE]!=token) { fprintf(stderr,"Concurrent request detected\n"); return -1; }
    return 0;
}

/* Compact 5x7 bitmap glyphs: digits followed by uppercase A-Z. */
static const uint8_t glyphs[36][7]={
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,1,14},{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
 {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},
 {31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
 {14,4,4,4,4,4,14},{7,2,2,2,2,18,12},{17,18,20,24,20,18,17},
 {16,16,16,16,16,16,31},{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},
 {14,17,17,17,17,17,14},{30,17,17,30,16,16,16},{14,17,17,17,21,18,13},
 {30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
 {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},
 {17,17,10,4,10,17,17},{17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
};
struct screen {
    int fd,w,h,bpp; struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
    uint8_t *fb; uint32_t *rgb;
    uint32_t red[256],green[256],blue[256],alpha;
};
struct touch {
    int fd,slot,x,y,down,latched,valid_x,valid_y,dropped;
    int gesture,fine_lo,fine_hi;
    struct input_absinfo ax,ay;
};
static int button_at(int w,int x,int y) {
    int left=w-320;
    if(y<38 || y>=86 || x<left || x>=left+310) return -1;
    int i=(x-left)/105;
    return i<3 && (x-left)%105<100 ? i : -1;
}
static int view_button_at(int w,int h,int x,int y) {
    int step=(w-40)/6;
    if(step<=8 || y<h-110 || y>=h-62 || x<20 || x>=20+step*6) return -1;
    int n=(x-20)/step;
    return (x-20)%step<step-6 ? (tool_page ? n+16 : n+3) : -1;
}
static int page_button_at(int w,int h,int x,int y) {
    return x>=w-150 && x<w-10 && y>=h-57 && y<h-10 ? 15 : -1;
}
static void tool_action(int b,int frozen) {
    if(b==15) { tool_page=(tool_page+1)%5; if(tool_page==4) gen_verify(); view_dirty=1; return; }
    int visible=visible_samples(view),step=visible/10;
    if(tool_page==1) {
        int start=window_start(visible,last_trigger_index,pan_samples);
        if(b==16) start-=step;
        else if(b==17) start+=step;
        else if(b==18) { pan_samples=0; view_dirty=1; return; }
        else return;
        if(start<0) start=0;
        if(start>N-visible) start=N-visible;
        pan_samples=start-window_start(visible,last_trigger_index,0);
    } else if(tool_page==2) {
        if(!frozen) return;
        if(b==16) cursor_enabled=!cursor_enabled;
        else if(b==17) cursor_selected=!cursor_selected;
        else if(b==18 || b==19) {
            cursor_index[cursor_selected]+=b==18 ? -1 : 1;
            if(cursor_index[cursor_selected]<0) cursor_index[cursor_selected]=0;
            if(cursor_index[cursor_selected]>=N) cursor_index[cursor_selected]=N-1;
        } else if(b==20) {
            int start=window_start(visible,last_trigger_index,pan_samples);
            cursor_index[0]=start+visible/4; cursor_index[1]=start+visible*3/4;
            cursor_enabled=1;
        } else if(b==21) cursor_enabled=0;
    }
    view_dirty=1;
}
static int trigger_button_at(int w,int h,int x,int y) {
    int step=(w-40)/6;
    if(step<=8 || y<h-162 || y>=h-114 || x<20 || x>=20+step*6) return -1;
    return (x-20)%step<step-6 ? (x-20)/step+9 : -1;
}
static struct trigger_config trigger_action(struct trigger_config t,int b) {
    if(b==9) t.mode=t.mode==2 ? 1 : t.mode==1 ? 0 : 2;
    else if(b==10) t.falling=!t.falling;
    else if(b==11) { t.level+=512; if(t.level>16383) t.level=16383; }
    else if(b==12) { t.level-=512; if(t.level<0) t.level=0; }
    else if(b==13) t.pre=(t.pre+512)%2048;
    else if(b==14) t.level=8192;
    return t;
}
static int touch_open(struct touch *t,int probe,int quiet) {
    memset(t,0,sizeof(*t)); t->fd=open("/dev/input/event0",O_RDONLY|O_NONBLOCK);
    if(t->fd<0) { if(!quiet) perror("Touch unavailable; retrying automatically; UART controls remain available"); return -1; }
    char name[128]={0};
    if(ioctl(t->fd,EVIOCGNAME(sizeof(name)),name)<0 || !strstr(name,"Goodix") ||
       ioctl(t->fd,EVIOCGABS(ABS_X),&t->ax)<0 || ioctl(t->fd,EVIOCGABS(ABS_Y),&t->ay)<0 ||
       t->ax.maximum<=t->ax.minimum || t->ay.maximum<=t->ay.minimum) {
        if(!quiet) fprintf(stderr,"Unsupported touch device/range; retrying automatically; UART only\n");
        close(t->fd); t->fd=-1; return -1;
    }
    printf("TOUCH name=%s X=%d..%d Y=%d..%d mapping=direct-scaled UNVERIFIED\n",
           name,t->ax.minimum,t->ax.maximum,t->ay.minimum,t->ay.maximum);
    if(probe) { close(t->fd); t->fd=-1; }
    return 0;
}
static int touch_retry_due(int fd,double now,double *next) {
    if(fd>=0 || now<*next) return 0;
    *next=now+1.0;
    return 1;
}
static int frozen_frame(void) { return paused && !single_pending && !capture_waiting && (last_flags&1); }
static int map_track(int x,int left,int right,int lo,int hi) {
    if(x<left) x=left;
    if(x>right) x=right;
    return lo+(int)((int64_t)(x-left)*(hi-lo)+(right-left)/2)/(right-left);
}
/* Frequency is stored in 0.001 MHz (kHz) units.  The AD9142A NCO path is
 * validated through 250 MHz; keep square/triangle at their established
 * 32 MHz UI limit while exposing the full sine/NCO range. */
static int gen_freq_max_khz(int wave) { return wave==0?250000:32000; }
static const char *gen_wave_name(int wave,int freq_khz) {
    static const char *names[]={"sine","square","triangle"};
    return wave==0 && freq_khz>30000?"nco":names[wave];
}
static int gen_hash_matches(const char *path,const char *expected) {
    char cmd[180],line[160];
    if(snprintf(cmd,sizeof(cmd),"sha256sum %s",path)>=(int)sizeof(cmd)) return 0;
    FILE *f=popen(cmd,"r"); if(!f) return 0;
    int ok=fgets(line,sizeof(line),f)!=NULL && !strncmp(line,expected,64);
    if(pclose(f)!=0) ok=0;
    return ok;
}
static void custom_init_if_needed(void) {
    if(custom_ui_initialized) return;
    cui_init(&custom_ui);
    /* Admission is intentionally false in this candidate.  A future build
     * may change this only after the system CDC/DAC-IO/owner contract is
     * signed off; init itself never opens /dev/mem. */
    custom_ui_backend_init(&custom_backend,false);
    custom_ui_initialized=1;
}
static void custom_invalidate_package(void) {
    custom_package_ready=0;
    custom_apply_refused=0;
    custom_ui.package_used=0;
    custom_ui.state=custom_ui.expression_length?CUI_EDITING:CUI_EMPTY;
    custom_ui.build_error=0;
}
static int custom_build_package(void *context,const char *expression,
                                uint32_t frequency_hz,int32_t amplitude_q15,
                                int16_t offset_q15,uint32_t transaction,
                                uint8_t *package,size_t capacity,size_t *used) {
    struct cwg_table table;
    enum cwg_error e;
    (void)context;
    e=cwg_generate(expression,amplitude_q15,offset_q15,&table);
    if(e==CWG_ERR_ARGUMENT) {
        /* cwg_generate intentionally has a small error enum.  Preserve the
         * formula parser's actual error for the user's edit/build feedback. */
        struct cf_table probe;
        enum cf_error f=cf_generate(expression,&probe);
        if(f!=CF_OK) return -(100+(int)f);
    }
    if(e!=CWG_OK) return -(int)e;
    e=cwg_pack(&table,transaction,frequency_hz,amplitude_q15,offset_q15,
               package,capacity,used);
    return e==CWG_OK?0:-(int)e;
}
static void custom_set_expression(const char *expression) {
    if(!expression) return;
    custom_init_if_needed();
    cui_clear(&custom_ui);
    for(size_t i=0;expression[i] && i<CUI_MAX_EXPR;i++)
        (void)cui_append(&custom_ui,expression[i]);
    custom_invalidate_package();
}
static void custom_begin_edit(int field) {
    if(field<1 || field>4) return;
    custom_init_if_needed();
    custom_edit_field=field;
    if(field==1) snprintf(custom_edit_buf,sizeof(custom_edit_buf),"%s",custom_ui.expression);
    else if(field==2) snprintf(custom_edit_buf,sizeof(custom_edit_buf),"%u",custom_ui.frequency_hz);
    else if(field==3) snprintf(custom_edit_buf,sizeof(custom_edit_buf),"%d",custom_ui.amplitude_q15);
    else snprintf(custom_edit_buf,sizeof(custom_edit_buf),"%d",(int)custom_ui.offset_q15);
    snprintf(custom_status,sizeof(custom_status),"EDIT BUFFER ONLY - OK ACCEPTS, CANCEL KEEPS OLD VALUES");
    view_dirty=1;
    printf("CUSTOM_EDIT_BEGIN FIELD=%d VALUE=%s\n",field,custom_edit_buf);
}
static void custom_edit_reject(const char *reason) {
    snprintf(custom_status,sizeof(custom_status),"EDIT REJECTED - %s",reason);
    view_dirty=1;
    printf("CUSTOM_EDIT_REJECT FIELD=%d REASON=%s VALUE=%s\n",
           custom_edit_field,reason,custom_edit_buf);
}
static void custom_edit_commit(void) {
    if(!custom_edit_field) return;
    if(custom_edit_field==1) {
        if(!custom_edit_buf[0]) { custom_edit_reject("EMPTY FORMULA"); return; }
        custom_set_expression(custom_edit_buf);
    } else {
        /* strtol alone also accepts whitespace and signs.  The integer
         * keypad contract accepts digits, with one leading '-' for offset. */
        const char *p=custom_edit_buf;
        if(custom_edit_field==4 && *p=='-') p++;
        if(!*p) { custom_edit_reject("INTEGER REQUIRED"); return; }
        for(;*p;p++) if(*p<'0' || *p>'9') {
            custom_edit_reject("INTEGER REQUIRED"); return;
        }
        char *end=NULL; errno=0;
        long v=strtol(custom_edit_buf,&end,10);
        if(errno || end==custom_edit_buf || *end) {
            custom_edit_reject("INTEGER OVERFLOW"); return;
        }
        if(custom_edit_field==2) {
            if(v<1L || v>100000000L || !cui_set_frequency(&custom_ui,(uint32_t)v)) {
                custom_edit_reject("FREQUENCY 1 TO 100000000 HZ"); return;
            }
        } else if(custom_edit_field==3) {
            if(v<0L || v>32767L || !cui_set_amplitude(&custom_ui,(int32_t)v)) {
                custom_edit_reject("AMPLITUDE 0 TO 32767 Q15"); return;
            }
        } else if(v< -32768L || v>32767L || !cui_set_offset(&custom_ui,(int32_t)v)) {
            custom_edit_reject("OFFSET -32768 TO 32767 Q15"); return;
        }
        custom_invalidate_package();
    }
    snprintf(custom_status,sizeof(custom_status),"EDIT ACCEPTED - BUILD VALIDATES FORMULA AND CRC");
    custom_edit_field=0;
    view_dirty=1;
    printf("CUSTOM_EDIT_COMMIT FREQ_HZ=%u AMP_Q15=%d OFFSET_Q15=%d\n",
           custom_ui.frequency_hz,custom_ui.amplitude_q15,(int)custom_ui.offset_q15);
}
static void custom_build(void) {
    custom_init_if_needed();
    if(custom_edit_field) { custom_edit_reject("PRESS OK BEFORE BUILD"); return; }
    int rc=cui_build(&custom_ui,custom_build_package,NULL);
    custom_package_ready=rc==0;
    custom_apply_refused=0;
    if(rc==0) {
        struct cwg_header h;
        int16_t samples[CWG_TABLE_LEN];
        enum cwg_error unpack=cwg_unpack(custom_ui.package,custom_ui.package_used,
                                         &h,samples,CWG_TABLE_LEN);
        if(unpack!=CWG_OK) {
            custom_package_ready=0; custom_ui.state=CUI_ERROR;
            custom_ui.build_error=-(int)unpack; custom_ui.package_used=0;
            snprintf(custom_status,sizeof(custom_status),"READBACK REJECTED - %s",cwg_error_string(unpack));
        } else {
            snprintf(custom_status,sizeof(custom_status),"PACKAGE READY CRC %08X - PREVIEW ONLY",h.payload_crc32);
            printf("CUSTOM_PACKAGE_READY TRANSACTION=%u FREQ_HZ=%u CRC=%08X BYTES=%zu\n",
                   h.transaction,h.frequency_hz,h.payload_crc32,custom_ui.package_used);
        }
    } else {
        const char *reason=rc<=-100?cf_error_string((enum cf_error)(-rc-100)):
            rc<0?cwg_error_string((enum cwg_error)(-rc)):"UNKNOWN";
        snprintf(custom_status,sizeof(custom_status),"BUILD REJECTED - %s (ERR %d)",reason,rc);
        printf("CUSTOM_PACKAGE_REJECT ERR=%d REASON=%s\n",rc,reason);
    }
    view_dirty=1;
}
static void custom_apply(void) {
    if(!custom_package_ready) custom_build();
    if(!custom_package_ready) return; /* keep the actual build error visible */
    /* The candidate backend is fail-closed until the system-level CDC and
     * DAC-I/O contracts are closed.  Once admitted, this call performs the
     * complete FENCE -> OFF -> 1024 write -> readback -> COMMIT/receipt ->
     * RELEASE -> enable transaction through custom_ui_backend. */
    int rc=custom_ui_backend_apply(&custom_backend,custom_ui.package,
                                   custom_ui.package_used);
    if(rc<0) {
        custom_apply_refused=1;
        snprintf(custom_status,sizeof(custom_status),
                 "APPLY REFUSED - %s, GEN UNCHANGED",
                 custom_ui_backend_status(&custom_backend));
        printf("CUSTOM_APPLY_REFUSED REASON=%s RC=%d\n",
               custom_ui_backend_status(&custom_backend),rc);
    } else {
        custom_apply_refused=0;
        snprintf(custom_status,sizeof(custom_status),
                 "CUSTOM ACTIVE - COMMIT RECEIPT VERIFIED");
        printf("CUSTOM_APPLY_OK FREQ_HZ=%u STATUS=%s\n",
               custom_ui.frequency_hz,custom_ui_backend_status(&custom_backend));
    }
    view_dirty=1;
}
static void custom_discard(void) {
    custom_init_if_needed();
    custom_invalidate_package();
    snprintf(custom_status,sizeof(custom_status),"PACKAGE DISCARDED - ORIGINAL GEN OUTPUT UNCHANGED");
    printf("CUSTOM_DISCARD_NO_MMIO\n");
    view_dirty=1;
}
static void custom_edit_key(const char *key) {
    if(!custom_edit_field || !key) return;
    if(!strcmp(key,"OK")) { custom_edit_commit(); return; }
    if(!strcmp(key,"CANCEL")) {
        custom_edit_field=0;
        snprintf(custom_status,sizeof(custom_status),"EDIT CANCELLED - ORIGINAL VALUES AND GEN UNCHANGED");
        view_dirty=1; return;
    }
    if(!strcmp(key,"CLR")) { custom_edit_buf[0]='\0'; view_dirty=1; return; }
    if(!strcmp(key,"BKSP")) {
        size_t n=strlen(custom_edit_buf);
        if(n) custom_edit_buf[n-1]='\0';
        view_dirty=1; return;
    }
    if(!*key) return;
    if(custom_edit_field!=1) {
        if(!strcmp(key,"-")) {
            if(custom_edit_field!=4 || custom_edit_buf[0]) return;
        } else if(key[1] || key[0]<'0' || key[0]>'9') return;
    }
    size_t length=strlen(custom_edit_buf),added=strlen(key);
    if(added>CUI_MAX_EXPR-length) { custom_edit_reject("FORMULA BUFFER FULL"); return; }
    memcpy(custom_edit_buf+length,key,added+1u);
    view_dirty=1;
}
static int custom_edit_touch(int x,int y,int w,int h) {
    (void)w; (void)h;
    if(!custom_edit_field) return 0;
    if(custom_edit_field==1) {
        const int left=16,top=214,step=128,kh=43;
        if(x>=left && x<left+6*step && y>=top && y<top+5*kh &&
           (x-left)%step<120 && (y-top)%kh<38)
            custom_edit_key(custom_formula_keys[(y-top)/kh][(x-left)/step]);
    } else {
        const int left=130,top=214,step=135,kh=48;
        if(x>=left && x<left+4*step && y>=top && y<top+4*kh &&
           (x-left)%step<126 && (y-top)%kh<42)
            custom_edit_key(custom_number_keys[(y-top)/kh][(x-left)/step]);
    }
    return 1;
}
static void gen_begin_edit(int field) {
    gen_edit_field=field;
    if(field==1) snprintf(gen_edit_buf,sizeof(gen_edit_buf),"%.6f",gen_freq_khz/1000.0);
    else snprintf(gen_edit_buf,sizeof(gen_edit_buf),"%d",gen_amp_pct);
    view_dirty=1;
    printf("GEN_EDIT_BEGIN FIELD=%d VALUE=%s\n",field,gen_edit_buf);
}
static void sweep_begin_edit(int field) {
    gen_edit_field=field;
    if(field==3) snprintf(gen_edit_buf,sizeof(gen_edit_buf),"%.1f",sweep.start_mhz);
    else if(field==4) snprintf(gen_edit_buf,sizeof(gen_edit_buf),"%.1f",sweep.stop_mhz);
    else if(field==5) snprintf(gen_edit_buf,sizeof(gen_edit_buf),"%.1f",sweep.pass_threshold_db);
    else if(field==6) snprintf(gen_edit_buf,sizeof(gen_edit_buf),"%u",sweep.points);
    else if(field==7) snprintf(gen_edit_buf,sizeof(gen_edit_buf),"%u",sweep.average_frames);
    else if(field==8) snprintf(gen_edit_buf,sizeof(gen_edit_buf),"%u",sweep.max_retries);
    else snprintf(gen_edit_buf,sizeof(gen_edit_buf),"%d",sweep.logarithmic);
    view_dirty=1;
    printf("SWEEP_EDIT_BEGIN FIELD=%d VALUE=%s\n",field,gen_edit_buf);
}
static int sweep_save_config(void) {
    char tmp[PATH_MAX];
    int n=snprintf(tmp,sizeof(tmp),"%s.tmp.%ld",SWEEP_CONFIG_PATH,(long)getpid());
    if(n<0 || (size_t)n>=sizeof(tmp)) return -1;
    FILE *f=fopen(tmp,"w");
    if(!f) return -1;
    int ok=fprintf(f,"# diag sweep config v2\nstart_mhz=%.6f\nstop_mhz=%.6f\npass_threshold_db=%.6f\npoints=%u\nscale=%s\naverage=%u\nretries=%u\n",
                   sweep.start_mhz,sweep.stop_mhz,sweep.pass_threshold_db,sweep.points,
                   sweep.logarithmic?"log":"linear",sweep.average_frames,sweep.max_retries)>0;
    if(fflush(f)!=0 || fsync(fileno(f))<0) ok=0;
    if(fclose(f)!=0) ok=0;
    f=NULL; /* fclose releases the stream even when it reports an error. */
    if(ok) {
        if(rename(tmp,SWEEP_CONFIG_PATH)==0 && diag_sync_directory()==0) {
        printf("SWEEP_CONFIG_SAVED PATH=%s START=%.3f STOP=%.3f PASS_DB=%.3f POINTS=%u SCALE=%s AVG=%u RETRIES=%u\n",
               SWEEP_CONFIG_PATH,sweep.start_mhz,sweep.stop_mhz,sweep.pass_threshold_db,
               sweep.points,sweep.logarithmic?"LOG":"LINEAR",sweep.average_frames,sweep.max_retries);
        return 0;
        }
    }
    if(f) fclose(f);
    unlink(tmp);
    return -1;
}
static void gen_edit_clear(void) { gen_edit_buf[0]=0; view_dirty=1; }
static void gen_edit_backspace(void) {
    size_t n=strlen(gen_edit_buf); if(n) gen_edit_buf[n-1]=0; view_dirty=1;
}
static void gen_edit_commit(void) {
    char *end=NULL; errno=0; double value=strtod(gen_edit_buf,&end);
    if(errno || end==gen_edit_buf || *end || !isfinite(value)) {
        printf("GEN_EDIT_REJECT FIELD=%d VALUE=%s\n",gen_edit_field,gen_edit_buf); return;
    }
    if(gen_edit_field>=3 && gen_edit_field<=9) {
        if(gen_edit_field==3) {
            if(value<0.3) value=0.3;
            if(value>=sweep.stop_mhz) value=sweep.stop_mhz-0.001;
            sweep.start_mhz=value;
        } else if(gen_edit_field==4) {
            if(value<=sweep.start_mhz) value=sweep.start_mhz+0.001;
            if(value>SWEEP_MAX_MHZ) value=SWEEP_MAX_MHZ;
            sweep.stop_mhz=value;
        } else if(gen_edit_field==5) {
            if(value>0.0) value=0.0;
            if(value<-60.0) value=-60.0;
            sweep.pass_threshold_db=value;
        } else if(gen_edit_field==6) {
            unsigned p=(unsigned)lround(value);
            if(p!=8 && p!=16 && p!=32 && p!=64) { printf("SWEEP_EDIT_REJECT POINTS=%u\n",p); return; }
            sweep.points=p;
        } else if(gen_edit_field==7) {
            unsigned a=(unsigned)lround(value);
            if(a<1) a=1;
            if(a>16) a=16;
            sweep.average_frames=a;
        } else if(gen_edit_field==8) {
            unsigned r=(unsigned)lround(value);
            if(r>5) r=5;
            sweep.max_retries=r;
        } else {
            sweep.logarithmic=value>=0.5?1:0;
        }
        sweep.calibrated=0;
        reflect_invalidate_calibration();
        if(sweep_save_config()<0) fprintf(stderr,"SWEEP_CONFIG_SAVE_FAILED PATH=%s\n",SWEEP_CONFIG_PATH);
        gen_edit_field=0; view_dirty=1;
        printf("SWEEP_EDIT_COMMIT START=%.1fMHZ STOP=%.1fMHZ PASS_DB=%.1f POINTS=%u SCALE=%s AVG=%u RETRIES=%u\n",
               sweep.start_mhz,sweep.stop_mhz,sweep.pass_threshold_db,sweep.points,
               sweep.logarithmic?"LOG":"LINEAR",sweep.average_frames,sweep.max_retries);
    } else if(gen_edit_field==1) {
        int max=gen_freq_max_khz(gen_wave);
        int next=(int)lround(value*1000.0);
        if(next<1000) next=1000;
        if(next>max) next=max;
        gen_freq_khz=next;
    } else {
        int next=(int)lround(value);
        if(next<0) next=0;
        if(next>50) next=50;
        gen_amp_pct=next;
    }
    gen_pending=1; gen_edit_field=0; view_dirty=1;
    printf("GEN_EDIT_COMMIT FREQ=%.6fMHZ AMP_DIGITAL=%d%% STEP_MHZ=0.001\n",gen_freq_khz/1000.0,gen_amp_pct);
}
static void gen_edit_key(const char *key) {
    if(!strcmp(key,"OK")) { gen_edit_commit(); return; }
    if(!strcmp(key,"CANCEL")) { gen_edit_field=0; view_dirty=1; return; }
    if(!strcmp(key,"CLR")) { gen_edit_clear(); return; }
    if(!strcmp(key,"BKSP")) { gen_edit_backspace(); return; }
    if(!strcmp(key,".") && strchr(gen_edit_buf,'.')) return;
    if(strlen(gen_edit_buf)+strlen(key)+1>=sizeof(gen_edit_buf)) return;
    strcat(gen_edit_buf,key); view_dirty=1;
}
static int gen_edit_touch(int x,int y,int w,int h) {
    (void)w;
    if(!gen_edit_field) return 0;
    if(y>=h-47) return 1;
    static const char *keys[4][4]={{"1","2","3","BKSP"},{"4","5","6","CLR"},{"7","8","9","."},{"0","-","OK","CANCEL"}};
    int left=25,top=185,step=112,kh=48;
    if(x<left || x>=left+4*step || y<top || y>=top+4*kh) return 1;
    int col=(x-left)/step,row=(y-top)/kh;
    if(col<0 || col>=4 || row<0 || row>=4) return 1;
    gen_edit_key(keys[row][col]);
    return 1;
}
static int gen_verify(void) {
    if(gen_verified<0) {
        const char *bit_path="/media/sd-mmcblk0p1/system.bit";
        // Filled with the signed release hashes before deployment. Neither
        // boot-file hash is treated as a live PL readback.
        /* Keep both the known-good release and the ROM-only ECO candidate
         * accepted. The ECO changes only square-wave BRAM INIT data, but it
         * necessarily changes the boot-file hash. */
        int bit_ok=gen_hash_matches(bit_path,"d379bd7b392f592649366fb4ca3b4d9e10bf6b18dab34b15a562c3e2391840d1") ||
            gen_hash_matches(bit_path,"028520e05d8bdc709862e73ecf4aa1f4d5c1593eb53f29a7a6974353957fa432") ||
            gen_hash_matches(bit_path,"247540c28e9e641e28ef3ec52657fbed0194b3d685460a11c29941a2b6d7af69") ||
            gen_hash_matches(bit_path,"b18abc536de26ed9bc7eb74f53fa94d51f531cf9172967ee61dddfaf98161a09");
        int ctl_ok=gen_hash_matches("/usr/local/bin/dacctl","439ea6e3d9f1346d64cd4a194282e25a92509c5b64ce94f953b7c0f73d5a3c41") ||
            gen_hash_matches("/usr/local/bin/dacctl","c82787f92394269364c24b75e68670bca9cdf757930a15c0e318747324897e12");
        gen_verified=bit_ok && ctl_ok;
        printf("GEN_FILE_GUARD=%s (boot file, not a live PL readback)\n",gen_verified?"MATCH":"REFUSE");
    }
    return gen_verified;
}
#ifndef GEN_CONTROL_EXEC
static int gen_control_exec(int on,int freq_khz,int wave,int amp_pct) {
    char freq[16],amp[16];
    snprintf(freq,sizeof(freq),"%d.%03d",freq_khz/1000,freq_khz%1000);
    snprintf(amp,sizeof(amp),"%d",amp_pct);
    pid_t pid=fork(); if(pid<0) return -1;
    if(pid==0) {
        if(on) {
            char *const args[]={"dacctl","--freq",freq,"--wave",(char *)gen_wave_name(wave,freq_khz),
                "--amp-percent",amp,"--apply","--confirm-map","0x41230000",NULL};
            execv("/usr/local/bin/dacctl",args);
        } else {
            char *const args[]={"dacctl","--off","--apply","--confirm-map","0x41230000",NULL};
            execv("/usr/local/bin/dacctl",args);
        }
        _exit(127);
    }
    int status;
    while(waitpid(pid,&status,0)<0) { if(errno!=EINTR) return -1; }
    return WIFEXITED(status) && WEXITSTATUS(status)==0 ? 0 : -1;
}
#define GEN_CONTROL_EXEC gen_control_exec
#endif
static void reflect_raw_prepare_ui(void) {
    if(!reflect_raw_ui_initialized) {
        fr_ui_init(&reflect_raw_ui);
        fr_ui_set_manual_mode(&reflect_raw_ui,reflect_manual_mode);
        reflect_raw_ui_initialized=1;
    }
    reflect_raw_ui.application_admitted=reflect_manual_mode && reflect_frontend_confirmed &&
        reflect_files.ready && !reflect_ps_adapter.safety_errors;
    if(reflect_raw_ui.capture.active) return; /* Keep an active ARM configuration immutable. */
    struct fr_raw_config c={0};
    c.points=sweep.points?sweep.points:16;
    c.frames_per_path=sweep.average_frames?sweep.average_frames:1;
    c.guard_frames=2; c.settle_us=1000; c.timeout_us=5000000;
    c.min_fwd_rms=8; c.min_rev_rms=4; /* Provisional CODE thresholds, not calibration. */
    double start=sweep.start_mhz,stop=sweep.stop_mhz;
    if(c.points>FR_RAW_MAX_POINTS || !isfinite(start) || !isfinite(stop) ||
       start<1.0 || stop>180.0 || stop<=start) {
        reflect_raw_ui.notice=FR_UI_BAD_CONFIG; return;
    }
    for(unsigned i=0;i<c.points;i++) {
        double fraction=c.points<=1?0:(double)i/(c.points-1);
        double mhz=sweep.logarithmic?exp(log(start)+fraction*log(stop/start)):
                                    start+(stop-start)*fraction;
        c.frequency_hz[i]=(uint32_t)llround(mhz*1000.0)*1000u;
    }
    if(fr_ui_prepare(&reflect_raw_ui,&c,sweep.logarithmic?1:0)<0)
        reflect_raw_ui.notice=FR_UI_BAD_CONFIG;
    else if(reflect_raw_ui.notice==FR_UI_BAD_CONFIG)
        reflect_raw_ui.notice=FR_UI_WAIT_HARDWARE;
}
/* Bind the existing backend at the application boundary. Refuse an in-flight
 * SCOPE frame instead of relabelling it as REFLECT or cancelling it from a tap.
 * Default permit is zero; guarded main-loop preparation may grant one handoff
 * only after caller-attested frontend and checked platform resources. */
static void reflect_prepare_files(void) {
    /* Startup preparation only: no permission grant or hardware callbacks. */
    int rc=FR_PLATFORM_FILES_OPEN(&reflect_files);
    reflect_resources.dac_tool_verified=rc==0 && reflect_files.ready;
    reflect_resources.dac_image=reflect_resources.dac_tool_verified?&reflect_files.image:NULL;
    printf("REFLECT_FILES READY=%d ERRORS=%u START_DISABLED=1\n",
           reflect_resources.dac_tool_verified,reflect_files.errors);
}
static int reflect_backend_begin(void) {
    if(reflect_manual_only && !reflect_manual_mode) {
        reflect_raw_ui.notice=FR_UI_WAIT_HARDWARE;
        return FR_RAW_FAILED;
    }
    if(reflect_backend_permit!=1) {
        if(reflect_frontend_confirmed==1 && reflect_files.ready &&
           !reflect_ps_adapter.safety_errors &&
           !reflect_admission_requested && !sweep.active && !reflect.active &&
           !reflect_ps_exclusive() && !reflect_backend.active) {
            reflect_raw_prepare_ui();
            if(reflect_raw_ui.notice==FR_UI_BAD_CONFIG)return FR_RAW_FAILED;
            reflect_admission_requested=1;
            reflect_platform_generation=(uint32_t)control_generation;
            reflect_platform_revision=trigger_revision;
            memset(&reflect_platform_watch,0,sizeof(reflect_platform_watch));
            reflect_platform_next_probe=0;
            reflect_raw_ui.notice=FR_UI_PREPARING;
            return FR_RAW_PROGRESS; /* Main loop does filesystem/proc preparation. */
        }
        reflect_raw_ui.notice=FR_UI_WAIT_HARDWARE;
        return FR_RAW_FAILED;
    }
    if(capture_waiting || sweep.active || reflect.active || reflect_ps_exclusive() ||
       reflect_backend.active) {
        reflect_raw_ui.notice=FR_UI_RESOURCE_BUSY;
        return FR_RAW_FAILED;
    }
    reflect_raw_prepare_ui();
    if(reflect_raw_ui.notice==FR_UI_BAD_CONFIG) return FR_RAW_FAILED;
    if (reflect_manual_mode) {
        if(reflect_manual_phase==2) {
            if(reflect_manual_session.points!=reflect_raw_ui.pending.points) {
                reflect_raw_ui.notice=FR_UI_BAD_CONFIG;return FR_RAW_FAILED;
            }
            for(unsigned i=0;i<reflect_manual_session.points;i++)
                if((uint32_t)llround(reflect_manual_session.frequency_mhz[i]*1e6)!=
                   reflect_raw_ui.pending.frequency_hz[i]) {
                    reflect_raw_ui.notice=FR_UI_BAD_CONFIG;return FR_RAW_FAILED;
                }
        }
        if (reflect_manual_phase != 2 && reflect_manual_prepare_session() < 0) {
            reflect_raw_ui.notice=FR_UI_BAD_CONFIG; return FR_RAW_FAILED;
        }
        if (reflect_manual_phase == 2 &&
            fr_manual_session_confirm_reverse_connected(&reflect_manual_session) < 0) {
            reflect_raw_ui.notice=FR_UI_CAPTURE_FAILED; return FR_RAW_FAILED;
        }
        reflect_manual_phase = reflect_manual_phase == 2 ? 3 : 1;
        reflect_raw_ui.notice = reflect_manual_phase == 3 ?
            FR_UI_MANUAL_CAPTURE_REV : FR_UI_PREPARING;
    }
    reflect_backend.manual_no_switch=reflect_manual_mode;
    int rc=fr_backend_start(&reflect_backend,&reflect_ps_adapter,
            &reflect_raw_ui.pending,reflect_resources,reflect_backend_permit,
            reflect_backend_amplitude,(uint32_t)control_generation,trigger_revision);
    if(reflect_backend.active && reflect_ps_adapter.owned) {
        /* A failed start may already own a child and need asynchronous OFF.
         * Preserve that owner; normal main-loop polling performs the drain. */
        reflect_clear_requested=0;
        reflect_backend_permit=0; /* A grant is for one resource handoff only. */
        reflect_raw_ui.selected=0;
        reflect_raw_ui.synthetic=0;
        reflect_raw_ui.logarithmic=sweep.logarithmic?1:0;
        reflect_raw_ui.capture_meta=(struct fr_ui_capture_meta){
            .requested_amplitude_pct=reflect_backend.dac.amplitude_pct,.known=1};
        gen_applied=0; gen_pending=1;
        reflect_raw_ui.notice=reflect_ps_adapter.stopping?FR_UI_STOPPING:FR_UI_PREPARING;
    } else reflect_raw_ui.notice=FR_UI_CAPTURE_FAILED;
    return rc;
}
static void reflect_backend_notice(void) {
    if(!reflect_ps_adapter.owned) return;
    if(reflect_raw_ui.capture_meta.known) {
        reflect_raw_ui.capture_meta.output_off_confirmed=
            fr_dac_owner_off_confirmed(&reflect_backend.dac);
        reflect_raw_ui.capture_meta.cleanup_confirmed=fr_ps_clean(&reflect_ps_adapter);
    }
    enum fr_ui_notice before=reflect_raw_ui.notice;
    if(reflect_ps_adapter.safety_errors) reflect_raw_ui.notice=FR_UI_CAPTURE_FAILED;
    else if(!fr_ps_clean(&reflect_ps_adapter)) {
        if(reflect_ps_adapter.stopping) reflect_raw_ui.notice=FR_UI_STOPPING;
        else if(reflect_raw_ui.capture.active)
            reflect_raw_ui.notice=reflect_raw_ui.capture.frequency_pending?
                FR_UI_PREPARING:FR_UI_ACQUIRING;
    } else if(before==FR_UI_STOPPING || before==FR_UI_PREPARING || before==FR_UI_ACQUIRING) {
        if (reflect_manual_mode && reflect_raw_ui.capture.completed &&
            reflect_ps_adapter.reason == FR_PS_COMPLETE && reflect_manual_phase == 1) {
            double fwd[FR_MANUAL_MAX_POINTS];
            unsigned n=reflect_raw_ui.capture.config.points;
            for(unsigned i=0;i<n;i++) fwd[i]=reflect_raw_ui.capture.trace[i].fwd_rms;
            if(fr_manual_session_forward_done(&reflect_manual_session,fwd,n)<0) {
                reflect_manual_phase=0; reflect_raw_ui.notice=FR_UI_CAPTURE_FAILED;
            } else {
                memcpy(reflect_manual_forward_trace,reflect_raw_ui.capture.trace,
                       n*sizeof(reflect_manual_forward_trace[0]));
                reflect_manual_phase=2;
                reflect_raw_ui.notice=FR_UI_MANUAL_WAIT_REV;
            }
        } else if (reflect_manual_mode && reflect_raw_ui.capture.completed &&
                   reflect_ps_adapter.reason == FR_PS_COMPLETE && reflect_manual_phase == 3) {
            double rev[FR_MANUAL_MAX_POINTS];
            unsigned n=reflect_raw_ui.capture.config.points;
            for(unsigned i=0;i<n;i++) rev[i]=reflect_raw_ui.capture.trace[i].fwd_rms;
            if(fr_manual_session_reverse_done(&reflect_manual_session,rev,n,8.0)<0) {
                reflect_manual_phase=0; reflect_raw_ui.notice=FR_UI_CAPTURE_FAILED;
            } else {
                /* Both curves must represent the two physical connections,
                 * not the duplicate no-switch samples of the REV run. */
                for(unsigned i=0;i<n;i++) {
                    struct fr_raw_point *q=&reflect_raw_ui.capture.trace[i];
                    const struct fr_raw_point *f=&reflect_manual_forward_trace[i];
                    /* Manual REV was captured using the no-switch FWD slot. */
                    q->rev_frames=q->fwd_frames;
                    q->rev_first_seq=q->fwd_first_seq;
                    q->rev_last_seq=q->fwd_last_seq;
                    q->fwd_frames=f->fwd_frames;
                    q->fwd_first_seq=f->fwd_first_seq;
                    q->fwd_last_seq=f->fwd_last_seq;
                    q->quality=(f->quality & (FR_RAW_LOW_FWD|FR_RAW_CLIPPED)) |
                        (q->quality & FR_RAW_CLIPPED) |
                        ((q->quality & FR_RAW_LOW_FWD)?FR_RAW_LOW_REV:0);
                    reflect_raw_ui.capture.trace[i].fwd_rms=
                        reflect_manual_session.forward_rms[i];
                    reflect_raw_ui.capture.trace[i].rev_rms=
                        reflect_manual_session.reverse_rms[i];
                    reflect_raw_ui.capture.trace[i].sample_ratio=
                        reflect_manual_session.result[i].relative_reflection_magnitude;
                }
                reflect_manual_phase=4;
                reflect_raw_ui.notice=FR_UI_COMPLETE;
            }
        } else {
            reflect_raw_ui.notice=reflect_clear_requested?FR_UI_CLEARED:
                reflect_ps_adapter.reason==FR_PS_ERROR?FR_UI_CAPTURE_FAILED:
                reflect_raw_ui.capture.completed?FR_UI_COMPLETE:FR_UI_STOPPED;
        }
        /* Confirm only this application's real backend owner, not any fake
         * callback or the mere end of a DAC child. */
        if(fr_dac_owner_off_confirmed(&reflect_backend.dac)) {gen_applied=2;gen_pending=1;}
    }
    if(before!=reflect_raw_ui.notice) view_dirty=1;
}
/* SWEEP uses the DAC's 15-bit amplitude word through dacctl, so keep the
 * fractional percentage generated by the flatness table instead of rounding
 * it to the integer-only GEN panel setting. */
#ifdef SWEEP_CONTROL_EXEC_DEFAULT
static int sweep_gen_control_exec(int on,int freq_khz,double amp_pct) {
    char freq[16],amp[24];
    snprintf(freq,sizeof(freq),"%d.%03d",freq_khz/1000,freq_khz%1000);
    snprintf(amp,sizeof(amp),"%.6f",amp_pct);
    pid_t pid=fork(); if(pid<0) return -1;
    if(pid==0) {
        if(on) {
            char *const args[]={"dacctl","--freq",freq,"--wave",(char *)gen_wave_name(0,freq_khz),
                "--amp-percent",amp,"--apply","--confirm-map","0x41230000",NULL};
            execv("/usr/local/bin/dacctl",args);
        } else {
            char *const args[]={"dacctl","--off","--apply","--confirm-map","0x41230000",NULL};
            execv("/usr/local/bin/dacctl",args);
        }
        _exit(127);
    }
    int status;
    while(waitpid(pid,&status,0)<0) { if(errno!=EINTR) return -1; }
    return WIFEXITED(status) && WEXITSTATUS(status)==0 ? 0 : -1;
}
#endif
static void gen_action(int button) {
    if(button>=0 && button<=2) {
        gen_wave=button;
        if(gen_freq_khz>gen_freq_max_khz(gen_wave)) gen_freq_khz=gen_freq_max_khz(gen_wave);
        gen_pending=1; view_dirty=1;
        printf("GEN_WAVE_PENDING=%d\n",gen_wave); return;
    }
    if(button!=3 && button!=4) return;
    mask617_source_changed();
    /* OFF cancels an unstarted RAW admission, then uses ordinary GEN OFF.
     * An admission without a DAC owner cannot confirm physical/digital OFF. */
    if(button==4 && reflect_admission_requested)reflect_platform_abort();
    if(reflect_ps_exclusive()) {
        if(button==3) printf("GEN_APPLY_REFUSED REFLECT_OWNS_DAC\n");
        else {
            (void)fr_ps_stop(&reflect_ps_adapter,FR_PS_STOP,
                            (uint64_t)(seconds()*1000000.0));
            reflect_raw_ui.notice=FR_UI_STOPPED;
            gen_applied=reflect_ps_adapter.owned &&
                !(reflect_ps_adapter.safety_errors&FR_PS_OFF_FAILED)?2:3;
            printf("GEN_OFF_ROUTED_TO_REFLECT_STOP CLEANUP_PENDING=%d SAFETY=%u\n",
                   reflect_ps_adapter.pending,reflect_ps_adapter.safety_errors);
        }
        view_dirty=1; return;
    }
    if(!gen_verify())
        fprintf(stderr,"GEN_WARNING: boot/tool hash mismatch; control remains enabled by user policy\n");
    if(GEN_CONTROL_EXEC(button==3,gen_freq_khz,gen_wave,gen_amp_pct)==0) {
        gen_applied=button==3?1:2;
        if(button==3) {
            gen_active_freq_khz=gen_freq_khz;
            gen_active_amp_pct=gen_amp_pct;
            gen_active_wave=gen_wave;
            gen_pending=0;
        }
        printf("GEN_COMMAND_SENT ON=%d FREQ=%.6fMHZ WAVE=%d AMP_DIGITAL=%d%% STEP_MHZ=0.001 ACTUAL_VOLTS_UNCALIBRATED\n",
            button==3,gen_freq_khz/1000.0,gen_wave,gen_amp_pct);
    } else { gen_applied=3; fprintf(stderr,"GEN_COMMAND_FAILED\n"); }
    view_dirty=1;
}
static int drag_update(struct touch *t,int x,int w) {
    int visible=visible_samples(view);
    if(t->gesture==1) {
        pan_samples=map_track(x,40,w-180,0,N-visible)-window_start(visible,last_trigger_index,0);
    } else if(t->gesture>=4 && t->gesture<=6) {
        int lane=t->gesture-4,part=(w-40)/3,left=40+lane*part,right=20+(lane+1)*part-20;
        view_drag=1;
        if(t->gesture==4) view.ns=view_times[map_track(x,left,right,0,VIEW_TIME_COUNT-1)];
        else if(t->gesture==5) view.codes=view_scales[map_track(x,left,right,0,VIEW_SCALE_COUNT-1)];
        else view.offset=8192-map_track(x,left,right,0,16384);
    } else if(t->gesture==7 || t->gesture==8) {
        trigger_drag=t->gesture;
        if(t->gesture==7) preview_level=map_track(x,290,510,0,16383);
        else preview_pre=fine_pretrigger?map_track(x,550,w-40,0,1022)*2:map_track(x,550,w-40,0,3)*512;
    } else if(t->gesture==9 || t->gesture==10) {
        if(t->gesture==9) gen_freq_khz=map_track(x,40,w/2-20,1000,gen_freq_max_khz(gen_wave));
        else gen_amp_pct=map_track(x,w/2+20,w-40,0,50);
        gen_pending=1;
    } else if(t->gesture==2 || t->gesture==3) {
        if(!frozen_frame()) { fine_drag_x=-1; t->gesture=0; return 0; }
        if(t->gesture==3) fine_drag_x=x<330?330:x>w-30?w-30:x;
        cursor_index[cursor_selected]=t->gesture==2 ?
            map_track(x,45,w-25,window_start(visible,last_trigger_index,pan_samples),window_start(visible,last_trigger_index,pan_samples)+visible-1) :
            map_track(x,330,w-30,0,N-1);
        if(t->gesture==3) {
            int start=window_start(visible,last_trigger_index,pan_samples),index=cursor_index[cursor_selected];
            if(index<start || index>=start+visible) {
                start=index-visible/2;
                if(start<0) start=0;
                if(start>N-visible) start=N-visible;
                pan_samples=start-window_start(visible,last_trigger_index,0);
            }
        }
    } else return 0;
    view_dirty=1;
    return 1;
}
static int drag_press(struct touch *t,int x,int y,int w,int h) {
    if(mask617_touch(x,y,w,h)) return 1;
    if(diag_ui_state.mode==DIAG_UI_SCOPE && tool_page==1 &&
       x>=20 && x<175 && y>=4 && y<34) {
        scope_stats_enabled683=!scope_stats_enabled683;
        scope_stats_context_live683=0;
        ss683_reset(&scope_stats683,scope_stats_enabled683?"ON":"OFF");
        view_dirty=1;
        printf("SCOPE_STATS683 ON=%d WINDOW=16 READOUT_ONLY=1 RAW_UNCHANGED=1\n",
               scope_stats_enabled683);
        return 1;
    }
    if(diag_ui_state.mode==DIAG_UI_SCOPE && tool_page!=4 &&
       y>=4 && y<34 && x>=185 && x<305) {
        if(single_pending || (paused && capture_waiting) || reflect_ps_exclusive()) {
            printf("LOW_FREQ681_BUSY WAIT_FOR_SINGLE_OR_RAW\n");
            return 1;
        }
        int entering681=tool_page!=5;
        tool_page=entering681?5:0;
        lf681_init(&lf_history681);
        trigger_revision++;
        view_dirty=1;
        printf("LOW_FREQ681 MODE=%s HOST_TIMED_SNAPSHOTS=1 CONTINUOUS_RAW=0\n",
               entering681?"SLOW":"FAST");
        return 1;
    }
    /* The custom panel is confined to GEN within SCOPE mode.  A remembered
     * tool_page==4 must never steal a touch from MONITOR/SWEEP/REFLECT. */
    if(diag_ui_state.mode==DIAG_UI_SCOPE && tool_page==4) {
        if(y>=4 && y<34 && x>=185 && x<305) {
            custom_init_if_needed(); custom_panel=1; custom_edit_field=0;
            view_dirty=1; printf("CUSTOM_PANEL_OPEN PREVIEW_ONLY=1\n"); return 1;
        }
        if(custom_panel) {
            if(y<38 && x>=320 && x<495) {
                custom_panel=0; custom_edit_field=0; view_dirty=1;
                /* Let the original SCOPE/DIAG header process the touch. */
            } else {
                if(y>=43 && y<73 && x>=650 && x<770) {
                    custom_panel=0; custom_edit_field=0; view_dirty=1;
                    printf("CUSTOM_PANEL_CLOSE\n"); return 1;
                }
                if(custom_edit_field) return custom_edit_touch(x,y,w,h);
                if(y>=207 && y<247 && x>=20 && x<780) {
                    int field=(x-20)/190;
                    if((x-20)%190<175) custom_begin_edit(field+1);
                    return 1;
                }
                if(y>=375 && y<420 && x>=20 && x<w-20) {
                    int step=(w-40)/4,action=(x-20)/step;
                    if((x-20)%step>=step-6) return 1;
                    if(action==0) custom_build();
                    else if(action==1) custom_apply();
                    else if(action==2) custom_discard();
                    else { custom_panel=0; view_dirty=1; printf("CUSTOM_PANEL_CLOSE\n"); }
                }
                return 1; /* never activate covered GEN/trigger/navigation */
            }
        }
    }
    if(diag_ui_state.mode==DIAG_UI_DIAG && diag_ui_state.page==DIAG_PAGE_MONITOR &&
       x>=590 && x<790 && y>=35 && y<70) {
        if(diag_export_report()<0) fprintf(stderr,"DIAG_REPORT_EXPORT_FAILED PATH=%s\n",REPORT_RESULT_PATH);
        else printf("DIAG_REPORT_EXPORT_TOUCH_TOP\n");
        view_dirty=1;
        return 1;
    }
    if(diag_ui_state.mode==DIAG_UI_DIAG && diag_ui_state.page==DIAG_PAGE_MONITOR &&
       x>=510 && x<775 && y>=273 && y<305) {
        if(x<610) {
            double now=seconds();
            if(diag_baseline_learning || (diag_new_test_last_press>=0.0 &&
               now-diag_new_test_last_press<0.75)) return 1;
            struct diag_monitor_config c=diag_monitor_state.cfg;
            if(diag_start_new_test(c)<0) {
                diag_storage_error=1;
                fprintf(stderr,"MONITOR_NEW_TEST_FAILED ERRNO=%d\n",errno);
            } else {
                diag_new_test_last_press=now;
                printf("MONITOR_NEW_TEST BASELINE_FRAMES=%u EVENT_SEQUENCE=%u RECOVERY_HOLD_MS=%llu\n",
                       c.baseline_frames,diag_monitor_state.event_sequence,
                       (unsigned long long)c.recovery_hold_ms);
            }
        } else if(x<670) {
            if(diag_monitor_state.state==DIAG_MONITOR_FAULT) {
                printf("MONITOR_SETTINGS_BUSY ACTIVE_EVENT=%u\n",diag_monitor_state.event_sequence);
                return 1;
            }
            unsigned level=(diag_sensitivity_level(&diag_monitor_state.cfg)+1)%3;
            struct diag_monitor_config c=diag_monitor_state.cfg;
            diag_apply_sensitivity(&c,level);
            diag_reset_monitor_baseline(c);
            if(diag_save_monitor_config(&c)<0) fprintf(stderr,"MONITOR_CONFIG_SAVE_FAILED PATH=%s\n",MONITOR_CONFIG_PATH);
            printf("MONITOR_SENSITIVITY_CHANGED LEVEL=%u\n",level);
        } else {
            if(diag_monitor_state.state==DIAG_MONITOR_FAULT) return 1;
            diag_monitor_state.cfg.recovery_hold_ms=
                diag_next_recovery_hold_ms(diag_monitor_state.cfg.recovery_hold_ms);
            if(diag_save_monitor_config(&diag_monitor_state.cfg)<0)
                fprintf(stderr,"MONITOR_CONFIG_SAVE_FAILED PATH=%s\n",MONITOR_CONFIG_PATH);
            printf("MONITOR_RECOVERY_HOLD_CHANGED MS=%llu\n",
                   (unsigned long long)diag_monitor_state.cfg.recovery_hold_ms);
        }
        view_dirty=1;
        return 1;
    }
    /* SWEEP configuration uses the same proven numeric keypad as GEN.  This
     * must be handled before the diagnostic page navigation consumes the
     * touch. */
    if(diag_ui_state.mode==DIAG_UI_DIAG && diag_ui_state.page==DIAG_PAGE_SWEEP &&
       gen_edit_field>=3) return gen_edit_touch(x,y,w,h);
    /* The dedicated OPTIONS page keeps setting controls away from the trace.
     * Its four large buttons open the numeric keypad; BACK restores the full
     * SWEEP plot. */
    if(diag_ui_state.mode==DIAG_UI_DIAG && diag_ui_state.page==DIAG_PAGE_SWEEP &&
       sweep_options_panel && !sweep.active) {
        if(y>=140 && y<195) { sweep_begin_edit(x<400?3:4); return 1; }
        else if(y>=205 && y<260) { sweep_begin_edit(x<400?6:7); return 1; }
        else if(y>=270 && y<325) { sweep_begin_edit(x<400?8:9); return 1; }
        else if(y>=335 && y<390) {
            if(x<400) sweep_begin_edit(5);
            else { sweep_options_panel=0; view_dirty=1; }
            return 1;
        }
    }
    /* DIAG has its own top selector and five-key navigation.  Consume these
     * touches before the legacy SCOPE page handler so the original six keys
     * and all sliders retain their exact coordinates and behavior. */
    if(diag_ui_state.mode==DIAG_UI_DIAG && diag_ui_state.page==DIAG_PAGE_REPORT) {
        if(y>=38 && y<78 && x>=585 && x<790) {
            if(diag_export_report()<0) fprintf(stderr,"DIAG_REPORT_EXPORT_FAILED PATH=%s\n",REPORT_RESULT_PATH);
            else printf("DIAG_REPORT_EXPORT_TOUCH_REPORT_PAGE\n");
            view_dirty=1; return 1;
        }
        if(y>=320 && y<360 && x>=20 && x<780) {
            if(x<390) diag_report_select_event(-1);
            else diag_report_select_event(1);
            view_dirty=1; return 1;
        }
        if(y>=370 && y<415 && x>=20 && x<780) {
            if(x<270) diag_ui_state.page=DIAG_PAGE_MONITOR;
            else if(x<535) {
                if(diag_export_report()<0) fprintf(stderr,"DIAG_REPORT_EXPORT_FAILED PATH=%s\n",REPORT_RESULT_PATH);
                else printf("DIAG_REPORT_EXPORT_TOUCH_REPORT_PAGE\n");
            } else {
                diag_ui_state.mode=DIAG_UI_SCOPE;
                if(tool_page==5) { lf681_init(&lf_history681); trigger_revision++; }
            }
            view_dirty=1; return 1;
        }
        if(y>=34 && y<433) return 1;
    }
    /* REFLECT RAW owns its mode/action buttons.  Reserve those hit regions
     * before the generic DIAG selector: otherwise the legacy navigation
     * handler consumes y=400 and returns to MONITOR before START/CAPTURE REV
     * can reach the two-stage MANUAL session.  The top DIAG selector remains
     * available because only the raw page's action bands are reserved. */
    int reflect_raw_action_reserved =
        (diag_ui_state.mode==DIAG_UI_DIAG &&
         diag_ui_state.page==DIAG_PAGE_REFLECT && reflect_raw_candidate_enabled &&
         (((y>=101 && y<125) && (x>=515 && x<775)) ||
          (y>=382 && y<421 && x>=20 && x<775)));
    if(!reflect_raw_action_reserved && diag_ui_touch(&diag_ui_state,w,h,x,y)) {
        if(tool_page==5) { lf681_init(&lf_history681); trigger_revision++; }
        if(diag_ui_state.mode!=DIAG_UI_DIAG || diag_ui_state.page!=DIAG_PAGE_SWEEP)
            sweep_options_panel=0;
        if(reflect.active && (diag_ui_state.mode!=DIAG_UI_DIAG || diag_ui_state.page!=DIAG_PAGE_REFLECT))
            reflect_stop();
        view_dirty=1;
        printf("DIAG_TOUCH MODE=%s PAGE=%d\n",
               diag_ui_state.mode==DIAG_UI_DIAG?"DIAG":"SCOPE",diag_ui_state.page);
        return 1;
    }
    if(diag_replay_touch(x,y)) { view_dirty=1; return 1; }
    if(diag_select_history_row(x,y)) {
        view_dirty=1;
        printf("DIAG_REPLAY_SELECT SEQ=%u INDEX=%u LOADED=%d\n",
               diag_last_event.sequence,diag_selected_event_index,diag_replay_valid);
        return 1;
    }
    if(diag_ui_state.mode==DIAG_UI_DIAG && diag_ui_state.page==DIAG_PAGE_SWEEP &&
       y>=370 && y<420) {
        if(x>=25 && x<160) {
            if(!sweep.active) sweep_begin(0);
            return 1;
        }
        if(x>=165 && x<280) {
            sweep_stop();
            return 1;
        }
        if(x>=285 && x<430) {
            if(!sweep.active) sweep_begin(1);
            return 1;
        }
        if(x>=435 && x<580) {
            if(!sweep.active) sweep_begin(2);
            return 1;
        }
        if(x>=585 && x<675 && !sweep.active) {
            if(sweep_save_scalar()<0) fprintf(stderr,"SWEEP_SCALAR_SAVE_FAILED PATH=%s\n",SWEEP_SCALAR_PATH);
            else printf("SWEEP_SCALAR_EXPORT_TOUCH PATH=%s\n",SWEEP_SCALAR_PATH);
            view_dirty=1;
            return 1;
        }
        if(x>=685 && !sweep.active) {
            if(sweep_save_result()<0) fprintf(stderr,"SWEEP_CSV_SAVE_FAILED PATH=%s\n",SWEEP_RESULT_PATH);
            else if(diag_export_report()<0) fprintf(stderr,"DIAG_REPORT_EXPORT_FAILED PATH=%s\n",REPORT_RESULT_PATH);
            else printf("SWEEP_CSV_EXPORT_TOUCH PATH=%s\n",SWEEP_RESULT_PATH);
            view_dirty=1;
            return 1;
        }
    }
    if(diag_ui_state.mode==DIAG_UI_DIAG && diag_ui_state.page==DIAG_PAGE_SWEEP &&
       y>=155 && y<205 && !sweep.active) {
        if(x>=600) {
            sweep_options_panel=1;
            view_dirty=1;
        }
        return 1;
    }
    if(diag_ui_state.mode==DIAG_UI_DIAG && diag_ui_state.page==DIAG_PAGE_SWEEP &&
       y>=205 && y<250 && !sweep.active) {
        if(x>=440 && x<570) sweep_begin_edit(7);
        else if(x>=570 && x<700) sweep_begin_edit(8);
        else if(x<190) sweep_begin_edit(6);
        else if(x<380) sweep_begin_edit(7);
        else if(x<570) sweep_begin_edit(8);
        else sweep_begin_edit(9);
        return 1;
    }
    /* A tap in the completed SWEEP plot positions the active A/B cursor at
     * the nearest measured point, then alternates the active cursor. */
    if(diag_ui_state.mode==DIAG_UI_DIAG && diag_ui_state.page==DIAG_PAGE_SWEEP &&
       y>=286 && y<356 && x>=25 && x<=775 && !sweep.active && sweep.valid_points) {
        unsigned p=(unsigned)lround((double)(x-25)*(sweep.valid_points-1)/750.0);
        if(p>=sweep.valid_points) p=sweep.valid_points-1;
        sweep_cursor_point[sweep_cursor_selected]=p;
        printf("SWEEP_CURSOR %c POINT=%u FREQ=%.6fMHZ DB=%.6f\n",
               sweep_cursor_selected?'B':'A',p+1,sweep.freq_mhz[p],sweep.relative_db[p]);
        sweep_cursor_selected=1-sweep_cursor_selected;
        view_dirty=1;
        return 1;
    }
    if(diag_ui_state.mode==DIAG_UI_DIAG && diag_ui_state.page==DIAG_PAGE_REFLECT &&
       reflect_raw_candidate_enabled) {
        if(w==800 && h==480 && y>=101 && y<125 && x>=515 && x<640 &&
           !reflect_ps_exclusive()) {
            reflect_manual_mode=reflect_manual_only?1:!reflect_manual_mode;
            reflect_manual_phase=0;
            memset(&reflect_manual_session,0,sizeof(reflect_manual_session));
            fr_ui_set_manual_mode(&reflect_raw_ui,reflect_manual_mode);
            view_dirty=1;
            printf("REFLECT_MODE %s\n",reflect_manual_mode?"MANUAL":"AUTO");
            return 1;
        }
        if(w==800 && h==480 && y>=101 && y<125 && x>=650 && x<775 && !reflect_vector_view) {
            if(!reflect_ps_exclusive()) {
                if(!reflect_vector_ui.initialized)fr_vector_ui_init(&reflect_vector_ui);
                reflect_vector_view=1;view_dirty=1;
            }
            return 1;
        }
        if(reflect_vector_view) {
            if(!reflect_ps_exclusive()) {
                int action=fr_vector_ui_touch(&reflect_vector_ui,0,x,y,DIAG_STORAGE_DIR);
                if(action==2)reflect_vector_view=0;
                if(action)view_dirty=1;
            }
            return 1;
        }
        reflect_raw_prepare_ui();
        if(w==800 && h==480 && y>=382 && y<421 && x>=20 && x<166) {
            if(reflect_manual_mode && reflect_manual_phase==2)
                reflect_raw_ui.notice=FR_UI_MANUAL_CAPTURE_REV;
            int rc=reflect_backend_begin();
            reflect_backend_notice();view_dirty=1;
            printf("REFLECT_RAW_START RESULT=%d NOTICE=%d NEXT_START_PERMIT=%d\n",
                   rc,reflect_raw_ui.notice,reflect_backend_permit==1);
            return 1;
        }
        /* Stop owned raw resources before STOP/CLEAR modifies the UI model.
         * Keep the adapter's token/owner alive until asynchronous cleanup. */
        if(y>=382 && y<421 && ((x>=172 && x<318) || (x>=324 && x<470)))
            reflect_platform_abort();
        if(y>=382 && y<421 && ((x>=172 && x<318) || (x>=324 && x<470)))
            (void)fr_ps_stop(&reflect_ps_adapter,FR_PS_STOP,
                            (uint64_t)(seconds()*1000000.0));
        /* MANUAL export owns the actual EXPORT key, not CLEAR. */
        if(reflect_manual_mode && y>=382 && y<421 && x>=476 && x<622) {
            reflect_raw_ui.notice=reflect_manual_export(REFLECT_RAW_RESULT_PATH)==0 ?
                FR_UI_EXPORT_OK:FR_UI_EXPORT_FAILED;
            view_dirty=1;
            return 1;
        }
        if(fr_ui_touch(&reflect_raw_ui,x,y,w,h,REFLECT_RAW_RESULT_PATH)) {
            if(y>=382 && y<421 && x>=324 && x<470) {
                reflect_clear_requested=1;
                reflect_manual_phase=0;
                memset(&reflect_manual_session,0,sizeof(reflect_manual_session));
            }
            reflect_backend_notice();
            view_dirty=1;
            printf("REFLECT_RAW_TOUCH NOTICE=%d SELECTED=%u NEXT_START_PERMIT=%d\n",
                   reflect_raw_ui.notice,reflect_raw_ui.selected,reflect_backend_permit==1);
        }
        return 1; /* No legacy scalar CAL or DAC command from this page. */
    }
    if(diag_ui_state.mode==DIAG_UI_DIAG && diag_ui_state.page==DIAG_PAGE_REFLECT &&
       y>=350 && y<420 && x>=20 && x<w-20) {
        int slot=(x-20)/((w-40)/5);
        if(slot==0 && !reflect.active) reflect_begin(REFLECT_CAL_LOAD);
        else if(slot==1 && !reflect.active) reflect_begin(REFLECT_CAL_OPEN);
        else if(slot==2 && !reflect.active) reflect_begin(REFLECT_CAL_SHORT);
        else if(slot==3 && !reflect.active) reflect_begin(REFLECT_MEASURE);
        else if(slot==4) reflect_stop();
        return 1;
    }
    if(diag_ui_state.mode==DIAG_UI_DIAG && diag_ui_state.page==DIAG_PAGE_DTF) {
        if(!reflect_ps_exclusive() && fr_vector_ui_touch(&reflect_vector_ui,1,x,y,DIAG_STORAGE_DIR))
            view_dirty=1;
        return 1;
    }
    if(diag_ui_state.mode==DIAG_UI_DIAG) return 1;
    if(tool_page==4 && gen_edit_field) return gen_edit_touch(x,y,w,h);
    /* Frequency/amplitude are edited as numeric values.  The former slider
     * lanes remain visually documented but no longer require pixel-precise
     * dragging; tapping either value opens the keypad. */
    if(tool_page==4 && y>=h-190 && y<h-112 && x>=20 && x<w-20) {
        gen_begin_edit(x<w/2?1:2); return 1;
    }
    if(y>=h-47 && y<h && x>=20 && x<w-20) {
        /* Six keys share the row: five pages plus AUTO SET, which acts on the
         * newest measured frame and leaves the current page selected. */
        int tab=(x-20)/((w-40)/6);
        if(tab<0) tab=0;
        if(tab>5) tab=5;
        if(tab==5) {
            if(lf_scope_active681()) { lf681_init(&lf_history681); view_dirty=1; }
            else autoset();
            return 1;
        }
        if(tool_page==5) { lf681_init(&lf_history681); trigger_revision++; }
        tool_page=tab;
        if(tool_page==3) { spectrum_history_reset(); spectrum_cache_valid=0; }
        if(tool_page==2) { control_handler(SIGUSR1); cursor_auto=1; printf("CURSOR_AUTO_STOP_REQUEST\n"); }
        if(tool_page==4) gen_verify();
        view_dirty=1; return 1;
    }
    if(tool_page==3 && y>=h-110 && y<h-62 && x>=20 && x<w-20) {
        int step=(w-40)/6, action=(x-20)/step;
        if(action<0) action=0;
        if(action>5) action=5;
        if(action==0) spectrum_span_next();
        else if(action==1 || action==2) spectrum_marker_selected=(unsigned)(action-1);
        else if(action==3) spectrum_average_next();
        else if(action==4) spectrum_trace_reset();
        else if(!spectrum_cache_valid) spectrum_csv_status=SPECTRUM_CSV_NO_FRAME;
        else spectrum_csv_status=spectrum_export_csv()==0?SPECTRUM_CSV_SAVED:SPECTRUM_CSV_FAILED;
        printf("SPECTRUM_ACTION ACTION=%d SPAN=%.0f-%.0f AVG=%u M1=%.6f M2=%.6f\n",
               action,spectrum_start_mhz,spectrum_stop_mhz,spectrum_average_target,
               spectrum_marker_mhz(0),spectrum_marker_mhz(1));
        view_dirty=1; return 1;
    }
    if(tool_page==3 && y>=105 && y<h-184 && x>=23 && x<w-3 && spectrum_cache_valid) {
        int left=45,right=w-25;
        int k=spectrum_marker_set_from_x((int)spectrum_marker_selected,x,left,right);
        if(k>=0) {
            printf("SPECTRUM_MARKER M%d BIN=%d FREQ_MHZ=%.6f DBFS=%.3f\n",
                   spectrum_marker_selected+1,k,k*FFT_BIN_HZ/1e6,
                   spectrum_display_db(&spectrum_cache,k));
            view_dirty=1; return 1;
        }
    }
    if(tool_page==4 && y>=h-162 && y<h-114 && x>=20 && x<w-20) {
        t->gesture=x<w/2?9:10;
        drag_update(t,x,w); printf("GEN_SLIDER_BEGIN KIND=%d\n",t->gesture); return 1;
    }
    if(tool_page==4 && y>=h-110 && y<h-62 && x>=20 && x<w-20) {
        int b=(x-20)/((w-40)/5); if(b>4) b=4;
        gen_action(b); return 1;
    }
    if(tool_page==5 && y>=h-175 && y<h-47 && x>=20 && x<w-20) {
        if(y>=h-110 && x<240) {
            lf681_init(&lf_history681); view_dirty=1;
            printf("LOW_FREQ681_CLEAR\n");
        }
        return 1;
    }
    if(tool_page==3 && y>=h-162 && y<h-114) return 1;
    if(y>=h-162 && y<h-114 && x>=270) {
        if(x<530) { t->gesture=7; preview_level=trigger.level; }
        else { t->gesture=8; preview_pre=trigger.pre; }
        drag_update(t,x,w); printf("SLIDER_BEGIN KIND=%d\n",t->gesture); return 1;
    }
    if(tool_page==0 && y>=h-110 && y<h-62 && x>=20 && x<w-20) {
        t->gesture=4+(x-20)/((w-40)/3);
        if(t->gesture>6) t->gesture=6;
        drag_update(t,x,w); printf("SLIDER_BEGIN KIND=%d\n",t->gesture); return 1;
    }
    if(tool_page==1 && y>=h-110 && y<h-62) {
        if(x>=20 && x<w-160) { t->gesture=1; drag_update(t,x,w); }
        else if(x>=w-150 && x<w-10) { pan_samples=0; view_dirty=1; }
        return 1;
    }
    if(tool_page==2 && y>=h-110 && y<h-62) {
        if(!frozen_frame()) { printf("CURSOR_BLOCKED WAITING=%d PAUSED=%d FLAGS=%u\n",capture_waiting,(int)paused,last_flags); return 1; }
        if(x>=20 && x<100) cursor_enabled=!cursor_enabled;
        else if(x>=110 && x<190) cursor_selected=!cursor_selected;
        else if(x>=200 && x<290) tool_action(20,1);
        else if(x>=308 && x<w-10 && cursor_enabled) {
            t->fine_lo=cursor_index[cursor_selected]-20;
            t->fine_hi=cursor_index[cursor_selected]+20;
            if(t->fine_lo<0) t->fine_lo=0;
            if(t->fine_hi>=N) t->fine_hi=N-1;
            t->gesture=3; drag_update(t,x,w); printf("CURSOR_SLIDER_BEGIN SELECT=%d\n",cursor_selected);
        }
        view_dirty=1; return 1;
    }
    if(tool_page==2 && frozen_frame() && cursor_enabled && y>=105 && y<h-184 && x>=23 && x<w-3) {
        int visible=visible_samples(view),start=window_start(visible,last_trigger_index,pan_samples);
        int picked=-1,best=23;
        for(int k=0;k<2;k++) {
            int j=k?1-cursor_selected:cursor_selected,position=cursor_index[j]-start;
            if(position<0 || position>=visible) continue;
            int distance=abs(x-(45+position*(w-70)/visible));
            if(distance<best) { best=distance; picked=j; }
        }
        if(picked>=0) { cursor_selected=picked; t->gesture=2; view_dirty=1; }
        return 1;
    }
    return 0;
}
static int touch_event(struct touch *t,const struct input_event *e,int w,int h) {
    if(e->type==EV_SYN && e->code==SYN_DROPPED) {
        view_drag=0; trigger_drag=0;
        fine_drag_x=-1; t->gesture=0; t->dropped=1; t->latched=1; t->valid_x=t->valid_y=0; return -1;
    }
    if(e->type==EV_ABS) {
        if(e->code==ABS_MT_SLOT) t->slot=e->value;
        else if(e->code==ABS_X || (e->code==ABS_MT_POSITION_X && t->slot==0)) {
            t->x=e->value; t->valid_x=1;
        } else if(e->code==ABS_Y || (e->code==ABS_MT_POSITION_Y && t->slot==0)) {
            t->y=e->value; t->valid_y=1;
        } else if(e->code==ABS_MT_TRACKING_ID && t->slot==0) t->down=e->value>=0;
    } else if(e->type==EV_KEY && e->code==BTN_TOUCH) t->down=e->value!=0;
    if(e->type!=EV_SYN || e->code!=SYN_REPORT) return -1;
    if(!t->down) {
        if(view_drag) { view_drag=0; save_view(view); }
        if(trigger_drag) {
            if((trigger_drag==7 && preview_level!=trigger.level) || (trigger_drag==8 && preview_pre!=trigger.pre)) {
                if(trigger_drag==7) trigger.level=preview_level;
                else trigger.pre=preview_pre;
                trigger_revision++;
                printf("TRIGGER_SLIDER_COMMIT LEVEL=%d PRE=%d REV=%u\n",trigger.level,trigger.pre,trigger_revision);
            }
            trigger_drag=0;
        }
        if(t->gesture==9 || t->gesture==10)
            printf("GEN_SLIDER_END FREQ=%.6fMHZ AMP_DIGITAL=%d%% STEP_MHZ=0.001 PENDING=%d\n",gen_freq_khz/1000.0,gen_amp_pct,gen_pending);
        else if(t->gesture) printf("SLIDER_END KIND=%d CURSOR=%d\n",t->gesture,cursor_index[cursor_selected]);
        /* evdev suppresses unchanged ABS coordinates across contacts. Keep
         * the last known axes on a normal release, otherwise same-row keys
         * and repeated taps silently fail. SYN_DROPPED still invalidates them. */
        fine_drag_x=-1; t->gesture=0; t->latched=0; t->dropped=0; return -1;
    }
    if(t->dropped || !t->valid_x || !t->valid_y) return -1;
    if(t->x<t->ax.minimum || t->x>t->ax.maximum || t->y<t->ay.minimum || t->y>t->ay.maximum) return -1;
    int x=(int)((int64_t)(t->x-t->ax.minimum)*(w-1)/(t->ax.maximum-t->ax.minimum));
    int y=(int)((int64_t)(t->y-t->ay.minimum)*(h-1)/(t->ay.maximum-t->ay.minimum));
    if(t->gesture) { drag_update(t,x,w); return -1; }
    if(t->latched) return -1;
    t->latched=1;
#ifdef MONITOR_ASYNC694
    if(mask694_exit_hold) {
        if(x>=476 && x<622 && y>=375 && y<420)mask694_exit_retry=1;
        return -1;
    }
#endif
    int power=power_hit(w,h,x,y);
    if(power || power_dialog) { if(power) power_action(power); return -1; }
    if(drag_press(t,x,y,w,h)) return -1;
    int b=button_at(w,x,y);
    if(b<0) b=view_button_at(w,h,x,y);
    if(b<0) b=trigger_button_at(w,h,x,y);
    printf("TOUCH_PRESS raw=%d,%d screen=%d,%d button=%d\n",t->x,t->y,x,y,b);
    return b;
}
static void touch_poll(struct touch *t,int w,int h) {
    if(t->fd<0) return;
    for(int i=0;i<256;i++) {
        struct input_event e; ssize_t n=read(t->fd,&e,sizeof(e));
        if(n<0 && (errno==EAGAIN || errno==EINTR)) break;
        if(n!=(ssize_t)sizeof(e)) {
            view_drag=0; trigger_drag=0;
            fine_drag_x=-1; t->gesture=0;
            fprintf(stderr,"Touch read failed; UART only\n"); close(t->fd); t->fd=-1; break;
        }
        int b=touch_event(t,&e,w,h);
        if(b==0) control_handler(SIGCONT);
        else if(b==1) control_handler(SIGUSR1);
        else if(b==2 && lf_scope_active681()) printf("LOW_FREQ681_SINGLE_UNSUPPORTED USE_PAUSE\n");
        else if(b==2) control_handler(SIGUSR2);
        else if(b>=3 && b<=8) apply_view_button(b);
        else if(b>=9 && b<=14) {
            trigger=trigger_action(trigger,b); trigger_revision++;
            printf("TRIGGER_CONFIG MODE=%d LEVEL=%d FALLING=%d PRE=%d\n",trigger.mode,trigger.level,trigger.falling,trigger.pre);
        }
        else if(b>=15 && b<=21) tool_action(b,paused && !single_pending && !capture_waiting && (last_flags&1));
    }
}
static uint32_t channel(unsigned x,struct fb_bitfield b) {
    return b.length ? (uint32_t)(((uint64_t)x*((1ULL<<b.length)-1)+127)/255)<<b.offset : 0;
}
static uint32_t packed(uint32_t rgb,const struct fb_var_screeninfo *v) {
    return channel((rgb>>16)&255,v->red)|channel((rgb>>8)&255,v->green)|
           channel(rgb&255,v->blue)|channel(255,v->transp);
}
static int open_screen(struct screen *s,int probe) {
    memset(s,0,sizeof(*s)); s->fd=open("/dev/fb0",O_RDWR);
    if(s->fd<0) { perror("fb0"); return -1; }
    if(ioctl(s->fd,FBIOGET_VSCREENINFO,&s->v)<0 || ioctl(s->fd,FBIOGET_FSCREENINFO,&s->f)<0) {
        perror("fb ioctl"); close(s->fd); return -1;
    }
    s->w=s->v.xres; s->h=s->v.yres; s->bpp=s->v.bits_per_pixel/8;
    printf("FB visible=%ux%u virtual=%ux%u bpp=%u stride=%u offset=%u,%u memory=%u visual=%u\n",
           s->v.xres,s->v.yres,s->v.xres_virtual,s->v.yres_virtual,s->v.bits_per_pixel,
           s->f.line_length,s->v.xoffset,s->v.yoffset,s->f.smem_len,s->f.visual);
    printf("FB channels R=%u/%u G=%u/%u B=%u/%u A=%u/%u\n",s->v.red.offset,s->v.red.length,
           s->v.green.offset,s->v.green.length,s->v.blue.offset,s->v.blue.length,s->v.transp.offset,s->v.transp.length);
    if(probe) { close(s->fd); return 0; }
    if(s->w<480 || s->h<272 || s->w>1920 || s->h>1200 ||
       (s->v.bits_per_pixel!=16 && s->v.bits_per_pixel!=24 && s->v.bits_per_pixel!=32) ||
       s->v.nonstd || s->f.visual!=FB_VISUAL_TRUECOLOR || s->f.type!=FB_TYPE_PACKED_PIXELS ||
       s->v.red.length>8 || s->v.green.length>8 || s->v.blue.length>8 || s->v.transp.length>8 ||
       s->v.red.msb_right || s->v.green.msb_right || s->v.blue.msb_right || s->v.transp.msb_right ||
       s->v.red.offset+s->v.red.length>s->v.bits_per_pixel ||
       s->v.green.offset+s->v.green.length>s->v.bits_per_pixel ||
       s->v.blue.offset+s->v.blue.length>s->v.bits_per_pixel ||
       s->v.transp.offset+s->v.transp.length>s->v.bits_per_pixel ||
       ((uint64_t)s->v.xoffset+s->w)*s->bpp>s->f.line_length ||
       ((uint64_t)s->v.yoffset+s->h-1)*s->f.line_length+((uint64_t)s->v.xoffset+s->w)*s->bpp>s->f.smem_len) {
        fprintf(stderr,"Unsupported framebuffer layout\n"); close(s->fd); return -1;
    }
    s->fb=mmap(NULL,s->f.smem_len,PROT_READ|PROT_WRITE,MAP_SHARED,s->fd,0);
    if(s->fb==MAP_FAILED) { perror("fb mmap"); close(s->fd); return -1; }
    s->rgb=calloc((size_t)s->w*s->h,sizeof(*s->rgb));
    if(!s->rgb) { munmap(s->fb,s->f.smem_len); close(s->fd); return -1; }
    for(unsigned i=0;i<256;i++) {
        s->red[i]=channel(i,s->v.red); s->green[i]=channel(i,s->v.green);
        s->blue[i]=channel(i,s->v.blue);
    }
    s->alpha=channel(255,s->v.transp);
    return 0;
}
static void pixel(struct screen *s,int x,int y,uint32_t c) {
    if(x>=0 && y>=0 && x<s->w && y<s->h) s->rgb[(size_t)y*s->w+x]=c;
}
static void line(struct screen *s,int x,int y,int x1,int y1,uint32_t c) {
    int dx=abs(x1-x),dy=-abs(y1-y),sx=x<x1?1:-1,sy=y<y1?1:-1,e=dx+dy;
    for(;;) { pixel(s,x,y,c); if(x==x1 && y==y1) break;
        int e2=2*e; if(e2>=dy) { e+=dy; x+=sx; } if(e2<=dx) { e+=dx; y+=sy; }
    }
}
static void plot_line(struct screen *s,int x,int y,int x1,int y1,uint32_t c,
                      int left,int top,int right,int bottom) {
    int dx=abs(x1-x),dy=-abs(y1-y),sx=x<x1?1:-1,sy=y<y1?1:-1,e=dx+dy;
    for(;;) {
        if(x>=left && x<=right && y>=top && y<=bottom) pixel(s,x,y,c);
        if(x==x1 && y==y1) break;
        int e2=2*e; if(e2>=dy) { e+=dy; x+=sx; } if(e2<=dx) { e+=dx; y+=sy; }
    }
}
static void text(struct screen *s,int x,int y,const char *str,int scale,uint32_t c) {
    for(;*str;str++,x+=6*scale) {
        int idx=*str>='0'&&*str<='9'?*str-'0':(*str>='A'&&*str<='Z'?*str-'A'+10:-1);
        for(int row=0;row<7;row++) {
            unsigned bits=idx>=0?glyphs[idx][row]:0;
            if(*str=='.' && row==6) bits=4;
            if(*str=='-' && row==3) bits=14;
            if(*str==':' && (row==2 || row==5)) bits=4;
            if(*str=='/' && row<5) bits=1U<<row;
            for(int col=0;col<5;col++) if(bits&(1U<<(4-col)))
                for(int j=0;j<scale;j++) for(int k=0;k<scale;k++) pixel(s,x+col*scale+k,y+row*scale+j,c);
        }
    }
}
static void power_box(struct screen *s,int x,int y,int w,int h,const char *label,uint32_t ink) {
    line(s,x,y,x+w-1,y,ink); line(s,x,y+h-1,x+w-1,y+h-1,ink);
    line(s,x,y,x,y+h-1,ink); line(s,x+w-1,y,x+w-1,y+h-1,ink);
    text(s,x+(w-(int)strlen(label)*12)/2,y+(h-14)/2,label,2,ink);
}
/* CUSTOM owns these glyphs.  Do not expand or otherwise alter the established
 * renderer used by the measured SCOPE/FFT/diagnostic pages. */
static void custom_text(struct screen *s,int x,int y,const char *str,int scale,uint32_t c) {
    static const uint8_t lower[26][7]={
        {0,0,14,1,15,17,15},{16,16,30,17,17,17,30},
        {0,0,14,17,16,17,14},{1,1,15,17,17,17,15},
        {0,0,14,17,31,16,14},{6,9,8,28,8,8,8},
        {0,0,15,17,15,1,14},{16,16,30,17,17,17,17},
        {4,0,12,4,4,4,14},{2,0,6,2,2,18,12},
        {16,16,18,20,24,20,18},{12,4,4,4,4,4,14},
        {0,0,26,21,21,21,21},{0,0,30,17,17,17,17},
        {0,0,14,17,17,17,14},{0,0,30,17,30,16,16},
        {0,0,15,17,15,1,1},{0,0,22,25,16,16,16},
        {0,0,15,16,14,1,30},{8,8,28,8,8,9,6},
        {0,0,17,17,17,19,13},{0,0,17,17,17,10,4},
        {0,0,17,17,21,21,10},{0,0,17,10,4,10,17},
        {0,0,17,17,15,1,14},{0,0,31,2,4,8,31}
    };
    for(;*str;str++,x+=6*scale) {
        unsigned char ch=(unsigned char)*str;
        int idx=ch>='0'&&ch<='9'?ch-'0':ch>='A'&&ch<='Z'?ch-'A'+10:-1;
        for(int row=0;row<7;row++) {
            unsigned bits=idx>=0?glyphs[idx][row]:0;
            if(ch>='a' && ch<='z') bits=lower[ch-'a'][row];
            else if(ch=='.' && row==6) bits=4;
            else if(ch==',' && (row==5 || row==6)) bits=row==5?4:8;
            else if(ch=='-' && row==3) bits=14;
            else if(ch=='+' && row>=1 && row<=5) bits=row==3?31:4;
            else if(ch=='*') bits=(row==1 || row==5)?17:(row==2 || row==4)?10:row==3?4:0;
            else if(ch=='/' && row>=1 && row<=5) bits=1u<<(row-1);
            else if(ch=='(') bits=(row==0 || row==6)?2:(row==1 || row==5)?4:8;
            else if(ch==')') bits=(row==0 || row==6)?8:(row==1 || row==5)?4:2;
            else if(ch==':' && (row==2 || row==5)) bits=4;
            else if(ch=='=' && (row==2 || row==4)) bits=31;
            else if(ch=='^') bits=row==0?4:row==1?10:row==2?17:0;
            else if(ch=='_' && row==6) bits=31;
            else if(idx<0 && !(ch>='a' && ch<='z') && ch!=' ' &&
                    !strchr(".,-+*/():=^_",ch)) {
                static const uint8_t unknown[7]={14,17,1,2,4,0,4};
                bits=unknown[row];
            }
            for(int col=0;col<5;col++) if(bits&(1u<<(4-col)))
                for(int j=0;j<scale;j++) for(int k=0;k<scale;k++)
                    pixel(s,x+col*scale+k,y+row*scale+j,c);
        }
    }
}
static void custom_text_wrapped(struct screen *s,int x,int y,const char *str,
                                unsigned columns,unsigned rows,uint32_t c) {
    if(!columns || !rows) return;
    char row[CUI_MAX_EXPR+1u];
    if(columns>CUI_MAX_EXPR) columns=CUI_MAX_EXPR;
    for(unsigned n=0;n<rows && *str;n++) {
        size_t length=strlen(str);
        if(length>columns) length=columns;
        memcpy(row,str,length); row[length]='\0';
        custom_text(s,x,y+(int)n*12,row,1,c);
        str+=length;
    }
}
static void render_power(struct screen *s) {
    power_box(s,s->w-260,4,150,30,"POWER",0xffb75e);
    if(!power_dialog) return;
    int left=s->w/2-230,top=s->h/2-110;
    for(int y=top;y<top+240;y++) for(int x=left;x<left+460;x++) pixel(s,x,y,0x182630);
    line(s,left,top,left+459,top,0xffb75e); line(s,left,top+239,left+459,top+239,0xffb75e);
    line(s,left,top,left,top+239,0xffb75e); line(s,left+459,top,left+459,top+239,0xffb75e);
    const char *title=power_dialog==2?"SHUTTING DOWN":power_dialog==3?"SHUTDOWN FAILED":"POWER OFF DEVICE?";
    text(s,left+35,top+25,title,3,0xffb75e);
    text(s,left+25,top+65,power_dialog==2?"DO NOT DISCONNECT POWER YET":"CONFIRM TO SAFELY SHUT DOWN LINUX",2,0xe4edf5);
    text(s,left+25,top+98,power_dialog==2?"WAIT FOR SYSTEM HALT BEFORE POWER SWITCH":"CANCEL LEAVES ACQUISITION UNCHANGED",1,0xe4edf5);
    if(power_dialog!=2) {
        power_box(s,left+20,s->h/2+55,180,55,"CANCEL",0xe4edf5);
        power_box(s,left+260,s->h/2+55,180,55,power_dialog==3?"RETRY":"POWER OFF",0xffb75e);
    }
}
static void flush_screen(struct screen *s) {
    uint64_t flush691=p690_now();
    if(packed_flush691 && fb691_copy(s->fb,s->f.smem_len,s->f.line_length,
       s->v.xoffset,s->v.yoffset,(unsigned)s->w,(unsigned)s->h,(unsigned)s->bpp,
       s->rgb,(size_t)s->w*s->h,s->red,s->green,s->blue,s->alpha)==0) {
        p690_end(P691_FLUSH,flush691);return;
    }
    for(int y=0;y<s->h;y++) {
        uint8_t *dst=s->fb+(size_t)(y+s->v.yoffset)*s->f.line_length+(size_t)s->v.xoffset*s->bpp;
        for(int x=0;x<s->w;x++) {
            uint32_t rgb=s->rgb[(size_t)y*s->w+x];
            uint32_t p=s->red[(rgb>>16)&255]|s->green[(rgb>>8)&255]|s->blue[rgb&255]|s->alpha;
            for(int k=0;k<s->bpp;k++) dst[x*s->bpp+k]=(uint8_t)(p>>(8*k));
        }
    }
    p690_end(P691_FLUSH,flush691);
}
static void reflect_raw_paint_line(void *ctx,int x,int y,int x1,int y1,uint32_t c) {
    line((struct screen *)ctx,x,y,x1,y1,c);
}
static void reflect_raw_paint_text(void *ctx,int x,int y,const char *t,int scale,uint32_t c) {
    text((struct screen *)ctx,x,y,t,scale,c);
}
static void render_diag_panel(struct screen *s,const uint32_t *a,unsigned frames,uint32_t token) {
    (void)a; (void)frames; (void)token;
    const uint32_t ink=0xe4edf5,grid=0x283b49,accent=0xffb75e,wave=0x00e0aa;
    char b[160];
    text(s,20,15,"Z20 DIAG",2,ink);
    power_box(s,320,4,85,30,"SCOPE",diag_ui_state.mode==DIAG_UI_SCOPE?accent:ink);
    power_box(s,410,4,85,30,"DIAG",diag_ui_state.mode==DIAG_UI_DIAG?accent:ink);
    if(diag_ui_state.page==DIAG_PAGE_MONITOR)
        power_box(s,600,38,175,24,"EXPORT REPORT",0x00e0aa);
    text(s,s->w-100,15,capture_waiting?"WAIT":paused?"STOP":"RUN",2,paused?accent:wave);
    struct diag_ui_snapshot snap=diag_ui_make_snapshot(&diag_ui_state,&diag_monitor_state,
        diag_have_sample?&diag_current_sample:NULL,diag_have_event?&diag_last_event:NULL);
    const char *page=snap.page_label?snap.page_label:"MONITOR";
    text(s,25,62,page,3,accent);
    if(diag_ui_state.page==DIAG_PAGE_MONITOR) {
        if(!diag_storage_error) {
            snprintf(b,sizeof(b),"SPEC %s <=%.1fM MIN%.0f / FAULT %u / UNCONF %u / GAP %.2f MS",
                mask686_state_name(),mask617_config.last_bin*FFT_BIN_HZ/1e6,mask617_config.floor_db,mask686_confirmed+(mask617_runtime.capture.active && mask617_runtime.capture.event.confirmed),
                mask686_transient+(mask617_runtime.capture.active && !mask617_runtime.capture.event.confirmed),
                monitor686_cadence.max_gap_us/1000.0);
            text(s,25,95,b,1,mask686_confirmed || mask686_transient?accent:ink);
        }
        const char *state=snap.state_label?snap.state_label:"BASELINE";
        if(diag_baseline_learning && diag_monitor_state.state!=DIAG_MONITOR_NO_SIGNAL)
            state="LEARNING BASELINE";
        snprintf(b,sizeof(b),"STATE  %s",state); text(s,25,112,b,2,state[0]=='F'?0xff6060:wave);
        snprintf(b,sizeof(b),"LIVE QUALITY: %s",diag_quality_label(&latest_metrics,latest_metrics_valid));
        text(s,430,115,b,1,latest_metrics_valid && latest_metrics.freq_valid &&
             !latest_metrics.rail && !latest_metrics.low_signal?wave:accent);
        text(s,430,130,"QUALITY LABEL ONLY / NO FILTERING",1,ink);
        line(s,25,145,775,145,grid); line(s,25,300,775,300,grid);
        snprintf(b,sizeof(b),"CURRENT RMS       %.1f CODE",snap.current_rms); text(s,40,175,b,2,ink);
        snprintf(b,sizeof(b),"BASELINE RMS      %.1f CODE",snap.baseline_rms); text(s,40,210,b,2,ink);
        snprintf(b,sizeof(b),"SESSION EVENTS     %u",diag_session_event_count); text(s,430,175,b,2,ink);
        if(snap.last_duration_ms) snprintf(b,sizeof(b),"LAST DURATION     %llu MS",(unsigned long long)snap.last_duration_ms);
        else snprintf(b,sizeof(b),"LAST DURATION     --");
        text(s,430,210,b,2,ink);
        if(diag_monitor_state.state==DIAG_MONITOR_FAULT) {
            uint64_t now_ms=diag_current_sample.timestamp_ms;
            uint64_t elapsed=now_ms>=diag_monitor_state.event_start_ms?now_ms-diag_monitor_state.event_start_ms:0;
            if(diag_monitor_state.exit_count) {
                uint64_t held=now_ms>=diag_monitor_state.recovery_start_ms?now_ms-diag_monitor_state.recovery_start_ms:0;
                snprintf(b,sizeof(b),"#%u RECOVERY %llu / %llu MS",diag_monitor_state.event_sequence,
                         (unsigned long long)held,(unsigned long long)diag_monitor_state.cfg.recovery_hold_ms);
            } else snprintf(b,sizeof(b),"ACTIVE #%u  %llu MS",diag_monitor_state.event_sequence,(unsigned long long)elapsed);
            text(s,430,242,b,1,accent);
        }
        if(diag_storage_error) text(s,25,95,"STORAGE ERROR - EVIDENCE NOT CONFIRMED",1,0xff6060);
        snprintf(b,sizeof(b),"SEVERITY           %.1f %%",snap.severity*100.0); text(s,40,260,b,2,ink);
        text(s,430,260,"RMS RECOV HOLD 10-5000MS / NOT SAMPLE RATE",1,ink);
        if(diag_have_event) {
            snprintf(b,sizeof(b),"LAST EVENT   #%u   %llu MS   %.1f %%",
                diag_last_event.sequence,(unsigned long long)diag_last_event.duration_ms,
                diag_last_event.severity*100.0);
            /* Event metadata stays in the history; this row is phase control. */
            static const char *phase_names[]={"PRE","ONSET","WORST","RECOV","LIVE"};
            for(int p=0;p<5;p++) power_box(s,25+p*77,305,75,25,
                p==REPLAY_WORST && diag_replay_legacy?"LAST":phase_names[p],
                diag_replay_phase==(enum diag_replay_phase)p?accent:ink);
            if(diag_replay_phase==REPLAY_LIVE)
                snprintf(b,sizeof(b),"LIVE SELECTED / EVENT RETAINED");
            else if(diag_freeze_ready && diag_freeze_sequence==diag_last_event.sequence)
                snprintf(b,sizeof(b),"FROZEN %u FRAMES",diag_freeze_frames);
            else snprintf(b,sizeof(b),"FREEZE UNAVAILABLE");
            text(s,430,315,b,1,diag_freeze_ready?wave:accent);
            /* Compact event history: newest event first.  This is deliberately
             * metadata-only for now; the frozen binary remains the source for
             * raw waveform replay and is not altered by the list view. */
            text(s,430,345,"EVENT HISTORY",1,wave);
            unsigned shown=diag_event_history_count<3?diag_event_history_count:3;
            for(unsigned j=0;j<shown;j++) {
                unsigned idx=diag_event_history_count-1-j;
                snprintf(b,sizeof(b),"%c#%u  %llums  %.1f%%",
                    idx==diag_selected_event_index?'*':' ',
                    diag_event_history[idx].sequence,
                    (unsigned long long)diag_event_history[idx].duration_ms,
                    diag_event_history[idx].severity*100.0);
                text(s,430,365+(int)j*18,b,1,ink);
            }
            line(s,420,330,420,420,grid);
            power_box(s,510,275,100,28,"NEW TEST",0xe4edf5);
            snprintf(b,sizeof(b),"SENS%u",diag_sensitivity_level(&diag_monitor_state.cfg));
            power_box(s,610,275,60,28,b,0xffb75e);
            snprintf(b,sizeof(b),"H%lluMS",(unsigned long long)diag_monitor_state.cfg.recovery_hold_ms);
            power_box(s,670,275,105,28,b,0x00e0aa);
        } else {
            if(diag_baseline_learning) {
                uint64_t elapsed=diag_have_sample && diag_baseline_started_ms &&
                    diag_current_sample.timestamp_ms>=diag_baseline_started_ms ?
                    diag_current_sample.timestamp_ms-diag_baseline_started_ms:0;
                snprintf(b,sizeof(b),"LEARNING %u FRAMES  %llu / 2000 MS",
                         diag_monitor_state.baseline_count,(unsigned long long)elapsed);
                text(s,430,335,b,1,accent);
                text(s,430,355,"KEEP REFERENCE LINK NORMAL",1,ink);
            } else if(diag_baseline_completed_ms && diag_have_sample &&
                      diag_current_sample.timestamp_ms>=diag_baseline_completed_ms &&
                      diag_current_sample.timestamp_ms-diag_baseline_completed_ms<5000) {
                text(s,430,335,"NEW BASELINE READY",1,wave);
                snprintf(b,sizeof(b),"RMS %.1f CODE  FREQ %.6f MHZ",
                         diag_monitor_state.baseline_rms,diag_monitor_state.baseline_freq);
                text(s,430,355,b,1,ink);
            } else {
                text(s,430,335,"LIVE LINK FEEDBACK",1,wave);
                text(s,430,355,"WAITING FOR FIRST FAULT EVENT",1,ink);
            }
            power_box(s,510,275,100,28,"NEW TEST",0xe4edf5);
            snprintf(b,sizeof(b),"SENS%u",diag_sensitivity_level(&diag_monitor_state.cfg));
            power_box(s,610,275,60,28,b,0xffb75e);
            snprintf(b,sizeof(b),"H%lluMS",(unsigned long long)diag_monitor_state.cfg.recovery_hold_ms);
            power_box(s,670,275,105,28,b,0x00e0aa);
        }
        /* Raw samples remain visible during normal monitoring and baseline
         * learning.  A completed event with valid replay uses its immutable
         * frame; never label the current live ring as frozen evidence. */
        int show_frozen=diag_have_event && diag_replay_valid;
        if(!diag_have_event) text(s,25,322,"LIVE / AUTO SCALE",1,wave);
        if(show_frozen) {
            if(diag_replay_legacy) snprintf(b,sizeof(b),"LEGACY LAST FRAME / NO PHASE MAP");
            else snprintf(b,sizeof(b),"FRAME %u  MONO %llu MS / SPARSE",diag_replay_index,
                          (unsigned long long)diag_replay_timestamp);
        } else snprintf(b,sizeof(b),"%s",diag_replay_unavailable?"PHASE UNAVAILABLE / SHOWING LIVE":"LIVE / AUTO SCALE");
        text(s,25,423,b,1,show_frozen?accent:wave);
        line(s,25,335,410,335,grid); line(s,25,420,410,420,grid);
        if(show_frozen || diag_ring_count) {
            unsigned slot=(diag_ring_next+DIAG_RING_FRAMES-1)%DIAG_RING_FRAMES;
            const uint32_t *plot=show_frozen?diag_replay_frame:diag_frame_ring[slot];
            unsigned lo=UINT_MAX,hi=0;
            for(int i=0;i<N;i++) { if(plot[i]<lo) lo=plot[i]; if(plot[i]>hi) hi=plot[i]; }
            unsigned range=hi-lo;
            /* A minimum scale prevents a few codes of ADC noise from filling
             * the entire panel.  This changes only the drawing scale. */
            if(range<128) range=128;
            double centre=((double)hi+lo)/2.0;
            int px=25,py=378-(int)lround(((double)plot[0]-centre)*80.0/range);
            for(int i=1;i<N;i++) {
                int x=25+i*385/(N-1);
                int y=378-(int)lround(((double)plot[i]-centre)*80.0/range);
                line(s,px,py,x,y,wave); px=x; py=y;
            }
            snprintf(b,sizeof(b),"RAW VPP %u CODE",hi-lo);
            text(s,430,410,b,1,ink);
        } else text(s,40,370,"WAITING FOR ADC FRAME",1,ink);
    } else if(diag_ui_state.page==DIAG_PAGE_SWEEP) {
        text(s,25,112,"SWEEP - RELATIVE INSERTION LOSS (S21)",2,ink);
        snprintf(b,sizeof(b),"STATUS  %s",sweep.active?(sweep.mode==2?"FLAT CAL":sweep.mode==1?"CALIBRATING":"RUNNING"):
                 sweep.error?"ERROR":sweep.completed?"COMPLETE":"READY");
        text(s,25,150,b,2,sweep.error?0xff6060:sweep.active?wave:accent);
        snprintf(b,sizeof(b),"RANGE %.1f - %.1f MHZ   POINTS %u   %s   DRIVE %s",
                 sweep.start_mhz>0?sweep.start_mhz:1.0,sweep.stop_mhz>0?sweep.stop_mhz:SWEEP_MAX_MHZ,
                 sweep.points,sweep.logarithmic?"LOG":"LINEAR",
                 sweep_flatness.calibrated?"FLAT":"25 PCT");
        text(s,25,182,b,1,ink);
        power_box(s,600,155,175,35,"OPTIONS",0x00e0aa);
        text(s,600,195,"OPEN SETTINGS",1,0x9bb3c6);
        snprintf(b,sizeof(b),"PROGRESS %u / %u",sweep.valid_points,sweep.points);
        text(s,25,210,b,1,ink);
        snprintf(b,sizeof(b),"REF %s  FLAT %s  PASS %.1f DB  AVG %u RETRY %u",
                 sweep.calibrated?"READY":"--",sweep_flatness.calibrated?"READY":"--",sweep.pass_threshold_db,
                 sweep.average_frames,sweep.max_retries);
        text(s,250,210,b,1,sweep.calibrated?wave:0xffb75e);
        snprintf(b,sizeof(b),"RESULT  %s",sweep_result_label());
        text(s,600,228,b,1,!strcmp(sweep_result_label(),"PASS")?wave:
             !strcmp(sweep_result_label(),"FAIL")||!strcmp(sweep_result_label(),"LOW_SIGNAL")?0xff6060:0xffb75e);
        if(sweep.invalid_points) {
            snprintf(b,sizeof(b),"LOW-SIGNAL %u",sweep.invalid_points);
            text(s,600,210,b,1,0xff6060);
        }
        double active_freq=sweep.active?sweep_frequency(sweep.point):
            (sweep.valid_points?sweep.freq_mhz[sweep.valid_points-1]:0.0);
        /* Make the in-point acquisition visible as well as the completed
         * point count.  Previously this line stayed unchanged during the
         * four settling frames and four averaging frames, which made a
         * running sweep look frozen on the LCD even though the ADC was
         * working. */
        if(sweep.active)
            snprintf(b,sizeof(b),"ACTIVE POINT %u/%d  FREQ %.1f MHZ  ACQ %u/4 SET %u",
                     sweep.point+1,sweep.points,active_freq,
                     sweep.accum_frames,sweep.settle_frames);
        else
            snprintf(b,sizeof(b),"ACTIVE POINT %u/%d   FREQ %.1f MHZ",
                     sweep.valid_points,sweep.points,active_freq);
        text(s,25,230,b,1,sweep.active?wave:ink);
        double band_lo=0.0,band_hi=0.0;
        int band_points=sweep_pass_band(&band_lo,&band_hi);
        if(band_points>0)
            snprintf(b,sizeof(b),"PASS BAND %.1f - %.1f MHZ  (%d/%u POINTS)",band_lo,band_hi,band_points,sweep.points);
        else
            snprintf(b,sizeof(b),"PASS BAND --  (0/%u POINTS)",sweep.points);
        text(s,25,248,b,1,sweep.calibrated?ink:0xffb75e);
        /* Fractional progress advances during settle/average frames, so the
         * progress bar also moves between completed frequency points. */
        double progress_points=(double)sweep.valid_points;
        if(sweep.active) {
            double in_point=(double)(4-sweep.settle_frames)+sweep.accum_frames;
            progress_points=(double)sweep.point+in_point/8.0;
            if(progress_points<0.0) progress_points=0.0;
            if(progress_points>(double)sweep.points)
                progress_points=(double)sweep.points;
        }
        int progress_width=(int)lround(750.0*progress_points/sweep.points);
        line(s,25,260,775,260,grid); line(s,25,268,775,268,grid);
        if(progress_width>0) line(s,25,264,25+progress_width,264,wave);
        const uint32_t sweep_trace=sweep.mode==2?0xc070ff:sweep.mode==1?0x50b8ff:0xffd050;
        text(s,25,278,sweep.calibrated?
             "TOP: GREEN CAL / YELLOW RAW    BOTTOM: RELATIVE S21":
             (sweep.mode==2?"TOP: PURPLE SOURCE FLAT CAPTURE":
              sweep.mode==1?"TOP: BLUE CAL REF CAPTURE":"TOP: YELLOW RAW LOOPBACK RESPONSE"),1,sweep_trace);
        /* Split the plot into two different quantities.  The upper pane is
         * deliberately the uncorrected ADC RMS response so generator/ADC
         * ripple remains visible.  The lower pane is the calibrated ratio
         * 20*log10(measured/reference), which is the value used for S21 and
         * PASS/FAIL.  Keeping separate axes prevents the common high-end
         * response from looking like cable gain. */
        const int raw_top=286,raw_bottom=318,s21_top=322,s21_bottom=355;
        line(s,25,raw_top,775,raw_top,grid); line(s,25,raw_bottom,775,raw_bottom,grid);
        line(s,25,s21_top,775,s21_top,grid); line(s,25,s21_bottom,775,s21_bottom,grid);
        text(s,742,288,"RAW",1,0x9bb3c6); text(s,736,324,"S21",1,0x9bb3c6);
        int s21_zero_y=s21_top+(int)lround(3.0*(s21_bottom-s21_top)/12.0);
        double pass_db=sweep.pass_threshold_db;
        if(pass_db>3.0) pass_db=3.0;
        if(pass_db<-9.0) pass_db=-9.0;
        int s21_pass_y=s21_top+(int)lround((3.0-pass_db)*(s21_bottom-s21_top)/12.0);
        line(s,25,s21_zero_y,775,s21_zero_y,0x237a70);
        line(s,25,s21_pass_y,775,s21_pass_y,0x803030);
        text(s,752,s21_zero_y-8,"0",1,0x50d080);
        text(s,744,s21_pass_y-8,"LIM",1,0xff6060);
        if(sweep.valid_points || (sweep.calibrated && sweep.reference_points)) {
            double maxr=1.0;
            for(unsigned i=0;i<sweep.valid_points;i++) {
                if(sweep.rms_code[i]>maxr) maxr=sweep.rms_code[i];
            }
            if(sweep.calibrated)
                for(unsigned i=0;i<sweep.reference_points;i++) if(sweep.reference_rms[i]>maxr) maxr=sweep.reference_rms[i];
            if(sweep.calibrated) {
                /* Keep the actual CAL REF curve visible during START SWEEP.
                 * Both traces use the same raw-amplitude scale here, which
                 * makes frequency-dependent ripple and not just S21 visible. */
                int px=25,py=raw_bottom-(int)lround((sweep.reference_rms[0]/maxr)*(raw_bottom-raw_top-3));
                for(unsigned i=1;i<sweep.reference_points;i++) {
                    int x=25+(int)(i*750/(sweep.points-1));
                    int y=raw_bottom-(int)lround((sweep.reference_rms[i]/maxr)*(raw_bottom-raw_top-3));
                    line(s,px,py-1,x,y-1,0x50d080); line(s,px,py,x,y,0x50d080); line(s,px,py+1,x,y+1,0x50d080);
                    px=x; py=y;
                }
            }
            if(!sweep.valid_points) goto sweep_trace_done;
            int px=25,py=raw_bottom-(int)lround((sweep.rms_code[0]/maxr)*(raw_bottom-raw_top-3));
            for(unsigned i=1;i<sweep.valid_points;i++) {
                int x=25+(int)(i*750/(sweep.points-1));
                int y=raw_bottom-(int)lround((sweep.rms_code[i]/maxr)*(raw_bottom-raw_top-3));
                line(s,px,py-1,x,y-1,sweep_trace); line(s,px,py,x,y,sweep_trace); line(s,px,py+1,x,y+1,sweep_trace);
                px=x; py=y;
            }
            if(sweep.calibrated) {
                /* The two traces can be numerically identical on a healthy
                 * loopback.  Draw calibration markers after the yellow trace
                 * so CAL REF remains visible instead of being overpainted. */
                for(unsigned i=0;i<sweep.reference_points;i++) {
                    int x=25+(int)lround((double)i*750.0/(sweep.points-1));
                    int y=raw_bottom-(int)lround((sweep.reference_rms[i]/maxr)*(raw_bottom-raw_top-3));
                    line(s,x,y-4,x,y+4,0x50d080);
                    if(i+1<sweep.reference_points && (i%2)==0) {
                        int nx=25+(int)lround((double)(i+1)*750.0/(sweep.points-1));
                        int ny=raw_bottom-(int)lround((sweep.reference_rms[i+1]/maxr)*(raw_bottom-raw_top-3));
                        line(s,x,y,nx,ny,0x50d080);
                    }
                }
                /* Fixed +3 to -9 dB lower scale.  Values outside the pane
                 * are clipped visually but remain unchanged in CSV. */
                double first_db=sweep.relative_db[0];
                if(first_db>3.0) first_db=3.0;
                if(first_db<-9.0) first_db=-9.0;
                int rx=25,ry=s21_top+(int)lround((3.0-first_db)*(s21_bottom-s21_top)/12.0);
                for(unsigned i=1;i<sweep.valid_points;i++) {
                    double db=sweep.relative_db[i];
                    if(db>3.0) db=3.0;
                    if(db<-9.0) db=-9.0;
                    int x=25+(int)lround((double)i*750.0/(sweep.points-1));
                    int y=s21_top+(int)lround((3.0-db)*(s21_bottom-s21_top)/12.0);
                    line(s,rx,ry-1,x,y-1,0xffd050); line(s,rx,ry,x,y,0xffd050);
                    rx=x; ry=y;
                }
            }
sweep_trace_done: ;
        }
        if((sweep.active || sweep.completed) && sweep.valid_points>0) {
            /* Keep the sweep cursor after completion as a last-point marker. */
            unsigned cursor_point=sweep.active?sweep.point:sweep.valid_points-1;
            if(cursor_point>=sweep.points) cursor_point=sweep.points-1;
            int active_x=25+(int)lround((double)cursor_point*750.0/(sweep.points-1));
            line(s,active_x,raw_top+1,active_x,s21_bottom-1,0xffb75e);
            if(active_x>=775) line(s,active_x-1,raw_top+1,active_x-1,s21_bottom-1,0xffb75e);
            int marker_y=sweep.active?
                348-(int)lround(50.0*(double)sweep.accum_frames/(double)sweep.average_frames):348;
            line(s,active_x-4,marker_y,active_x+4,marker_y,0xffb75e);
            if(!sweep.active) text(s,650,278,"LAST POINT",1,0xffb75e);
        }
        if(!sweep.active && sweep.valid_points) {
            unsigned pa=sweep_cursor_point[0]<sweep.valid_points?sweep_cursor_point[0]:0;
            unsigned pb=sweep_cursor_point[1]<sweep.valid_points?sweep_cursor_point[1]:sweep.valid_points-1;
            unsigned cursor_span=sweep.valid_points>1?sweep.valid_points-1:1;
            int xa=25+(int)lround((double)pa*750.0/cursor_span);
            int xb=25+(int)lround((double)pb*750.0/cursor_span);
            line(s,xa,raw_top+1,xa,s21_bottom-1,0x58aaff); line(s,xb,raw_top+1,xb,s21_bottom-1,0xffffff);
            text(s,xa>760?760:xa,290,"A",1,0x58aaff);
            text(s,xb>760?760:xb,305,"B",1,0xffffff);
            snprintf(b,sizeof(b),"CUR A %.3fMHZ %.2fDB  B %.3fMHZ %.2fDB  D %.3fMHZ %.2fDB",
                     sweep.freq_mhz[pa],sweep.relative_db[pa],sweep.freq_mhz[pb],
                     sweep.relative_db[pb],sweep.freq_mhz[pb]-sweep.freq_mhz[pa],
                     sweep.relative_db[pb]-sweep.relative_db[pa]);
            text(s,25,358,b,1,ink);
        }
        power_box(s,25,370,135,45,"START",sweep.active?grid:accent);
        power_box(s,165,370,115,45,"STOP",sweep.active?accent:grid);
        power_box(s,285,370,145,45,"CAL REF",sweep.active?grid:accent);
        power_box(s,435,370,145,45,"FLAT CAL",sweep.active?grid:0xc070ff);
        power_box(s,585,370,90,45,"DATA",sweep.active?grid:accent);
        power_box(s,685,370,90,45,"CSV",sweep.active?grid:accent);
        snprintf(b,sizeof(b),"FLAT %s   S21 %s",sweep_flatness.calibrated?"READY":"NOT SET",
                 sweep.calibrated?"READY":"CAL REF FIRST");
        text(s,520,425,b,1,sweep_flatness.calibrated&&sweep.calibrated?wave:0xffb75e);
        if(sweep_options_panel && !sweep.active) {
            char opt[40];
            for(int y=90;y<425;y++) for(int x=15;x<785;x++) pixel(s,x,y,0x101820);
            line(s,15,90,784,90,accent); line(s,15,424,784,424,accent);
            text(s,35,102,"SWEEP OPTIONS",3,accent);
            text(s,35,132,"SELECT A PARAMETER FOR NUMERIC ENTRY",1,ink);
            snprintf(opt,sizeof(opt),"START %.3f MHZ",sweep.start_mhz);
            power_box(s,40,140,340,55,opt,wave);
            snprintf(opt,sizeof(opt),"STOP %.3f MHZ",sweep.stop_mhz);
            power_box(s,420,140,340,55,opt,wave);
            snprintf(opt,sizeof(opt),"POINTS %u",sweep.points);
            power_box(s,40,205,340,55,opt,wave);
            snprintf(opt,sizeof(opt),"AVERAGE %u",sweep.average_frames);
            power_box(s,420,205,340,55,opt,wave);
            snprintf(opt,sizeof(opt),"RETRIES %u",sweep.max_retries);
            power_box(s,40,270,340,55,opt,wave);
            power_box(s,420,270,340,55,sweep.logarithmic?"SCALE LOG":"SCALE LINEAR",wave);
            snprintf(opt,sizeof(opt),"PASS %.1f DB",sweep.pass_threshold_db);
            power_box(s,40,335,340,55,opt,wave);
            power_box(s,420,335,340,55,"BACK",accent);
        }
    } else if(diag_ui_state.page==DIAG_PAGE_REPORT) {
        struct monitor_event_health eh; memset(&eh,0,sizeof(eh));
        const char *label="NO EVENTS", *sev="--", *reason="NO_EVENT";
        double score=0.0; unsigned seq=0; uint64_t dur=0;
        const struct diag_monitor_event *latest=diag_event_history_count?
            &diag_event_history[diag_selected_event_index<diag_event_history_count?
                                diag_selected_event_index:diag_event_history_count-1]:NULL;
        if(latest && monitor_event_health_summarize(latest,&eh)==0) {
            label=eh.event_health_label; sev=eh.severity; reason=eh.quality_reason;
            score=eh.severity_score; seq=latest->sequence; dur=latest->duration_ms;
        }
        text(s,25,112,"LATEST REPORT SUMMARY",2,ink);
        snprintf(b,sizeof(b),"EVENT %s   %u/%u   SEQ %u   DURATION %llums",label,
                 diag_event_history_count?diag_selected_event_index+1:0,
                 diag_event_history_count,seq,(unsigned long long)dur);
        text(s,25,150,b,1,!strcmp(label,"FAIL")?0xff6060:!strcmp(label,"WARNING")?0xffb75e:wave);
        snprintf(b,sizeof(b),"SEVERITY %-6s   HEALTH SCORE %.3f   REASON %s",sev,score,reason);
        text(s,25,183,b,1,ink);
        snprintf(b,sizeof(b),"MONITOR EVENTS %u / %u   SENS %u   RECOV HOLD %lluMS",
                 diag_session_event_count,diag_event_history_count,
                 diag_sensitivity_level(&diag_monitor_state.cfg),
                 (unsigned long long)diag_monitor_state.cfg.recovery_hold_ms);
        text(s,25,210,b,1,ink);
        snprintf(b,sizeof(b),"SWEEP %s   POINTS %u   RANGE %.3f-%.3f MHZ   %s",
                 sweep.completed?sweep_result_label():"NOT RUN",sweep.valid_points,
                 sweep.start_mhz,sweep.stop_mhz,sweep.logarithmic?"LOG":"LINEAR");
        text(s,25,240,b,1,sweep.completed?wave:0xffb75e);
        snprintf(b,sizeof(b),"PASS LIMIT %.2f DB   AVG %u   RETRIES %u",
                 sweep.pass_threshold_db,sweep.average_frames,sweep.max_retries);
        text(s,25,265,b,1,ink);
        text(s,25,305,"REPORT IS A SESSION SUMMARY; CSV REMAINS THE AUTHORITATIVE RECORD",1,0x9bb3c6);
        power_box(s,20,320,360,38,"PREVIOUS EVENT",diag_event_history_count?accent:0x283b49);
        power_box(s,400,320,380,38,"NEXT EVENT",diag_event_history_count?accent:0x283b49);
        power_box(s,585,38,190,35,"EXPORT REPORT",wave);
        power_box(s,20,370,240,45,"BACK MONITOR",accent);
        power_box(s,280,370,240,45,"EXPORT CSV",wave);
        power_box(s,540,370,240,45,"SCOPE",ink);
    } else if(diag_ui_state.page==DIAG_PAGE_SELFTEST) {
        const char *adc=latest_metrics_valid?"FRAME OK":"WAITING FOR FRAME";
        const char *dac=gen_applied==1?"ON REQUESTED":gen_applied==2?"DIGITAL OFF":gen_applied==3?"COMMAND ERROR":"STATE UNKNOWN";
        const char *storage=access(DIAG_STORAGE_DIR,W_OK)==0?"WRITABLE":"NOT WRITABLE";
        const char *report=access(REPORT_RESULT_PATH,R_OK)==0?"AVAILABLE":"NOT YET EXPORTED";
        const char *sweep_ref=sweep.calibrated?"READY":"NOT SET";
        text(s,25,112,"SOFTWARE / DEVICE SELF-TEST",2,ink);
        snprintf(b,sizeof(b),"ADC CAPTURE       %-18s   FREQ %.3f MHZ",adc,latest_metrics_valid?latest_metrics.mhz:0.0);
        text(s,25,150,b,1,latest_metrics_valid?wave:0xffb75e);
        snprintf(b,sizeof(b),"DAC / GEN         %-18s   AMP %d PCT",dac,gen_amp_pct);
        text(s,25,178,b,1,gen_applied==3?0xff6060:ink);
        snprintf(b,sizeof(b),"MONITOR CONFIG  SENS %u  RECOV HOLD %lluMS 10-5000MS",
                 diag_sensitivity_level(&diag_monitor_state.cfg),
                 (unsigned long long)diag_monitor_state.cfg.recovery_hold_ms);
        text(s,25,206,b,1,ink);
        snprintf(b,sizeof(b),"SWEEP CONFIG       %u POINTS   %.1f-%.1f MHZ   REF %s",
                 sweep.points,sweep.start_mhz,sweep.stop_mhz,sweep_ref);
        text(s,25,234,b,1,sweep.calibrated?wave:0xffb75e);
        snprintf(b,sizeof(b),"REFLECT CAL        LOAD %s   OPEN %s   SHORT %s",
                 reflect.have_load?"OK":"--",reflect.have_open?"OK":"--",reflect.have_short?"OK":"--");
        text(s,25,262,b,1,reflect_ready()?wave:0xffb75e);
        snprintf(b,sizeof(b),"REPORT FILE        %-18s   DIR %s",report,storage);
        text(s,25,290,b,1,access(DIAG_STORAGE_DIR,W_OK)==0?wave:0xffb75e);
        text(s,25,326,"TOUCH INPUT        ACTIVE (THIS PAGE IS TOUCH-REACHABLE)",1,wave);
        text(s,25,354,"PL / RF             NOT PROBED   |   NO HARDWARE LOOPBACK CLAIM",1,0xffb75e);
        snprintf(b,sizeof(b),"SIGNAL TREND       FRAMES %u  AM/ASK %.1f%%  FSK SPAN %.1fKHZ  CHANGES %u",
                 signal_trend_cache.frames,
                 signal_trend_cache.amplitude_index_pct,
                 signal_trend_cache.frequency_span_hz/1000.0,
                 signal_trend_cache.observed_changes);
        text(s,25,382,b,1,signal_trend_cache.frames>=8?wave:0xffb75e);
        text(s,25,408,"SELF-TEST CHECKS SOFTWARE STATE AND FILE AVAILABILITY ONLY",1,0x9bb3c6);
    } else if(diag_ui_state.page==DIAG_PAGE_REFLECT && reflect_raw_candidate_enabled) {
        reflect_raw_prepare_ui();
        struct fr_ui_paint paint={s,reflect_raw_paint_line,reflect_raw_paint_text};
        if(reflect_vector_view)fr_vector_ui_render(&reflect_vector_ui,0,paint);
        else {
            fr_ui_render(&reflect_raw_ui,paint,s->w,s->h);
            power_box(s,515,101,125,24,"MANUAL",reflect_manual_mode?accent:ink);
            power_box(s,650,101,125,24,"VECTOR",reflect_ps_exclusive()?grid:accent);
        }
        if(reflect_ps_adapter.safety_errors)
            text(s,25,87,"RF CLEANUP UNCONFIRMED - DO NOT RESTART",1,0xffb75e);
    } else if(diag_ui_state.page==DIAG_PAGE_REFLECT) {
        text(s,25,105,"REFLECT - SINGLE ADC SCALAR",2,ink);
        snprintf(b,sizeof(b),"STATUS  %s",reflect.active?reflect_mode_name(reflect.mode):
                 reflect.error?"ERROR":reflect.completed?"COMPLETE":"READY");
        text(s,25,140,b,2,reflect.error?0xff6060:reflect.active?wave:accent);
        snprintf(b,sizeof(b),"RANGE %.1f - %.1f MHZ   POINTS %u   %s   AMP 10 PCT",
                 reflect_start_mhz(),reflect_stop_mhz(),reflect_points(),
                 sweep.logarithmic?"LOG":"LINEAR");
        text(s,25,170,b,1,ink);
        snprintf(b,sizeof(b),"CAL  LOAD %s   OPEN %s   SHORT %s",
                 reflect.have_load?"OK":"--",reflect.have_open?"OK":"--",reflect.have_short?"OK":"--");
        text(s,25,190,b,1,reflect_ready()?wave:0xffb75e);
        snprintf(b,sizeof(b),"PROGRESS %u/%u   AVG %u   RETRY %u",
                 reflect.valid_points,reflect_points(),sweep.average_frames,sweep.max_retries);
        text(s,430,190,b,1,ink);
        text(s,25,210,"APPROX MAGNITUDE ONLY - NO COMPLEX PHASE / DTF",1,0xffb75e);
        if(reflect.mode==REFLECT_MEASURE && reflect.valid_points) {
            unsigned i=reflect.valid_points-1;
            const char *quality=reflect_trend_quality(reflect.measured_rms[i],reflect.gamma[i]);
            if(strcmp(quality,"VALID_TREND"))
                snprintf(b,sizeof(b),"FREQ %.3f MHZ   REL REF N/A   %s",
                         reflect.freq_mhz[i],quality);
            else
                snprintf(b,sizeof(b),"FREQ %.3f MHZ   REL REF %.3f   TREND ONLY",
                         reflect.freq_mhz[i],reflect.gamma[i]);
            text(s,25,230,b,1,strcmp(quality,"VALID_TREND")?0xff6060:wave);
        } else {
            snprintf(b,sizeof(b),"CONNECT %s STANDARD, THEN PRESS ITS CAL KEY",
                     reflect.mode==REFLECT_CAL_OPEN?"OPEN":reflect.mode==REFLECT_CAL_SHORT?"SHORT":"50 OHM LOAD");
            text(s,25,230,b,1,ink);
        }
        line(s,25,250,775,250,grid); line(s,25,340,775,340,grid);
        if(reflect.valid_points) {
            if(reflect.mode==REFLECT_MEASURE) {
                int px=0,py=0,have_previous=0;
                for(unsigned i=0;i<reflect.valid_points;i++) {
                    if(strcmp(reflect_trend_quality(reflect.measured_rms[i],reflect.gamma[i]),"VALID_TREND")) {
                        have_previous=0; continue;
                    }
                    int x=25+(int)lround((double)i*750.0/(reflect_points()-1));
                    double g=fmin(fmax(reflect.gamma[i],0.0),1.0);
                    int y=340-(int)lround(g*80.0);
                    if(have_previous) {
                        line(s,px,py-1,x,y-1,wave); line(s,px,py,x,y,wave); line(s,px,py+1,x,y+1,wave);
                    }
                    line(s,x,y-2,x,y+2,wave);
                    px=x; py=y; have_previous=1;
                }
                text(s,625,255,"REL REF 0..1",1,wave);
            } else {
                const double *trace=reflect.mode==REFLECT_CAL_OPEN?reflect.open_rms:
                    reflect.mode==REFLECT_CAL_SHORT?reflect.short_rms:reflect.load_rms;
                double maxr=1.0; for(unsigned i=0;i<reflect.valid_points;i++) if(trace[i]>maxr) maxr=trace[i];
                int px=25,py=340-(int)lround(trace[0]/maxr*80.0);
                for(unsigned i=1;i<reflect.valid_points;i++) {
                    int x=25+(int)lround((double)i*750.0/(reflect_points()-1));
                    int y=340-(int)lround(trace[i]/maxr*80.0);
                    line(s,px,py,x,y,0x50b8ff); px=x; py=y;
                }
                text(s,640,255,"RAW RMS",1,0x50b8ff);
            }
        }
        const char *rb[] = {"CAL LOAD","CAL OPEN","CAL SHORT","MEASURE","STOP"};
        int rstep=(s->w-40)/5;
        for(int i=0;i<5;i++) {
            uint32_t c=i==4?(reflect.active?accent:grid):(reflect.active?grid:(i==3&&!reflect_ready()?grid:accent));
            power_box(s,20+i*rstep,350,rstep-6,65,rb[i],c);
        }
    } else {
        if(!reflect_vector_ui.initialized)fr_vector_ui_init(&reflect_vector_ui);
        struct fr_ui_paint paint={s,reflect_raw_paint_line,reflect_raw_paint_text};
        fr_vector_ui_render(&reflect_vector_ui,1,paint);
    }
    int step=(s->w-40)/7; const char *nav[]={"MONITOR","SWEEP","REFLECT","DTF","REPORT","SELFTEST","SCOPE"};
    for(int j=0;j<7;j++) {
        int x=20+j*step; uint32_t c=(j==6?0x00e0aa:(j==(int)diag_ui_state.page?accent:ink));
        line(s,x,s->h-47,x+step-6,s->h-47,c); line(s,x,s->h-1,x+step-6,s->h-1,c);
        line(s,x,s->h-47,x,s->h-1,c); line(s,x+step-6,s->h-47,x+step-6,s->h-1,c);
        text(s,x+(step-6-(int)strlen(nav[j])*12)/2,s->h-31,nav[j],2,c);
    }
}
static void render_custom_panel(struct screen *s) {
    const uint32_t ink=0xe4edf5,accent=0xffb75e,grid=0x283b49,wave=0x00e0aa;
    custom_init_if_needed();
    for(int y=40;y<s->h;y++) for(int x=0;x<s->w;x++) pixel(s,x,y,0x101820);
    custom_text(s,20,51,"CUSTOM - PREVIEW ONLY",2,accent);
    power_box(s,650,43,120,30,"BACK",ink);
    char b[128];
    if(custom_edit_field) {
        const char *field=custom_edit_field==1?"EDIT FORMULA":
            custom_edit_field==2?"EDIT INTEGER FREQUENCY HZ":
            custom_edit_field==3?"EDIT AMPLITUDE Q15":"EDIT OFFSET Q15";
        custom_text(s,20,87,field,2,ink);
        custom_text_wrapped(s,20,112,custom_edit_buf[0]?custom_edit_buf:"(empty)",
                            126,3,ink);
        custom_text_wrapped(s,20,167,custom_status,126,2,accent);
        const char *hint=custom_edit_field==1?"OK ACCEPTS BUFFER - BUILD IS ON THE MAIN PANEL":
            custom_edit_field==2?"VALID RANGE 1 TO 100000000 HZ - INTEGER ONLY":
            custom_edit_field==3?"VALID RANGE 0 TO 32767 Q15 - INTEGER ONLY":
            "VALID RANGE -32768 TO 32767 Q15 - INTEGER ONLY";
        custom_text(s,20,196,hint,1,grid);
        if(custom_edit_field==1) {
            for(int row=0;row<5;row++) for(int col=0;col<6;col++) {
                int x=16+col*128,y=214+row*43;
                const char *key=custom_formula_keys[row][col];
                uint32_t c=(!strcmp(key,"OK") || !strcmp(key,"CANCEL"))?accent:ink;
                line(s,x,y,x+119,y,c); line(s,x,y+37,x+119,y+37,c);
                line(s,x,y,x,y+37,c); line(s,x+119,y,x+119,y+37,c);
                custom_text(s,x+(120-(int)strlen(key)*12)/2,y+12,key,2,c);
            }
        } else {
            for(int row=0;row<4;row++) for(int col=0;col<4;col++) {
                int x=130+col*135,y=214+row*48;
                const char *key=custom_number_keys[row][col];
                if(!*key) continue;
                uint32_t c=(!strcmp(key,"OK") || !strcmp(key,"CANCEL"))?accent:ink;
                if(!strcmp(key,"-") && custom_edit_field!=4) c=grid;
                line(s,x,y,x+125,y,c); line(s,x,y+41,x+125,y+41,c);
                line(s,x,y,x,y+41,c); line(s,x+125,y,x+125,y+41,c);
                custom_text(s,x+(126-(int)strlen(key)*12)/2,y+14,key,2,c);
            }
        }
    } else {
        custom_text(s,20,86,"FORMULA - t IS NORMALIZED TIME FROM 0 TO 1",1,ink);
        custom_text_wrapped(s,20,101,custom_ui.expression[0]?custom_ui.expression:"(empty)",
                            126,3,ink);
        snprintf(b,sizeof(b),"FREQUENCY %u HZ",custom_ui.frequency_hz);
        custom_text(s,20,147,b,1,ink);
        snprintf(b,sizeof(b),"AMP %d Q15",custom_ui.amplitude_q15);
        custom_text(s,300,147,b,1,ink);
        snprintf(b,sizeof(b),"OFFSET %d Q15",(int)custom_ui.offset_q15);
        custom_text(s,505,147,b,1,ink);
        custom_text(s,20,164,"SOFTWARE RANGE IS NOT A VERIFIED ANALOG BANDWIDTH",1,grid);
        custom_text_wrapped(s,20,183,custom_status,126,1,
                            custom_apply_refused || custom_ui.state==CUI_ERROR?accent:wave);
        const char *fields[]={"FORMULA","FREQUENCY","AMPLITUDE","OFFSET"};
        for(int i=0;i<4;i++) {
            int x=20+i*190;
            line(s,x,207,x+174,207,ink); line(s,x,246,x+174,246,ink);
            line(s,x,207,x,246,ink); line(s,x+174,207,x+174,246,ink);
            custom_text(s,x+9,216,fields[i],1,ink);
            custom_text(s,x+9,232,"TAP TO EDIT",1,wave);
        }
        custom_text(s,25,258,"DIGITAL TABLE - NOT ADC OR ANALOG OUTPUT",1,grid);
        line(s,25,274,775,274,grid); line(s,25,316,775,316,grid);
        line(s,25,358,775,358,grid);
        if(custom_package_ready) {
            struct cwg_header h;
            int16_t samples[CWG_TABLE_LEN];
            enum cwg_error e=cwg_unpack(custom_ui.package,custom_ui.package_used,
                                        &h,samples,CWG_TABLE_LEN);
            if(e==CWG_OK) {
                int minimum=32767,maximum=-32768;
                for(unsigned i=0;i<CWG_TABLE_LEN;i++) {
                    if(samples[i]<minimum) minimum=samples[i];
                    if(samples[i]>maximum) maximum=samples[i];
                    if(i) {
                        int x0=25+(int)((i-1u)*750u/(CWG_TABLE_LEN-1u));
                        int x1=25+(int)(i*750u/(CWG_TABLE_LEN-1u));
                        int y0=316-(int)lround((double)samples[i-1u]*42.0/32768.0);
                        int y1=316-(int)lround((double)samples[i]*42.0/32768.0);
                        plot_line(s,x0,y0,x1,y1,wave,25,274,775,358);
                    }
                }
                snprintf(b,sizeof(b),"MIN %d MAX %d Q15",minimum,maximum);
                custom_text(s,480,258,b,1,wave);
            } else custom_text(s,50,307,"PACKAGE CRC INVALID - REBUILD REQUIRED",1,accent);
        } else custom_text(s,210,307,"BUILD TO PREVIEW 1024 Q15 SAMPLES",1,grid);
        const char *actions[]={"BUILD","APPLY","DISCARD","BACK"};
        int step=(s->w-40)/4;
        for(int i=0;i<4;i++) {
            int x=20+i*step;
            uint32_t c=i==1?accent:ink;
            line(s,x,375,x+step-7,375,c); line(s,x,419,x+step-7,419,c);
            line(s,x,375,x,419,c); line(s,x+step-7,375,x+step-7,419,c);
            custom_text(s,x+(step-6-(int)strlen(actions[i])*12)/2,390,actions[i],2,c);
        }
    }
    custom_text(s,20,438,"PREVIEW ONLY - ALL ACTIONS LEAVE THE ORIGINAL GEN OUTPUT UNCHANGED",1,accent);
    custom_text(s,20,454,"FORMULA PEAK ABOVE 1 IS NORMALIZED - AMP AND OFFSET ARE Q15",1,grid);
    custom_text(s,20,468,"APPLY BLOCKED - DISCARD CLEARS THE PACKAGE, NOT THE DAC OUTPUT",1,grid);
}
static void render_gen_keypad(struct screen *s) {
    const uint32_t ink=0xe4edf5,accent=0xffb75e,grid=0x283b49;
    for(int y=90;y<430;y++) for(int x=15;x<785;x++) pixel(s,x,y,0x101820);
    line(s,15,90,784,90,accent); line(s,15,429,784,429,accent);
    const char *title=gen_edit_field==1?"SET FREQUENCY (MHZ)":
        gen_edit_field==2?"SET DIGITAL AMP (%)":
        gen_edit_field==3?"SET SWEEP START (MHZ)":
        gen_edit_field==4?"SET SWEEP STOP (MHZ)":"SET PASS THRESHOLD (DB)";
    if(gen_edit_field==6) title="SET SWEEP POINTS (8/16/32/64)";
    else if(gen_edit_field==7) title="SET SWEEP AVERAGE FRAMES";
    else if(gen_edit_field==8) title="SET SWEEP RETRIES";
    else if(gen_edit_field==9) title="SET SWEEP SCALE (0=LINEAR 1=LOG)";
    const char *range=gen_edit_field<=2?(gen_edit_field==1?"STEP 0.001  RANGE 1 ... 250":"RANGE 0 ... 50"):
        gen_edit_field==5?"RANGE -60 ... 0":
        gen_edit_field==6?"VALUES 8,16,32,64":
        gen_edit_field==7?"RANGE 1 ... 16":
        gen_edit_field==8?"RANGE 0 ... 5":"VALUES 0 OR 1";
    text(s,25,105,title,2,accent);
    text(s,25,135,gen_edit_buf[0]?gen_edit_buf:"0",3,ink);
    text(s,300,140,range,1,ink);
    static const char *keys[4][4]={{"1","2","3","BKSP"},{"4","5","6","CLR"},{"7","8","9","."},{"0","-","OK","CANCEL"}};
    int left=25,top=185,step=112,kw=100,kh=48;
    for(int row=0;row<4;row++) for(int col=0;col<4;col++) {
        int x=left+col*step,y=top+row*kh; uint32_t c=(row==3 && col>=2)?accent:ink;
        line(s,x,y,x+kw,y,c); line(s,x,y+kh-1,x+kw,y+kh-1,c);
        line(s,x,y,x,y+kh-1,c); line(s,x+kw,y,x+kw,y+kh-1,c);
        text(s,x+(kw-(int)strlen(keys[row][col])*12)/2,y+16,keys[row][col],2,c);
    }
    text(s,560,140,"ENTER VALUE THEN OK",1,grid);
}
static void render_lowfreq681(struct screen *s) {
    const uint32_t ink=0xe4edf5,grid=0x283b49,wave=0x00e0aa,accent=0xffb75e;
    uint64_t now681=(uint64_t)llround(seconds()*1000000.0);
    const struct lf681_point *latest681=lf_history681.count?
        lf681_at(&lf_history681,lf_history681.count-1):NULL;
    if(paused && latest681) now681=latest681->received_us;
    struct lf681_summary q681; lf681_summarize(&lf_history681,now681,&q681);
    char b681[160];
    power_box(s,185,4,120,30,"FAST",ink);
    if(q681.frequency_valid) snprintf(b681,sizeof(b681),"EST %.2f HZ",q681.frequency_hz);
    else snprintf(b681,sizeof(b681),"LF FREQ N/A");
    text(s,20,43,b681,2,wave);
    snprintf(b681,sizeof(b681),"SNAPSHOT MEAN %.0f..%.0f CODE / %zu POINTS",
             q681.mean_min,q681.mean_max,q681.valid_count);
    text(s,20,70,b681,1,ink);
    if(q681.frequency_valid)
        snprintf(b681,sizeof(b681),"SAMPLE-BRACKET EST %.2f..%.2f HZ",q681.frequency_low_hz,q681.frequency_high_hz);
    else snprintf(b681,sizeof(b681),"%s",lf681_reason(q681.status));
    text(s,20,87,b681,1,accent);
    const char *actions681[]={"RUN","PAUSE","N/A"};
    for(int i681=0;i681<3;i681++) {
        int x681=s->w-320+i681*105;
        power_box(s,x681,38,99,48,actions681[i681],i681==2?grid:ink);
    }
    const int left681=60,right681=s->w-25,top681=125,bottom681=300;
    double low681=q681.valid_count?q681.mean_min:8192.0;
    double high681=q681.valid_count?q681.mean_max:8192.0;
    double span681=high681-low681; if(span681<128.0) span681=128.0;
    double center681=(low681+high681)*0.5;
    low681=center681-span681*0.625; high681=center681+span681*0.625;
    for(int j681=0;j681<=10;j681++) {
        int x681=left681+(right681-left681)*j681/10;
        line(s,x681,top681,x681,bottom681,grid);
        snprintf(b681,sizeof(b681),"%d",j681-10);
        text(s,x681-5,bottom681+5,b681,1,ink);
    }
    for(int j681=0;j681<=4;j681++) {
        int y681=top681+(bottom681-top681)*j681/4;
        line(s,left681,y681,right681,y681,grid);
    }
    snprintf(b681,sizeof(b681),"%.0f",high681); text(s,12,top681,b681,1,ink);
    snprintf(b681,sizeof(b681),"%.0f",low681); text(s,12,bottom681-7,b681,1,ink);
    for(unsigned i681=0;i681<lf_history681.count;i681++) {
        const struct lf681_point *p681=lf681_at(&lf_history681,i681);
        if(!p681 || !p681->valid || p681->received_us>now681 ||
           now681-p681->received_us>10000000u) continue;
        int x681=right681-(int)((double)(now681-p681->received_us)*(right681-left681)/10000000.0);
        int y681=top681+(int)lround((high681-p681->mean)*(bottom681-top681)/(high681-low681));
        if(y681<top681)y681=top681;
        if(y681>bottom681)y681=bottom681;
        /* Each dot is one independent RAW-frame mean. Never interpolate
         * across frames or imply that the unsampled interval was recorded. */
        line(s,x681-1,y681,x681+1,y681,p681->break_before?accent:wave);
        line(s,x681,y681-1,x681,y681+1,p681->break_before?accent:wave);
    }
    text(s,20,325,"1 S/DIV - 10 S HISTORY - CODE UNITS",1,ink);
    snprintf(b681,sizeof(b681),"POINT GAP MAX %.1f MS / INDEPENDENT 5.12 US SNAPSHOTS",q681.max_gap_us/1000.0);
    text(s,20,344,b681,1,accent);
    text(s,20,361,"HOST RECEIVE TIMES; NOT CONTINUOUS ADC / NO PULSE-WIDTH OR JITTER CLAIM",1,accent);
    power_box(s,20,s->h-110,180,48,"CLEAR",ink);
    text(s,220,382,"DOTS = FRAME MEANS / GAPS NOT RECORDED",1,ink);
    text(s,220,399,"LOW-FREQ ONLY; FAST EVENTS MAY BE MISSED",1,accent);
    const char *nav681[]={"VIEW","HORIZ","CURSOR","FFT","GEN","CLEAR"};
    int step681=(s->w-40)/6;
    for(int j681=0;j681<6;j681++)
        power_box(s,20+j681*step681,s->h-47,step681-6,47,nav681[j681],ink);
}
#include "spectrum_mask617_paint.inc"
static void render(struct screen *s,const uint32_t *a,const struct metrics *v,unsigned frames,uint32_t token) {
    const uint32_t bg=0x101820,grid=0x283b49,ink=0xe4edf5,wave=0x00e0aa;
    /* Diagnostic mode: paint a static black field and skip all drawing so the
     * panel's data lines stay quiet while captures keep running. */
    if(static_screen) {
        for(int i=0;i<s->w*s->h;i++) s->rgb[i]=0;
        return;
    }
    for(int i=0;i<s->w*s->h;i++) s->rgb[i]=bg;
    if(diag_ui_state.mode==DIAG_UI_DIAG) {
        if(diag_ui_state.page==DIAG_PAGE_MONITOR && mask617_panel) {
            text(s,20,15,"Z20 DIAG",2,ink);
            power_box(s,320,4,85,30,"SCOPE",ink);
            power_box(s,410,4,85,30,"DIAG",0xffb75e);
            power_box(s,185,4,120,30,"MASK",ink);
            text(s,s->w-100,15,capture_waiting?"WAIT":paused?"STOP":"RUN",2,ink);
            render_mask617(s);
        } else {
            render_diag_panel(s,a,frames,token);
            if(diag_ui_state.page==DIAG_PAGE_MONITOR) power_box(s,185,4,120,30,"MASK",ink);
        }
        if(diag_ui_state.page==DIAG_PAGE_SWEEP && gen_edit_field>=3)
            render_gen_keypad(s);
        render_power(s);
        flush_screen(s);
        view_dirty=0;
        return;
    }
    scope_stats_context683();
    if(tool_page==1)
        power_box(s,20,4,155,30,scope_stats_enabled683?"STATS ON":"STATS OFF",ink);
    else text(s,20,15,"Z20 LIVE ADC",2,ink);
    power_box(s,320,4,85,30,"SCOPE",0xffb75e);
    power_box(s,410,4,85,30,"DIAG",ink);
    text(s,s->w-100,15,capture_waiting?"WAIT":paused?"STOP":"RUN",2,paused?0xffb75e:wave);
    if(tool_page==3) power_box(s,185,4,120,30,"MASK",ink);
    if(tool_page==3 && mask617_panel) {
        render_mask617(s); render_power(s); flush_screen(s); view_dirty=0; return;
    }
    if(tool_page==4) power_box(s,185,4,120,30,"CUSTOM",ink);
    if(tool_page==4 && custom_panel) {
        render_custom_panel(s);
        render_power(s);
        flush_screen(s);
        view_dirty=0;
        return;
    }
    if(tool_page==5) {
        render_lowfreq681(s);
        render_power(s); flush_screen(s); view_dirty=0;
        return;
    }
    if(tool_page!=4) power_box(s,185,4,120,30,"SLOW",ink);
    char b[150];
    struct hf_model hf={0};
    int hf_active=frames && tool_page!=3 &&
        hf_model_update(a,v,token,last_trigger_index,last_trigger_level,trigger.falling,&hf);
    if(hf_active && scope_stats_enabled683)ss683_reset(&scope_stats683,"HF");
    /* Header and trace must refer to the same frame, including the first
     * frame after changing frequency or entering FFT. */
    if(frames && tool_page==3 && (!spectrum_cache_valid || spectrum_cache_token!=token)) {
        spectrum_measure(a,&spectrum_cache);
        spectrum_cache_token=token; spectrum_cache_valid=1;
    }
    int fft_freq_valid=frames && tool_page==3 && spectrum_cache_valid &&
        spectrum_cache_token==token && spectrum_cache.peak_valid && !v->rail && !v->low_signal;
    int use_fft_freq=fft_freq_valid && !v->freq_valid && !hf_active;
    int ordinary_scope682=tool_page>=0 && tool_page<=2 && !last_frame_raw_owned682;
    enum sp685_quality scope_quality685=sp685_classify(v->freq_valid,v->mhz,
        v->period_ns,v->timing_valid685,v->timing_reason685,v->rail,v->low_signal);
    int scope_gate685=ordinary_scope682 && !hf_active;
    int readout_valid685=scope_gate685 ? scope_quality685!=SP685_INVALID : v->freq_valid;
    int fit_readout682=0;
    double readout_mhz682=sp682_frequency_mhz(scope_gate685 && scope_quality685==SP685_FIT,
        v->freq_valid,v->mhz,v->period_ns,&fit_readout682);
    if(frames && (readout_valid685 || hf_active || fft_freq_valid))
        snprintf(b,sizeof(b),"FREQ %.6f MHZ%s  FRAME %u",
                 hf_active?hf.mhz:use_fft_freq?spectrum_cache.peak_mhz:readout_mhz682,
                 (hf_active || use_fft_freq)?" FFT":fit_readout682?" FIT":
                 scope_gate685 && scope_quality685==SP685_EDGE_EST?" EDGE":"",frames);
    else snprintf(b,sizeof(b),"FREQ N/A  FRAME %u",frames);
    text(s,20,43,b,2,wave);
    if(frames && hf_active) snprintf(b,sizeof(b),"VPP FIT %.0f CODE  RMS-AC %.1f CODE",2.0*hf.amp,v->rms_ac);
    else if(frames) snprintf(b,sizeof(b),"VPP RAW %u CODE  RMS-AC %.1f CODE",v->hi-v->lo,v->rms_ac);
    else snprintf(b,sizeof(b),"VPP N/A  RMS-AC N/A - NO FRAME");
    text(s,20,69,b,2,ink);
    if(use_fft_freq)
        snprintf(b,sizeof(b),"FFT PEAK %.6f MHZ  RAW PERIOD UNLOCKED",spectrum_cache.peak_mhz);
    else if(frames && scope_gate685 && v->freq_valid && scope_quality685==SP685_INVALID)
        snprintf(b,sizeof(b),"TIMING INVALID - %s; FREQ/PERIOD N/A",
                 v->timing_reason685?v->timing_reason685:"UNKNOWN_RESULT");
    else if(frames && scope_gate685 && scope_quality685==SP685_EDGE_EST)
        snprintf(b,sizeof(b),"PERIOD %.3f NS  EDGE EST - %s / JITTER N/A",
                 1000.0/v->mhz,v->timing_reason685);
    else if(frames && (readout_valid685 || hf_active)) {
        if(!hf_active && isfinite(v->period_ns))
            snprintf(b,sizeof(b),ordinary_scope682?
                     "PERIOD %.3f NS  PERIOD JIT EST RMS %.3f NS  PP %.3f NS":
                     "PERIOD %.3f NS  JITTER RMS %.3f NS  PP %.3f NS",
                     v->period_ns,v->jitter_rms_ns,v->jitter_pp_ns);
        else snprintf(b,sizeof(b),"PERIOD %.3f NS  MEAN %.1f CODE",1000/(hf_active?hf.mhz:v->mhz),v->mean);
    }
    else snprintf(b,sizeof(b),"%s",!frames?"NO VALID FRAME":v->rail?"ADC RAIL WARNING - FREQUENCY INVALID":v->low_signal?"LOW SIGNAL - FREQUENCY INVALID":"INSUFFICIENT / UNSTABLE PERIODS - FREQUENCY INVALID");
    text(s,20,91,b,1,ink);
    /* Serial-readable evidence for the fitted readout, so a capture can be
     * judged without reading pixels off the panel. */
    if(hf_active && (frames<=3 || frames%200==0))
        printf("HF_FIT FRAME=%u FREQ_MHZ=%.6f AMP_CODE=%.2f VPP_FIT_CODE=%.2f VPP_RAW_CODE=%u SPUR_DB=%.1f\n",
               frames,hf.mhz,hf.amp,2.0*hf.amp,v->hi-v->lo,spectrum_cache.spur_db);
    const char *labels[]={"RUN","PAUSE","SINGLE"};
    for(int i=0;i<3;i++) {
        int x=s->w-320+i*105;
        uint32_t c=(i==0 && !paused)||(i==1 && paused)?0xffb75e:ink;
        line(s,x,38,x+99,38,c); line(s,x,85,x+99,85,c);
        line(s,x,38,x,85,c); line(s,x+99,38,x+99,85,c);
        text(s,x+(100-(int)strlen(labels[i])*12)/2,55,labels[i],2,c);
    }
    int left=45,right=s->w-25,top=105,bottom=s->h-184;
    for(int j=0;j<=10;j++) { int x=left+(right-left)*j/10; line(s,x,top,x,bottom,grid); }
    for(int j=0;j<=8;j++) { int y=top+(bottom-top)*j/8; line(s,left,y,right,y,grid); }
    line(s,left,(top+bottom)/2,right,(top+bottom)/2,0x537083);
    double span=v->hi-v->lo; if(span<128) span=128;
    double gain=view.codes?(double)(bottom-top)/(8*view.codes):(bottom-top)*0.40/(span/2);
    double center=(top+bottom)/2.0,reference=(view.codes?8192:v->mean)+view.offset;
    /* The low-safe-output GEN preview must remain visible without changing
     * the saved oscilloscope scale. This is display-only autoscaling. */
    if(tool_page==4) { gain=(bottom-top)*0.40/(span/2); reference=v->mean+view.offset; }
    int visible=visible_samples(view);
    /* At high frequency the raw 400 MSPS record is too compressed at the
     * normal wide timebases. Keep the saved VIEW setting, but render the
     * fitted tone over a 10 ns/div equivalent window. */
    if(hf_active && hf.mhz>=40.0 && visible>40) visible=40;
    int start=window_start(visible,last_trigger_index,pan_samples);
    double edge_shift682=0;
    int edge_align682=sp682_edge_shift(a,N,last_flags,last_trigger_index,
        last_trigger_level,last_trigger_falling682,v->lo,v->hi,
        ordinary_scope682 && !hf_active && !cursor_enabled && frames &&
        v->freq_valid && !v->rail && !v->low_signal,&edge_shift682);
    if(ordinary_scope682 && frames && (frames<=3 || frames%200==0))
        printf("SCOPE_PRECISION682 FRAME=%u TOKEN=%u FIT=%d FREQ_MHZ=%.9f LEGACY_MHZ=%.9f PERIOD_NS=%.9f ALIGN=%d SHIFT_SAMPLES=%.9f RAW_UNCHANGED=1\n",
               frames,token,fit_readout682,readout_mhz682,v->mhz,v->period_ns,
               edge_align682,edge_shift682);
    if(tool_page==3 && frames) {
        if(!spectrum_cache_valid || spectrum_cache_token!=token) {
            spectrum_measure(a,&spectrum_cache);
            spectrum_cache_token=token; spectrum_cache_valid=1;
            if(frames<=3 || frames%200==0) {
                printf("FFT_FRAME=%u TOKEN=%u PEAK_VALID=%d PEAK_MHZ=%.6f BIN_AMP_CODE=%.2f BIN_KHZ=%.4f ENBW_KHZ=%.4f\n",
                       frames,token,spectrum_cache.peak_valid,spectrum_cache.peak_mhz,
                       spectrum_cache.peak_code,FFT_BIN_HZ/1000.0,FFT_HANN_ENBW_HZ/1000.0);
                if(rf_metrics_cache_valid)
                    printf("RF_METRICS FUND_BIN=%u HD2_DB=%.3f HD3_DB=%.3f THD_PCT=%.3f SFDR_DB=%.3f SNR_DB=%.3f THRESHOLD_BW_20DBC_KHZ=%.3f NOISE_DB=%.3f\n",
                           rf_metrics_cache.fundamental_bin,rf_metrics_cache.hd2_db,
                           rf_metrics_cache.hd3_db,rf_metrics_cache.thd_percent,
                           rf_metrics_cache.sfdr_db,rf_metrics_cache.snr_db,
                           rf_metrics_cache.occupied_bw_hz/1000.0,rf_metrics_cache.noise_floor_db);
            }
        }
        const struct spectrum *sp=&spectrum_cache;
        int span_k0=0,span_k1=N/2;
        if(spectrum_display_range(&span_k0,&span_k1)<0) { span_k0=0; span_k1=N/2; }
        if(spectrum_history_frames>1) {
            int row_h=(bottom-top)/SPECTRUM_WATERFALL_ROWS;
            for(int row=0;row<SPECTRUM_WATERFALL_ROWS;row++) {
                unsigned src=(spectrum_waterfall_head+row)%SPECTRUM_WATERFALL_ROWS;
                int y0=top+row*row_h, y1=(row==SPECTRUM_WATERFALL_ROWS-1)?bottom:y0+row_h-1;
                for(int x=left;x<right;x++) {
                    int k=span_k0+(int)((int64_t)(x-left)*(span_k1-span_k0)/(right-left));
                    unsigned v=spectrum_waterfall[src][k];
                    uint32_t c=((v/5)<<16)|((v/3)<<8)|v;
                    line(s,x,y0,x+1<right?x+1:x,y1,c);
                }
            }
        }
        int oldx=left,oldy=bottom;
        for(int x=left;x<right;x++) {
            int k0=span_k0+(int)((int64_t)(x-left)*(span_k1-span_k0)/(right-left));
            int k1=span_k0+(int)((int64_t)(x-left+1)*(span_k1-span_k0)/(right-left));
            if(k1<=k0) k1=k0+1;
            double db=-100; for(int k=k0;k<k1 && k<=N/2;k++) if(spectrum_display_db(sp,k)>db) db=spectrum_display_db(sp,k);
            int y=top+(int)lround((-db)*(bottom-top)/100.0);
            if(y<top) y=top;
            if(y>bottom) y=bottom;
            if(x>left) plot_line(s,oldx,oldy,x,y,wave,left,top,right,bottom);
            oldx=x; oldy=y;
        }
        if(spectrum_peak_hold_valid && spectrum_history_frames>1) {
            int hx=left,hy=bottom;
            for(int x=left+1;x<right;x++) {
                int k0=span_k0+(int)((int64_t)(x-left)*(span_k1-span_k0)/(right-left));
                int k1=span_k0+(int)((int64_t)(x-left+1)*(span_k1-span_k0)/(right-left));
                if(k1<=k0) k1=k0+1;
                double db=-100; for(int k=k0;k<k1 && k<=N/2;k++) if(spectrum_peak_hold[k]>db) db=spectrum_peak_hold[k];
                int y=top+(int)lround((-db)*(bottom-top)/100.0);
                if(y<top) y=top;
                if(y>bottom) y=bottom;
                if(x>left) plot_line(s,hx,hy,x,y,0xffb75e,left,top,right,bottom);
                hx=x;hy=y;
            }
        }
        for(int j=0;j<=4;j++) {
            double f=spectrum_start_mhz+(spectrum_stop_mhz-spectrum_start_mhz)*j/4.0;
            snprintf(b,sizeof(b),"%.0f",f); text(s,left+j*(right-left)/4-5,bottom-12,b,1,ink);
        }
        for(int j=0;j<2;j++) {
            int k=spectrum_marker_bin[j];
            if(k<0 || k<span_k0 || k>span_k1) continue;
            int x=left+(int)((int64_t)(k-span_k0)*(right-left)/(span_k1-span_k0));
            uint32_t c=j==(int)spectrum_marker_selected?0xffffff:0x58aaff;
            line(s,x,top,x,bottom,c);
            text(s,x<right-22?x+3:x-20,top+4+j*10,j?"M2":"M1",1,c);
        }
        if(sp->peak_valid && rf_metrics_cache_valid)
            snprintf(b,sizeof(b),"SPECTRUM PEAK RAW %.3fM  THD %.2f%%  SFDR %.1fdB  SNR %.1fdB  AVG %u HOLD %u",
                     sp->peak_mhz,rf_metrics_cache.thd_percent,rf_metrics_cache.sfdr_db,rf_metrics_cache.snr_db,
                     spectrum_average_target,spectrum_history_frames);
        else if(sp->peak_valid) snprintf(b,sizeof(b),"FFT HANN  PEAK %.3f MHZ  BIN AMP %.1f CODE",sp->peak_mhz,sp->peak_code);
        else snprintf(b,sizeof(b),"FFT HANN  PEAK N/A - LOW SIGNAL");
        text(s,20,304,b,1,ink);
    } else if(hf_active) {
        int prevx=left;
        double first_sample=start;
        int prevy=(int)lround(center-(hf_model_value(&hf,first_sample)-reference)*gain);
        for(int x=left+1;x<right;x++) {
            double sample=start+(double)(x-left)*visible/(right-left);
            int y=(int)lround(center-(hf_model_value(&hf,sample)-reference)*gain);
            plot_line(s,prevx,prevy,x,y,wave,left,top,right,bottom); prevx=x; prevy=y;
        }
        text(s,right-145,top+5,"HF RECON",1,0xffb75e);
    } else {
        int prevx=sp682_plot_x(left,right-left,visible,0,edge_shift682);
        int prevy=(int)lround(center-(a[start]-reference)*gain);
        /* Each sample is 2.5ns; plot width denotes exactly ten ns/div intervals.
         * Last real point ends one sampling interval before the right boundary. */
        for(int i=1;frames && i<visible;i++) {
            int x=sp682_plot_x(left,right-left,visible,i,edge_shift682);
            int y=(int)lround(center-(a[start+i]-reference)*gain);
            plot_line(s,prevx,prevy,x,y,wave,left,top,right,bottom); prevx=x; prevy=y;
        }
        if(edge_align682) text(s,right-140,top+5,"EDGE ALIGN",1,0xffb75e);
    }
    if(tool_page!=3 && (last_flags&1)) {
        int level_y=(int)lround(center-(last_trigger_level-reference)*gain);
        if(level_y>=top && level_y<=bottom) {
            for(int x=left;x<right;x+=8) line(s,x,level_y,x+3<right?x+3:right,level_y,0xffb75e);
            text(s,right-12,level_y>bottom-8?bottom-8:level_y,"T",1,0xffb75e);
        }
        int pos=(int)last_trigger_index-start;
        if(pos>=0 && pos<visible) {
            int x=left+pos*(right-left)/visible;
            line(s,x,top,x,bottom,last_flags&2 ? 0xffb75e : 0x537083);
        }
    }
    int frozen=frames && paused && !single_pending && !capture_waiting;
    if(cursor_enabled && frozen && tool_page!=3 && tool_page!=4) for(int j=0;j<2;j++) {
        int pos=cursor_index[j]-start;
        if(pos>=0 && pos<visible) {
            int x=left+pos*(right-left)/visible;
            line(s,x,top,x,bottom,j==cursor_selected?0xffffff:0x58aaff);
            text(s,x,top+3,j?"B":"A",1,0xffffff);
        }
    }
    if(tool_page!=4 && tool_page!=3) {
        char mode_label[20];
        snprintf(mode_label,sizeof(mode_label),"%s",trigger.mode==2?"AUTO":trigger.mode==1?"NORMAL":"FREE");
        const char *tl[]={mode_label,trigger.falling?"FALLING":"RISING"};
        int ts=(s->w-40)/6,ty=s->h-162;
        for(int i=0;i<2;i++) {
            int x=20+i*ts,width=ts-6;
            line(s,x,ty,x+width-1,ty,ink); line(s,x,ty+47,x+width-1,ty+47,ink);
            line(s,x,ty,x,ty+47,ink); line(s,x+width-1,ty,x+width-1,ty+47,ink);
            text(s,x+(width-(int)strlen(tl[i])*12)/2,ty+16,tl[i],2,ink);
        }
        snprintf(b,sizeof(b),"LEVEL %d CODE - %s",trigger.level,!frames?"NO FRAME":last_flags&2?"TRIGGERED":"FORCED");
        text(s,20,s->h-179,b,1,0xffb75e);
    }
    snprintf(b,sizeof(b),"TIME %d NS/DIV  SCALE %d CODE/DIV  START %d",view.ns,view.codes,start);
    if(tool_page!=2 && tool_page!=3 && tool_page!=4 && !(tool_page==1 && scope_stats_enabled683))
        text(s,20,s->h-57,b,1,ink);
    if(cursor_enabled && frozen) snprintf(b,sizeof(b),"A %d  B %d  DT %.1f NS  DAMP %d CODE",cursor_index[0],cursor_index[1],(cursor_index[1]-cursor_index[0])*2.5,(int)a[cursor_index[1]]-(int)a[cursor_index[0]]);
    else snprintf(b,sizeof(b),"CAPTURE 2048  TOKEN %u  CURSORS REQUIRE STOP",token);
    if(tool_page==2) text(s,20,s->h-57,b,1,ink);
    int navstep=(s->w-40)/6;
    const char *nav[]={"VIEW","HORIZ","CURSOR","FFT","GEN","AUTO"};
    for(int j=0;j<6;j++) {
        int x=20+j*navstep;
        /* AUTO SET is an action key, drawn green so it reads differently from
         * the five page keys that show the selected page in amber. */
        uint32_t c=j==5?0x00e0aa:j==tool_page?0xffb75e:ink;
        line(s,x,s->h-47,x+navstep-6,s->h-47,c);
        line(s,x,s->h-1,x+navstep-6,s->h-1,c);
        text(s,x+(navstep-(int)strlen(nav[j])*12)/2,s->h-31,nav[j],2,c);
    }
    int sy=s->h-85;
    if(tool_page!=4 && tool_page!=3) {
        int level=trigger_drag==7?preview_level:trigger.level,pre=trigger_drag==8?preview_pre:trigger.pre;
        snprintf(b,sizeof(b),"LEVEL %d CODE%s",level,trigger_drag==7?" PREVIEW":"");
        text(s,280,s->h-160,b,1,ink);
        if(fine_pretrigger) snprintf(b,sizeof(b),"PRE %.1f PCT / %d SAMPLES",pre*100.0/N,pre);
        else snprintf(b,sizeof(b),"PRETRIG %d PCT / 4 STOPS%s",pre*100/N,trigger_drag==8?" PREVIEW":"");
        text(s,540,s->h-160,b,1,ink);
        int trigger_y=s->h-135;
        line(s,290,trigger_y,510,trigger_y,ink);
        int lx=290+level*220/16383;
        line(s,lx,trigger_y-10,lx,trigger_y+10,0xffb75e);
        line(s,550,trigger_y,s->w-40,trigger_y,ink);
        int px=550+(fine_pretrigger?pre*(s->w-590)/2044:(pre/512)*(s->w-590)/3);
        line(s,px,trigger_y-10,px,trigger_y+10,0xffb75e);
    }
    if(tool_page==0) {
        int part=(s->w-40)/3;
        for(int j=0;j<3;j++) {
            int left=40+j*part,right=20+(j+1)*part-20,knob;
            if(j==0) { snprintf(b,sizeof(b),"TIME %d NS/DIV",view.ns); knob=left+detent_index(view_times,VIEW_TIME_COUNT,view.ns)*(right-left)/(VIEW_TIME_COUNT-1); }
            else if(j==1) { snprintf(b,sizeof(b),view.codes?"SCALE %d CODE/DIV":"SCALE AUTO DISPLAY",view.codes); knob=left+detent_index(view_scales,VIEW_SCALE_COUNT,view.codes)*(right-left)/(VIEW_SCALE_COUNT-1); }
            else { snprintf(b,sizeof(b),"Y POSITION %d CODE",-view.offset); knob=left+(8192-view.offset)*(right-left)/16384; }
            text(s,left-10,sy-22,b,1,ink);
            line(s,left,sy,right,sy,ink);
            line(s,knob,sy-10,knob,sy+10,0xffb75e);
            if(j==0) for(int k=0;k<VIEW_TIME_COUNT;k++) line(s,left+k*(right-left)/(VIEW_TIME_COUNT-1),sy-4,left+k*(right-left)/(VIEW_TIME_COUNT-1),sy+4,ink);
            else if(j==1) for(int k=0;k<VIEW_SCALE_COUNT;k++) line(s,left+k*(right-left)/(VIEW_SCALE_COUNT-1),sy-4,left+k*(right-left)/(VIEW_SCALE_COUNT-1),sy+4,ink);
        }
    }
    if(tool_page==1) {
        int knob=40+start*(s->w-220)/(N-visible);
        line(s,40,sy,s->w-180,sy,ink);
        line(s,knob,sy-14,knob,sy+14,0xffb75e);
        line(s,knob-7,sy-14,knob+7,sy-14,0xffb75e);
        line(s,knob-7,sy+14,knob+7,sy+14,0xffb75e);
        text(s,s->w-130,sy-7,"HOME",2,ink);
        if(scope_stats_enabled683) {
            struct ss683_summary st;ss683_summarize(&scope_stats683,&st);
            snprintf(b,sizeof(b),"READOUT STATS %s N %u/16 SPAN %.3f S MAXGAP %.3f S",
                     paused?"HOLD":st.n<SS683_CAP?"WARMUP":"ROLLING",st.n,
                     st.span_us/1e6,st.max_gap_us/1e6);
            text(s,20,s->h-70,b,1,0xffb75e);
            if(st.valid) {
                snprintf(b,sizeof(b),"MEAN %.2f HZ  SD %.2f HZ  PP %.2f HZ",st.mean_hz,st.sd_hz,st.pp_hz);
                text(s,20,s->h-62,b,1,ink);
                snprintf(b,sizeof(b),"MIN %.2f HZ MAX %.2f HZ - SPARSE FRAMES / NOMINAL FS",st.min_hz,st.max_hz);
            } else {
                snprintf(b,sizeof(b),"MEAN/SD/PP N/A - NEED 2 VALID FIT FRAMES (%s)",st.reason?st.reason:"EMPTY");
                text(s,20,s->h-62,b,1,ink);
                snprintf(b,sizeof(b),"SPARSE FRAMES / NOMINAL FS - NOT ABSOLUTE ACCURACY");
            }
            text(s,20,s->h-54,b,1,ink);
        }
    } else if(tool_page==2) {
        text(s,24,sy-7,cursor_enabled?"ON":"OFF",2,frozen?ink:grid);
        text(s,115,sy-7,cursor_selected?"B":"A",2,frozen?ink:grid);
        text(s,205,sy-7,"RESET",2,frozen?ink:grid);
        line(s,330,sy,s->w-30,sy,frozen?ink:grid);
        int knob=330+cursor_index[cursor_selected]*(s->w-360)/(N-1);
        line(s,knob,sy-14,knob,sy+14,0xffb75e);
        text(s,330,sy-23,frozen?"CURSOR POSITION - DRAG A/B LINE FOR LOCAL DETAIL":"WAITING FOR FROZEN FRAME",1,ink);
    } else if(tool_page==3) {
        snprintf(b,sizeof(b),"SPAN %.0f-%.0f MHZ",spectrum_start_mhz,spectrum_stop_mhz);
        text(s,20,328,b,1,ink);
        /* Keep the displayed physics tied to the same constants used by the
         * FFT calculation.  The previous single long label could overlap the
         * window name on the 800 px LCD and could silently drift if N/Fs
         * changed. */
        snprintf(b,sizeof(b),"BIN %.4f KHZ",FFT_BIN_HZ/1000.0);
        text(s,250,328,b,1,ink);
        snprintf(b,sizeof(b),"ENBW %.4f KHZ",FFT_HANN_ENBW_HZ/1000.0);
        text(s,450,328,b,1,ink);
        text(s,650,328,spectrum_average_valid?"HANN/WF32/AVG":"HANN/WF32",1,ink);
        if(rf_metrics_cache_valid) {
            snprintf(b,sizeof(b),"HD2 %.1fdB  HD3 %.1fdB  -20dBc BW %.1fkHz  NF %.1fdB",
                     rf_metrics_cache.hd2_db,rf_metrics_cache.hd3_db,
                     rf_metrics_cache.occupied_bw_hz/1000.0,rf_metrics_cache.noise_floor_db);
            text(s,20,316,b,1,ink);
        }
        if(spectrum_marker_bin[0]>=0 && spectrum_marker_bin[1]>=0) {
            double f0=spectrum_marker_mhz(0),f1=spectrum_marker_mhz(1);
            double d0=spectrum_display_db(&spectrum_cache,spectrum_marker_bin[0]);
            double d1=spectrum_display_db(&spectrum_cache,spectrum_marker_bin[1]);
            snprintf(b,sizeof(b),"M1 %.3fM %.1fDBFS  M2 %.3fM %.1fDBFS  DF %.3fM  DDB %.1f",
                     f0,d0,f1,d1,f1-f0,d1-d0);
        } else snprintf(b,sizeof(b),"M%d SELECTED  TAP PLOT TO PLACE MARKER",spectrum_marker_selected+1u);
        text(s,20,345,b,1,0x58aaff);
        if(spectrum_csv_status!=SPECTRUM_CSV_NONE) {
            const char *status=spectrum_csv_status==SPECTRUM_CSV_SAVED?"CSV SAVED: spectrum_latest.csv":
                               spectrum_csv_status==SPECTRUM_CSV_NO_FRAME?"CSV NOT SAVED: NO FRAME":
                               "CSV SAVE FAILED: CHECK STORAGE";
            text(s,20,358,status,1,spectrum_csv_status==SPECTRUM_CSV_SAVED?0x00e0aa:0xffb75e);
        }
        const char *actions[]={"SPAN","M1","M2","AVG","RESET","CSV"};
        int action_x=20,action_w=(s->w-40)/6;
        for(int j=0;j<6;j++) {
            uint32_t c=(j==1+(int)spectrum_marker_selected?0x58aaff:(j==3 && spectrum_average_target>1)?0xffb75e:ink);
            power_box(s,action_x+j*action_w,s->h-110,action_w-6,48,actions[j],c);
        }
    } else if(tool_page==4) {
        const char *status=gen_applied==1?"ON REQUESTED":gen_applied==2?"DIGITAL OFF":gen_applied==3?"COMMAND ERROR":"STATE UNKNOWN";
        if(gen_applied==1)
            snprintf(b,sizeof(b),"GEN %s%s - ACTIVE %.6f MHZ %d PCT %s",status,gen_pending?" / PENDING":"",
                gen_active_freq_khz/1000.0,gen_active_amp_pct,
                gen_active_wave==0?"SINE":gen_active_wave==1?"SQUARE":"TRIANGLE");
        else
            snprintf(b,sizeof(b),"GEN %s%s - ADC PREVIEW AUTO - OUT VOLTS UNCALIBRATED",status,gen_pending?" / PENDING":"");
        /* Keep the live generator status in the narrow strip below the plot.
         * The old field border started at y=299 and crossed this text. */
        text(s,20,s->h-179,b,1,gen_applied==3?0xffb75e:ink);
        /* Numeric fields are intentionally point-and-enter controls.  The
         * previous slider was too coarse for frequency and made accidental
         * touches change the generator setting. */
        int field_top=s->h-168, field_bottom=s->h-120;
        int fl=25, fr=s->w/2-10, al=s->w/2+10, ar=s->w-25;
        line(s,fl,field_top,fr,field_top,ink); line(s,fl,field_bottom,fr,field_bottom,ink);
        line(s,fl,field_top,fl,field_bottom,ink); line(s,fr,field_top,fr,field_bottom,ink);
        line(s,al,field_top,ar,field_top,ink); line(s,al,field_bottom,ar,field_bottom,ink);
        line(s,al,field_top,al,field_bottom,ink); line(s,ar,field_top,ar,field_bottom,ink);
        snprintf(b,sizeof(b),"FREQUENCY %.6f MHZ  STEP 0.001",gen_freq_khz/1000.0);
        text(s,fl+12,field_top+8,b,1,ink);
        text(s,fl+12,field_top+31,"TAP TO EDIT",1,0x00e0aa);
        snprintf(b,sizeof(b),"DIGITAL AMP %d PCT",gen_amp_pct);
        text(s,al+12,field_top+8,b,1,ink);
        text(s,al+12,field_top+31,"TAP TO EDIT",1,0x00e0aa);
        const char *choices[]={"SINE","SQUARE","TRIANGLE","APPLY","OFF"};
        int step=(s->w-40)/5;
        for(int i=0;i<5;i++) {
            int bx=20+i*step; uint32_t c=(i==gen_wave && i<3)?0xffb75e:ink;
            if(i==4 && gen_applied==2) c=0xffb75e;
            line(s,bx,s->h-110,bx+step-6,s->h-110,c);
            line(s,bx,s->h-62,bx+step-6,s->h-62,c);
            line(s,bx,s->h-110,bx,s->h-62,c);
            line(s,bx+step-6,s->h-110,bx+step-6,s->h-62,c);
            text(s,bx+(step-6-(int)strlen(choices[i])*12)/2,s->h-92,choices[i],2,c);
        }
        text(s,20,s->h-57,gen_verified==1?(gen_applied==1?"BOOT FILE MATCH - OUTPUT COMMAND SENT":"BOOT FILE MATCH - APPLY TO ENABLE"):
            "GEN FILE WARNING - CONTROL ENABLED",1,gen_verified==1?ink:0xffb75e);
    }
    if(tool_page==4 && gen_edit_field) render_gen_keypad(s);
    if(view_dirty) {
        printf("VIEW_APPLIED NS_DIV=%d VISIBLE=%d CODE_DIV=%d OFFSET=%d FRAME=%u TOKEN=%u FREQ_MHZ=%.6f VPP_CODE=%u RMS_AC_CODE=%.3f\n",
               view.ns,visible,view.codes,view.offset,frames,token,v->mhz,v->hi-v->lo,v->rms_ac);
        view_dirty=0;
    }
    render_power(s);
    flush_screen(s);
}
static unsigned unpack(uint32_t p,struct fb_bitfield b) {
    unsigned max=(1U<<b.length)-1; return b.length?(((p>>b.offset)&max)*255+max/2)/max:0;
}
static int dump_screen(const struct screen *s,const char *path) {
    FILE *f=fopen(path,"wb"); if(!f) { perror("screenshot"); return -1; }
    fprintf(f,"P6\n%d %d\n255\n",s->w,s->h);
    uint8_t *row=malloc((size_t)s->w*3); if(!row) { fclose(f); return -1; }
    int result=0;
    for(int y=0;y<s->h;y++) {
        const uint8_t *src=s->fb+(size_t)(y+s->v.yoffset)*s->f.line_length+(size_t)s->v.xoffset*s->bpp;
        for(int x=0;x<s->w;x++) {
            uint32_t p=0; for(int k=0;k<s->bpp;k++) p|=(uint32_t)src[x*s->bpp+k]<<(8*k);
            row[x*3]=unpack(p,s->v.red); row[x*3+1]=unpack(p,s->v.green); row[x*3+2]=unpack(p,s->v.blue);
        }
        if(fwrite(row,3,(size_t)s->w,f)!=(size_t)s->w) { result=-1; break; }
    }
    free(row); if(fclose(f)) result=-1; return result;
}
static int selftest(void) {
    double rg=reflect_relative(10.0,110.0,110.0,60.0);
    if(fabs(rg-0.5)>1e-9) return 1;
    rg=reflect_relative(10.0,110.0,110.0,10.0);
    if(rg!=0.0) return 1;
    if(window_start(400,512,0)!=412 || window_start(400,512,-10000)!=0 || window_start(400,512,10000)!=1648) return 1;
    if(page_button_at(800,480,700,440)!=15 || page_button_at(800,480,600,440)!=-1) return 1;
    for(int page=1;page<=2;page++) {
        tool_page=page;
        for(int i=0;i<6;i++) if(view_button_at(800,480,30+i*126,390)!=i+16) return 1;
    }
    tool_page=1; tool_action(17,0); if(pan_samples!=40) return 1;
    tool_action(18,0); if(pan_samples) return 1;
    tool_page=2; tool_action(16,0); if(cursor_enabled) return 1;
    tool_action(16,1); if(!cursor_enabled) return 1;
    cursor_index[0]=0; tool_action(18,1); if(cursor_index[0]) return 1;
    cursor_index[0]=N-1; tool_action(19,1); if(cursor_index[0]!=N-1) return 1;
    tool_page=0; cursor_enabled=0;
    struct trigger_config tc={2,8192,0,512};
    if(trigger_action(tc,9).mode!=1 || trigger_action(tc,10).falling!=1 ||
       trigger_action(tc,11).level!=8704 || trigger_action(tc,12).level!=7680 ||
       trigger_action(tc,13).pre!=1024 || trigger_action((struct trigger_config){0,0,1,1536},13).pre!=0 ||
       trigger_action((struct trigger_config){0,16383,1,0},11).level!=16383 ||
       trigger_action((struct trigger_config){0,0,1,0},12).level!=0 || trigger_action(tc,14).level!=8192) return 1;
    for(int i=0;i<6;i++) if(trigger_button_at(800,480,30+i*126,340)!=i+9) return 1;
    if(trigger_button_at(800,480,140,340)!=-1 || trigger_button_at(800,480,30,317)!=-1 ||
       trigger_button_at(800,480,30,366)!=-1) return 1;
    struct view_config q={50,1024,0};
    /* AUTO SET timebase table: about four periods across ten divisions, with
     * the nearest detent chosen in log space. */
    if(autoset_ns_div(1)!=500 || autoset_ns_div(2)!=200 || autoset_ns_div(3)!=100 ||
       autoset_ns_div(5)!=100 || autoset_ns_div(10)!=50 || autoset_ns_div(20)!=20 ||
       autoset_ns_div(30)!=10 || autoset_ns_div(0)!=0 || autoset_ns_div(-1)!=0) return 1;
    int auto_signal=0;
    view=(struct view_config){200,4096,300};
    latest_metrics_valid=0; memset(&latest_metrics,0,sizeof(latest_metrics));
    struct view_config auto_view=autoset_target(&auto_signal);
    if(auto_signal || auto_view.ns!=200 || auto_view.codes!=0 || auto_view.offset!=0) return 1;
    latest_metrics.lo=7000; latest_metrics.hi=9400;
    latest_metrics.freq_valid=1; latest_metrics.mhz=2; latest_metrics_valid=1;
    auto_view=autoset_target(&auto_signal);
    if(!auto_signal || auto_view.ns!=200 || auto_view.codes!=0 || auto_view.offset!=0) return 1;
    latest_metrics.mhz=30; auto_view=autoset_target(&auto_signal);
    if(auto_view.ns!=10) return 1;
    latest_metrics_valid=0; memset(&latest_metrics,0,sizeof(latest_metrics));
    view=(struct view_config){100,2048,0};
    if(view_action(q,3).ns!=100 || view_action((struct view_config){500,1024,0},3).ns!=10 ||
       view_action(q,4).codes!=2048 || view_action((struct view_config){100,4096,0},4).codes!=8192 ||
       view_action((struct view_config){100,0,0},4).codes!=256 ||
       view_action(q,5).offset!=-512 || view_action(q,6).offset!=512 ||
       view_action((struct view_config){100,2048,-8192},5).offset!=-8192 ||
       view_action((struct view_config){100,2048,8192},6).offset!=8192 ||
       view_action((struct view_config){100,2048,512},7).offset!=0 ||
       view_action((struct view_config){100,2048,512},8).codes!=0 ||
       view_action((struct view_config){100,2048,512},8).offset!=0) return 1;
    for(int i=0;i<6;i++) if(view_button_at(800,480,30+i*126,390)!=i+3) return 1;
    if(view_button_at(800,480,140,390)!=-1 || view_button_at(800,480,30,369)!=-1 ||
       view_button_at(800,480,30,418)!=-1 || view_button_at(800,480,780,390)!=-1) return 1;
    double next=0;
    if(!touch_retry_due(-1,10,&next) || next!=11 || touch_retry_due(-1,10.5,&next) ||
       touch_retry_due(4,11,&next) || next!=11 || !touch_retry_due(-1,11,&next) || next!=12) return 1;
    uint32_t a[N]; struct metrics v;
    for(int i=0;i<N;i++) a[i]=(uint32_t)lround(8192+5000*sin(2*3.141592653589793*i/40.0));
    if(measure(a,&v)<0 || fabs(v.mhz-10)>0.001 || v.hi-v.lo!=10000 || fabs(v.rms_ac-3535.5)>20) return 1;
    for(int i=0;i<N;i++) {
        int noise=(i*37%23)-11;
        a[i]=(uint32_t)lround(8192+300*sin(2*3.141592653589793*i/400.0)+noise);
    }
    if(measure(a,&v)<0 || !v.freq_valid || fabs(v.mhz-1)>0.01 || v.rail) return 1;
    for(int i=0;i<N;i++) a[i]=(uint32_t)lround(8192+5000*sin(2*3.141592653589793*i/40.0));
    struct spectrum sp;
    if(spectrum_measure(a,&sp)<0 || !sp.peak_valid || fabs(sp.peak_mhz-10)>0.08 || fabs(sp.peak_code-5000)>150) return 1;
    spectrum_history_reset();
    spectrum_average_set(4);
    for(int n=0;n<4;n++) if(spectrum_measure(a,&sp)<0) return 1;
    if(!spectrum_average_valid || spectrum_average_count!=4 ||
       fabs(spectrum_average_db[sp.peak_bin]-sp.db[sp.peak_bin])>1e-9) return 1;
    spectrum_average_next();
    if(spectrum_average_target!=8 || spectrum_average_valid) return 1;
    spectrum_history_reset();
    int test_k0=0,test_k1=0;
    if(spectrum_display_range(&test_k0,&test_k1)<0 || test_k0!=0 || test_k1!=N/2) return 1;
    spectrum_span_next();
    if(fabs(spectrum_start_mhz)>1e-9 || fabs(spectrum_stop_mhz-100.0)>1e-9) return 1;
    spectrum_average_set(4);
    if(spectrum_average_target!=4 || spectrum_average_valid) return 1;
    if(spectrum_marker_set_from_x(0,45,45,775)!=0 ||
       spectrum_marker_bin[0]!=0 || fabs(spectrum_marker_mhz(0))>1e-9) return 1;
    if(spectrum_marker_set_from_x(1,775,45,775)<0 || spectrum_marker_bin[1]<=0) return 1;
    spectrum_history_reset();
    /* Fs/4 input must use the explicitly labelled, trigger-anchored display
     * reconstruction even when the raw hysteretic frequency detector has an
     * unlucky four-sample phase. */
    for(int i=0;i<N;i++) a[i]=(uint32_t)lround(8192+600*sin(2*3.141592653589793*i/4.0+0.37));
    if(measure(a,&v)<0) return 1;
    last_flags=3; trigger.falling=0; spectrum_cache_valid=0; hf_model_reset();
    struct hf_model hm;
    if(!hf_model_update(a,&v,100,512,8192,0,&hm) || !hm.valid || fabs(hm.mhz-100)>0.08 ||
       fabs(hf_model_value(&hm,512)-8192)>1.0 || hf_model_value(&hm,512.01)<=hf_model_value(&hm,512)) return 1;
    for(int i=0;i<N;i++) a[i]=(uint32_t)lround(8192+660*sin(2*3.141592653589793*i/4.0+1.11));
    if(measure(a,&v)<0 || !hf_model_update(a,&v,101,512,8192,0,&hm) ||
       fabs(hf_model_value(&hm,512)-8192)>1.0 || hm.amp<600 || hm.amp>670) return 1;
    for(int i=0;i<N;i++) a[i]=(uint32_t)lround(8192+600*sin(2*3.141592653589793*i/40.0));
    if(measure(a,&v)<0 || hf_model_update(a,&v,102,512,8192,0,&hm)) return 1;
    a[12]=16384; if(measure(a,&v)==0) return 1;
    memset(a,0,sizeof(a)); if(measure(a,&v)<0 || v.mhz!=0 || v.rms_ac!=0) return 1;
    for(int i=0;i<N;i++) a[i]=8192;
    if(spectrum_measure(a,&sp)<0 || sp.peak_valid || sp.peak_mhz!=0) return 1;
    if(v.freq_valid || !v.rail || !v.low_signal) return 1;
    for(int i=0;i<N;i++) a[i]=8192;
    if(measure(a,&v)<0 || v.freq_valid || !v.low_signal || v.rail) return 1;
    for(int i=0;i<N;i++) a[i]=(uint32_t)lround(8192+8*sin(2*3.141592653589793*i/40.0));
    if(measure(a,&v)<0 || v.freq_valid || !v.low_signal) return 1;
    for(int i=0;i<N;i++) a[i]=(uint32_t)lround(8192+5000*sin(2*3.141592653589793*i/3.0));
    if(measure(a,&v)<0 || v.freq_valid) return 1;
    struct fb_var_screeninfo fmt; memset(&fmt,0,sizeof(fmt));
    fmt.red.offset=16; fmt.red.length=8; fmt.green.offset=8; fmt.green.length=8; fmt.blue.length=8;
    if(packed(0x12ab34,&fmt)!=0x12ab34 || unpack(0x12ab34,fmt.green)!=0xab) return 1;
    if(button_at(800,490,50)!=0 || button_at(800,590,50)!=1 || button_at(800,700,50)!=2 ||
       button_at(800,582,50)!=-1 || button_at(800,700,100)!=-1) return 1;
    struct touch t; memset(&t,0,sizeof(t)); t.ax.maximum=799; t.ay.maximum=479;
    struct input_event e; memset(&e,0,sizeof(e));
    e.type=EV_ABS; e.code=ABS_X; e.value=700; touch_event(&t,&e,800,480);
    e.code=ABS_Y; e.value=50; touch_event(&t,&e,800,480);
    e.type=EV_KEY; e.code=BTN_TOUCH; e.value=1; touch_event(&t,&e,800,480);
    e.type=EV_SYN; e.code=SYN_REPORT;
    if(touch_event(&t,&e,800,480)!=2 || touch_event(&t,&e,800,480)!=-1) return 1;
    e.type=EV_KEY; e.code=BTN_TOUCH; e.value=0; touch_event(&t,&e,800,480);
    e.type=EV_SYN; e.code=SYN_REPORT; touch_event(&t,&e,800,480);
    if(t.latched || !t.valid_x || !t.valid_y) return 1;
    struct view_config vc={50,1024,-8192};
    if(!valid_view(vc) || visible_samples(vc)!=200) return 1;
    vc.ns=500; if(!valid_view(vc) || visible_samples(vc)!=2000) return 1;
    vc.ns=1000; if(valid_view(vc)) return 1;
    vc.ns=100; vc.codes=1; if(valid_view(vc)) return 1;
    struct screen clip; memset(&clip,0,sizeof(clip)); clip.w=clip.h=64;
    uint32_t pixels[4096]={0}; clip.rgb=pixels;
    plot_line(&clip,0,0,63,63,1,8,8,55,55);
    for(int y=0;y<64;y++) for(int x=0;x<64;x++)
        if(pixels[y*64+x] && (x<8 || x>55 || y<8 || y>55)) return 1;
    /* Navigation row: five page keys plus the AUTO SET action key, which must
     * not change the selected page. Run last because it touches view state. */
    struct touch nt; memset(&nt,0,sizeof(nt));
    view_drag=0; trigger_drag=0; tool_page=0;
    if(!drag_press(&nt,90,450,800,480) || tool_page!=0) return 1;
    if(!drag_press(&nt,215,450,800,480) || tool_page!=1) return 1;
    if(!drag_press(&nt,340,450,800,480) || tool_page!=2) return 1;
    if(!drag_press(&nt,465,450,800,480) || tool_page!=3) return 1;
    tool_page=4; autoset_count=0;
    if(!drag_press(&nt,713,450,800,480) || tool_page!=4 || autoset_count!=1) return 1;
    if(!drag_press(&nt,779,450,800,480) || tool_page!=4 || autoset_count!=2) return 1;
    /* SIGHUP defers the same action to the main loop. */
    if(autoset_pending) return 1;
    control_handler(SIGHUP);
    if(!autoset_pending) return 1;
    autoset_pending=0;
    printf("SELFTEST_PASS metrics and RGB packing\n"); return 0;
}
#ifndef FR_PS_RECOVERY_NOW
static uint64_t recovery_monotonic_us(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)<0 || t.tv_sec<0 ||
       t.tv_nsec<0 || t.tv_nsec>=1000000000L) return UINT64_MAX;
    uint64_t sec=(uint64_t)t.tv_sec,us=(uint64_t)t.tv_nsec/1000;
    if(sec>(UINT64_MAX-us)/1000000) return UINT64_MAX;
    return sec*1000000+us;
}
#define FR_PS_RECOVERY_NOW() recovery_monotonic_us()
#endif
#ifndef FR_PS_RECOVERY_PAUSE
#define FR_PS_RECOVERY_PAUSE() pause_ns(1000000)
#endif
#ifndef FR_PLATFORM_NOW
#define FR_PLATFORM_NOW() FR_PS_RECOVERY_NOW()
#endif
static uint64_t reflect_platform_clock(void *unused) {
    (void)unused;return FR_PLATFORM_NOW();
}
static int reflect_platform_release(void *ctx) {
    int rc=FR_PLATFORM_RF_RELEASE(ctx);
    reflect_resources.rf_handle_owned=0;
    return rc;
}
static void reflect_platform_abort(void) {
    reflect_admission_requested=0;reflect_backend_permit=0;
    /* An active backend keeps its RF handle until independent OFF/ACK.
     * Admission has no DAC child; RF acquire and handoff occur in one tick. */
}
static void reflect_platform_failed(void) {
    reflect_platform_abort();reflect_raw_ui.notice=FR_UI_CAPTURE_FAILED;view_dirty=1;
}
/* Main-loop preparation, not a touch callback. Existing file/writer/backend
 * modules do the checks; this only assembles borrowed resources. Default-off
 * candidate never scans proc, requests RF GPIO or creates a DAC lock here.
 * proc/filesystem calls are not a hard real-time latency guarantee. */
static void reflect_platform_tick(int pending) {
    if(!reflect_admission_requested)return;
    if(reflect_frontend_confirmed!=1 || diag_ui_state.mode!=DIAG_UI_DIAG ||
       diag_ui_state.page!=DIAG_PAGE_REFLECT ||
       (uint32_t)control_generation!=reflect_platform_generation ||
       trigger_revision!=reflect_platform_revision || sweep.active || reflect.active ||
       stopped) {
        reflect_platform_abort();reflect_raw_ui.notice=FR_UI_STOPPED;view_dirty=1;return;
    }
    uint64_t now=FR_PLATFORM_NOW();
    if(!reflect_platform_watch.seeded &&
       fr_writer_watch_init(&reflect_platform_watch,now,3000000u,50000u)!=FR_WRITER_WAIT) {
        reflect_platform_failed();return;
    }
    if(now==UINT64_MAX || now<reflect_platform_watch.last_us ||
       now>=reflect_platform_watch.deadline_us || reflect_platform_adc_owned!=1 ||
       !reflect_platform_mailbox || reflect_platform_mailbox==MAP_FAILED ||
       reflect_platform_reader_fd<0 || !reflect_files.ready) {
        reflect_platform_failed();return;
    }
    reflect_platform_watch.last_us=now;
    if(pending || now<reflect_platform_next_probe)return;
    if(FR_PLATFORM_FILES_CHECK(&reflect_files)<0) {reflect_platform_failed();return;}
    struct fr_writer_snapshot snapshot;
    struct fr_writer_probe_config pc={.tool_fd=reflect_files.tool_fd,
        .reader_fd=reflect_platform_reader_fd,.tool_path="/usr/local/bin/dacctl",
        .proc_root="/proc",.lock_path="/run/lock/dac_control.lock",.self_pid=getpid()};
    if(FR_PLATFORM_WRITER_PROBE(pc,&snapshot)<0) {reflect_platform_failed();return;}
    uint64_t after=FR_PLATFORM_NOW();
    int observed=fr_writer_watch_step(&reflect_platform_watch,&snapshot,after);
    if(observed==FR_WRITER_FAILED) {reflect_platform_failed();return;}
    reflect_platform_next_probe=after+50000u;
    if(observed!=FR_WRITER_QUIET_OBSERVED)return;
    /* Two quiet snapshots are evidence, not an atomic session lock. The ADC
     * reader lock excludes cooperating UIs, and this process excludes its
     * GEN/SWEEP while admitted. Opt-in additionally attests that independent
     * DAC commands remain excluded for this whole session; arbitrary root
     * writers ignoring this contract cannot be excluded by these snapshots. */
    if(FR_PLATFORM_FILES_CHECK(&reflect_files)<0 ||
       reflect_platform_rf.handle_fd>=0 || reflect_platform_rf.mapping) {
        reflect_platform_failed();return;
    }
    /* 188/189 measured zynq_gpio at gpiochip2; label/118-line checks remain
     * mandatory in rf_emio_open, so a changed enumeration fails closed. */
    if(FR_PLATFORM_RF_OPEN(&reflect_platform_rf,"/dev/gpiochip2",1)<0) {
        if((reflect_platform_rf.handle_fd>=0 || reflect_platform_rf.mapping) &&
           reflect_platform_release(&reflect_platform_rf)<0)
            reflect_ps_adapter.safety_errors|=FR_PS_RELEASE_FAILED;
        reflect_platform_failed();return;
    }
    after=FR_PLATFORM_NOW();
    if(after==UINT64_MAX || after<reflect_platform_watch.last_us ||
       after>=reflect_platform_watch.deadline_us ||
       (uint32_t)control_generation!=reflect_platform_generation ||
       trigger_revision!=reflect_platform_revision || stopped ||
       FR_PLATFORM_FILES_CHECK(&reflect_files)<0) {
        if(reflect_platform_release(&reflect_platform_rf)<0)
            reflect_ps_adapter.safety_errors|=FR_PS_RELEASE_FAILED;
        reflect_platform_failed();return;
    }
    reflect_resources=(struct fr_backend_resources){.mailbox=reflect_platform_mailbox,
        .rf_ctx=&reflect_platform_rf,.monotonic_us=reflect_platform_clock,
        .rf_select=FR_PLATFORM_RF_SELECT,.rf_release=reflect_platform_release,
        .adc_lock_owned=1,.dac_writers_idle=1,.rf_handle_owned=1,
        .frontend_verified=reflect_frontend_confirmed,.dac_tool_verified=1,
        .dac_image=&reflect_files.image};
    reflect_admission_requested=0;reflect_backend_permit=1;capture_waiting=0;
    (void)reflect_backend_begin();
    /* No backend ownership means no DAC job was handed off. Close ONLY our
     * newly acquired RF handle; failed starts with owner use normal OFF drain. */
    if(!reflect_backend.active) {
        reflect_backend_permit=0;
        if(reflect_platform_release(&reflect_platform_rf)<0)
            reflect_ps_adapter.safety_errors|=FR_PS_RELEASE_FAILED;
    }
    reflect_backend_notice();view_dirty=1;
}
/* Called only with the existing ADC flock held, before touching trigger
 * configuration or issuing REQUEST. Never reuse an orphan's samples. */
static int recover_adc_mailbox(volatile uint32_t *m,int adc_lock_owned) {
    struct fr_ps_recovery r;
    fr_ps_recovery_init(&r,FR_PS_RECOVERY_NOW());
    for(;;) {
        barrier();
        struct fr_ps_mailbox_state s={m[REQ],m[DONE],m[2060],0};
        barrier();
        uint32_t req=m[REQ],done=m[DONE],flags=m[2060];
        s.stable=s.req==req && s.done==done && s.flags==flags;
        int action=fr_ps_recovery_step(&r,s,adc_lock_owned,FR_PS_RECOVERY_NOW());
        if(action==FR_PS_RECOVERY_READY) {
            printf("ADC_STARTUP_READY TOKEN=%u ORPHAN_DRAINED=%d OLD_SAMPLES_DISCARDED\n",
                   r.token,r.cancel_sent); return 0;
        }
        if(action==FR_PS_RECOVERY_FAILED) {
            fprintf(stderr,"ADC_STARTUP_BLOCKED ERROR=%d TOKEN=%u NO_NEW_REQUEST\n",
                    r.error,r.token); return -1;
        }
        if(action==FR_PS_RECOVERY_CANCEL) {
            m[2054]=r.token; barrier();
            printf("ADC_STARTUP_CANCEL TOKEN=%u\n",r.token);
        }
        FR_PS_RECOVERY_PAUSE();
    }
}
#ifndef FR_PS_SHUTDOWN_NOW
#define FR_PS_SHUTDOWN_NOW() FR_PS_RECOVERY_NOW()
#endif
#ifndef FR_PS_SHUTDOWN_PAUSE
#define FR_PS_SHUTDOWN_PAUSE() pause_ns(1000000)
#endif
/* Exit only: finite drain while ADC lock, mailbox and callback context remain
 * alive. This never runs on the interactive path or from a signal handler.
 * 4.5s accommodates prior child cancellation + separately timed OFF; it is
 * an observation budget, not a guarantee of kernel/child responsiveness. */
static int reflect_shutdown(volatile uint32_t *m,int pending,uint32_t token) {
    uint64_t now=FR_PS_SHUTDOWN_NOW(),last=now;
    (void)fr_ps_stop(&reflect_ps_adapter,FR_PS_EXIT,now);
    if(pending) {
        if(m==MAP_FAILED || !m || !token)return -1;
        barrier();
        if(m[REQ]==token) {m[2054]=token;barrier();}
    }
    uint64_t budget=reflect_ps_adapter.owned?4500000u:FR_PS_CANCEL_US;
    if(now>UINT64_MAX-budget)return -1;
    uint64_t end=now+budget;
    for(;;) {
        now=FR_PS_SHUTDOWN_NOW();
        if(now<last || now>=end)return -1;
        last=now;
        if(pending) {
            barrier();
            struct fr_ps_observation o={.req_before=m[REQ],.done_before=m[DONE],
                .flags_before=m[2060],.index_before=m[2061]};
            barrier();o.req_after=m[REQ];o.done_after=m[DONE];
            o.flags_after=m[2060];o.index_after=m[2061];o.observed_us=now;
            if(o.req_before==token && o.done_before==token &&
               o.req_after==token && o.done_after==token &&
               o.flags_before==o.flags_after &&
               (o.flags_after==1 || o.flags_after==3 || o.flags_after==4)) {
                if(reflect_ps_adapter.pending) (void)fr_ps_consume(&reflect_ps_adapter,&o);
                pending=0;
            }
        }
        if(reflect_ps_adapter.owned) (void)fr_ps_poll(&reflect_ps_adapter,now);
        uint64_t after=FR_PS_SHUTDOWN_NOW();
        if(after<last || after>=end)return -1;
        last=after;
        int raw_pending=reflect_ps_adapter.owned &&
            (reflect_ps_adapter.pending || reflect_ps_adapter.output_pending ||
             !reflect_ps_adapter.released);
        if(!pending && !raw_pending)
            return reflect_ps_adapter.owned && !fr_ps_clean(&reflect_ps_adapter)?-1:0;
        FR_PS_SHUTDOWN_PAUSE();
    }
}
#ifdef MONITOR_ASYNC694
/* Main-thread exit only; REFLECT keeps its mailbox/context until its stop
 * finishes. Ordinary GEN is then explicitly OFF before any storage drain. */
struct mask694_exit_progress {int reflect_stopped,output_off;};
static int mask694_exit_outputs(volatile uint32_t *m,int pending,uint32_t token,
                               struct mask694_exit_progress *progress) {
    if(!progress->reflect_stopped) {
        custom_ui_backend_close(&custom_backend);
        reflect_platform_abort();
        if(reflect_shutdown(m,pending,token)<0)return -1;
        progress->reflect_stopped=1;
    }
    if(!progress->output_off) {
        gen_action(4);
        if(gen_applied!=2 || reflect_ps_exclusive())return -1;
        progress->output_off=1;
    }
    return 0;
}
static int mask694_final_poweroff(void) {
    if(mask694_pipeline || gen_applied!=2 || reflect_ps_exclusive())return -1;
    fflush(stdout);
    return SCOPE_POWEROFF_EXEC();
}
#endif
int main(int argc,char **argv) {
    unsigned limit=30; int probe=0,fft_start=0,slow_start681=0; const char *dump=NULL;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--profile690")) {p690_selected=1;continue;}
        if(!strcmp(argv[i],"--packed-flush691")) {packed_flush691=1;continue;}
#ifdef MONITOR_ASYNC694
        if(!strcmp(argv[i],"--async-store694")) {mask694_selected=1;continue;}
#endif
        if(!strcmp(argv[i],"--touch-probe")) { struct touch t; return touch_open(&t,1,0)<0; }
        if(!strcmp(argv[i],"--selftest")) return selftest();
        if(!strcmp(argv[i],"--probe")) probe=1;
        else if(!strcmp(argv[i],"--reflect-raw-hardware-verified")) reflect_frontend_confirmed=1;
        else if(!strcmp(argv[i],"--reflect-manual-verified")) {
            reflect_frontend_confirmed=1;reflect_manual_mode=1;reflect_manual_only=1;
        }
        else if(!strcmp(argv[i],"--fft-start")) fft_start=1;
        else if(!strcmp(argv[i],"--slow-start")) slow_start681=1;
        else if(!strcmp(argv[i],"--static-screen")) static_screen=1;
        else if(!strcmp(argv[i],"--frames") && i+1<argc) {
            char *end; errno=0; unsigned long n=strtoul(argv[++i],&end,10);
            if(errno || *end || n>1000000) return 2;
            limit=(unsigned)n;
        } else if(!strcmp(argv[i],"--dump") && i+1<argc) dump=argv[++i];
        else { fprintf(stderr,"Usage: %s [--probe|--selftest] [--fft-start] [--static-screen] [--frames N (0=continuous)] [--dump PPM] [--reflect-raw-hardware-verified (attests PL/wiring/matching and exclusive DAC use; NOT S11/phase)]\n",argv[0]); return 2; }
    }
    if(fft_start) { tool_page=3; spectrum_history_reset(); }
    if(slow_start681) { tool_page=5; lf681_init(&lf_history681); }
    setvbuf(stdout,NULL,_IOLBF,0); struct screen s;
    if(open_screen(&s,probe)<0) return 1;
    if(probe) return 0;
    int lock=open("/run/lock/adc_snapshot.lock",O_CREAT|O_RDWR|O_CLOEXEC,0600);
    if(lock<0 || flock(lock,LOCK_EX|LOCK_NB)<0) { fprintf(stderr,"ADC reader busy\n"); if(lock>=0) close(lock); free(s.rgb); munmap(s.fb,s.f.smem_len); close(s.fd); return 1; }
    struct touch touch; touch.fd=-1;
    int pending=0,cancel_requested=0,operation_single=0,operation_level=8192;
    int operation_falling682=0;
    uint32_t token=0,display_token=0;
    sig_atomic_t operation_generation=0;
    unsigned operation_revision=0;
    double cancel_deadline=0,next_capture=0;
    double touch_next=0,start_time=0;
    unsigned frames=0,rendered_frames=UINT_MAX,rendered_trigger_revision=UINT_MAX;
    sig_atomic_t rendered_generation=-1;
    int rendered_tool=-1,rendered_waiting=-1,rendered_power=-1,rendered_paused=-1,rendered_single=-1;
#ifdef MONITOR_ASYNC694
    int instrument_ready694=0;
#endif
    uint32_t a[N]={0};
    struct metrics v={0};
    int mem=open("/dev/mem",O_RDWR|O_SYNC),result=1;
    volatile uint32_t *m=MAP_FAILED;
    if(mem<0) { perror("devmem"); goto cleanup; }
    m=mmap(NULL,16384,PROT_READ|PROT_WRITE,MAP_SHARED,mem,0x42000000);
    if(m==MAP_FAILED) { perror("ADC mmap"); goto cleanup; }
    barrier();
    if(m[MAGIC]!=0x534e5031U || m[COUNT]!=N || (m[2052]!=0x54524731U && m[2052]!=0x54524732U)) {
        fprintf(stderr,"TRG1 hardware required; no request written\n"); goto cleanup;
    }
    if(recover_adc_mailbox(m,1)<0) goto cleanup;
    fine_pretrigger=m[2052]==0x54524732U;
    printf("PRETRIGGER_CAPABILITY FINE=%d MAX=%d STEP=%d\n",fine_pretrigger,fine_pretrigger?2044:1536,fine_pretrigger?2:512);
    set_handler(SIGINT,stop_handler); set_handler(SIGTERM,stop_handler);
    set_handler(SIGUSR1,control_handler); set_handler(SIGUSR2,control_handler); set_handler(SIGCONT,control_handler);
    set_handler(SIGHUP,control_handler);
    touch_open(&touch,0,0);
    touch_next=seconds()+1.0;
    start_time=seconds();
    diag_ui_init(&diag_ui_state);
    memset(&diag_current_sample,0,sizeof(diag_current_sample));
    memset(&diag_last_event,0,sizeof(diag_last_event));
    diag_have_sample=diag_have_event=0;
    mkdir(DIAG_STORAGE_DIR,0755);
    if(mkdir(REFLECT_STORAGE_DIR,0755)<0 && errno!=EEXIST)
        fprintf(stderr,"REFLECT_STORAGE_UNAVAILABLE %s\n",REFLECT_STORAGE_DIR);
    struct diag_monitor_config diag_cfg={16,0.65,0.82,0.20,2,3,750,16};
    diag_load_monitor_config(&diag_cfg);
    diag_monitor_init(&diag_monitor_state,diag_cfg);
    sweep_load_config();
    sweep_load_flatness();
    sweep_load_reference();
    reflect_load_calibration();
    if(mask617_boot()!=0) fprintf(stderr,"MASK_INIT_FAILED EXISTING_INSTRUMENT_CONTINUES\n");
    fr_ps_init(&reflect_ps_adapter,&reflect_raw_ui.capture);
    reflect_prepare_files();
    if(reflect_frontend_confirmed==1) {
        reflect_platform_adc_owned=1;reflect_platform_mailbox=m;
        reflect_platform_reader_fd=open("/proc/self/exe",O_RDONLY|O_CLOEXEC);
        printf("REFLECT_RAW_OPT_IN FILES_READY=%d PHASE_UNVERIFIED=1 NO_S11_PERMISSION\n",
               reflect_files.ready);
    }
    char diag_csv_path[160];
    snprintf(diag_csv_path,sizeof(diag_csv_path),"%s/diag_monitor_events.csv",DIAG_STORAGE_DIR);
    diag_csv_fd=open(diag_csv_path,O_CREAT|O_APPEND|O_WRONLY,0644);
    if(diag_csv_fd<0) {
        snprintf(diag_csv_path,sizeof(diag_csv_path),"/var/log/diag_monitor_events.csv");
        diag_csv_fd=open(diag_csv_path,O_CREAT|O_APPEND|O_WRONLY,0644);
    }
    if(diag_csv_fd<0) {
        snprintf(diag_csv_path,sizeof(diag_csv_path),"/tmp/diag_monitor_events.csv");
        diag_csv_fd=open(diag_csv_path,O_CREAT|O_APPEND|O_WRONLY,0600);
    }
    if(diag_csv_fd>=0) {
        off_t csv_end=lseek(diag_csv_fd,0,SEEK_END);
        if(csv_end==0) diag_monitor_write_csv_header(diag_csv_fd);
        else diag_load_event_history(diag_csv_path);
    }
#ifdef MONITOR_ASYNC694
    instrument_ready694=1;
main_loop694:
#endif
    while(!stopped && (!limit || frames<limit)) {
        uint64_t loop690=p690_now();
        read_view();
        if(touch_retry_due(touch.fd,seconds(),&touch_next)) touch_open(&touch,0,1);
        touch_poll(&touch,s.w,s.h);
        monitor686_tick();
        mask617_tick();
        if(p690.enabled && (mask617_clock_bad || !mask617_ready ||
           !mask617_engaged() || !mask617_owner_eligible()))
            p690_report(stdout,"MASK_STATE_EXIT");
        scope_stats_context683();
        scope_stats_expire683((uint64_t)llround(seconds()*1000000.0));
        (void)fr_ps_control_sync(&reflect_ps_adapter,(uint32_t)control_generation,
                                trigger_revision,(uint64_t)(seconds()*1000000.0));
        if(reflect_ps_adapter.owned &&
           (diag_ui_state.mode!=DIAG_UI_DIAG || diag_ui_state.page!=DIAG_PAGE_REFLECT))
            (void)fr_ps_stop(&reflect_ps_adapter,FR_PS_STOP,
                            (uint64_t)(seconds()*1000000.0));
        if(pending && !cancel_requested &&
           (operation_generation!=control_generation || operation_revision!=trigger_revision)) {
            if(operation_single && operation_revision!=trigger_revision && operation_generation==control_generation)
                single_pending=1;
            m[2054]=token; barrier(); cancel_requested=1; cancel_deadline=seconds()+2;
            (void)fr_ps_stop(&reflect_ps_adapter,FR_PS_STOP,
                            (uint64_t)(seconds()*1000000.0));
            printf("TRIGGER_CANCEL_REQUEST TOKEN=%u\n",token);
        }
        barrier();
        if(pending && m[DONE]==token) {
            p690_done_at(token,p690_now());
            uint64_t frame690=p690_now();
            unsigned flags=m[2060],position=m[2061];
            int raw_owned_frame=reflect_ps_adapter.pending;
            struct fr_ps_observation raw_observation={
                .req_before=m[REQ],.done_before=m[DONE],
                .flags_before=flags,.index_before=position};
            if(!cancel_requested && (flags&1)) {
                uint64_t part690=p690_now();
                if(position>=N || copy_completed(m,a,token)<0) goto cleanup;
                p690_end(P690_COPY,part690);part690=p690_now();
                if(measure(a,&v)<0) goto cleanup;
                p690_end(P690_MEASURE,part690);
                if(reflect_ps_adapter.pending) {
                    barrier();
                    raw_observation.req_after=m[REQ]; raw_observation.done_after=m[DONE];
                    raw_observation.flags_after=m[2060]; raw_observation.index_after=m[2061];
                    raw_observation.samples=a; raw_observation.count=N;
                    raw_observation.copy_stable=1;
                    raw_observation.observed_us=(uint64_t)(seconds()*1000000.0);
                    int raw_rc=fr_ps_consume(&reflect_ps_adapter,&raw_observation);
                    if(raw_rc==FR_RAW_COMPLETE || raw_rc<0)
                        reflect_raw_ui.notice=FR_UI_STOPPING;
                    view_dirty=1;
                }
                part690=p690_now();
                (void)mask617_on_frame(a,&v,token,raw_owned_frame);
                p690_end(P690_MASK,part690);
                if(monitor686_fast())
                    (void)m686_observe(&monitor686_cadence,token,(uint64_t)llround(seconds()*1e6));
                last_flags=flags; last_trigger_index=position; last_trigger_level=operation_level; display_token=token; frames++;
                last_trigger_falling682=operation_falling682;
                last_frame_raw_owned682=raw_owned_frame;
                scope_stats_frame683(&v,token,(uint64_t)llround(seconds()*1000000.0),raw_owned_frame);
                if(scope_stats_enabled683 && !raw_owned_frame && frames%200==0) {
                    struct ss683_summary st;ss683_summarize(&scope_stats683,&st);
                    printf("SCOPE_STATS683 FRAME=%u TOKEN=%u N=%u VALID=%d MEAN_HZ=%.9f SD_HZ=%.9f PP_HZ=%.9f SPAN_US=%llu GAP_US=%llu SPARSE=1 NOMINAL_FS=1\n",
                           frames,token,st.n,st.valid,st.mean_hz,st.sd_hz,st.pp_hz,
                           (unsigned long long)st.span_us,(unsigned long long)st.max_gap_us);
                }
                if(!raw_owned_frame && lf_scope_active681())
                    (void)lf681_push(&lf_history681,token,(uint64_t)llround(seconds()*1000000.0),
                                     v.mean,v.lo,v.hi,!v.rail);
                if(!raw_owned_frame) {
                    diag_ring_times[diag_ring_next]=(uint64_t)llround(seconds()*1000.0);
                    memcpy(diag_frame_ring[diag_ring_next],a,sizeof(diag_frame_ring[0]));
                    diag_ring_next=(diag_ring_next+1)%DIAG_RING_FRAMES;
                    if(diag_ring_count<DIAG_RING_FRAMES) diag_ring_count++;
                }
                latest_metrics=v; latest_metrics_valid=1;
                struct signal_frame trend_frame={
                    token,(uint64_t)llround(seconds()*1000000.0),v.rms_ac,
                    v.freq_valid?v.mhz*1e6:NAN,v.freq_valid};
                (void)signal_trend_push(&signal_frame_history,&trend_frame);
                (void)signal_trend_summarize(&signal_frame_history,100000.0,&signal_trend_cache);
                if(signal_frame_history.count>=8 && (frames<=8 || frames%200==0))
                    printf("SIGNAL_TREND FRAMES=%u AMP_INDEX_PCT=%.3f FREQ_SPAN_HZ=%.3f CHANGES=%u MAX_GAP_MS=%.3f\n",
                           signal_trend_cache.frames,signal_trend_cache.amplitude_index_pct,
                           signal_trend_cache.frequency_span_hz,signal_trend_cache.observed_changes,
                           signal_trend_cache.max_gap_ms);
                diag_current_sample.timestamp_ms=(uint64_t)llround(seconds()*1000.0);
                diag_current_sample.rms_code=v.rms_ac;
                diag_current_sample.vpp_code=(double)(v.hi-v.lo);
                diag_current_sample.frequency_mhz=v.mhz;
                diag_current_sample.frequency_valid=v.freq_valid;
                diag_have_sample=1;
                struct diag_monitor_event completed;
                unsigned old_enter=diag_monitor_state.enter_count;
                enum diag_monitor_state old_state=diag_monitor_state.state;
                int diag_result=(sweep.active || reflect.active || raw_owned_frame) ? 0 :
                    diag_update_live_monitor(&diag_current_sample,&completed);
                if(!sweep.active && !reflect.active && !raw_owned_frame)
                    diag_capture_evidence(a,old_enter,old_state);
                if(!sweep.active && !reflect.active && !raw_owned_frame)
                    measurement_quality_update_frame(&v);
                else measurement_quality_mark_invalid(sweep.active?
                    "SWEEP_POINT_PENDING":"REFLECT_QUALITY_NOT_BOUND");
                if(old_state==DIAG_MONITOR_NORMAL && diag_monitor_state.state==DIAG_MONITOR_FAULT) {
                    if(diag_reserve_active_event()<0) {
                        diag_storage_error=1;
                        fprintf(stderr,"MONITOR_ONSET_SAVE_FAILED SEQ=%u\n",diag_monitor_state.event_sequence);
                    } else printf("MONITOR_ONSET_SAVED SEQ=%u\n",diag_monitor_state.event_sequence);
                }
                if(diag_result>0) {
                    completed.pre_frames=diag_evidence.pre;
                    completed.post_frames=diag_evidence.post;
                    diag_last_event=completed; diag_have_event=1;
                    diag_history_add(&completed);
                    diag_session_event_count++;
                    int frozen_ok=diag_write_frozen_event(&completed)==0;
                    int csv_ok=diag_csv_fd>=0 &&
                        diag_monitor_write_csv_event(diag_csv_fd,&completed)>0 && fsync(diag_csv_fd)==0;
                    diag_storage_error=!frozen_ok || !csv_ok;
                    if(diag_storage_error) fprintf(stderr,"MONITOR_EVIDENCE_SAVE_FAILED SEQ=%u FROZEN=%d CSV=%d\n",completed.sequence,frozen_ok,csv_ok);
                    diag_evidence_active=0;
                    diag_load_replay_event(completed.sequence);
                    if(diag_export_report()<0) { diag_storage_error=1; fprintf(stderr,"MONITOR_REPORT_SAVE_FAILED\n"); }
                    printf("MONITOR_EVENT SEQ=%u START_MS=%llu END_MS=%llu DURATION_MS=%llu SEVERITY=%.3f\n",
                        completed.sequence,(unsigned long long)completed.start_ms,
                        (unsigned long long)completed.end_ms,(unsigned long long)completed.duration_ms,
                        completed.severity);
                }
                if(!raw_owned_frame) { sweep_update(&v); reflect_update(&v); }
                printf("TRIGGER_FRAME FRAME=%u TOKEN=%u FLAGS=%u INDEX=%u SINGLE=%d\n",frames,token,flags,position,operation_single);
                if(frames<=3 || (limit && frames==limit) || frames%200==0)
                    printf("DISPLAY_FRAME=%u TOKEN=%u COUNT=%d FREQ_MHZ=%.6f MIN=%u MAX=%u RMS_AC_CODE=%.3f\n",frames,token,N,v.mhz,v.lo,v.hi,v.rms_ac);
            } else if(!cancel_requested && !(flags&4)) {
                fprintf(stderr,"Invalid trigger completion flags=%u\n",flags); goto cleanup;
            }
            if(reflect_ps_adapter.pending && (cancel_requested || (flags&4))) {
                barrier();
                raw_observation.req_after=m[REQ]; raw_observation.done_after=m[DONE];
                raw_observation.flags_after=m[2060]; raw_observation.index_after=m[2061];
                raw_observation.observed_us=(uint64_t)(seconds()*1000000.0);
                (void)fr_ps_consume(&reflect_ps_adapter,&raw_observation);
            }
            pending=0; cancel_requested=0; next_capture=seconds()+m686_delay_us(monitor686_fast())/1e6;
            p690_end(P690_FRAME,frame690);
        }
        if(pending && cancel_requested && seconds()>cancel_deadline) {
            fprintf(stderr,"Trigger cancel acknowledgment timeout\n"); goto cleanup;
        }
        /* Confirm asynchronous frequency/OFF jobs before considering ARM. */
        if(reflect_ps_adapter.owned &&
           fr_ps_poll(&reflect_ps_adapter,(uint64_t)(seconds()*1000000.0))<0) {
            reflect_raw_ui.notice=FR_UI_STOPPING; view_dirty=1;
        }
        reflect_backend_notice();
        reflect_platform_tick(pending);
        int planned_single=0,planned_mode=trigger.mode;
        if(!pending && reflect_capture_plan(&planned_single,&planned_mode) &&
           seconds()>=next_capture && (!limit || frames<limit)) {
            barrier(); token=m[REQ]+1; if(!token) token=1;
            if(token==m[DONE]) { ++token; if(!token) token=1; }
            int raw_owner=reflect_ps_exclusive();
            planned_mode=lf_capture_mode681(planned_mode,planned_single,raw_owner);
            planned_mode=m686_capture_mode(monitor686_fast(),planned_mode);
            int arm_rc=fr_ps_arm(&reflect_ps_adapter,token,
                                (uint64_t)(seconds()*1000000.0));
            if(arm_rc<0) goto cleanup;
            if(!raw_owner || arm_rc==FR_RAW_PROGRESS) {
            operation_single=planned_single;
            if(!reflect_ps_exclusive()) single_pending=0;
            operation_level=trigger.level;
            operation_falling682=trigger.falling;
            m[2054]=0; m[2056]=(uint32_t)trigger.level;
            m[2057]=(uint32_t)((operation_single?3:planned_mode)|(trigger.falling<<2));
            m[2058]=(uint32_t)trigger.pre; m[2059]=20000000U;
            barrier();
            mask617_request_capture(token);
            m[REQ]=token; barrier(); pending=1;
            p690_request_at(token,p690_now());
            operation_generation=control_generation; operation_revision=trigger_revision;
            printf("TRIGGER_ARM TOKEN=%u MODE=%d LEVEL=%d FALLING=%d PRE=%d\n",token,operation_single?3:planned_mode,trigger.level,trigger.falling,trigger.pre);
            }
        }
        capture_waiting=pending;
        /* SIGHUP runs AUTO SET from the main loop so no file or stdio work
         * happens inside the signal handler. */
        if(autoset_pending) { autoset_pending=0; autoset(); }
        if(cursor_auto && frozen_frame()) { tool_action(20,1); cursor_auto=0; }
        if((frames!=rendered_frames || view_dirty || tool_page!=rendered_tool ||
           capture_waiting!=rendered_waiting || power_dialog!=rendered_power ||
           paused!=rendered_paused || single_pending!=rendered_single ||
           control_generation!=rendered_generation || trigger_revision!=rendered_trigger_revision) &&
           (!monitor686_fast() || seconds()>=monitor686_next_paint ||
            power_dialog!=rendered_power || tool_page!=rendered_tool)) {
            uint64_t paint690=p690_now();
            render(&s,a,&v,frames,display_token);
            p690_end(P690_PAINT,paint690);
            monitor686_next_paint=seconds()+0.1;
            rendered_frames=frames; rendered_tool=tool_page;
            rendered_waiting=capture_waiting; rendered_power=power_dialog;
            rendered_paused=paused; rendered_single=single_pending;
            rendered_generation=control_generation; rendered_trigger_revision=trigger_revision;
        }
        power_tick();
        pause_ns(monitor686_fast()?250000:pending && !operation_single && trigger.mode!=1 ? 1000000 :
                 pending || !paused || single_pending ? 5000000 : 50000000);
        p690_end(P690_LOOP,loop690);
    }
    if(dump && frames && dump_screen(&s,dump)<0) goto cleanup;
    printf("DISPLAY_DONE frames=%u elapsed=%.3f\n",frames,seconds()-start_time); result=0;
cleanup:
#ifdef MONITOR_ASYNC694
    if(mask694_selected) {
        int prior_result=result;
        struct mask694_exit_progress exit_progress={0};
        for(;;) {
            int off_ok=mask694_exit_outputs(m,pending,token,&exit_progress)==0;
            if(exit_progress.reflect_stopped)pending=0;
            if(off_ok && (!mask655_pending || mask655_commit_pending()==0) &&
               mask617_shutdown()==0) {result=prior_result;break;}
            result=1;
            if(!mask694_pipeline) {
                mask694_power_after_drain=0;
                fprintf(stderr,"ASYNC694_EXIT_OFF_UNCONFIRMED_NO_QUEUE\n");break;
            }
            mask694_exit_hold=1;mask694_exit_retry=0;
            paused=1;single_pending=0;power_dialog=0;view_drag=trigger_drag=0;
            touch.gesture=0;
            diag_ui_state.mode=DIAG_UI_DIAG;diag_ui_state.page=DIAG_PAGE_MONITOR;
            mask617_panel=1;
            fprintf(stderr,"ASYNC694_EXIT_HELD OUTPUT_OFF=%d RAM_RETAINED=1 RETRY_TOUCH_ONLY=1\n",off_ok);
            while(!mask694_exit_retry) {
                if(touch_retry_due(touch.fd,seconds(),&touch_next))touch_open(&touch,0,1);
                touch_poll(&touch,s.w,s.h);
                (void)mask694_pump();
                mask617_note(off_ok?"EXIT HELD - UNSAVED RAM RETAINED / TAP RETRY":
                                        "EXIT HELD - OUTPUT OFF UNCONFIRMED / TAP RETRY");
                render(&s,a,&v,frames,display_token);
                pause_ns(100000000);
            }
            if(mask655_pending)(void)mask655_commit_pending();
            if(mask694_pipeline && !mask655_pending)(void)mask694_retry();
        }
        mask694_exit_hold=0;
    } else
#endif
    if(mask617_shutdown()!=0) {fprintf(stderr,"MASK_SHUTDOWN_UNSAVED_EVIDENCE\n");result=1;}
    p690_report(stdout,"CLEANUP");
#ifdef MONITOR_ASYNC694
    if(mask694_selected && mask694_power_after_drain && instrument_ready694) {
        for(;;) {
            if(mask694_final_poweroff()==0)break;
            fprintf(stderr,"POWER_OFF_FAILED_AFTER_DURABLE694 RETRY_OR_CANCEL_UI_RETAINED=1\n");
            power_dialog=3;shutdown_requested=0;
            while(power_dialog!=0 && !shutdown_requested) {
                if(touch_retry_due(touch.fd,seconds(),&touch_next))touch_open(&touch,0,1);
                touch_poll(&touch,s.w,s.h);
                render(&s,a,&v,frames,display_token);
                pause_ns(100000000);
            }
            if(power_dialog==0) {
                /* CANCEL resumes a paused instrument with output OFF and a
                 * fresh unarmed MASK session; all UI/MMIO contexts still live. */
                mask694_power_after_drain=0;shutdown_requested=0;stopped=0;
                paused=1;single_pending=0;pending=cancel_requested=0;capture_waiting=0;
                if(mask617_boot()!=0)fprintf(stderr,"MASK_REOPEN_AFTER_CANCEL694_FAILED\n");
                next_capture=seconds();view_dirty=1;rendered_power=-1;
                printf("POWER_CANCEL694 PAUSED_OUTPUT_OFF_NEW_UNARMED_SESSION\n");
                goto main_loop694;
            }
            shutdown_requested=0;
        }
    }
#endif
#ifdef MONITOR_ASYNC694
    if(!mask694_selected) {
#endif
    custom_ui_backend_close(&custom_backend);
    reflect_platform_abort();
    if(reflect_shutdown(m,pending,token)<0) {
        fprintf(stderr,"REFLECT_CLEANUP_UNCONFIRMED SAFETY=%u PENDING=%d\n",
                reflect_ps_adapter.safety_errors,reflect_ps_adapter.pending);
        result=1;
    }
#ifdef MONITOR_ASYNC694
    }
#endif
    if(touch.fd>=0) close(touch.fd);
    int borrowers_done=!reflect_backend.active && !fr_dac_job_busy(&reflect_backend.dac.job) &&
        !reflect_ps_adapter.pending && !reflect_ps_adapter.output_pending &&
        (!reflect_ps_adapter.owned || reflect_ps_adapter.released);
    if(borrowers_done && (reflect_platform_rf.handle_fd>=0 || reflect_platform_rf.mapping) &&
       reflect_platform_release(&reflect_platform_rf)<0)result=1;
    if(reflect_platform_reader_fd>=0) {close(reflect_platform_reader_fd);reflect_platform_reader_fd=-1;}
    reflect_platform_adc_owned=0;reflect_platform_mailbox=NULL;
    if(fr_platform_files_close(&reflect_files,borrowers_done)<0) {
        fprintf(stderr,"REFLECT_FILES_CLOSE_UNCONFIRMED ERRORS=%u\n",reflect_files.errors);
        result=1;
    }
    if(diag_csv_fd>=0) close(diag_csv_fd);
    if(m!=MAP_FAILED) munmap((void*)m,16384);
    if(mem>=0) close(mem);
    close(lock); free(s.rgb); munmap(s.fb,s.f.smem_len); close(s.fd);
    return result;
}
