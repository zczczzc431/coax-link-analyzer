#include "lowfreq681.h"
#include <stdio.h>

static unsigned checks;
#define CHECK(condition) do { \
    ++checks; \
    if(!(condition)) { \
        fprintf(stderr, "LOWFREQ681_FAIL line=%d check=%s\n", __LINE__, #condition); \
        return 1; \
    } \
} while(0)

static double square_mean(uint64_t t) {
    return t % UINT64_C(1000000) < UINT64_C(500000) ? 8000.0 : 10000.0;
}

static int push_mean(struct lf681_ring *r, uint32_t token, uint64_t t, double mean) {
    return lf681_push(r, token, t, mean, (unsigned)mean - 2u,
                     (unsigned)mean + 2u, 1);
}

static int test_irregular_1hz(void) {
    const uint64_t steps[] = {40000, 70000, 50000, 60000, 30000, 80000};
    struct lf681_ring r, saved;
    struct lf681_summary s;
    uint64_t t = UINT64_C(1000000000000000);
    uint32_t token = 1;
    size_t n = 0;
    lf681_init(&r);
    while(t < UINT64_C(1000000009900000)) {
        CHECK(push_mean(&r, token++, t, square_mean(t)) == LF681_PUSH_OK);
        t += steps[n++ % (sizeof(steps) / sizeof(steps[0]))];
    }
    t = lf681_at(&r, r.count - 1u)->received_us;
    lf681_summarize(&r, t, &s);
    CHECK(s.status == LF681_OK && s.frequency_valid);
    CHECK(s.rising_edges >= 3u && s.valid_count == r.count);
    CHECK(s.mean_min == 8000.0 && s.mean_max == 10000.0);
    CHECK(s.frequency_hz > 0.98 && s.frequency_hz < 1.02);
    CHECK(s.frequency_low_hz <= 1.0 && s.frequency_high_hz >= 1.0);
    CHECK(s.frequency_low_hz <= s.frequency_hz && s.frequency_hz <= s.frequency_high_hz);
    CHECK(s.max_gap_us == 80000u);
    saved = r;
    CHECK(push_mean(&r, 1, t + 50000u, 9000.0) == LF681_REJECT_TOKEN);
    CHECK(push_mean(&r, token, t - 1u, 9000.0) == LF681_REJECT_TIME);
    CHECK(push_mean(&r, token, t, 9000.0) == LF681_REJECT_TIME);
    CHECK(lf681_push(&r, token, t + 50000u, NAN, 0, 1, 1) == LF681_REJECT_VALUE);
    CHECK(lf681_push(&r, token, t + 50000u, INFINITY, 0, 16383, 1) == LF681_REJECT_VALUE);
    CHECK(lf681_push(&r, token, t + 50000u, 9000, 9001, 9000, 1) == LF681_REJECT_VALUE);
    CHECK(lf681_push(&r, token, t + 50000u, 9000, 0, 16384, 1) == LF681_REJECT_VALUE);
    CHECK(lf681_push(&r, token, t + 50000u, 9001, 8999, 9000, 1) == LF681_REJECT_VALUE);
    CHECK(lf681_push(&r, token, t + 50000u, 9000, 8999, 9001, 2) == LF681_REJECT_VALUE);
    CHECK(lf681_push(&r, 0, t + 50000u, 9000, 8999, 9001, 1) == LF681_REJECT_VALUE);
    CHECK(memcmp(&r, &saved, sizeof(r)) == 0);
    lf681_summarize(&r, t - 1u, &s);
    CHECK(s.status == LF681_BAD_TIME && !s.frequency_valid);
    lf681_summarize(&r, t + LF681_GAP_US + 1u, &s);
    CHECK(s.status == LF681_GAP && !s.frequency_valid);
    CHECK(s.max_gap_us == LF681_GAP_US + 1u);
    return 0;
}

