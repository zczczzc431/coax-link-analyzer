#include "spectrum_mask612.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Independent public-API tests. No FFT, application, hardware or file I/O. */
static unsigned checks;
static unsigned failures;

#define CHECK(expr) do { \
    ++checks; \
    if (!(expr)) { \
        ++failures; \
        fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #expr); \
    } \
} while (0)

struct fixture {
    struct sm612_model model;
    struct sm612_config config;
    struct sm612_identity identity;
    struct sm612_frame frame;
    struct sm612_decision decision;
    double code[SM612_MAX_BINS];
};

static int near(double actual, double expected)
{
    return isfinite(actual) && fabs(actual - expected) < 1e-8;
}

static double amplitude(double db)
{
    return 8191.0 * pow(10.0, db / 20.0);
}

static void setup(struct fixture *f, unsigned n)
{
    memset(f, 0, sizeof(*f));
    sm612_init(&f->model);
    sm612_defaults(&f->config);
    memset(f->config.exclude, 0, sizeof(f->config.exclude));
    f->config.first_bin = 3u;
    f->config.last_bin = 3u;
    f->config.learn_frames = 2u;
    f->config.enter_frames = 2u;
    f->config.recover_frames = 2u;
    f->config.upper_margin_db = 6.0;
    f->config.lower_margin_db = 6.0;
    f->config.floor_db = -120.0;
    f->config.lower_signal_margin_db = 6.0;
    f->config.min_learn_peak_db = -80.0;
    f->config.max_learn_spread_db = 120.0;
    f->config.hysteresis_db = 2.0;
    f->config.max_gap_us = 1000u;
    f->config.upper_enabled = 1;
    f->config.lower_enabled = 1;
    f->identity.session = UINT64_C(0x612);
    f->identity.input_signature = UINT64_C(0x1234);
    f->identity.source_signature = UINT64_C(0x5678);
    f->identity.fs_hz = 16000000.0;
    f->identity.fft_size = n;
    f->identity.format = SM612_FORMAT;
    f->frame.identity = f->identity;
    f->frame.code = f->code;
    f->frame.bins = n / 2u + 1u;
    f->frame.valid = 1;
}

static void level(struct fixture *f, double db)
{
    memset(f->code, 0, sizeof(f->code));
    f->code[3] = amplitude(db);
}

static int push(struct fixture *f, uint32_t token, uint64_t us)
{
    f->frame.token = token;
    f->frame.timestamp_us = us;
    memset(&f->decision, 0xa5, sizeof(f->decision));
    return sm612_push(&f->model, &f->frame, &f->decision);
}

static void begin(struct fixture *f)
{
    CHECK(sm612_begin_learn(&f->model, &f->config, &f->identity) == 0);
    CHECK(f->model.state == SM612_LEARNING);
    CHECK(!f->model.template_valid);
}

static void learn(struct fixture *f, double db)
{
    begin(f);
    level(f, db);
    CHECK(push(f, 1u, 100u) == 0);
    CHECK(f->model.state == SM612_LEARNING);
    CHECK(f->model.learned == 1u);
    CHECK(push(f, 2u, 200u) == 0);
    CHECK(f->model.state == SM612_READY);
    CHECK(f->model.template_valid);
    CHECK((f->decision.flags & SM612_LEARNED) != 0u);
}

static void arm(struct fixture *f)
{
    CHECK(sm612_arm(&f->model, &f->identity) == 0);
    CHECK(f->model.state == SM612_ARMED);
    CHECK(f->model.event_id == 0u);
}

static void alarm2(struct fixture *f)
{
    level(f, 10.0);
    CHECK(push(f, 3u, 300u) == 0);
    CHECK(f->model.state == SM612_CANDIDATE);
    CHECK(push(f, 4u, 400u) == 0);
    CHECK(f->model.state == SM612_ALARM);
    CHECK(f->decision.event_id != 0u);
}

static void test_learning_power_and_idle(void)
{
    struct fixture f;
    double expected = 10.0 * log10((0.1 * 0.1 + 0.2 * 0.2) / 2.0);
    double wrong_db_mean = (-20.0 + 20.0 * log10(0.2)) / 2.0;
    setup(&f, 16u);
    CHECK(f.model.state == SM612_NO_TEMPLATE);
    CHECK(sm612_arm(&f.model, &f.identity) == -1);
    begin(&f);
    level(&f, -20.0);
    CHECK(push(&f, 1u, 100u) == 0);
    f.code[3] = 1638.2;
    CHECK(push(&f, 2u, 200u) == 0);
    CHECK(f.model.state == SM612_READY);
    CHECK(near(f.model.baseline_db[3], expected));
    CHECK(fabs(f.model.baseline_db[3] - wrong_db_mean) > 0.9);
    CHECK(near(f.model.upper_db[3], expected + 6.0));
    CHECK(near(f.model.lower_db[3], expected - 6.0));
    CHECK(f.model.lower_valid[3] != 0u);
    level(&f, 20.0);
    CHECK(push(&f, 3u, 300u) == 0);
    CHECK(f.model.state == SM612_READY);
    CHECK((f.decision.flags & (SM612_FIRST | SM612_CONFIRMED)) == 0u);
    arm(&f);
}

