#define _POSIX_C_SOURCE 200809L

#include "custom_wave_mmio_adapter_linux.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static int valid_adapter(const struct cwb_mmio_linux *a) {
    return a && a->mem_fd >= 0 && a->axi != MAP_FAILED &&
           a->gpio != MAP_FAILED;
}

static void delay_ms(unsigned ms) {
    struct timespec req = { (time_t)(ms / 1000u),
                            (long)(ms % 1000u) * 1000000L };
    while (nanosleep(&req, &req) < 0 && errno == EINTR) {}
}

int cwb_mmio_linux_open(struct cwb_mmio_linux *adapter) {
    void *axi_map;
    void *gpio_map;
    if (!adapter) { errno = EINVAL; return -1; }
    adapter->mem_fd = -1;
    adapter->axi = MAP_FAILED;
    adapter->gpio = MAP_FAILED;
    adapter->gpio_tuning_word = 0;
    adapter->mem_fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
    if (adapter->mem_fd < 0) return -1;
    axi_map = mmap(NULL, CWB_MMIO_AXI_MAP_BYTES, PROT_READ | PROT_WRITE,
                   MAP_SHARED, adapter->mem_fd, CWB_MMIO_AXI_BASE);
    if (axi_map == MAP_FAILED) { cwb_mmio_linux_close(adapter); return -1; }
    gpio_map = mmap(NULL, CWB_MMIO_GPIO_MAP_BYTES, PROT_READ | PROT_WRITE,
                    MAP_SHARED, adapter->mem_fd, CWB_MMIO_GPIO_BASE);
    if (gpio_map == MAP_FAILED) { munmap(axi_map, CWB_MMIO_AXI_MAP_BYTES);
        adapter->axi = MAP_FAILED; cwb_mmio_linux_close(adapter); return -1; }
    adapter->axi = (volatile uint32_t *)axi_map;
    adapter->gpio = (volatile uint32_t *)gpio_map;
    return 0;
}

void cwb_mmio_linux_close(struct cwb_mmio_linux *adapter) {
    if (!adapter) return;
    if (adapter->axi != MAP_FAILED) munmap((void *)adapter->axi,
                                           CWB_MMIO_AXI_MAP_BYTES);
    if (adapter->gpio != MAP_FAILED) munmap((void *)adapter->gpio,
                                            CWB_MMIO_GPIO_MAP_BYTES);
    if (adapter->mem_fd >= 0) close(adapter->mem_fd);
    adapter->mem_fd = -1;
    adapter->axi = MAP_FAILED;
    adapter->gpio = MAP_FAILED;
}

int cwb_mmio_linux_axi_write32(void *context, uint32_t offset,
                               uint32_t value) {
    struct cwb_mmio_linux *a = (struct cwb_mmio_linux *)context;
    if (!valid_adapter(a) || (offset & 3u) || offset >= CWB_MMIO_MAP_BYTES)
        return -1;
    a->axi[offset / 4u] = value;
    __sync_synchronize();
    return 0;
}

int cwb_mmio_linux_axi_read32(void *context, uint32_t offset,
                              uint32_t *value) {
    struct cwb_mmio_linux *a = (struct cwb_mmio_linux *)context;
    if (!value || !valid_adapter(a) || (offset & 3u) ||
        offset >= CWB_MMIO_MAP_BYTES) return -1;
    __sync_synchronize();
    *value = a->axi[offset / 4u];
    return 0;
}

int cwb_mmio_linux_set_dac_words(void *context, uint32_t tuning_word,
                                 uint32_t control_word) {
    struct cwb_mmio_linux *a = (struct cwb_mmio_linux *)context;
    if (!valid_adapter(a)) return -1;
    /* Same ordering as the existing dacctl candidate.  GPIO2 bit31 is the
     * control-word commit pulse; GPIO_DATA is the stable 32-bit FTW bus. */
    a->gpio[2] = control_word;
    __sync_synchronize(); delay_ms(2);
    a->gpio[0] = tuning_word;
    __sync_synchronize(); delay_ms(2);
    a->gpio[2] = control_word | 0x80000000u;
    __sync_synchronize(); delay_ms(2);
    a->gpio[2] = control_word;
    __sync_synchronize(); delay_ms(2);
    a->gpio_tuning_word = tuning_word;
    return 0;
}
