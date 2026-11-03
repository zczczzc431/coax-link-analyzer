#ifndef FR_DAC_OWNER_H
#define FR_DAC_OWNER_H
#include "fr_dac_job.h"

enum fr_dac_owner_state {
    FR_OWNER_IDLE, FR_OWNER_FREQUENCY, FR_OWNER_READY,
    FR_OWNER_DRAIN, FR_OWNER_OFF, FR_OWNER_CLOSED, FR_OWNER_FAULT
};
enum fr_dac_owner_error {
    FR_OWNER_FREQUENCY_FAILED=1u, FR_OWNER_OFF_FAILED=2u,
    FR_OWNER_LOST_CHILD=4u, FR_OWNER_CLOCK=8u
};
struct fr_dac_owner {
    struct fr_dac_job job;
    enum fr_dac_owner_state state;
    unsigned errors, frequency_job_errors, off_job_errors;
    uint64_t last_us;
    double amplitude_pct;
    int permitted;
    int use_image;
    struct fr_dac_image image;
};
/* Zero-initialize ONCE. Caller has exclusive DAC ownership, verified front
 * end and a trusted direct-child dacctl. prepare does not discover hardware.
 * No shell/device access here; job executes only with explicit permit.
 * CLOCK/lost-child/OFF failure cannot be silently reset by prepare. */
int fr_dac_owner_prepare(struct fr_dac_owner *,int permit,double amplitude_pct);
/* Snapshot verified image, borrow its fd through final OFF or fault cleanup.
 * No fallback to legacy path, including when OFF cannot be launched. */
int fr_dac_owner_prepare_image(struct fr_dac_owner *,int permit,double amplitude_pct,
                                const struct fr_dac_image *);
/* RAW async callback contract: frequency returns 1=pending, -1=failure;
 * frequency_poll returns 0=pending, 1=confirmed command exit, -1=failure. */
int fr_dac_owner_frequency(struct fr_dac_owner *,uint32_t hz,uint64_t now_us);
int fr_dac_owner_frequency_poll(struct fr_dac_owner *,uint64_t now_us);
/* off is idempotent: cancel/drain old job first, then spawn exactly one OFF.
 * off/off_poll: 0=pending (poll only), 1=pending (off)/confirmed (poll),
 * -1=unconfirmed failure. off returns 0 only if OFF already confirmed.
 * FAULT polling still reaps any known child; lost-child never signals a PID.
 * Killing a job is not OFF. CLOSED proves command completion, not analog zero. */
int fr_dac_owner_off(struct fr_dac_owner *,uint64_t now_us);
int fr_dac_owner_off_poll(struct fr_dac_owner *,uint64_t now_us);
int fr_dac_owner_off_confirmed(const struct fr_dac_owner *);
#endif
