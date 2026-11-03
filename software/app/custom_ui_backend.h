#ifndef CUSTOM_UI_BACKEND_H
#define CUSTOM_UI_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "custom_wave/backend/custom_wave_guard_mmio_linux.h"

/*
 * UI-side owner for the candidate CUSTOM transaction.  The default build is
 * deliberately not admitted: constructing this object does not open /dev/mem
 * or touch GPIO.  A later, separately approved build may set admitted=1 after
 * the system CDC/DAC-IO/owner evidence is closed.
 */
struct custom_ui_backend {
    bool admitted;
    bool opened;
    struct cwb_mmio_linux mmio;
    struct cwg_mmio_linux guard;
    struct cwb_backend backend;
    struct cwg_guard_report last_report;
};

void custom_ui_backend_init(struct custom_ui_backend *ui, bool admitted);
int custom_ui_backend_open(struct custom_ui_backend *ui);
void custom_ui_backend_close(struct custom_ui_backend *ui);
int custom_ui_backend_apply(struct custom_ui_backend *ui,
                            const uint8_t *package, size_t package_length);
int custom_ui_backend_disable(struct custom_ui_backend *ui);
const char *custom_ui_backend_status(const struct custom_ui_backend *ui);

#endif