static void test_strict_equal_and_single_frame(void)
{
    struct fixture f;
    uint64_t first_event;
    setup(&f, 16u);
    f.config.upper_margin_db = 0.0;
    f.config.lower_margin_db = 0.0;
    f.config.hysteresis_db = 0.0;
    f.config.enter_frames = 1u;
    f.config.recover_frames = 1u;
    learn(&f, 0.0);
    arm(&f);
    CHECK(near(f.model.upper_db[3], 0.0));
    CHECK(near(f.model.lower_db[3], 0.0));
    CHECK(push(&f, 3u, 300u) == 0);
    CHECK(f.model.state == SM612_ARMED);
    CHECK(f.decision.current_direction == 0);
    CHECK((f.decision.flags & SM612_FIRST) == 0u);
    f.code[3] = 8191.0 * (1.0 + 1e-6);
    CHECK(push(&f, 4u, 400u) == 0);
    CHECK(f.model.state == SM612_ALARM);
    CHECK((f.decision.flags & (SM612_FIRST | SM612_WORST | SM612_CONFIRMED)) ==
          (SM612_FIRST | SM612_WORST | SM612_CONFIRMED));
    CHECK(f.decision.current_direction == 1);
    CHECK(f.decision.first_token == 4u && f.decision.worst_token == 4u);
    first_event = f.decision.event_id;
    CHECK(first_event != 0u);
    f.code[3] = 8191.0;
    CHECK(push(&f, 5u, 500u) == 0);
    CHECK((f.decision.flags & SM612_RECOVERED) != 0u);
    CHECK(f.decision.ended_event_id == first_event);
    CHECK(f.decision.first_token == 4u && f.decision.worst_token == 4u);
    CHECK(f.model.state == SM612_ARMED && f.model.event_id == 0u);
    f.code[3] = 8191.0 * (1.0 - 1e-6);
    CHECK(push(&f, 6u, 600u) == 0);
    CHECK(f.model.state == SM612_ALARM);
    CHECK(f.decision.current_direction == -1);
    CHECK(f.decision.event_id > first_event);
}

static void test_first_worst_and_hysteresis(void)
{
    struct fixture f;
    uint64_t event;
    setup(&f, 16u);
    learn(&f, 0.0);
    arm(&f);
    level(&f, 8.0);
    CHECK(push(&f, 3u, 300u) == 0);
    CHECK(f.decision.state == SM612_CANDIDATE);
    CHECK((f.decision.flags & (SM612_FIRST | SM612_WORST)) == (SM612_FIRST | SM612_WORST));
    CHECK((f.decision.flags & SM612_CONFIRMED) == 0u);
    CHECK(f.decision.event_id == 0u);
    CHECK(f.decision.first_token == 3u && f.decision.first_us == 300u);
    CHECK(f.decision.worst_token == 3u && f.decision.worst_us == 300u);
    CHECK(f.decision.current_bin == 3u && f.decision.current_direction == 1);
    CHECK(near(f.decision.current_excess_db, 2.0));
    level(&f, 10.0);
    CHECK(push(&f, 4u, 400u) == 0);
    CHECK(f.decision.state == SM612_ALARM);
    CHECK((f.decision.flags & (SM612_CONFIRMED | SM612_WORST)) == (SM612_CONFIRMED | SM612_WORST));
    CHECK((f.decision.flags & SM612_FIRST) == 0u);
    event = f.decision.event_id;
    CHECK(event != 0u && f.decision.first_token == 3u);
    CHECK(f.decision.worst_token == 4u && near(f.decision.worst_excess_db, 4.0));
    CHECK(push(&f, 5u, 500u) == 0);
    CHECK((f.decision.flags & SM612_WORST) == 0u);
    CHECK(f.decision.worst_token == 4u);
    level(&f, 8.0);
    CHECK(push(&f, 6u, 600u) == 0);
    CHECK(f.decision.current_direction == 1 && near(f.decision.current_excess_db, 2.0));
    CHECK(f.decision.worst_token == 4u && near(f.decision.worst_excess_db, 4.0));
    level(&f, -12.0);
    CHECK(push(&f, 7u, 700u) == 0);
    CHECK((f.decision.flags & SM612_WORST) != 0u);
    CHECK(f.decision.worst_token == 7u && f.decision.worst_direction == -1);
    CHECK(near(f.decision.worst_excess_db, 6.0));
    level(&f, 5.0);
    CHECK(push(&f, 8u, 800u) == 0);
    CHECK(f.model.state == SM612_ALARM && f.model.recover_count == 0u);
    CHECK((f.decision.flags & SM612_RECOVERED) == 0u);
    level(&f, 0.0);
    CHECK(push(&f, 9u, 900u) == 0);
    CHECK(f.model.state == SM612_ALARM && f.model.recover_count == 1u);
    level(&f, -5.0);
    CHECK(push(&f, 10u, 1000u) == 0);
    CHECK(f.model.state == SM612_ALARM && f.model.recover_count == 0u);
    level(&f, 0.0);
    CHECK(push(&f, 11u, 1100u) == 0);
    CHECK(push(&f, 12u, 1200u) == 0);
    CHECK((f.decision.flags & SM612_RECOVERED) != 0u);
    CHECK(f.decision.ended_event_id == event);
    CHECK(f.decision.first_token == 3u && f.decision.worst_token == 7u);
    CHECK(f.decision.worst_direction == -1 && near(f.decision.worst_excess_db, 6.0));
    CHECK(f.model.state == SM612_ARMED && f.model.event_id == 0u);
}

