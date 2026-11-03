#ifndef LOWFREQ681_H
#define LOWFREQ681_H

/* Host-received, discontinuous RAW snapshot statistics.  These are not a
 * continuous ADC record.  Frequency bounds bracket observed transitions;
 * they do not include unknown host delivery latency or exclude aliasing. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#define LF681_CAPACITY 512u
#define LF681_WINDOW_US UINT64_C(10000000)
#define LF681_GAP_US UINT64_C(250000)
#define LF681_MIN_POINTS 16u
#define LF681_MIN_MEAN_SPAN 32.0
#define LF681_MAX_HZ 2.0

enum lf681_status {
    LF681_OK = 0, LF681_NO_DATA, LF681_TOO_FEW_POINTS, LF681_LOW_SPAN,
    LF681_GAP, LF681_TOO_FEW_EDGES, LF681_TOO_FAST, LF681_BAD_TIME,
    LF681_IRREGULAR
};
enum lf681_push_result {
    LF681_PUSH_OK = 0, LF681_REJECT_VALUE = -1,
    LF681_REJECT_TIME = -2, LF681_REJECT_TOKEN = -3
};
struct lf681_point {
    uint32_t token;
    uint64_t received_us;
    double mean;
    unsigned lo, hi;
    int valid, break_before;
};
struct lf681_ring {
    struct lf681_point points[LF681_CAPACITY];
    size_t head, count; /* head is the next write slot. */
};
struct lf681_summary {
    enum lf681_status status;
    size_t count, valid_count;
    unsigned rising_edges;
    uint64_t first_us, last_us, max_gap_us;
    double mean_min, mean_max;
    double frequency_hz, frequency_low_hz, frequency_high_hz;
    int frequency_valid;
};

static inline void lf681_init(struct lf681_ring *r) {
    if(r) memset(r, 0, sizeof(*r));
}

/* Chronological index: zero is the oldest retained point. */
static inline const struct lf681_point *lf681_at(const struct lf681_ring *r,
                                                 size_t index) {
    if(!r || index >= r->count) return NULL;
    return &r->points[(r->head + LF681_CAPACITY - r->count + index)
                      % LF681_CAPACITY];
}

/* A validity marker breaks the trace; its legal statistics are not used.
 * Rejected input leaves the ring unchanged.  Tokens must be unique within
 * the retained 512-point history (normal 32-bit request-token wrap is allowed). */
static inline int lf681_push(struct lf681_ring *r, uint32_t token,
                             uint64_t received_us, double mean,
                             unsigned lo, unsigned hi, int valid) {
    const struct lf681_point *previous;
    struct lf681_point p;
    size_t i;
    if(!r || !token || !isfinite(mean) || lo > hi || hi > 16383u ||
       mean < (double)lo || mean > (double)hi || (valid != 0 && valid != 1))
        return LF681_REJECT_VALUE;
    previous = r->count ? lf681_at(r, r->count - 1u) : NULL;
    if(previous && received_us <= previous->received_us) return LF681_REJECT_TIME;
    for(i = 0; i < r->count; ++i)
        if(lf681_at(r, i)->token == token) return LF681_REJECT_TOKEN;
    p.token = token; p.received_us = received_us;
    p.mean = mean; p.lo = lo; p.hi = hi; p.valid = valid;
    p.break_before = !valid || (previous &&
        (!previous->valid || received_us - previous->received_us > LF681_GAP_US));
    r->points[r->head] = p;
    r->head = (r->head + 1u) % LF681_CAPACITY;
    if(r->count < LF681_CAPACITY) ++r->count;
    return LF681_PUSH_OK;
}

static inline const char *lf681_reason(enum lf681_status status) {
    switch(status) {
    case LF681_OK: return "COARSE HOST SNAPSHOTS";
    case LF681_NO_DATA: return "NO SNAPSHOTS";
    case LF681_TOO_FEW_POINTS: return "TOO FEW POINTS";
    case LF681_LOW_SPAN: return "LOW MEAN SPAN";
    case LF681_GAP: return "GAP / INVALID POINT";
    case LF681_TOO_FEW_EDGES: return "NEED 3 RISING EDGES";
    case LF681_TOO_FAST: return "TOO FAST FOR SNAPSHOTS";
    case LF681_BAD_TIME: return "INVALID HOST TIME";
    case LF681_IRREGULAR: return "IRREGULAR EDGE PERIODS";
    default: return "UNKNOWN";
    }
}

/* Rolling ten-second window ending at now_us.  All times are HOST reception
 * times.  No missing point is invented, interpolated, or concatenated.
 * At least three hysteretic rising transitions are required.  Their brackets
 * yield a deliberately coarse observed frequency interval, not a calibrated
 * frequency accuracy claim.  Every adjacent rising-edge period bracket must
 * admit a common period, compatible with the first-to-last estimate.  Even
 * an OK result cannot rule out input aliasing or unknown host latency. */
