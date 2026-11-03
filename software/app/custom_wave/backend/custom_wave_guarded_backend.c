#include "custom_wave_guarded_backend.h"

#include <string.h>

struct guarded_adapter {
    const struct cwb_io *inner;
    void *inner_context;
    uint32_t pending_ftw;
    bool pending_enable;
    bool disable_ok;
};

static int guarded_axi_write(void *context, uint32_t offset, uint32_t value) {
    struct guarded_adapter *a = (struct guarded_adapter *)context;
    return a && a->inner && a->inner->axi_write32
               ? a->inner->axi_write32(a->inner_context, offset, value)
               : -1;
}

static int guarded_axi_read(void *context, uint32_t offset, uint32_t *value) {
    struct guarded_adapter *a = (struct guarded_adapter *)context;
    return a && a->inner && a->inner->axi_read32
               ? a->inner->axi_read32(a->inner_context, offset, value)
               : -1;
}

/*
 * cwb_apply_package() first sends a real disable and, only after receipt,
 * sends the final CUSTOM enable.  Forward the disable immediately; hold the
 * enable in the candidate adapter until the guard RELEASE has succeeded.
 */
static int guarded_set_dac_words(void *context, uint32_t tuning_word,
                                 uint32_t control_word) {
    struct guarded_adapter *a = (struct guarded_adapter *)context;
    const bool enable = (control_word & (1u << 27)) != 0u;
    if (!a || !a->inner || !a->inner->set_dac_words) return -1;
    if (enable) {
        a->pending_ftw = tuning_word;
        a->pending_enable = true;
        return 0;
    }
    {
        int rc = a->inner->set_dac_words(a->inner_context, tuning_word,
                                          control_word);
        if (!enable) a->disable_ok = rc == 0;
        return rc;
    }
}

const char *cwg_guard_result_string(enum cwg_guard_result result) {
    static const char *const names[] = {
        "ok", "bad-argument", "fence-failed", "backend-failed",
        "release-failed", "enable-failed", "abort-failed"
    };
    return (unsigned)result < sizeof(names) / sizeof(names[0])
               ? names[result] : "unknown";
}

static void mark_abort(const struct cwg_guard_io *guard, uint32_t token,
                       struct cwg_guard_report *report) {
    if (!guard || !guard->abort || !report) return;
    report->abort_called = true;
    report->abort_ok = guard->abort(guard->context, token) == 0;
    /* An abort failure is more severe than the initiating error: the caller
     * must not mistake a failed cleanup for a contained transaction failure. */
    if (!report->abort_ok) report->result = CWG_GUARD_ABORT_FAILED;
}

/*
 * A guarded transaction is fail-closed at the software boundary as well as
 * at the DAC callback.  The shadow backend is deliberately not published
 * until the final enable succeeds; consequently the caller's backend may
 * still describe the previous, already-active package when a later APPLY
 * fails.  Clear that stale fact on every failure path and make one last OFF
 * attempt when the intercepted OFF did not complete.  A successful callback
 * is the only basis for output_off_confirmed; active=false is intentionally
 * conservative and means "do not treat this backend as enabled".
 */
static void fail_closed(struct cwb_backend *backend,
                        struct guarded_adapter *adapter,
                        struct cwg_guard_report *report) {
    int rc;
    if (!backend) return;
    backend->active = false;
    if (adapter && !adapter->disable_ok && backend->io.set_dac_words) {
        rc = backend->io.set_dac_words(
            backend->io.context, backend->tuning_word,
            cwb_custom_control_word(false));
        adapter->disable_ok = (rc == 0);
    }
    if (report)
        report->output_off_confirmed = adapter && adapter->disable_ok;
}

enum cwg_guard_result cwg_apply_package_guarded(
    struct cwb_backend *backend, const uint8_t *package, size_t package_length,
    const struct cwg_guard_io *guard, uint32_t token,
    struct cwg_guard_report *report) {
    struct guarded_adapter adapter;
    struct cwb_backend shadow;
    struct cwb_io shadow_io;
    struct cwb_io original_io;
    enum cwb_error backend_error;

    memset(&adapter, 0, sizeof(adapter));

    if (!report) return CWG_GUARD_BAD_ARGUMENT;
    memset(report, 0, sizeof(*report));
    report->result = CWG_GUARD_BAD_ARGUMENT;
    report->token = token;
    report->output_off_confirmed = false;
    if (!backend || !package || !guard || token == 0u || !guard->fence ||
        !guard->release || !guard->abort || !backend->io.set_dac_words)
        return report->result;

    original_io = backend->io;

    report->fence_called = true;
    report->fence_ok = guard->fence(guard->context, token) == 0;
    if (!report->fence_ok) {
        report->result = CWG_GUARD_FENCE_FAILED;
        /* A rejected fence must not leave a previously published package
         * reported as active.  Try the same reviewed OFF callback used by
         * the normal transaction path, but report confirmation only on rc=0.
         */
        fail_closed(backend, &adapter, report);
        mark_abort(guard, token, report);
        return report->result;
    }

    adapter.inner = &backend->io;
    adapter.inner_context = backend->io.context;
    adapter.pending_ftw = 0u;
    adapter.pending_enable = false;
    adapter.disable_ok = false;
    shadow_io = backend->io;
    shadow_io.context = &adapter;
    shadow_io.axi_write32 = guarded_axi_write;
    shadow_io.axi_read32 = guarded_axi_read;
    shadow_io.set_dac_words = guarded_set_dac_words;
    cwb_backend_init(&shadow, &shadow_io, backend->poll_limit);

    backend_error = cwb_apply_package(&shadow, package, package_length,
                                      &report->backend);
    report->backend_error = backend_error;
    if (backend_error != CWB_OK || !adapter.pending_enable) {
        report->result = CWG_GUARD_APPLY_FAILED;
        fail_closed(backend, &adapter, report);
        mark_abort(guard, token, report);
        return report->result;
    }

    report->release_called = true;
    report->release_ok = guard->release(guard->context, token) == 0;
    if (!report->release_ok) {
        report->result = CWG_GUARD_RELEASE_FAILED;
        fail_closed(backend, &adapter, report);
        mark_abort(guard, token, report);
        return report->result;
    }

    report->enable_called = true;
    report->enable_ok = backend->io.set_dac_words(
                            backend->io.context, adapter.pending_ftw,
                            cwb_custom_control_word(true)) == 0;
    if (!report->enable_ok) {
        /* The release callback must be fail-closed; reinforce it locally. */
        int off_rc = backend->io.set_dac_words(
            backend->io.context, adapter.pending_ftw,
            cwb_custom_control_word(false));
        report->output_off_confirmed = (off_rc == 0);
        report->result = CWG_GUARD_ENABLE_FAILED;
        adapter.disable_ok = report->output_off_confirmed;
        fail_closed(backend, &adapter, report);
        mark_abort(guard, token, report);
        return report->result;
    }

    /* Publish the shadow state only after the physical enable callback. */
    *backend = shadow;
    /* The shadow was only an interception vehicle; retain the caller's
     * original callbacks and context while publishing its active/tuning state. */
    backend->io = original_io;
    report->result = CWG_GUARD_OK;
    report->output_off_confirmed = false;
    return report->result;
}
