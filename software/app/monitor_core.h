#ifndef DIAG_MONITOR_CORE_H
#define DIAG_MONITOR_CORE_H

#include <stdint.h>
#include <stddef.h>

enum diag_monitor_state {
    DIAG_MONITOR_NO_SIGNAL=0,
    DIAG_MONITOR_BASELINE=1,
    DIAG_MONITOR_NORMAL=2,
    DIAG_MONITOR_FAULT=3
};

struct diag_monitor_config {
    unsigned baseline_frames;
    double drop_enter_ratio;
    double drop_exit_ratio;
    double freq_tolerance_ratio;
    unsigned enter_confirm_frames;
    unsigned exit_confirm_frames;
    uint64_t recovery_hold_ms;
    double min_valid_rms_code;
};

struct diag_monitor_sample {
    uint64_t timestamp_ms;
    double rms_code;
    double vpp_code;
    double frequency_mhz;
    int frequency_valid;
};

struct diag_monitor_event {
    uint32_t sequence;
    uint64_t start_ms;
    uint64_t end_ms;
    uint64_t duration_ms;
    double severity;
    double baseline_rms;
    double minimum_rms;
    double baseline_vpp;
    double minimum_vpp;
    unsigned pre_frames;
    unsigned post_frames;
};

struct diag_monitor {
    struct diag_monitor_config cfg;
    enum diag_monitor_state state;
    unsigned baseline_count;
    unsigned baseline_freq_count;
    double baseline_rms;
    double baseline_vpp;
    double baseline_freq;
    unsigned enter_count;
    uint64_t candidate_start_ms;
    double candidate_min_rms;
    double candidate_min_vpp;
    double candidate_severity;
    unsigned exit_count;
    uint64_t recovery_start_ms;
    uint64_t event_start_ms;
    double event_min_rms;
    double event_min_vpp;
    double event_severity;
    uint32_t event_sequence;
    uint64_t normal_frames;
    uint64_t fault_frames;
};

void diag_monitor_init(struct diag_monitor *m, struct diag_monitor_config cfg);
int diag_monitor_update(struct diag_monitor *m, const struct diag_monitor_sample *s,
                        struct diag_monitor_event *completed);
int diag_monitor_write_csv_header(int fd);
int diag_monitor_write_csv_event(int fd, const struct diag_monitor_event *e);
int diag_monitor_write_all(int fd,const void *data,size_t size);

#endif