static void test_candidate_cancel_and_inclusive_recovery(void)
{
    struct fixture f;
    setup(&f, 16u);
    f.config.hysteresis_db = 6.0; /* Both inclusive recovery boundaries are 0 dB. */
    learn(&f, 0.0);
    arm(&f);
    level(&f, 10.0);
    CHECK(push(&f, 3u, 300u) == 0);
    level(&f, 5.0); /* Entry-safe but outside the recovery interval. */
    CHECK(push(&f, 4u, 400u) == 0);
    CHECK((f.decision.flags & SM612_UNCONFIRMED) != 0u);
    CHECK(f.model.state == SM612_ARMED && f.model.event_id == 0u);
    level(&f, -10.0);
    CHECK(push(&f, 5u, 500u) == 0);
    CHECK((f.decision.flags & SM612_FIRST) != 0u);
    CHECK(f.decision.first_token == 5u);
    CHECK(push(&f, 6u, 600u) == 0);
    CHECK(f.model.state == SM612_ALARM);
    level(&f, 0.0);
    CHECK(push(&f, 7u, 700u) == 0);
    CHECK(f.model.state == SM612_ALARM && f.model.recover_count == 1u);
    CHECK(push(&f, 8u, 800u) == 0);
    CHECK((f.decision.flags & SM612_RECOVERED) != 0u);
    CHECK(f.model.state == SM612_ARMED);
}

static void test_loss_direction_and_protected_bins(void)
{
    struct fixture f;
    setup(&f, 16u);
    f.config.enter_frames = 1u;
    learn(&f, 0.0);
    arm(&f);
    memset(f.code, 0, sizeof(f.code)); /* valid ADC frame, physical signal vanished. */
    CHECK(push(&f, 3u, 300u) == 0);
    CHECK(f.model.state == SM612_ALARM && f.decision.current_direction == -1);
    CHECK(near(f.decision.current_excess_db, 294.0));
    CHECK((f.decision.flags & SM612_CONFIRMED) != 0u);

    setup(&f, 16u);
    f.config.first_bin = 1u;
    f.config.last_bin = 7u;
    f.config.exclude[4] = 1u;
    learn(&f, 0.0);
    arm(&f);
    f.code[0] = amplitude(50.0);
    f.code[4] = amplitude(50.0);
    f.code[8] = amplitude(50.0);
    CHECK(push(&f, 3u, 300u) == 0);
    CHECK(f.model.state == SM612_ARMED && f.decision.current_direction == 0);
    CHECK(f.model.lower_valid[1] == 0u && f.model.lower_valid[2] == 0u);
    CHECK(f.model.lower_valid[3] != 0u);
    CHECK(f.model.lower_valid[4] == 0u);
    CHECK(f.model.lower_valid[5] == 0u && f.model.lower_valid[7] == 0u);

    setup(&f, 16u);
    f.config.enter_frames = 1u;
    f.config.lower_enabled = 0;
    learn(&f, 0.0);
    arm(&f);
    memset(f.code, 0, sizeof(f.code));
    CHECK(push(&f, 3u, 300u) == 0);
    CHECK(f.model.state == SM612_ARMED);
    level(&f, 10.0);
    CHECK(push(&f, 4u, 400u) == 0);
    CHECK(f.model.state == SM612_ALARM && f.decision.current_direction == 1);

    setup(&f, 16u);
    f.config.enter_frames = 1u;
    f.config.upper_enabled = 0;
    learn(&f, 0.0);
    arm(&f);
    level(&f, 10.0);
    CHECK(push(&f, 3u, 300u) == 0);
    CHECK(f.model.state == SM612_ARMED);
    level(&f, -10.0);
    CHECK(push(&f, 4u, 400u) == 0);
    CHECK(f.model.state == SM612_ALARM && f.decision.current_direction == -1);
}

static void test_lower_gate(void)
{
    struct fixture f;
    setup(&f, 16u);
    f.config.floor_db = -60.0;
    f.config.lower_margin_db = 15.0;
    learn(&f, -50.0);
    CHECK(f.model.lower_valid[3] == 0u); /* Lower bound -65 is below floor. */
    arm(&f);
    memset(f.code, 0, sizeof(f.code));
    CHECK(push(&f, 3u, 300u) == 0);
    CHECK(f.model.state == SM612_ARMED);

    setup(&f, 16u);
    f.config.floor_db = -6.0;
    f.config.lower_signal_margin_db = 6.0;
    f.config.enter_frames = 1u;
    learn(&f, 0.0);
    CHECK(f.model.lower_valid[3] != 0u); /* Both lower gating comparisons equal. */
    arm(&f);
    memset(f.code, 0, sizeof(f.code));
    CHECK(push(&f, 3u, 300u) == 0);
    CHECK(f.model.state == SM612_ALARM && f.decision.current_direction == -1);
}

