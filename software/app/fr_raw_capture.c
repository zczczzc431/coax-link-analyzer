#include "fr_raw_capture.h"
#include <math.h>
#include <string.h>

static int fail(struct fr_raw_capture *s, enum fr_raw_error error) {
    s->active = 0;
    s->completed = 0;
    s->error = error;
    return FR_RAW_FAILED;
}
static int clock_now(struct fr_raw_capture *s, uint64_t *now) {
    *now = s->backend.monotonic_us(s->backend.ctx);
    if (*now < s->last_clock_us) return fail(s, FR_RAW_ERROR_CLOCK);
    s->last_clock_us = *now;
    return 0;
}
static int select_path(struct fr_raw_capture *s, enum fr_raw_path path) {
    uint64_t now;
    if (s->backend.select_path(s->backend.ctx, path) != 0)
        return fail(s, FR_RAW_ERROR_SWITCH);
    /* Timestamp AFTER the callback returns, including any backend delay. */
    if (clock_now(s, &now) < 0) return FR_RAW_FAILED;
    if (s->epoch == UINT64_MAX || s->config.settle_us > UINT64_MAX - now)
        return fail(s, FR_RAW_ERROR_CLOCK);
    s->ready_us = now + s->config.settle_us;
    if (s->config.timeout_us > UINT64_MAX - s->ready_us)
        return fail(s, FR_RAW_ERROR_CLOCK);
    s->deadline_us = s->ready_us + s->config.timeout_us;
    ++s->epoch;
    s->path = path;
    s->guard_left = s->config.guard_frames;
    s->average_count = 0;
    s->sum_squares = 0.0;
    return 0;
}
static int next_frequency(struct fr_raw_capture *s) {
    uint64_t before=0,now=0;
    if(s->frequency_poll && clock_now(s,&before)<0) return FR_RAW_FAILED;
    int rc=s->backend.set_frequency(s->backend.ctx,
                                    s->config.frequency_hz[s->point]);
    if(rc!=0 && !(s->frequency_poll && rc==FR_RAW_FREQUENCY_PENDING))
        return fail(s, FR_RAW_ERROR_FREQUENCY);
    if(s->frequency_poll) {
        if(clock_now(s,&now)<0 || s->config.timeout_us>UINT64_MAX-before)
            return fail(s,FR_RAW_ERROR_CLOCK);
        s->frequency_deadline_us=before+s->config.timeout_us;
        if(now>=s->frequency_deadline_us) return fail(s,FR_RAW_ERROR_TIMEOUT);
    }
    if(s->frequency_poll && rc==FR_RAW_FREQUENCY_PENDING) {
        s->frequency_pending=1;
        return 0;
    }
    s->frequency_pending=0;
    return select_path(s, FR_RAW_FWD);
}
int fr_raw_config_valid(const struct fr_raw_config *c) {
    if (!c || c->points == 0 || c->points > FR_RAW_MAX_POINTS ||
        c->frames_per_path == 0 || c->frames_per_path > 256 ||
        c->guard_frames > 1024 || c->timeout_us == 0 ||
        !isfinite(c->min_fwd_rms) || !isfinite(c->min_rev_rms) ||
        c->min_fwd_rms <= 0.0 || c->min_fwd_rms > FR_RAW_MAX_RMS_CODE ||
        c->min_rev_rms <= 0.0 || c->min_rev_rms > FR_RAW_MAX_RMS_CODE) return 0;
    for (unsigned i = 0; i < c->points; ++i)
        if (c->frequency_hz[i] < 1000000u || c->frequency_hz[i] > 180000000u)
            return 0;
    return 1;
}
static int start(struct fr_raw_capture *s, const struct fr_raw_config *cfg,
                 struct fr_raw_backend backend, uint64_t sequence_floor,
                 int (*frequency_poll)(void *)) {
    if (!s) return FR_RAW_FAILED;
    if (s->active) return FR_RAW_FAILED;
    /* Caller must abort an existing session before restarting. cfg may be
     * &s->config, so preserve its validated value before clearing state. */
    if (!fr_raw_config_valid(cfg) || !backend.set_frequency || !backend.select_path ||
        !backend.monotonic_us) {
        memset(s, 0, sizeof(*s));
        return fail(s, FR_RAW_ERROR_CONFIG);
    }
    struct fr_raw_config copy = *cfg;
    memset(s, 0, sizeof(*s));
    s->config = copy;
    s->backend = backend;
    s->frequency_poll=frequency_poll;
    s->last_sequence = sequence_floor;
    for (unsigned i = 0; i < s->config.points; ++i) {
        s->trace[i].frequency_hz = s->config.frequency_hz[i];
        s->trace[i].fwd_rms = NAN;
        s->trace[i].rev_rms = NAN;
        s->trace[i].sample_ratio = NAN;
    }
    s->active = 1;
    return next_frequency(s) < 0 ? FR_RAW_FAILED : FR_RAW_PROGRESS;
}
int fr_raw_start(struct fr_raw_capture *s,const struct fr_raw_config *cfg,
                 struct fr_raw_backend backend,uint64_t floor) {
    return start(s,cfg,backend,floor,NULL);
}
int fr_raw_start_async(struct fr_raw_capture *s,const struct fr_raw_config *cfg,
                       struct fr_raw_backend backend,uint64_t floor,
                       int (*poll)(void *)) {
    if(!s || s->active) return FR_RAW_FAILED;
    if(!poll || !fr_raw_config_valid(cfg)) return fail(s,FR_RAW_ERROR_CONFIG);
    for(unsigned i=0;i<cfg->points;i++)
        if(cfg->frequency_hz[i]%1000u) return fail(s,FR_RAW_ERROR_CONFIG);
    return start(s,cfg,backend,floor,poll);
}
int fr_raw_can_arm(const struct fr_raw_capture *s,uint64_t now) {
    return s && s->active && !s->frequency_pending && s->epoch &&
           now>=s->ready_us && now<=s->deadline_us;
}
int fr_raw_poll(struct fr_raw_capture *s) {
    uint64_t now;
    if (!s) return FR_RAW_FAILED;
    if (!s->active) return s->completed ? FR_RAW_COMPLETE : FR_RAW_FAILED;
    if (clock_now(s, &now) < 0) return FR_RAW_FAILED;
    if(s->frequency_pending) {
        if(now>=s->frequency_deadline_us) return fail(s,FR_RAW_ERROR_TIMEOUT);
        int rc=s->frequency_poll(s->backend.ctx);
        if(clock_now(s,&now)<0) return FR_RAW_FAILED;
        if(now>=s->frequency_deadline_us) return fail(s,FR_RAW_ERROR_TIMEOUT);
        if(rc==0) return FR_RAW_IGNORED;
        if(rc!=1) return fail(s,FR_RAW_ERROR_FREQUENCY);
        s->frequency_pending=0;
        return select_path(s,FR_RAW_FWD)<0?FR_RAW_FAILED:FR_RAW_PROGRESS;
    }
    if (now > s->deadline_us) return fail(s, FR_RAW_ERROR_TIMEOUT);
    return FR_RAW_IGNORED;
}
int fr_raw_push(struct fr_raw_capture *s, const struct fr_raw_frame *f) {
    uint64_t now;
    if (!s || !f) return FR_RAW_FAILED;
    if (!s->active) return s->completed ? FR_RAW_COMPLETE : FR_RAW_FAILED;
    if (clock_now(s, &now) < 0) return FR_RAW_FAILED;
    if(s->frequency_pending) {
        if(now>=s->frequency_deadline_us) return fail(s,FR_RAW_ERROR_TIMEOUT);
        ++s->ignored_frames;return FR_RAW_IGNORED;
    }
    if (now > s->deadline_us) return fail(s, FR_RAW_ERROR_TIMEOUT);
    /* Old/foreign frames do not consume the guard-frame or average budget. */
    if (f->sequence <= s->last_sequence || f->epoch != s->epoch ||
        f->frequency_hz != s->config.frequency_hz[s->point] ||
        f->started_us < s->ready_us) {
        ++s->ignored_frames;
        return FR_RAW_IGNORED;
    }
    if (f->completed_us < f->started_us || f->completed_us > now ||
        !isfinite(f->rms_ac_code) || f->rms_ac_code < 0.0 ||
        f->rms_ac_code > FR_RAW_MAX_RMS_CODE ||
        (f->clipped != 0 && f->clipped != 1))
        return fail(s, FR_RAW_ERROR_FRAME);
    s->last_sequence = f->sequence;
    if (s->guard_left) {
        --s->guard_left;
        return FR_RAW_IGNORED;
    }
    struct fr_raw_point *p = &s->trace[s->point];
    if (f->clipped) p->quality |= FR_RAW_CLIPPED;
    s->sum_squares += f->rms_ac_code * f->rms_ac_code;
    ++s->average_count;
    if (s->path == FR_RAW_FWD) {
        if (p->fwd_frames == 0) p->fwd_first_seq = f->sequence;
        p->fwd_last_seq = f->sequence;
        ++p->fwd_frames;
    } else {
        if (p->rev_frames == 0) p->rev_first_seq = f->sequence;
        p->rev_last_seq = f->sequence;
        ++p->rev_frames;
    }
    if (s->average_count < s->config.frames_per_path) return FR_RAW_PROGRESS;
    /* Equal-length frame AC-power mean; not an arithmetic amplitude mean. */
    double rms = sqrt(s->sum_squares / s->average_count);
    if (s->path == FR_RAW_FWD) {
        p->fwd_rms = rms;
        if (rms < s->config.min_fwd_rms) p->quality |= FR_RAW_LOW_FWD;
        return select_path(s, FR_RAW_REV) < 0 ? FR_RAW_FAILED : FR_RAW_PROGRESS;
    }
    p->rev_rms = rms;
    if (rms < s->config.min_rev_rms) p->quality |= FR_RAW_LOW_REV;
    if (p->quality == 0) p->sample_ratio = p->rev_rms / p->fwd_rms;
    p->complete = 1;
    ++s->point;
    if (s->point == s->config.points) {
        s->active = 0;
        s->completed = 1;
        return FR_RAW_COMPLETE;
    }
    return next_frequency(s) < 0 ? FR_RAW_FAILED : FR_RAW_PROGRESS;
}
void fr_raw_abort(struct fr_raw_capture *s) {
    if (s) (void)fail(s, FR_RAW_ERROR_ABORTED);
}
const char *fr_raw_quality_name(const struct fr_raw_point *p) {
    if (!p || !p->complete) return "PENDING";
    if (p->quality & FR_RAW_CLIPPED) return "CLIPPED";
    if (p->quality & FR_RAW_LOW_FWD) return "LOW_FWD";
    if (p->quality & FR_RAW_LOW_REV) return "LOW_REV";
    return "RAW_OK_UNCALIBRATED";
}
const char *fr_raw_error_name(enum fr_raw_error e) {
    switch (e) {
    case FR_RAW_ERROR_NONE: return "NONE";
    case FR_RAW_ERROR_CONFIG: return "CONFIG";
    case FR_RAW_ERROR_CLOCK: return "CLOCK";
    case FR_RAW_ERROR_FREQUENCY: return "FREQUENCY";
    case FR_RAW_ERROR_SWITCH: return "SWITCH";
    case FR_RAW_ERROR_TIMEOUT: return "TIMEOUT";
    case FR_RAW_ERROR_FRAME: return "FRAME";
    case FR_RAW_ERROR_ABORTED: return "ABORTED";
    default: return "UNKNOWN";
    }
}
int fr_raw_write_csv(FILE *file, const struct fr_raw_capture *s) {
    if (!file || !s || s->config.points == 0 ||
        s->config.points > FR_RAW_MAX_POINTS) return -1;
    /* Add comment metadata only: preserve the v1 data columns and values.
     * These are capture-owned settings, never the UI's next pending test. */
    if (fprintf(file, "# capture_settings_v1 points=%u frames_per_path=%u guard_frames=%u\n"
                     "# settle_us=%llu timeout_us=%llu clock=MONOTONIC\n"
                     "# min_fwd_rms_code=%.17g min_rev_rms_code=%.17g\n"
                     "# averaging=SQRT_MEAN_FRAME_AC_RMS_SQUARED; paths=SEQUENTIAL_FWD_REV\n"
                     "# frequency_hz=REQUESTED_NOT_MEASURED; raw_core_output_amplitude=NOT_RECORDED\n"
                     "# completed=RAW_POINTS_ONLY_NOT_OUTPUT_OFF_ACK\n",
                s->config.points, s->config.frames_per_path, s->config.guard_frames,
                (unsigned long long)s->config.settle_us,
                (unsigned long long)s->config.timeout_us,
                s->config.min_fwd_rms, s->config.min_rev_rms) < 0) return -1;
    if (fprintf(file, "# reflect_raw_v1; hardware=UNVERIFIED; calibration=NONE\n"
                     "# units=ADC_AC_RMS_CODE; sample_ratio=REV_RMS/FWD_RMS; "
                     "not_S11_not_VSWR_not_DTF\n"
                     "# active=%d completed=%d error=%s ignored_frames=%llu\n"
                     "frequency_hz,fwd_rms_code,rev_rms_code,sample_ratio,quality,"
                     "quality_bits,fwd_frames,rev_frames,fwd_first_seq,fwd_last_seq,"
                     "rev_first_seq,rev_last_seq\n",
                s->active, s->completed, fr_raw_error_name(s->error),
                (unsigned long long)s->ignored_frames) < 0) return -1;
    for (unsigned i = 0; i < s->config.points; ++i) {
        const struct fr_raw_point *p = &s->trace[i];
        if (fprintf(file, "%u,%.9g,%.9g,%.9g,%s,%u,%u,%u,%llu,%llu,%llu,%llu\n",
                    p->frequency_hz, p->fwd_rms, p->rev_rms, p->sample_ratio,
                    fr_raw_quality_name(p), p->quality, p->fwd_frames, p->rev_frames,
                    (unsigned long long)p->fwd_first_seq,
                    (unsigned long long)p->fwd_last_seq,
                    (unsigned long long)p->rev_first_seq,
                    (unsigned long long)p->rev_last_seq) < 0) return -1;
    }
    return ferror(file) ? -1 : 0;
}
