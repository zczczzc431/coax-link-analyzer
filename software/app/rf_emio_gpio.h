#ifndef RF_EMIO_GPIO_H
#define RF_EMIO_GPIO_H
#include "reflect_fwd_rev.h"
#include <stdint.h>

/* Zynq-7000 GPIO line offsets, NOT Linux global sysfs GPIO numbers.
 * EMIO0/1 (54/55) belong to touch and must never be requested here. */
#define RF_EMIO_VC1_LINE 56u
#define RF_EMIO_VC2_LINE 57u
#define RF_EMIO_SETTLE_US 1000u

struct rf_emio_gpio {
    int handle_fd;
    volatile uint32_t *mapping;
};

int rf_emio_mask_word(enum fr_switch_path path, uint32_t *word);
/* --inspect equivalent: chip/line metadata only, no request/direction/write. */
int rf_emio_inspect(const char *chip_path);
/* explicit_confirm is only given after candidate PL and physical wiring have
 * been verified. It is not a detector of the active FPGA configuration. */
int rf_emio_open(struct rf_emio_gpio *ctx, const char *chip_path,
                 int explicit_confirm);
int rf_emio_select(void *ctx, enum fr_switch_path path);
/* Nonblocking v2 selector: caller must enforce >=RF_EMIO_SETTLE_US from
 * callback completion via RAW timer/guard frames; latch is NOT RF feedback. */
int rf_emio_select_nowait(void *ctx, enum fr_switch_path path);
int rf_emio_close_checked(struct rf_emio_gpio *ctx);
void rf_emio_close(struct rf_emio_gpio *ctx);
#endif