static void test_invalid_quality_and_numeric(void)
{
    struct fixture f;
    unsigned mode;
    unsigned i;
    static const unsigned positions[] = {0u, 4u, 8u};
    const double bad[] = {NAN, INFINITY, -INFINITY, -1.0, DBL_MAX};
    for (mode = 0u; mode < 2u; ++mode) {
        uint64_t event;
        setup(&f, 16u);
        learn(&f, 0.0);
        arm(&f);
        alarm2(&f);
        event = f.decision.event_id;
        level(&f, 0.0);
        f.frame.valid = mode != 0u;
        f.frame.overload = mode != 0u;
        CHECK(push(&f, 5u, 500u) == -1);
        CHECK((f.decision.flags & (SM612_REJECTED | SM612_INTERRUPTED)) ==
              (SM612_REJECTED | SM612_INTERRUPTED));
        CHECK(f.decision.ended_event_id == event);
        CHECK(f.decision.reason == (mode == 0u ? SM612_BAD_FRAME : SM612_OVERLOAD));
        CHECK((f.decision.flags & SM612_RECOVERED) == 0u);
        CHECK(f.model.last_token == 5u && f.model.last_us == 500u);
        CHECK(f.model.event_id == 0u && f.model.template_valid);
        f.frame.valid = 1;
        f.frame.overload = 0;
        level(&f, 10.0);
        CHECK(push(&f, 6u, 600u) == 0);
        CHECK(f.model.state == SM612_CANDIDATE && f.decision.first_token == 6u);
        CHECK(push(&f, 7u, 700u) == 0);
        CHECK(f.model.state == SM612_ALARM && f.decision.event_id > event);
    }
    for (mode = 0u; mode < sizeof(bad) / sizeof(bad[0]); ++mode) {
        for (i = 0u; i < sizeof(positions) / sizeof(positions[0]); ++i) {
            setup(&f, 16u);
            f.config.exclude[4] = 1u;
            learn(&f, 0.0);
            arm(&f);
            f.code[positions[i]] = bad[mode];
            CHECK(push(&f, 3u, 300u) == -1);
            CHECK((f.decision.flags & SM612_REJECTED) != 0u);
            CHECK((f.decision.flags & (SM612_CONFIRMED | SM612_RECOVERED)) == 0u);
            CHECK(f.model.event_id == 0u);
            CHECK(f.model.last_token == 3u && f.model.last_us == 300u);
        }
    }
    for (mode = 0u; mode < 2u; ++mode) {
        setup(&f, 16u);
        learn(&f, 0.0);
        arm(&f);
        if (mode == 0u) f.frame.bins = 8u;
        else f.frame.code = NULL;
        CHECK(push(&f, 3u, 300u) == -1);
        CHECK((f.decision.flags & SM612_REJECTED) != 0u);
        CHECK(f.model.event_id == 0u);
    }
}

static void test_duplicate_stale_and_time(void)
{
    struct fixture f;
    unsigned mode;
    for (mode = 0u; mode < 4u; ++mode) {
        uint32_t rejected_token;
        uint64_t rejected_time;
        setup(&f, 16u);
        learn(&f, 0.0);
        arm(&f);
        level(&f, 10.0);
        CHECK(push(&f, 3u, 300u) == 0);
        rejected_token = mode == 0u ? 3u : mode == 1u ? 2u :
                         mode == 2u ? 4u : UINT32_C(0x80000003);
        rejected_time = mode == 2u ? 299u : 400u;
        CHECK(push(&f, rejected_token, rejected_time) == -1);
        CHECK((f.decision.flags & (SM612_REJECTED | SM612_GAP)) == (SM612_REJECTED | SM612_GAP));
        CHECK(f.decision.reason == (mode == 0u ? SM612_DUPLICATE : SM612_STALE_FRAME));
        CHECK(f.model.last_token == 3u && f.model.last_us == 300u);
        CHECK(f.model.event_id == 0u);
        CHECK(push(&f, 4u, 500u) == 0);
        CHECK(f.model.state == SM612_CANDIDATE && f.decision.first_token == 4u);
        CHECK((f.decision.flags & SM612_CONFIRMED) == 0u);
        CHECK(push(&f, 5u, 600u) == 0);
        CHECK(f.model.state == SM612_ALARM);
        {
            uint64_t event = f.decision.event_id;
            level(&f, 0.0);
            CHECK(push(&f, 5u, 700u) == -1);
            CHECK((f.decision.flags & (SM612_REJECTED | SM612_GAP | SM612_INTERRUPTED)) ==
                  (SM612_REJECTED | SM612_GAP | SM612_INTERRUPTED));
            CHECK(f.decision.ended_event_id == event);
            CHECK((f.decision.flags & SM612_RECOVERED) == 0u);
            CHECK(f.model.last_token == 5u && f.model.last_us == 600u);
        }
    }
}

