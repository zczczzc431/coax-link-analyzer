#ifndef CUSTOM_WAVE_GUARD_MMIO_LINUX_H
#define CUSTOM_WAVE_GUARD_MMIO_LINUX_H

#include "custom_wave_guarded_backend.h"
#include "custom_wave_mmio_adapter_linux.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CWG_MMIO_GUARD_CMD       0x1024u
#define CWG_MMIO_GUARD_TOKEN     0x1028u
#define CWG_MMIO_GUARD_RESP      0x102cu
#define CWG_MMIO_GUARD_RSP_TOKEN 0x1030u
#define CWG_MMIO_GUARD_ABI       0x1034u
#define CWG_MMIO_GUARD_ABI_VALUE 0x52420102u

struct cwg_mmio_linux {
    struct cwb_mmio_linux *base;
    uint32_t current_ftw;
    unsigned poll_limit;
    unsigned poll_delay_ms;
};

void cwg_mmio_linux_init(struct cwg_mmio_linux *guard,
                         struct cwb_mmio_linux *base);
int cwg_mmio_linux_fence(void *context, uint32_t token);
int cwg_mmio_linux_release(void *context, uint32_t token);
int cwg_mmio_linux_abort(void *context, uint32_t token);
int cwg_mmio_linux_set_dac_words(void *context, uint32_t tuning_word,
                                 uint32_t control_word);
struct cwg_guard_io cwg_mmio_linux_guard_io(struct cwg_mmio_linux *guard);

#ifdef __cplusplus
}
#endif

#endif