static inline void lf681_summarize(const struct lf681_ring *r, uint64_t now_us,
                                   struct lf681_summary *s) {
    uint64_t cutoff, previous_us = 0, last_valid_us = 0, last_low_us = 0;
    uint64_t first_edge_lo = 0, first_edge_hi = 0;
    uint64_t last_edge_lo = 0, last_edge_hi = 0;
    uint64_t period_lo = 0, period_hi = UINT64_MAX;
    double low, high;
    int have_previous = 0, have_valid = 0, have_low = 0, gap = 0;
    size_t i;
    const struct lf681_point *latest;
    if(!s) return;
    memset(s, 0, sizeof(*s));
    s->status = LF681_NO_DATA;
    s->mean_min = s->mean_max = NAN;
    s->frequency_hz = s->frequency_low_hz = s->frequency_high_hz = NAN;
    if(!r || !r->count) return;
    latest = lf681_at(r, r->count - 1u);
    if(now_us < latest->received_us) { s->status = LF681_BAD_TIME; return; }
    cutoff = now_us > LF681_WINDOW_US ? now_us - LF681_WINDOW_US : 0;
    for(i = 0; i < r->count; ++i) {
        const struct lf681_point *p = lf681_at(r, i);
        uint64_t dt;
        if(p->received_us < cutoff) continue;
        if(!s->count) s->first_us = p->received_us;
        s->last_us = p->received_us; ++s->count;
        if(have_previous) {
            dt = p->received_us - previous_us;
            if(dt > s->max_gap_us) s->max_gap_us = dt;
            if(p->break_before || dt > LF681_GAP_US) gap = 1;
        }
        previous_us = p->received_us; have_previous = 1;
        if(!p->valid) { gap = 1; continue; }
        if(have_valid) {
            dt = p->received_us - last_valid_us;
            if(dt > s->max_gap_us) s->max_gap_us = dt;
        } else s->mean_min = s->mean_max = p->mean;
        last_valid_us = p->received_us; have_valid = 1;
        if(p->mean < s->mean_min) s->mean_min = p->mean;
        if(p->mean > s->mean_max) s->mean_max = p->mean;
        ++s->valid_count;
    }
    if(!s->count) return;
    if(now_us - s->last_us > s->max_gap_us) s->max_gap_us = now_us - s->last_us;
    if(now_us - s->last_us > LF681_GAP_US) gap = 1;
    if(gap) { s->status = LF681_GAP; return; }
    if(s->valid_count < LF681_MIN_POINTS) { s->status = LF681_TOO_FEW_POINTS; return; }
    if(s->mean_max - s->mean_min < LF681_MIN_MEAN_SPAN) {
        s->status = LF681_LOW_SPAN; return;
    }
    low = s->mean_min + 0.35 * (s->mean_max - s->mean_min);
    high = s->mean_min + 0.65 * (s->mean_max - s->mean_min);
    for(i = 0; i < r->count; ++i) {
        const struct lf681_point *p = lf681_at(r, i);
        if(p->received_us < cutoff || !p->valid) continue;
        if(p->mean <= low) { last_low_us = p->received_us; have_low = 1; }
        else if(have_low && p->mean >= high) {
            if(!s->rising_edges) {
                first_edge_lo = last_low_us; first_edge_hi = p->received_us;
            } else {
                /* Each edge lies in [last-low, first-high].  Consecutive
                 * brackets bound one observed period without inventing an
                 * edge in an unobserved interval. */
                uint64_t adjacent_lo = last_low_us > last_edge_hi ?
                                       last_low_us - last_edge_hi : 0;
                uint64_t adjacent_hi = p->received_us - last_edge_lo;
                if(adjacent_lo > period_lo) period_lo = adjacent_lo;
                if(adjacent_hi < period_hi) period_hi = adjacent_hi;
            }
            last_edge_lo = last_low_us; last_edge_hi = p->received_us;
            ++s->rising_edges; have_low = 0;
        }
    }
    if(s->rising_edges < 3u || last_edge_lo <= first_edge_hi) {
        s->status = LF681_TOO_FEW_EDGES; return;
    }
    {
        double periods = (double)(s->rising_edges - 1u);
        double first_mid = (double)(first_edge_lo - s->first_us) +
                           0.5 * (double)(first_edge_hi - first_edge_lo);
        double last_mid = (double)(last_edge_lo - s->first_us) +
                          0.5 * (double)(last_edge_hi - last_edge_lo);
        double aggregate_lo = (double)(last_edge_lo - first_edge_hi) / periods;
        double aggregate_hi = (double)(last_edge_hi - first_edge_lo) / periods;
        double estimate = (last_mid - first_mid) / periods;
        double common_lo = fmax((double)period_lo, aggregate_lo);
        double common_hi = fmin((double)period_hi, aggregate_hi);
        if(common_lo <= 0.0 || common_lo > common_hi ||
           estimate < common_lo || estimate > common_hi) {
            s->status = LF681_IRREGULAR; return;
        }
        s->frequency_hz = 1000000.0 / estimate;
        s->frequency_low_hz = 1000000.0 / common_hi;
        s->frequency_high_hz = 1000000.0 / common_lo;
    }
    if(s->frequency_high_hz > LF681_MAX_HZ ||
       s->frequency_high_hz * (double)s->max_gap_us > 250000.0) {
        s->status = LF681_TOO_FAST; return;
    }
    s->status = LF681_OK; s->frequency_valid = 1;
}

#endif