static int test_rejections(void) {
    struct lf681_ring r;
    struct lf681_summary s;
    uint32_t i;
    lf681_init(&r);
    lf681_summarize(&r, 0, &s);
    CHECK(s.status == LF681_NO_DATA && !s.frequency_valid && isnan(s.mean_min));
    for(i = 0; i < 80u; ++i)
        CHECK(push_mean(&r, i + 1u, (uint64_t)i * 50000u, 8192.0) == LF681_PUSH_OK);
    lf681_summarize(&r, 3950000u, &s);
    CHECK(s.status == LF681_LOW_SPAN && !s.frequency_valid);
    CHECK(s.mean_min == 8192.0 && s.mean_max == 8192.0);
    lf681_init(&r);
    for(i = 0; i < 80u; ++i)
        CHECK(push_mean(&r, i + 1u, (uint64_t)i * 50000u,
                        8192.0 + (double)(i % 2u)) == LF681_PUSH_OK);
    lf681_summarize(&r, 3950000u, &s);
    CHECK(s.status == LF681_LOW_SPAN && !s.frequency_valid);
    lf681_init(&r);
    for(i = 0; i < 8u; ++i)
        CHECK(push_mean(&r, i + 1u, (uint64_t)i * 50000u,
                        i % 2u ? 10000.0 : 8000.0) == LF681_PUSH_OK);
    lf681_summarize(&r, 350000u, &s);
    CHECK(s.status == LF681_TOO_FEW_POINTS && !s.frequency_valid);
    lf681_init(&r);
    for(i = 0; i < 20u; ++i)
        CHECK(push_mean(&r, i + 1u, (uint64_t)i * 50000u,
                        square_mean((uint64_t)i * 50000u)) == LF681_PUSH_OK);
    lf681_summarize(&r, 950000u, &s);
    CHECK(s.status == LF681_TOO_FEW_EDGES && !s.frequency_valid);
    lf681_init(&r);
    for(i = 0; i < 100u; ++i)
        CHECK(push_mean(&r, i + 1u, (uint64_t)i * 10000u,
                        i % 10u < 5u ? 8000.0 : 10000.0) == LF681_PUSH_OK);
    lf681_summarize(&r, 990000u, &s);
    CHECK(s.status == LF681_TOO_FAST && !s.frequency_valid);
    /* The 2 Hz ceiling and the cadence gate are independent: a well-sampled
     * 1.5 Hz input is accepted, whereas a sparse cadence may reject 1 Hz. */
    lf681_init(&r);
    for(i = 0; i < 320u; ++i) {
        uint64_t t = (uint64_t)i * 20000u;
        CHECK(push_mean(&r, i + 1u, t,
                        (t * 3u) % 2000000u < 1000000u ? 8000.0 : 10000.0)
              == LF681_PUSH_OK);
    }
    lf681_summarize(&r, 6380000u, &s);
    CHECK(s.status == LF681_OK && s.frequency_valid);
    CHECK(s.frequency_low_hz <= 1.5 && s.frequency_high_hz >= 1.5);
    return 0;
}

static int test_gaps_and_window(void) {
    struct lf681_ring r;
    struct lf681_summary s;
    uint64_t t;
    uint32_t i, token = 1;
    lf681_init(&r);
    for(i = 0; i < 100u; ++i)
        CHECK(push_mean(&r, token++, (uint64_t)i * 50000u,
                        square_mean((uint64_t)i * 50000u)) == LF681_PUSH_OK);
    t = UINT64_C(4950000) + LF681_GAP_US;
    CHECK(push_mean(&r, token++, t, square_mean(t)) == LF681_PUSH_OK);
    CHECK(!lf681_at(&r, r.count - 1u)->break_before); /* Exactly 250 ms is allowed. */
    t += LF681_GAP_US + 1u;
    CHECK(push_mean(&r, token++, t, square_mean(t)) == LF681_PUSH_OK);
    CHECK(lf681_at(&r, r.count - 1u)->break_before);
    lf681_summarize(&r, t, &s);
    CHECK(s.status == LF681_GAP && !s.frequency_valid);
    CHECK(s.max_gap_us == LF681_GAP_US + 1u);
    for(i = 0; i < 220u; ++i) {
        t += 50000u;
        CHECK(push_mean(&r, token++, t, square_mean(t)) == LF681_PUSH_OK);
    }
    lf681_summarize(&r, t, &s);
    CHECK(s.status == LF681_OK && s.frequency_valid);
    CHECK(s.first_us >= t - LF681_WINDOW_US && s.last_us == t);
    CHECK(s.count == 201u && s.max_gap_us == 50000u);
    t += 50000u;
    CHECK(lf681_push(&r, token++, t, 8192, 8190, 8194, 0) == LF681_PUSH_OK);
    CHECK(lf681_at(&r, r.count - 1u)->break_before);
    t += 50000u;
    CHECK(push_mean(&r, token++, t, square_mean(t)) == LF681_PUSH_OK);
    CHECK(lf681_at(&r, r.count - 1u)->break_before);
    lf681_summarize(&r, t, &s);
    CHECK(s.status == LF681_GAP && !s.frequency_valid);
    CHECK(s.valid_count + 1u == s.count && s.max_gap_us == 100000u);
    lf681_summarize(&r, t + LF681_WINDOW_US + 1u, &s);
    CHECK(s.status == LF681_NO_DATA && !s.frequency_valid);
    return 0;
}

