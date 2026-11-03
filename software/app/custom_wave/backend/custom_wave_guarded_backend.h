#ifndef CUSTOM_WAVE_GUARDED_BACKEND_H
#define CUSTOM_WAVE_GUARDED_BACKEND_H

#include "custom_wave_backend.h"

/*
 * Candidate-only transaction wrapper for the 524 guard/top contract.
 *
 * The existing cwb_apply_package() proves the AXI/BRAM side, but it does not
 * own the guard mailbox.  This wrapper closes that software ordering gap by
 * fencing the output before any table transaction and releasing it only after
 * the readback + commit receipt have completed.  It deliberately uses
 * callbacks: no /dev/mem, GPIO, board, SD, or production address is assumed.
 */

enum cwg_guard_result {
    CWG_GUARD_OK = 0,
    CWG_GUARD_BAD_ARGUMENT,
    CWG_GUARD_FENCE_FAILED,
    CWG_GUARD_APPLY_FAILED,
    CWG_GUARD_RELEASE_FAILED,
    CWG_GUARD_ENABLE_FAILED,
    CWG_GUARD_ABORT_FAILED
};

struct cwg_guard_io {
    void *context;
    /* FENCE must close both source paths and establish this nonzero token. */
    int (*fence)(void *context, uint32_t token);
    /* RELEASE may open the selected source only for the same fenced token. */
    int (*release)(void *context, uint32_t token);
    /* Best-effort fail-closed action after any transaction failure. */
    int (*abort)(void *context, uint32_t token);
};

struct cwg_guard_report {
    enum cwg_guard_result result;
    enum cwb_error backend_error;
    uint32_t token;
    bool fence_called;
    bool fence_ok;
    bool release_called;
    bool release_ok;
    bool enable_called;
    bool enable_ok;
    bool abort_called;
    bool abort_ok;
    /* True only when the underlying disable callback returned success. */
    bool output_off_confirmed;
    struct cwb_report backend;
};

const char *cwg_guard_result_string(enum cwg_guard_result result);

/*
 * Apply a package while the guard owns the output transition.  The callback
 * contract is intentionally strict: a zero token or missing callback is
 * rejected; cwb_apply_package's final enable is intercepted until RELEASE
 * succeeds.  Any failure calls abort when available and never reports active.
 */
enum cwg_guard_result cwg_apply_package_guarded(
    struct cwb_backend *backend, const uint8_t *package, size_t package_length,
    const struct cwg_guard_io *guard, uint32_t token,
    struct cwg_guard_report *report);

#endif