static void test_gap_and_token_wrap(void)
{
    struct fixture f;
    uint64_t event;
    setup(&f, 16u);
    learn(&f, 0.0);
    arm(&f);
    level(&f, 10.0);
    CHECK(push(&f, 3u, 300u) == 0);
    CHECK(push(&f, 5u, 500u) == 0);
    CHECK((f.decision.flags & (SM612_GAP | SM612_FIRST | SM612_WORST)) ==
          (SM612_GAP | SM612_FIRST | SM612_WORST));
    CHECK(f.decision.first_token == 5u && f.decision.event_id == 0u);
    CHECK((f.decision.flags & SM612_CONFIRMED) == 0u);
    CHECK(push(&f, 6u, 600u) == 0);
    event = f.decision.event_id;
    CHECK(event != 0u);
    CHECK(push(&f, 7u, 1601u) == 0);
    CHECK((f.decision.flags & (SM612_GAP | SM612_INTERRUPTED | SM612_FIRST)) ==
          (SM612_GAP | SM612_INTERRUPTED | SM612_FIRST));
    CHECK(f.decision.ended_event_id == event);
    CHECK(f.decision.event_id == 0u && f.decision.first_token == 7u);
    CHECK(f.decision.worst_token == 7u && f.decision.first_us == 1601u);
    CHECK(push(&f, 8u, 1701u) == 0);
    CHECK(f.decision.event_id > event && f.model.state == SM612_ALARM);

    setup(&f, 16u);
    learn(&f, 0.0);
    arm(&f);
    level(&f, 10.0);
    CHECK(push(&f, 3u, 300u) == 0);
    CHECK(push(&f, 4u, 1300u) == 0);
    CHECK((f.decision.flags & SM612_GAP) == 0u); /* max_gap equality is continuous. */
    CHECK(f.model.state == SM612_ALARM);

    setup(&f, 16u);
    begin(&f);
    level(&f, 0.0);
    CHECK(push(&f, UINT32_MAX - 2u, 100u) == 0);
    CHECK(push(&f, UINT32_MAX - 1u, 200u) == 0);
    CHECK(f.model.state == SM612_READY);
    arm(&f);
    level(&f, 10.0);
    CHECK(push(&f, UINT32_MAX, 300u) == 0);
    CHECK(push(&f, 0u, 400u) == 0);
    CHECK(f.model.state == SM612_ALARM);
    CHECK((f.decision.flags & SM612_GAP) == 0u);
    CHECK(f.decision.first_token == UINT32_MAX && f.decision.frame_token == 0u);
    level(&f, 0.0);
    CHECK(push(&f, 1u, 500u) == 0);
    CHECK(push(&f, 2u, 600u) == 0);
    CHECK((f.decision.flags & SM612_RECOVERED) != 0u);
}

static void change_identity(struct sm612_identity *id, unsigned mode)
{
    switch (mode) {
    case 0u: ++id->session; break;
    case 1u: ++id->input_signature; break;
    case 2u: ++id->source_signature; break;
    case 3u: id->fs_hz *= 2.0; break;
    case 4u: id->fft_size *= 2u; break;
    default: ++id->format; break;
    }
}

static void test_identity_mismatch(void)
{
    struct fixture f;
    unsigned mode;
    for (mode = 0u; mode < 6u; ++mode) {
        struct sm612_identity altered;
        setup(&f, 16u);
        learn(&f, 0.0);
        altered = f.identity;
        CHECK(sm612_identity_equal(&f.identity, &altered));
        change_identity(&altered, mode);
        CHECK(!sm612_identity_equal(&f.identity, &altered));
        CHECK(sm612_arm(&f.model, &altered) == -1);
        CHECK(!f.model.template_valid);
        CHECK(sm612_arm(&f.model, &f.identity) == -1);

        setup(&f, 16u);
        learn(&f, 0.0);
        arm(&f);
        change_identity(&f.frame.identity, mode);
        CHECK(push(&f, 3u, 300u) == -1);
        CHECK((f.decision.flags & SM612_REJECTED) != 0u);
        CHECK(!f.model.template_valid);
        CHECK(sm612_arm(&f.model, &f.identity) == -1);
    }
    setup(&f, 16u);
    f.identity.session = 0u;
    CHECK(sm612_begin_learn(&f.model, &f.config, &f.identity) == -1);
    CHECK(!f.model.template_valid);
    setup(&f, 16u);
    f.identity.fs_hz = NAN;
    CHECK(sm612_begin_learn(&f.model, &f.config, &f.identity) == -1);
    setup(&f, 16u);
    f.identity.fs_hz = INFINITY;
    CHECK(sm612_begin_learn(&f.model, &f.config, &f.identity) == -1);
    setup(&f, 16u);
    f.identity.fs_hz = -1.0;
    CHECK(sm612_begin_learn(&f.model, &f.config, &f.identity) == -1);
    setup(&f, 16u);
    f.identity.fft_size = 12u;
    CHECK(sm612_begin_learn(&f.model, &f.config, &f.identity) == -1);
}

