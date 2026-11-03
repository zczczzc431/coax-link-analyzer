#include "scope_stats683.h"
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "SCOPE_STATS683_FAIL line=%d\n", __LINE__); exit(1); \
} } while (0)

static int near(double a, double b, double tolerance) {
    return isfinite(a) && isfinite(b) && fabs(a - b) <= tolerance;
}

static void empty(const struct ss683_state *s, const char *reason) {
    struct ss683_summary q;
    ss683_summarize(s, &q);
    CHECK(q.n == 0u && !q.valid);
    CHECK(isnan(q.mean_hz) && isnan(q.sd_hz) && isnan(q.min_hz));
    CHECK(isnan(q.max_hz) && isnan(q.pp_hz));
    CHECK(q.span_us == 0 && q.max_gap_us == 0);
    CHECK(strcmp(q.reason, reason) == 0);
}

static void seed(struct ss683_state *s) {
    ss683_reset(s, "SEED");
    CHECK(ss683_push(s, 1, 1000, 1000000.0, 1) == 1);
    CHECK(ss683_push(s, 2, 2000, 1000001.0, 1) == 1);
}

static void test_null_empty_single(void) {
    struct ss683_state s = {0};
    struct ss683_summary q;
    ss683_reset(NULL, "NULL");
    CHECK(ss683_push(NULL, 1, 1, 1.0, 1) == -1);
    ss683_summarize(NULL, NULL);
    ss683_summarize(&s, NULL);
    empty(NULL, "NULL_STATE");
    empty(&s, "RESET");
    ss683_reset(&s, NULL);
    empty(&s, "RESET");
    ss683_reset(&s, "START");
    CHECK(ss683_push(&s, 7, 9, 1000000.0, 1) == 1);
    ss683_summarize(&s, &q);
    CHECK(q.n == 1u && !q.valid && isnan(q.sd_hz) && isnan(q.pp_hz));
    CHECK(q.mean_hz == 1000000.0 && q.min_hz == q.mean_hz && q.max_hz == q.mean_hz);
    CHECK(q.span_us == 0 && q.max_gap_us == 0 && strcmp(q.reason, "START") == 0);
    ss683_reset(&s, "OFF");
    empty(&s, "OFF");
}

static void test_constant_rolling(void) {
    struct ss683_state s;
    struct ss683_summary q;
    ss683_reset(&s, "CONSTANT");
    for (uint32_t i = 1; i <= SS683_CAP; ++i)
        CHECK(ss683_push(&s, i, UINT64_C(1000) + i * UINT64_C(100), 1000000.0, 1) == 1);
    ss683_summarize(&s, &q);
    CHECK(q.n == SS683_CAP && q.valid && q.mean_hz == 1000000.0);
    CHECK(q.sd_hz == 0.0 && q.pp_hz == 0.0 && q.min_hz == q.max_hz);
    CHECK(q.span_us == 1500 && q.max_gap_us == 100);
    CHECK(ss683_push(&s, 17, 2700, 1000000.0, 1) == 1);
    ss683_summarize(&s, &q);
    CHECK(q.n == SS683_CAP && q.valid && q.span_us == 1500 && q.max_gap_us == 100);

    ss683_reset(&s, "ROLLING");
    for (uint32_t i = 1; i <= 20; ++i)
        CHECK(ss683_push(&s, i, i * UINT64_C(1000), (double)i, 1) == 1);
    ss683_summarize(&s, &q);
    CHECK(q.n == SS683_CAP && q.valid);
    CHECK(q.min_hz == 5.0 && q.max_hz == 20.0 && q.pp_hz == 15.0);
    CHECK(q.mean_hz == 12.5 && near(q.sd_hz, sqrt(21.25), 1e-12));
    CHECK(q.span_us == 15000 && q.max_gap_us == 1000);

    /* The large gap entering an evicted point must leave the summary too. */
    ss683_reset(&s, "GAP_EVICTION");
    CHECK(ss683_push(&s, 1, 100, 1.0, 1) == 1);
    CHECK(ss683_push(&s, 2, 200, 2.0, 1) == 1);
    CHECK(ss683_push(&s, 3, 500, 3.0, 1) == 1);
    for (uint32_t i = 4; i <= 18; ++i)
        CHECK(ss683_push(&s, i, UINT64_C(500) + (i - 3u) * UINT64_C(10), (double)i, 1) == 1);
    ss683_summarize(&s, &q);
    CHECK(q.n == SS683_CAP && q.min_hz == 3.0 && q.max_hz == 18.0);
    CHECK(q.span_us == 150 && q.max_gap_us == 10);
}

