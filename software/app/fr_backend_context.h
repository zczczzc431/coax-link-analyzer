#ifndef FR_BACKEND_CONTEXT_H
#define FR_BACKEND_CONTEXT_H
#include "fr_ps_adapter.h"
#include "fr_dac_owner.h"
#include "rf_emio_gpio.h"

struct fr_backend_resources {
    /* Borrowed until release; caller acquires these exclusively first.
     * Facts are NOT inferred from a UI title, FD number or file existence. */
    volatile uint32_t *mailbox;
    void *clock_ctx,*rf_ctx;
    uint64_t (*monotonic_us)(void *);
    int (*rf_select)(void *,enum fr_raw_path);
    int (*rf_release)(void *);
    int adc_lock_owned,dac_writers_idle,rf_handle_owned;
    int frontend_verified,dac_tool_verified;
    const struct fr_dac_image *dac_image; /* Verified FD borrowed until cleanup. */
};
enum fr_backend_error { FR_BACKEND_SWITCH=1u,FR_BACKEND_CANCEL=2u,
                       FR_BACKEND_RELEASE=4u };
struct fr_backend_context {
    struct fr_dac_owner dac;
    struct fr_backend_resources resources;
    int active,rf_released;
    /* MANUAL REFLECT deliberately leaves the physical RF switch untouched;
     * the user has connected one coupled port directly to the ADC. */
    int manual_no_switch;
    unsigned errors;
};
/* Zero-initialize context once, preserve the PS adapter token history.
 * Default permit=0 refuses BEFORE callbacks/jobs/GPIO/mailbox writes.
 * Preflight validates config/mailbox and caller-established resource facts.
 * Does not open devices or acquire locks; application hardware use is disabled
 * by default. A verified candidate may explicitly prepare guarded resources.
 * Real callbacks require verified candidate PL, electrical/RF matching and
 * trusted direct-child DAC writer. Synthetic tests supply fake callbacks. */
int fr_backend_start(struct fr_backend_context *,struct fr_ps_adapter *,
                      const struct fr_raw_config *,struct fr_backend_resources,
                      int explicit_permit,double amplitude_pct,
                      uint32_t control_generation,uint32_t trigger_revision);
struct fr_ps_io fr_backend_io(struct fr_backend_context *);
int fr_backend_frequency_poll(void *);
int fr_backend_output_poll(void *);
/* Optional existing EMIO adapters; they are never called automatically.
 * nowait select only writes EMIO2/3 (VC1/VC2), not touch; caller enforces timer.
 * Release is not Hi-Z. Latch matching is not a physical RF measurement. */
int fr_backend_emio_select(void *,enum fr_raw_path);
int fr_backend_emio_release(void *);
#endif