static void test_stop_suspend_and_busy(void)
{
    struct fixture f;
    unsigned mode;
    for (mode = 0u; mode < 2u; ++mode) {
        uint64_t event;
        setup(&f, 16u);
        learn(&f, 0.0);
        arm(&f);
        alarm2(&f);
        event = f.decision.event_id;
        sm612_stop(&f.model, (int)mode, &f.decision);
        CHECK((f.decision.flags & SM612_INTERRUPTED) != 0u);
        CHECK(f.decision.ended_event_id == event);
        CHECK(f.decision.first_token == 3u && f.decision.worst_token == 3u);
        CHECK(f.model.event_id == 0u && f.model.template_valid);
        if (mode != 0u) CHECK(f.model.state == SM612_SUSPENDED);
        level(&f, 10.0);
        CHECK(push(&f, 5u, 500u) == 0);
        CHECK(push(&f, 6u, 600u) == 0);
        CHECK(f.model.state != SM612_ALARM && f.model.state != SM612_CANDIDATE);
        CHECK((f.decision.flags & (SM612_FIRST | SM612_CONFIRMED)) == 0u);
        arm(&f);
        CHECK(push(&f, 7u, 700u) == 0);
        CHECK(f.model.state == SM612_CANDIDATE && f.decision.first_token == 7u);
        CHECK(push(&f, 8u, 800u) == 0);
        CHECK(f.model.state == SM612_ALARM && f.decision.event_id > event);
    }
    for (mode = 0u; mode < 3u; ++mode) {
        enum sm612_state state;
        uint64_t event;
        double baseline;
        setup(&f, 16u);
        learn(&f, 0.0);
        arm(&f);
        level(&f, 10.0);
        if (mode >= 1u) CHECK(push(&f, 3u, 300u) == 0);
        if (mode >= 2u) CHECK(push(&f, 4u, 400u) == 0);
        state = f.model.state;
        event = f.model.event_id;
        baseline = f.model.baseline_db[3];
        CHECK(sm612_begin_learn(&f.model, &f.config, &f.identity) == -1);
        CHECK(f.model.state == state);
        CHECK(f.model.template_valid && f.model.event_id == event);
        CHECK(f.model.baseline_db[3] == baseline);
    }
}

static void test_learning_failures_and_interruptions(void)
{
    struct fixture f;
    unsigned mode;
    for (mode = 0u; mode < 2u; ++mode) {
        setup(&f, 16u);
        f.config.min_learn_peak_db = -40.0;
        begin(&f);
        if (mode != 0u) level(&f, -60.0);
        CHECK(push(&f, 1u, 100u) == 0);
        CHECK(push(&f, 2u, 200u) == -1);
        CHECK(f.decision.reason == SM612_NO_SIGNAL);
        CHECK(!f.model.template_valid && f.model.state != SM612_READY);
        CHECK(sm612_arm(&f.model, &f.identity) == -1);
    }
    setup(&f, 16u);
    f.config.max_learn_spread_db = 3.0;
    begin(&f);
    level(&f, 0.0);
    CHECK(push(&f, 1u, 100u) == 0);
    level(&f, 8.0);
    CHECK(push(&f, 2u, 200u) == -1);
    CHECK(f.decision.reason == SM612_UNSTABLE_TEMPLATE);
    CHECK(!f.model.template_valid);

    setup(&f, 16u);
    f.config.last_bin = 4u;
    f.config.floor_db = -80.0;
    f.config.max_learn_spread_db = 1.0;
    begin(&f);
    level(&f, 0.0);
    CHECK(push(&f, 1u, 100u) == 0);
    f.code[4] = amplitude(-100.0); /* Variable noise-only bin must not veto LEARN. */
    CHECK(push(&f, 2u, 200u) == 0);
    CHECK(f.model.state == SM612_READY && f.model.template_valid);
    CHECK(f.model.lower_valid[4] == 0u);

    setup(&f, 16u);
    f.config.last_bin = 4u;
    f.config.exclude[4] = 1u;
    begin(&f);
    f.code[4] = 8191.0; /* Protected energy cannot satisfy the selected-signal gate. */
    CHECK(push(&f, 1u, 100u) == 0);
    CHECK(push(&f, 2u, 200u) == -1);
    CHECK(f.decision.reason == SM612_NO_SIGNAL && !f.model.template_valid);

    for (mode = 0u; mode < 2u; ++mode) {
        setup(&f, 16u);
        f.config.learn_frames = 3u;
        begin(&f);
        level(&f, 0.0);
        CHECK(push(&f, 1u, 100u) == 0);
        f.frame.valid = mode != 0u;
        f.frame.overload = mode != 0u;
        CHECK(push(&f, 2u, 200u) == -1);
        CHECK(f.model.learned == 0u && !f.model.template_valid);
        f.frame.valid = 1;
        f.frame.overload = 0;
        CHECK(push(&f, 3u, 300u) == 0);
        CHECK(f.model.learned == 1u);
        CHECK(push(&f, 5u, 500u) == 0);
        CHECK((f.decision.flags & SM612_GAP) != 0u);
        CHECK(f.model.learned == 1u && !f.model.template_valid);
        CHECK(push(&f, 6u, 600u) == 0);
        CHECK(f.model.learned == 2u);
        CHECK(push(&f, 7u, 700u) == 0);
        CHECK(f.model.state == SM612_READY && f.model.template_valid);
    }
    setup(&f, 16u);
    begin(&f);
    level(&f, 0.0);
    CHECK(push(&f, 1u, 100u) == 0);
    sm612_stop(&f.model, 0, &f.decision);
    CHECK(!f.model.template_valid && f.model.learned == 0u);
    CHECK(sm612_arm(&f.model, &f.identity) == -1);
}

