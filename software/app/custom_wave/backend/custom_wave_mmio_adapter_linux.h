#ifndef CUSTOM_WAVE_MMIO_ADAPTER_LINUX_H
#define CUSTOM_WAVE_MMIO_ADAPTER_LINUX_H

#include "custom_wave_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Candidate-only Linux adapter.  It is never opened by the package/contract
 * code; the caller must explicitly construct and close it. */
#define CWB_MMIO_AXI_BASE  0x41240000u
#define CWB_MMIO_GPIO_BASE 0x41230000u
/* The AXI window contains the table at 0x0000 and protocol/control registers
 * at 0x1000..0x1034. Mapping only 4 KiB makes every control/guard access fail;
 * keep the span explicit and bounded instead of silently accepting arbitrary
 * offsets. GPIO remains a separate 4 KiB window. */
#define CWB_MMIO_AXI_MAP_BYTES 0x2000u
#define CWB_MMIO_GPIO_MAP_BYTES 0x1000u
#define CWB_MMIO_MAP_BYTES CWB_MMIO_AXI_MAP_BYTES

struct cwb_mmio_linux {
    int mem_fd;
    volatile uint32_t *axi;
    volatile uint32_t *gpio;
    uint32_t gpio_tuning_word;
};

int cwb_mmio_linux_open(struct cwb_mmio_linux *adapter);
void cwb_mmio_linux_close(struct cwb_mmio_linux *adapter);
int cwb_mmio_linux_axi_write32(void *context, uint32_t offset,
                               uint32_t value);
int cwb_mmio_linux_axi_read32(void *context, uint32_t offset,
                              uint32_t *value);
int cwb_mmio_linux_set_dac_words(void *context, uint32_t tuning_word,
                                 uint32_t control_word);

static inline struct cwb_io cwb_mmio_linux_io(struct cwb_mmio_linux *adapter) {
    struct cwb_io io = { adapter, cwb_mmio_linux_axi_write32,
                         cwb_mmio_linux_axi_read32,
                         cwb_mmio_linux_set_dac_words };
    return io;
}

#ifdef __cplusplus
}
#endif

#endif
