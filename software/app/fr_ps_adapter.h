#ifndef FR_PS_ADAPTER_H
#define FR_PS_ADAPTER_H
#include "fr_raw_capture.h"
#include <stddef.h>

#define FR_PS_SAMPLES 2048u
#define FR_PS_CANCEL_US 2000000u
enum fr_ps_error { FR_PS_OK, FR_PS_TOKEN, FR_PS_CLOCK, FR_PS_FRAME };
enum fr_ps_reason { FR_PS_STOP, FR_PS_COMPLETE, FR_PS_ERROR, FR_PS_EXIT };
enum fr_ps_safety {
    FR_PS_OFF_FAILED=1u, FR_PS_CANCEL_FAILED=2u,
    FR_PS_CANCEL_TIMEOUT=4u, FR_PS_RELEASE_FAILED=8u,
    FR_PS_CLEANUP_CLOCK=16u
};
struct fr_ps_io {
    struct fr_raw_backend acquisition;
    /* These use acquisition.ctx; cancel_request only writes CANCEL, not ACK.
     * release_rf closes only the RF handle, NOT the ADC reader lock. */
    int (*dac_off)(void *);
    int (*cancel_request)(void *, uint32_t token);
    int (*release_rf)(void *);
};
struct fr_ps_observation {
    uint32_t req_before, done_before, flags_before, index_before;
    uint32_t req_after, done_after, flags_after, index_after;
    const uint32_t *samples;
    size_t count;
    int copy_stable;
    uint64_t observed_us;
};
struct fr_ps_admission {
    /* Caller-established facts, never inferred from a UI title/file hash. */
    int adc_lock_owned, mailbox_ready, writers_idle, frontend_verified;
    uint32_t control_generation, trigger_revision;
};
struct fr_ps_adapter {
    struct fr_raw_capture *capture;
    struct fr_ps_io io;
    struct fr_raw_frame armed;
    enum fr_raw_path armed_path;
    uint64_t sequence, last_clock_us, cancel_deadline_us;
    uint32_t last_token, pending_token;
    unsigned safety_errors;
    enum fr_ps_error error;
    enum fr_ps_reason reason;
    int seeded, pending, stopping, blocked, owned, released;
    int async_frequency, output_pending, controls_seeded;
    int (*output_poll)(void *ctx);
    uint32_t control_generation, trigger_revision;
};

/* Single PS ADC owner only; initialize once, not on each test/path/clear.
 * PS ARM and DONE observations bound acquisition time; not ADC timestamps.
 * No MMIO/device/GPIO access in this module. Guarded application code supplies
 * platform callbacks only after admission; default hardware use is disabled. */
void fr_ps_init(struct fr_ps_adapter *, struct fr_raw_capture *);
/* Explicit permit is not hardware detection. Caller must verify hardware,
 * acquire exclusive resources, and supply callbacks before permitting start. */
int fr_ps_start(struct fr_ps_adapter *, const struct fr_raw_config *,
                struct fr_ps_io, int explicit_permit);
/* Checked async entry: rejects incomplete admission BEFORE any callbacks.
 * frequency_poll follows fr_raw_start_async. dac_off may return 1=pending;
 * output_poll returns 0=pending, 1=confirmed OFF completion, -1=unconfirmed
 * failure. Pending output retains ownership/RF until explicitly confirmed;
 * physical zero is still not proven by command completion. */
int fr_ps_start_async(struct fr_ps_adapter *, const struct fr_raw_config *,
                      struct fr_ps_io, int explicit_permit,
                      const struct fr_ps_admission *,
                      int (*frequency_poll)(void *ctx),
                      int (*output_poll)(void *ctx));
/* Called from main loop, NOT signal handler; catches cancellation even when
 * no ADC request is outstanding (between points / command preparation). */
int fr_ps_control_sync(struct fr_ps_adapter *, uint32_t generation,
                       uint32_t trigger_revision, uint64_t now_us);
/* Call before REQUEST is written. A nonzero forward HW token is unwrapped;
 * preserve epoch/frequency/time from ARM, never from DONE/current UI. */
int fr_ps_arm(struct fr_ps_adapter *, uint32_t token, uint64_t now_us);
/* Feed only after stable raw copy, never HF fit, display interpolation or
 * phase estimate. Reject inconsistent metadata, cancel flags and bad codes. */
int fr_ps_consume(struct fr_ps_adapter *, const struct fr_ps_observation *);
/* OFF once, request cancellation, asynchronously drain matching DONE, then
 * release RF. Repeated stop is idempotent; error flags remain sticky.
 * Timeout releases RF but retains pending/block: ADC lock must remain owned
 * until caller recovers the outstanding request. RF close is NOT Hi-Z. */
int fr_ps_stop(struct fr_ps_adapter *, enum fr_ps_reason, uint64_t now_us);
int fr_ps_poll(struct fr_ps_adapter *, uint64_t now_us);
int fr_ps_clean(const struct fr_ps_adapter *);

/* Startup recovery after acquiring the existing advisory ADC lock. This
 * pure decision helper never clears REQUEST/DONE or assumes a lock survives
 * process exit. Only CANCEL may be written, once, by the caller. It cannot
 * exclude a reader which ignores the advisory lock. Old samples are discarded.
 * A stable observation means two barrier-separated metadata reads agree. */
enum fr_ps_recovery_action {
    FR_PS_RECOVERY_FAILED=-1, FR_PS_RECOVERY_WAIT=0,
    FR_PS_RECOVERY_READY=1, FR_PS_RECOVERY_CANCEL=2
};
enum fr_ps_recovery_error {
    FR_PS_RECOVERY_OK, FR_PS_RECOVERY_OWNER, FR_PS_RECOVERY_CLOCK,
    FR_PS_RECOVERY_TIMEOUT, FR_PS_RECOVERY_METADATA,
    FR_PS_RECOVERY_CONFLICT
};
struct fr_ps_mailbox_state { uint32_t req, done, flags; int stable; };
struct fr_ps_recovery {
    uint64_t last_us, deadline_us;
    uint32_t token;
    enum fr_ps_recovery_error error;
    int cancel_sent, ready;
};
void fr_ps_recovery_init(struct fr_ps_recovery *, uint64_t now_us);
int fr_ps_recovery_step(struct fr_ps_recovery *, struct fr_ps_mailbox_state,
                        int adc_lock_owned, uint64_t now_us);
#endif