static void test_production_fft_size(void)
{
    struct fixture f;
    setup(&f, 2048u);
    f.identity.fs_hz = 400000000.0;
    f.frame.identity = f.identity;
    f.config.first_bin = 1u;
    f.config.last_bin = 1023u;
    f.config.exclude[512] = 1u;
    f.config.enter_frames = 1u;
    learn(&f, 0.0);
    CHECK(f.model.bins == 1025u);
    arm(&f);
    f.code[512] = amplitude(50.0);
    f.code[1024] = amplitude(50.0);
    CHECK(push(&f, 3u, 300u) == 0);
    CHECK(f.model.state == SM612_ARMED);
    f.code[1023] = amplitude(-30.0); /* A new in-band spur on an originally quiet bin. */
    CHECK(push(&f, 4u, 400u) == 0);
    CHECK(f.model.state == SM612_ALARM);
    CHECK(f.decision.current_bin == 1023u && f.decision.current_direction == 1);
    CHECK(f.decision.first_token == 4u && f.decision.worst_bin == 1023u);
    CHECK(f.decision.event_id != 0u);
}

static void test_alias_relearn_and_arm_watermark(void)
{
    struct fixture f;
    struct sm612_config saved_config;
    struct sm612_identity saved_identity;
    setup(&f, 16u);
    f.config.exclude[4] = 1u;
    learn(&f, 0.0);
    arm(&f);
    level(&f, 10.0);
    CHECK(push(&f, 2u, 300u) == -1); /* ARM cannot reuse LEARN's final frame. */
    CHECK((f.decision.flags & (SM612_REJECTED | SM612_GAP)) == (SM612_REJECTED | SM612_GAP));
    CHECK(f.decision.reason == SM612_DUPLICATE);
    CHECK(f.model.last_token == 2u && f.model.last_us == 200u);
    CHECK(f.model.template_valid && f.model.event_id == 0u);
    CHECK(push(&f, 3u, 400u) == 0);
    CHECK(f.model.state == SM612_CANDIDATE && f.decision.first_token == 3u);
    CHECK((f.decision.flags & SM612_CONFIRMED) == 0u);
    CHECK(push(&f, 4u, 500u) == 0);
    CHECK(f.model.state == SM612_ALARM);
    sm612_stop(&f.model, 0, &f.decision);
    saved_config = f.model.config;
    saved_identity = f.model.identity;
    CHECK(sm612_begin_learn(&f.model, &f.model.config, &f.model.identity) == 0);
    CHECK(f.model.state == SM612_LEARNING && !f.model.template_valid);
    CHECK(memcmp(&f.model.config, &saved_config, sizeof(saved_config)) == 0);
    CHECK(sm612_identity_equal(&f.model.identity, &saved_identity));
    CHECK(f.model.bins == 9u);
    level(&f, 0.0);
    CHECK(push(&f, 5u, 600u) == 0);
    CHECK(push(&f, 6u, 700u) == 0);
    CHECK(f.model.state == SM612_READY && f.model.template_valid);
    CHECK(near(f.model.baseline_db[3], 0.0));
    CHECK(near(f.model.upper_db[3], 6.0) && near(f.model.lower_db[3], -6.0));
    CHECK(f.model.config.exclude[4] == 1u);
    arm(&f);
}

static void test_event_counter_limit(void)
{
    struct fixture f;
    setup(&f, 16u);
    f.config.enter_frames = 1u;
    learn(&f, 0.0);
    arm(&f);
    /* Fault injection avoids an impractical UINT64_MAX-event test run. */
    f.model.next_event = UINT64_MAX;
    level(&f, 10.0);
    CHECK(push(&f, 3u, 300u) == -1);
    CHECK(f.decision.reason == SM612_COUNTER_LIMIT);
    CHECK(f.decision.state == SM612_INVALID && f.model.state == SM612_INVALID);
    CHECK(!f.model.template_valid);
    CHECK(f.model.event_id == 0u && f.decision.event_id == 0u);
    CHECK((f.decision.flags & SM612_CONFIRMED) == 0u);
    CHECK(sm612_arm(&f.model, &f.identity) == -1);
    (void)push(&f, 4u, 400u);
    CHECK(f.model.state != SM612_ALARM && f.model.event_id == 0u);
    CHECK(f.decision.event_id == 0u);
    CHECK((f.decision.flags & SM612_CONFIRMED) == 0u);
}

static void test_gap_immediate_reconfirmation(void)
{
    struct fixture f;
    uint64_t old_event;
    uint64_t new_event;
    const unsigned boundary_flags = SM612_GAP | SM612_INTERRUPTED | SM612_FIRST |
                                    SM612_WORST | SM612_CONFIRMED;
    setup(&f, 16u);
    f.config.enter_frames = 1u;
    learn(&f, 0.0);
    arm(&f);
    level(&f, 10.0);
    CHECK(push(&f, 3u, 300u) == 0);
    CHECK(f.model.state == SM612_ALARM);
    old_event = f.decision.event_id;
    CHECK(old_event != 0u);
    level(&f, 14.0);
    CHECK(push(&f, 5u, 500u) == 0); /* Missing token interrupts the old event. */
    CHECK((f.decision.flags & boundary_flags) == boundary_flags);
    CHECK(f.decision.ended_event_id == old_event);
    new_event = f.decision.event_id;
    CHECK(new_event > old_event && f.model.event_id == new_event);
    CHECK(f.decision.state == SM612_ALARM && f.model.state == SM612_ALARM);
    CHECK(f.decision.first_token == 5u && f.decision.worst_token == 5u);
    CHECK(f.decision.first_us == 500u && f.decision.worst_us == 500u);
    CHECK(f.decision.current_bin == 3u && f.decision.worst_bin == 3u);
    CHECK(f.decision.current_direction == 1 && f.decision.worst_direction == 1);
    CHECK(near(f.decision.current_excess_db, 8.0));
    CHECK(near(f.decision.worst_excess_db, 8.0));
    level(&f, -20.0);
    CHECK(push(&f, 6u, 1501u) == 0); /* Time gap must provide the same boundary. */
    CHECK((f.decision.flags & boundary_flags) == boundary_flags);
    CHECK(f.decision.ended_event_id == new_event);
    CHECK(f.decision.event_id > new_event && f.model.event_id == f.decision.event_id);
    CHECK(f.decision.state == SM612_ALARM && f.model.state == SM612_ALARM);
    CHECK(f.decision.first_token == 6u && f.decision.worst_token == 6u);
    CHECK(f.decision.first_us == 1501u && f.decision.worst_us == 1501u);
    CHECK(f.decision.current_direction == -1 && f.decision.worst_direction == -1);
    CHECK(near(f.decision.current_excess_db, 14.0));
    CHECK(near(f.decision.worst_excess_db, 14.0));
}