static void test_variance_and_no_filter(void) {
    struct ss683_state s;
    struct ss683_summary q;
    ss683_reset(&s, "VARIANCE");
    for (uint32_t i = 1; i <= 4; ++i)
        CHECK(ss683_push(&s, i, i * UINT64_C(1000), (double)i, 1) == 1);
    ss683_summarize(&s, &q);
    CHECK(q.valid && q.mean_hz == 2.5 && near(q.sd_hz, sqrt(1.25), 1e-12));
    CHECK(q.pp_hz == 3.0);

    ss683_reset(&s, "SMALL_VARIANCE");
    const double low = 1000000.0 - 0.0001, high = 1000000.0 + 0.0001;
    for (uint32_t i = 1; i <= SS683_CAP; ++i)
        CHECK(ss683_push(&s, i, i * UINT64_C(1000), (i & 1u) ? low : high, 1) == 1);
    ss683_summarize(&s, &q);
    CHECK(q.valid && near(q.mean_hz, 1000000.0, 1e-9));
    CHECK(near(q.sd_hz, (high - low) / 2.0, 1e-12));
    CHECK(near(q.sd_hz, 0.0001, 1e-9) && q.pp_hz == high - low);

    ss683_reset(&s, "NO_OUTLIER_FILTER");
    CHECK(ss683_push(&s, 1, 1, 1000000.0, 1) == 1);
    CHECK(ss683_push(&s, 2, 2, 1000000.0, 1) == 1);
    CHECK(ss683_push(&s, 3, 3, 2000000.0, 1) == 1);
    ss683_summarize(&s, &q);
    CHECK(q.n == 3u && q.valid && q.pp_hz == 1000000.0);
    CHECK(near(q.mean_hz, 4000000.0 / 3.0, 1e-9));

    /* Positive finite observations must not overflow their squared spread. */
    ss683_reset(&s, "FINITE_EXTREMES");
    CHECK(ss683_push(&s, 1, 1, DBL_MAX, 1) == 1);
    CHECK(ss683_push(&s, 2, 2, DBL_MIN, 1) == 1);
    ss683_summarize(&s, &q);
    CHECK(q.valid && isfinite(q.mean_hz) && isfinite(q.sd_hz) && isfinite(q.pp_hz));
    CHECK(near(q.mean_hz / DBL_MAX, 0.5, 1e-15));
    CHECK(near(q.sd_hz / DBL_MAX, 0.5, 1e-15));
    ss683_reset(&s, "TINY_CONSTANT");
    CHECK(ss683_push(&s, 1, 1, DBL_MIN, 1) == 1);
    CHECK(ss683_push(&s, 2, 2, DBL_MIN, 1) == 1);
    ss683_summarize(&s, &q);
    CHECK(q.valid && q.mean_hz == DBL_MIN && q.sd_hz == 0.0 && q.pp_hz == 0.0);
}

static void test_duplicate_and_invalid(void) {
    struct ss683_state s;
    struct ss683_summary q;
    unsigned char saved[sizeof s];
    seed(&s);
    memcpy(saved, &s, sizeof s);
    CHECK(ss683_push(&s, 2, 9999, 9000000.0, 1) == 0);
    CHECK(memcmp(saved, &s, sizeof s) == 0);
    CHECK(ss683_push(&s, 2, 1, 3000000.0, 1) == 0);
    CHECK(memcmp(saved, &s, sizeof s) == 0);
    ss683_summarize(&s, &q);
    CHECK(memcmp(saved, &s, sizeof s) == 0); /* Summary is read-only. */
    CHECK(q.n == 2u && q.span_us == 1000 && q.max_gap_us == 1000);

    struct bad_input {
        uint32_t token;
        uint64_t time;
        double hz;
        int valid;
        const char *reason;
    } bad[] = {
        {3, 3000, 1.0, 0, "INVALID_FRAME"},
        {3, 3000, NAN, 1, "NONFINITE"},
        {3, 3000, INFINITY, 1, "NONFINITE"},
        {3, 3000, -INFINITY, 1, "NONFINITE"},
        {3, 3000, 0.0, 1, "NONPOSITIVE"},
        {3, 3000, -1.0, 1, "NONPOSITIVE"},
        {0, 3000, 1.0, 1, "TOKEN_ZERO"},
        {3, 0, 1.0, 1, "TIME_ZERO"},
        {2, 3000, NAN, 1, "NONFINITE"}, /* Invalid beats duplicate. */
        {2, 0, 1.0, 1, "TIME_ZERO"}
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        seed(&s);
        CHECK(ss683_push(&s, bad[i].token, bad[i].time, bad[i].hz, bad[i].valid) == -1);
        empty(&s, bad[i].reason);
        CHECK(ss683_push(&s, 1, 1, 1.0, 1) == 1); /* Reset has no hidden prior order. */
    }
}