static int test_ring_wrap(void) {
    struct lf681_ring r;
    struct lf681_summary s;
    uint32_t i;
    lf681_init(&r);
    for(i = 0; i < 700u; ++i)
        CHECK(push_mean(&r, i + 1u, (uint64_t)i * 20000u,
                        square_mean((uint64_t)i * 20000u)) == LF681_PUSH_OK);
    CHECK(r.count == LF681_CAPACITY && r.head == 700u % LF681_CAPACITY);
    CHECK(lf681_at(&r, 0)->token == 189u);
    CHECK(lf681_at(&r, r.count - 1u)->token == 700u);
    CHECK(lf681_at(&r, r.count) == NULL && lf681_at(NULL, 0) == NULL);
    for(i = 1; i < LF681_CAPACITY; ++i)
        CHECK(lf681_at(&r, i)->received_us - lf681_at(&r, i - 1u)->received_us == 20000u);
    lf681_summarize(&r, UINT64_C(699) * 20000u, &s);
    CHECK(s.status == LF681_OK && s.frequency_valid && s.count == 501u);
    CHECK(s.frequency_low_hz <= 1.0 && s.frequency_high_hz >= 1.0);
    lf681_init(&r);
    CHECK(push_mean(&r, UINT32_MAX, UINT64_MAX - 100000u, 8000.0) == LF681_PUSH_OK);
    CHECK(push_mean(&r, 1, UINT64_MAX - 50000u, 10000.0) == LF681_PUSH_OK);
    CHECK(!lf681_at(&r, 1)->break_before);
    lf681_summarize(&r, UINT64_MAX - 50000u, &s);
    CHECK(s.status == LF681_TOO_FEW_POINTS && s.max_gap_us == 50000u);
    return 0;
}

static int test_period_consistency(void) {
    const uint64_t steps[] = {71000, 93000, 83000, 91000, 77000, 90000};
    const uint64_t alternating[] = {500000, 700000, 2500000, 2700000,
                                   4500000, 4700000, 6500000, 6700000,
                                   8500000, 8700000};
    const uint64_t missed[] = {500000, 1500000, 2500000, 4500000,
                              5500000, 6500000, 7500000, 8500000, 9500000};
    const uint64_t jumping[] = {500000, 1300000, 2700000, 3400000,
                               5100000, 5700000, 6900000, 7600000, 9300000};
    const uint64_t *negative[] = {alternating, missed, jumping};
    const size_t negative_sizes[] = {
        sizeof(alternating) / sizeof(alternating[0]),
        sizeof(missed) / sizeof(missed[0]),
        sizeof(jumping) / sizeof(jumping[0])
    };
    struct lf681_ring r;
    struct lf681_summary s;
    uint32_t token;
    uint64_t phase, t;
    size_t i, j;
    /* At most 93 ms between snapshots, with ten different signal phases. */
    for(phase = 0; phase < 1000000u; phase += 100000u) {
        lf681_init(&r); token = 1; i = 0; t = 0;
        while(t < 9900000u) {
            CHECK(push_mean(&r, token++, t, square_mean(t + phase)) == LF681_PUSH_OK);
            t += steps[i++ % (sizeof(steps) / sizeof(steps[0]))];
        }
        lf681_summarize(&r, lf681_at(&r, r.count - 1u)->received_us, &s);
        CHECK(s.status == LF681_OK && s.frequency_valid);
        CHECK(s.frequency_low_hz <= 1.0 && s.frequency_high_hz >= 1.0);
        CHECK(s.frequency_hz >= s.frequency_low_hz && s.frequency_hz <= s.frequency_high_hz);
        CHECK(s.max_gap_us == 93000u);
    }
    /* Explicit rising-edge schedules, with a short high plateau after each
     * edge and 50 ms snapshots everywhere: there is no acquisition gap to
     * explain away the alternating/missing/irregular periods. */
    for(j = 0; j < sizeof(negative) / sizeof(negative[0]); ++j) {
        lf681_init(&r); token = 1;
        for(t = 0; t <= 9900000u; t += 50000u) {
            double mean = 8000.0;
            for(i = 0; i < negative_sizes[j]; ++i)
                if(t >= negative[j][i] && t - negative[j][i] < 100000u)
                    mean = 10000.0;
            CHECK(push_mean(&r, token++, t, mean) == LF681_PUSH_OK);
        }
        lf681_summarize(&r, 9900000u, &s);
        CHECK(s.status == LF681_IRREGULAR && !s.frequency_valid);
        CHECK(s.rising_edges >= 3u && s.max_gap_us == 50000u);
        CHECK(isnan(s.frequency_hz) && isnan(s.frequency_low_hz) && isnan(s.frequency_high_hz));
    }
    CHECK(strcmp(lf681_reason(LF681_IRREGULAR), "IRREGULAR EDGE PERIODS") == 0);
    return 0;
}

int main(void) {
    if(test_irregular_1hz() || test_rejections() || test_gaps_and_window() ||
       test_ring_wrap() || test_period_consistency()) return 1;
    printf("LOWFREQ681_PASS checks=%u\n", checks);
    return 0;
}