static void test_floor_bounded_recovery_and_default_guard(void)
{
    struct fixture f;
    struct sm612_config defaults;
    sm612_defaults(&defaults);
    CHECK(defaults.exclude[1] == 1u);
    CHECK(defaults.exclude[2] == 0u && defaults.exclude[614] == 0u);
    setup(&f, 16u);
    f.config.floor_db = -100.0;
    f.config.min_learn_peak_db = -140.0;
    f.config.lower_enabled = 0;
    learn(&f, -130.0);
    CHECK(near(f.model.upper_db[3], -100.0));
    arm(&f);
    level(&f, -90.0);
    CHECK(push(&f, 3u, 300u) == 0);
    CHECK(push(&f, 4u, 400u) == 0);
    CHECK(f.model.state == SM612_ALARM);
    level(&f, -100.5); /* Below reliable floor but not below floor-hysteresis. */
    CHECK(push(&f, 5u, 500u) == 0);
    CHECK(f.model.recover_count == 1u);
    CHECK(push(&f, 6u, 600u) == 0);
    CHECK(f.model.state == SM612_ARMED);
    CHECK((f.decision.flags & SM612_RECOVERED) != 0u);
}

static void test_noise_only_upper_learning_envelope(void)
{
    struct fixture f;
    setup(&f, 16u);
    f.config.last_bin = 4u;
    f.config.floor_db = -100.0;
    f.config.lower_signal_margin_db = 12.0;
    f.config.max_learn_spread_db = 3.0;
    begin(&f);
    level(&f, -20.0);
    f.code[4] = amplitude(-104.0);
    CHECK(push(&f, 1u, 100u) == 0);
    f.code[4] = amplitude(-94.0);
    CHECK(push(&f, 2u, 200u) == 0);
    CHECK(f.model.state == SM612_READY);
    CHECK(near(f.model.baseline_db[4], 10.0 * log10((pow(10.0,-10.4)+pow(10.0,-9.4))/2.0)));
    CHECK(near(f.model.upper_db[4], -88.0)); /* peak envelope +6, not dB mean. */
    CHECK(f.model.lower_valid[4] == 0u);
    CHECK(near(f.model.upper_db[3], -14.0)); /* Strong-bin mean contract unchanged. */
    arm(&f);
    f.code[4] = amplitude(-91.0);
    CHECK(push(&f, 3u, 300u) == 0);
    CHECK(f.model.state == SM612_ARMED);
    f.code[4] = amplitude(-80.0); /* A genuine new component above envelope. */
    CHECK(push(&f, 4u, 400u) == 0);
    CHECK(push(&f, 5u, 500u) == 0);
    CHECK(f.model.state == SM612_ALARM && f.decision.current_bin == 4u);
    CHECK(f.decision.current_direction == 1);
    CHECK(near(f.decision.current_excess_db, 8.0));
    f.code[4] = amplitude(-91.0);
    CHECK(push(&f, 6u, 600u) == 0);
    CHECK(push(&f, 7u, 700u) == 0);
    CHECK(f.model.state == SM612_ARMED && (f.decision.flags & SM612_RECOVERED));
}

int main(void)
{
    test_learning_power_and_idle();
    test_strict_equal_and_single_frame();
    test_first_worst_and_hysteresis();
    test_candidate_cancel_and_inclusive_recovery();
    test_loss_direction_and_protected_bins();
    test_lower_gate();
    test_invalid_quality_and_numeric();
    test_duplicate_stale_and_time();
    test_gap_and_token_wrap();
    test_identity_mismatch();
    test_stop_suspend_and_busy();
    test_learning_failures_and_interruptions();
    test_production_fft_size();
    test_alias_relearn_and_arm_watermark();
    test_event_counter_limit();
    test_gap_immediate_reconfirmation();
    test_floor_bounded_recovery_and_default_guard();
    test_noise_only_upper_learning_envelope();
    if (failures != 0u) {
        fprintf(stderr, "SM612_CORE_TEST_FAIL checks=%u failures=%u\n", checks, failures);
        return 1;
    }
    printf("SM612_CORE_TEST_PASS checks=%u\n", checks);
    return 0;
}