static void test_order_wrap_and_gaps(void) {
    struct ss683_state s;
    struct ss683_summary q;
    seed(&s);
    CHECK(ss683_push(&s, 3, 1999, 1.0, 1) == -1);
    empty(&s, "TIME_ORDER");
    seed(&s);
    CHECK(ss683_push(&s, 3, 2000, 1.0, 1) == -1);
    empty(&s, "TIME_ORDER");
    seed(&s);
    CHECK(ss683_push(&s, 1, 3000, 1.0, 1) == -1);
    empty(&s, "TOKEN_ORDER");
    seed(&s);
    CHECK(ss683_push(&s, UINT32_C(2) + UINT32_C(0x80000000), 3000, 1.0, 1) == -1);
    empty(&s, "TOKEN_ORDER");
    seed(&s);
    CHECK(ss683_push(&s, UINT32_C(2) + UINT32_C(0x7fffffff), 3000, 1.0, 1) == 1);

    ss683_reset(&s, "WRAP");
    CHECK(ss683_push(&s, UINT32_MAX - 1u, 1, 1.0, 1) == 1);
    CHECK(ss683_push(&s, UINT32_MAX, 2, 1.0, 1) == 1);
    CHECK(ss683_push(&s, 1, 3, 1.0, 1) == 1);
    CHECK(ss683_push(&s, 2, 4, 1.0, 1) == 1);
    ss683_summarize(&s, &q);
    CHECK(q.n == 4u && q.valid && q.span_us == 3 && q.max_gap_us == 1);
    CHECK(ss683_push(&s, 1, 5, 1.0, 1) == -1);
    empty(&s, "TOKEN_ORDER");

    ss683_reset(&s, "BOUNDARY");
    CHECK(ss683_push(&s, 1, 1, 1.0, 1) == 1);
    CHECK(ss683_push(&s, 2, 1000001, 2.0, 1) == 1);
    ss683_summarize(&s, &q);
    CHECK(q.n == 2u && q.span_us == SS683_MAX_GAP_US && q.max_gap_us == SS683_MAX_GAP_US);
    CHECK(strcmp(q.reason, "BOUNDARY") == 0);
    CHECK(ss683_push(&s, 3, 2000002, 3.0, 1) == 1);
    ss683_summarize(&s, &q);
    CHECK(q.n == 1u && !q.valid && q.mean_hz == 3.0 && isnan(q.sd_hz) && isnan(q.pp_hz));
    CHECK(q.span_us == 0 && q.max_gap_us == 0 && strcmp(q.reason, "GAP") == 0);
    CHECK(ss683_push(&s, 4, 2000003, 4.0, 1) == 1);
    ss683_summarize(&s, &q);
    CHECK(q.n == 2u && q.valid && q.span_us == 1 && q.max_gap_us == 1);
    CHECK(strcmp(q.reason, "GAP") == 0);

    seed(&s);
    CHECK(ss683_push(&s, 1, 2000002, 1.0, 1) == -1); /* Gap cannot excuse reverse token. */
    empty(&s, "TOKEN_ORDER");
    ss683_reset(&s, "TIME_MAX");
    CHECK(ss683_push(&s, 1, UINT64_MAX - UINT64_C(2), 1.0, 1) == 1);
    CHECK(ss683_push(&s, 2, UINT64_MAX - UINT64_C(1), 1.0, 1) == 1);
    CHECK(ss683_push(&s, 3, UINT64_MAX, 1.0, 1) == 1);
    ss683_summarize(&s, &q);
    CHECK(q.valid && q.span_us == 2 && q.max_gap_us == 1);
    CHECK(ss683_push(&s, 4, 1, 1.0, 1) == -1);
    empty(&s, "TIME_ORDER");
}

int main(void) {
    CHECK(SS683_CAP == 16u);
    test_null_empty_single();
    test_constant_rolling();
    test_variance_and_no_filter();
    test_duplicate_and_invalid();
    test_order_wrap_and_gaps();
    printf("SCOPE_STATS683_UNIT_PASS checks=%u POPULATION_SD OBSERVATIONS_ONLY NO_MMIO\n", checks);
    return 0;
}
