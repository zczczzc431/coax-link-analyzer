#ifndef CUSTOM_WAVE_BACKEND_H
#define CUSTOM_WAVE_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../package/custom_wavegen_package.h"

/*
 * Software-side contract for the candidate CUSTOM AXI window.  The transport
 * is deliberately callback based: this layer never opens /dev/mem and cannot
 * touch a board unless a separately reviewed adapter supplies these callbacks.
 */
#define CWB_TABLE_BASE            0x0000u
#define CWB_TABLE_WORDS           CWG_TABLE_LEN
#define CWB_TABLE_WORD_STRIDE    0x0004u
#define CWB_REG_CONTROL           0x1000u
#define CWB_REG_STATUS            0x1004u
#define CWB_REG_TRANSACTION       0x1008u
#define CWB_REG_FREQUENCY_HZ      0x100cu
#define CWB_REG_AMPLITUDE         0x1010u
#define CWB_REG_OFFSET            0x1014u
#define CWB_REG_DAC_CONFIG_STATUS 0x1018u
#define CWB_REG_READBACK_ABI      0x101cu
#define CWB_REG_COMMIT_RECEIPT    0x1020u

#define CWB_CONTROL_COMMIT        0x00000001u
#define CWB_STATUS_COMMIT_READY   0x00000001u
#define CWB_STATUS_WRITE_READY    0x00000002u
#define CWB_STATUS_ONLINE         0x00000010u
#define CWB_RECEIPT_VALID         0x00000100u
#define CWB_READBACK_ABI          0x52420101u

/* CUSTOM uses the existing DAC's 400 MSPS effective two-sample stream. */
#define CWB_CUSTOM_SAMPLE_RATE_HZ 400000000ull

enum cwb_error {
    CWB_OK = 0,
    CWB_ERR_ARGUMENT,
    CWB_ERR_IO,
    CWB_ERR_PACKAGE,
    CWB_ERR_FREQUENCY,
    CWB_ERR_OFFLINE,
    CWB_ERR_NOT_READY,
    CWB_ERR_TIMEOUT,
    CWB_ERR_RECEIPT,
    CWB_ERR_ENABLE,
    CWB_ERR_STATE,
    CWB_ERR_ABI,
    CWB_ERR_READBACK
};

struct cwb_io {
    void *context;
    int (*axi_write32)(void *context, uint32_t offset, uint32_t value);
    int (*axi_read32)(void *context, uint32_t offset, uint32_t *value);
    /*
     * The adapter owns the DAC GPIO transaction (including the existing
     * control-word pulse).  The backend never mmaps /dev/mem itself.
     * control_word is the complete word for the existing DAC GPIO.  For a
     * CUSTOM table it must contain mode != 3, DAC enable bit 30 and the
     * CUSTOM select bit 27 when enable is true.  The adapter must preserve
     * unrelated board policy and provide the same ordering as dacctl:
     * deassert pulse -> FTW stable -> assert pulse -> deassert pulse.
     */
    int (*set_dac_words)(void *context, uint32_t tuning_word,
                         uint32_t control_word);
};

struct cwb_backend {
    struct cwb_io io;
    uint32_t poll_limit;
    uint32_t tuning_word;
    bool active;
};

struct cwb_report {
    struct cwg_header header;
    uint32_t ftw;
    uint32_t table_writes;
    uint32_t table_reads;
    uint32_t readback_mismatches;
    uint32_t first_mismatch;
    uint32_t status_before;
    uint32_t status_after;
    uint32_t receipt_before;
    uint32_t receipt_after;
    bool receipt_before_valid;
    bool receipt_after_valid;
    bool readback_ok;
    bool committed;
    bool enabled;
};

const char *cwb_error_string(enum cwb_error error);

/* FTW = round(frequency_hz * 2^32 / 400e6), with no floating point. */
enum cwb_error cwb_frequency_to_ftw(uint32_t frequency_hz, uint32_t *ftw);

void cwb_backend_init(struct cwb_backend *backend, const struct cwb_io *io,
                      uint32_t poll_limit);

/* Apply a validated .cwg package to an inactive table and commit it. */
enum cwb_error cwb_apply_package(struct cwb_backend *backend,
                                 const uint8_t *package, size_t package_length,
                                 struct cwb_report *report);

/* Safe OFF path. It is idempotent and only calls the reviewed adapter. */
enum cwb_error cwb_disable(struct cwb_backend *backend);

/* Build the existing GPIO control word without touching hardware. */
uint32_t cwb_custom_control_word(bool enable);

#endif
