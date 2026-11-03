#define _POSIX_C_SOURCE 200809L

#include "custom_wave_guard_mmio_linux.h"

#include <errno.h>
#include <time.h>

static void delay_ms(unsigned ms) {
    struct timespec req = { (time_t)(ms / 1000u),
                            (long)(ms % 1000u) * 1000000L };
    while (nanosleep(&req, &req) < 0 && errno == EINTR) {}
}

static int read_guard_status(struct cwg_mmio_linux *g, uint32_t token,
                             uint32_t action) {
    uint32_t abi = 0, resp = 0, rsp_token = 0;
    unsigned i;
    if (!g || !g->base) return -1;
    if (cwb_mmio_linux_axi_read32(g->base, CWG_MMIO_GUARD_ABI, &abi) < 0 ||
        abi != CWG_MMIO_GUARD_ABI_VALUE) return -1;
    for (i = 0; i < g->poll_limit; ++i) {
        if (cwb_mmio_linux_axi_read32(g->base, CWG_MMIO_GUARD_RESP, &resp) < 0)
            return -1;
        if ((resp & (1u << 4)) != 0u) {
            if ((resp & 3u) != 0u || ((resp >> 2) & 3u) != action)
                return -1;
            if (cwb_mmio_linux_axi_read32(g->base, CWG_MMIO_GUARD_RSP_TOKEN,
                                           &rsp_token) < 0 ||
                rsp_token != token) return -1;
            return 0;
        }
        delay_ms(g->poll_delay_ms);
    }
    return -1;
}

static int issue_guard(struct cwg_mmio_linux *g, uint32_t token,
                       uint32_t action) {
    if (!g || !g->base || token == 0u || action > 1u) return -1;
    if (cwb_mmio_linux_axi_write32(g->base, CWG_MMIO_GUARD_TOKEN, token) < 0)
        return -1;
    if (cwb_mmio_linux_axi_write32(g->base, CWG_MMIO_GUARD_CMD, action) < 0)
        return -1;
    return read_guard_status(g, token, action);
}

void cwg_mmio_linux_init(struct cwg_mmio_linux *guard,
                         struct cwb_mmio_linux *base) {
    if (!guard) return;
    guard->base = base;
    guard->current_ftw = 0u;
    guard->poll_limit = 200u;
    guard->poll_delay_ms = 1u;
}

int cwg_mmio_linux_fence(void *context, uint32_t token) {
    return issue_guard((struct cwg_mmio_linux *)context, token, 0u);
}

int cwg_mmio_linux_release(void *context, uint32_t token) {
    return issue_guard((struct cwg_mmio_linux *)context, token, 1u);
}

int cwg_mmio_linux_abort(void *context, uint32_t token) {
    struct cwg_mmio_linux *g = (struct cwg_mmio_linux *)context;
    (void)token;
    if (!g || !g->base) return -1;
    // The guard has no ABORT command. Clearing the DAC control word is the
    // fail-closed software fallback; the hardware mailbox remains latched
    // faulted and will not release a stale token.
    return cwb_mmio_linux_set_dac_words(g->base, g->current_ftw, 0u);
}

int cwg_mmio_linux_set_dac_words(void *context, uint32_t tuning_word,
                                 uint32_t control_word) {
    struct cwg_mmio_linux *g = (struct cwg_mmio_linux *)context;
    if (!g || !g->base) return -1;
    g->current_ftw = tuning_word;
    return cwb_mmio_linux_set_dac_words(g->base, tuning_word, control_word);
}

struct cwg_guard_io cwg_mmio_linux_guard_io(struct cwg_mmio_linux *guard) {
    struct cwg_guard_io io = {
        guard, cwg_mmio_linux_fence, cwg_mmio_linux_release,
        cwg_mmio_linux_abort
    };
    return io;
}
