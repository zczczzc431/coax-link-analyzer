#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "rf_emio_gpio.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static const uint32_t PS_GPIO_BASE = 0xe000a000u;
enum { MAP_SIZE = 4096, BANK2_MASK_DATA_LSW = 0x10,
       BANK2_OUTPUT_DATA = 0x48, VC_MASK = 0x000cu };

int rf_emio_mask_word(enum fr_switch_path path, uint32_t *word) {
    if (!word || (path != FR_PATH_FORWARD && path != FR_PATH_REVERSE))
        return -1;
    /* Xilinx mask convention: upper-half 1 means DO NOT update this pin.
     * One masked write updates both controls, preserving touch and all others.
     * No read-modify-write of the shared bank or DAC control register. */
    *word = ((uint32_t)(0xffffu & ~VC_MASK) << 16) |
            (path == FR_PATH_FORWARD ? 4u : 8u);
    return 0;
}

static int open_checked_chip(const char *path) {
    if (!path) { errno = EINVAL; return -1; }
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    struct gpiochip_info chip = {0};
    if (ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &chip) < 0 ||
        strcmp(chip.label, "zynq_gpio") != 0 || chip.lines != 118) {
        close(fd); errno = ENODEV; return -1;
    }
    return fd;
}

int rf_emio_inspect(const char *path) {
    int fd = open_checked_chip(path);
    if (fd < 0) return -1;
    for (unsigned line = RF_EMIO_VC1_LINE; line <= RF_EMIO_VC2_LINE; ++line) {
        struct gpioline_info info = { .line_offset = line };
        if (ioctl(fd, GPIO_GET_LINEINFO_IOCTL, &info) < 0) {
            close(fd); return -1;
        }
        printf("RF_EMIO_INSPECT line=%u flags=0x%x consumer=%s NO_GPIO_WRITE\n",
               line, info.flags, info.consumer);
    }
    close(fd);
    return 0;
}

int rf_emio_open(struct rf_emio_gpio *ctx, const char *path,
                 int explicit_confirm) {
    if (!ctx) { errno = EINVAL; return -1; }
    *ctx = (struct rf_emio_gpio){ .handle_fd = -1, .mapping = NULL };
    if (explicit_confirm != 1) { errno = EPERM; return -1; }
    int chip = open_checked_chip(path);
    if (chip < 0) return -1;
    /* Reserve both lines through gpiolib, so another GPIO consumer cannot
     * modify their direction or values while our masked output writes occur. */
    struct gpiohandle_request req = {0};
    req.lines = 2;
    req.lineoffsets[0] = RF_EMIO_VC1_LINE;
    req.lineoffsets[1] = RF_EMIO_VC2_LINE;
    req.flags = GPIOHANDLE_REQUEST_OUTPUT;
    req.default_values[0] = 0;
    req.default_values[1] = 1;
    snprintf(req.consumer_label, sizeof(req.consumer_label), "rf-emio-candidate");
    if (ioctl(chip, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        close(chip); return -1;
    }
    close(chip);
    ctx->handle_fd = req.fd;
    int flags=fcntl(ctx->handle_fd,F_GETFD);
    if(flags<0 || fcntl(ctx->handle_fd,F_SETFD,flags|FD_CLOEXEC)<0) {
        rf_emio_close(ctx);return -1;
    }
    int mem = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
    if (mem < 0) { rf_emio_close(ctx); return -1; }
    void *map = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                     mem, PS_GPIO_BASE);
    close(mem);
    if (map == MAP_FAILED) { rf_emio_close(ctx); return -1; }
    ctx->mapping = map;
    if (rf_emio_select(ctx, FR_PATH_REVERSE) < 0) {
        rf_emio_close(ctx); return -1;
    }
    return 0;
}

int rf_emio_select(void *opaque, enum fr_switch_path path) {
    struct rf_emio_gpio *ctx = opaque;
    uint32_t word;
    if (!ctx || ctx->handle_fd < 0 || !ctx->mapping ||
        rf_emio_mask_word(path, &word) < 0) { errno = EINVAL; return -1; }
    ctx->mapping[BANK2_MASK_DATA_LSW / sizeof(uint32_t)] = word;
    __sync_synchronize();
    /* Chip switching time is not ADC frame freshness. Allow 1 ms here, and
     * require the caller to discard pre-switch/in-flight frames separately. */
    struct timespec delay = { .tv_sec = 0,
                             .tv_nsec = RF_EMIO_SETTLE_US * 1000L };
    while (nanosleep(&delay, &delay) < 0) {
        if (errno != EINTR) return -1;
    }
    /* Output-only ECO intentionally leaves the original EMIOGPIOI tied low.
     * DATA_RO / GPIOHANDLE_GET_LINE_VALUES cannot measure these outputs.
     * Check the PS OUTPUT LATCH only, not actual pin voltage or RF selection. */
    uint32_t expected = path == FR_PATH_FORWARD ? 4u : 8u;
    if ((ctx->mapping[BANK2_OUTPUT_DATA / sizeof(uint32_t)] & VC_MASK) != expected) {
        errno = EIO; return -1;
    }
    return 0;
}

int rf_emio_select_nowait(void *opaque,enum fr_switch_path path) {
    struct rf_emio_gpio *ctx=opaque;uint32_t word;
    if(!ctx || ctx->handle_fd<0 || !ctx->mapping ||
       rf_emio_mask_word(path,&word)<0) {errno=EINVAL;return -1;}
    ctx->mapping[BANK2_MASK_DATA_LSW/sizeof(uint32_t)]=word;
    __sync_synchronize();
    uint32_t expected=path==FR_PATH_FORWARD?4u:8u;
    if((ctx->mapping[BANK2_OUTPUT_DATA/sizeof(uint32_t)]&VC_MASK)!=expected) {
        errno=EIO;return -1;
    }
    return 0;
}
int rf_emio_close_checked(struct rf_emio_gpio *ctx) {
    if(!ctx)return -1;
    int rc=0;
    if(ctx->mapping) {
        if(munmap((void *)ctx->mapping,MAP_SIZE)<0)rc=-1;
        else ctx->mapping=NULL;
    }
    if(ctx->handle_fd>=0) {
        /* Linux close can release the descriptor even on error: never retry
         * a stale number which may now belong to another consumer. */
        int fd=ctx->handle_fd;ctx->handle_fd=-1;
        if(close(fd)<0)rc=-1;
    }
    return rc;
}
void rf_emio_close(struct rf_emio_gpio *ctx) {
    if (!ctx) return;
    /* No explicit output reset here. After GPIO close, userspace no longer
     * controls the state: retaining the last level OR Hi-Z is not guaranteed.
     * Never reset the shared bank, and release only after caller's OFF drain. */
    if (ctx->mapping) munmap((void *)ctx->mapping, MAP_SIZE);
    if (ctx->handle_fd >= 0) close(ctx->handle_fd);
    *ctx = (struct rf_emio_gpio){ .handle_fd = -1, .mapping = NULL };
}
