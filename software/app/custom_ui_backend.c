#define _POSIX_C_SOURCE 200809L

#include "custom_ui_backend.h"

#include <sys/mman.h>
#include <string.h>

void custom_ui_backend_init(struct custom_ui_backend *ui, bool admitted) {
    if (!ui) return;
    memset(ui, 0, sizeof(*ui));
    ui->admitted = admitted;
    ui->mmio.mem_fd = -1;
    ui->mmio.axi = MAP_FAILED;
    ui->mmio.gpio = MAP_FAILED;
}

int custom_ui_backend_open(struct custom_ui_backend *ui) {
    struct cwb_io io;
    if (!ui || !ui->admitted || ui->opened) return -1;
    if (cwb_mmio_linux_open(&ui->mmio) < 0) return -1;
    io = cwb_mmio_linux_io(&ui->mmio);
    cwb_backend_init(&ui->backend, &io, 200u);
    cwg_mmio_linux_init(&ui->guard, &ui->mmio);
    ui->opened = true;
    return 0;
}

void custom_ui_backend_close(struct custom_ui_backend *ui) {
    if (!ui) return;
    if (ui->opened) {
        /* Only a previously admitted/opened backend may issue the fail-closed
         * GPIO write.  The normal candidate never reaches this branch. */
        (void)custom_ui_backend_disable(ui);
        cwb_mmio_linux_close(&ui->mmio);
    }
    ui->opened = false;
}

int custom_ui_backend_apply(struct custom_ui_backend *ui,
                            const uint8_t *package, size_t package_length) {
    struct cwg_header header;
    int16_t samples[CWG_TABLE_LEN];
    struct cwg_guard_io guard_io;
    enum cwg_guard_result result;

    if (!ui || !package || !package_length) return -1;
    if (!ui->admitted || !ui->opened) return -2;
    if (cwg_unpack(package, package_length, &header, samples,
                   CWG_TABLE_LEN) != CWG_OK) return -3;
    if (!header.transaction) return -4;
    guard_io = cwg_mmio_linux_guard_io(&ui->guard);
    result = cwg_apply_package_guarded(&ui->backend, package, package_length,
                                       &guard_io, header.transaction,
                                       &ui->last_report);
    return result == CWG_GUARD_OK ? 0 : -(int)result;
}

int custom_ui_backend_disable(struct custom_ui_backend *ui) {
    if (!ui || !ui->opened) return -1;
    return cwb_disable(&ui->backend) == CWB_OK ? 0 : -1;
}

const char *custom_ui_backend_status(const struct custom_ui_backend *ui) {
    if (!ui || !ui->admitted) return "PL_BACKEND_ADMISSION_CLOSED";
    if (!ui->opened) return "BACKEND_NOT_OPEN";
    if (ui->backend.active) return "CUSTOM_ACTIVE";
    return "BACKEND_READY";
}
