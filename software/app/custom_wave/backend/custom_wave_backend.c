#include "custom_wave_backend.h"

#include <string.h>

static enum cwb_error io_write(const struct cwb_backend *b, uint32_t off,
                               uint32_t value) {
    if (!b->io.axi_write32 || b->io.axi_write32(b->io.context, off, value) != 0)
        return CWB_ERR_IO;
    return CWB_OK;
}

static enum cwb_error io_read(const struct cwb_backend *b, uint32_t off,
                              uint32_t *value) {
    if (!value || !b->io.axi_read32 ||
        b->io.axi_read32(b->io.context, off, value) != 0)
        return CWB_ERR_IO;
    return CWB_OK;
}

const char *cwb_error_string(enum cwb_error error) {
    static const char *const names[] = {
        "ok", "argument", "io", "package", "frequency", "offline",
        "not-ready", "timeout", "receipt", "enable", "state", "abi",
        "readback"
    };
    return (unsigned)error < sizeof(names) / sizeof(names[0])
               ? names[error] : "unknown";
}

enum cwb_error cwb_frequency_to_ftw(uint32_t frequency_hz, uint32_t *ftw) {
    uint64_t numerator;
    if (!ftw) return CWB_ERR_ARGUMENT;
    if (frequency_hz < CWG_MIN_HZ || frequency_hz > CWG_MAX_HZ)
        return CWB_ERR_FREQUENCY;
    numerator = (uint64_t)frequency_hz * (1ull << 32);
    /* Round to nearest integer; the denominator is the effective 400 MSPS. */
    *ftw = (uint32_t)((numerator + CWB_CUSTOM_SAMPLE_RATE_HZ / 2ull) /
                      CWB_CUSTOM_SAMPLE_RATE_HZ);
    return CWB_OK;
}

void cwb_backend_init(struct cwb_backend *backend, const struct cwb_io *io,
                      uint32_t poll_limit) {
    if (!backend) return;
    memset(backend, 0, sizeof(*backend));
    if (io) backend->io = *io;
    backend->poll_limit = poll_limit ? poll_limit : 1000u;
}

uint32_t cwb_custom_control_word(bool enable) {
    /* Mode 3 selects the AD9142A internal NCO and is deliberately excluded
     * from the CUSTOM admission predicate.  The table already contains its
     * amplitude and offset, so do not apply the GPIO amplitude field again. */
    return enable ? ((1u << 30) | (1u << 27)) : 0u;
}

static enum cwb_error check_ready(const struct cwb_backend *b,
                                  uint32_t required, uint32_t *status) {
    enum cwb_error e = io_read(b, CWB_REG_STATUS, status);
    if (e != CWB_OK) return e;
    if ((*status & CWB_STATUS_ONLINE) == 0u) return CWB_ERR_OFFLINE;
    if ((*status & required) != required) return CWB_ERR_NOT_READY;
    return CWB_OK;
}

static enum cwb_error read_receipt(const struct cwb_backend *b, uint32_t *raw) {
    return io_read(b, CWB_REG_COMMIT_RECEIPT, raw);
}

/*
 * The 522 AXI shell exposes the bridge's inactive-bank Port-A readback.  The
 * bridge returns two 16-bit mirrors in one word: lane 0 in bits 15:0 and
 * lane 1 in bits 31:16.  A CUSTOM table write populates both mirrors, so a
 * successful readback must match both halves.  This check runs before the
 * commit edge; a mismatch therefore leaves the DAC disabled and cannot make
 * a partially written table active.
 */
static enum cwb_error verify_table(const struct cwb_backend *b,
                                   const int16_t *samples,
                                   struct cwb_report *report) {
    for (uint32_t i = 0; i < CWG_TABLE_LEN; ++i) {
        uint32_t raw = 0;
        enum cwb_error e = io_read(b, CWB_TABLE_BASE + i * CWB_TABLE_WORD_STRIDE,
                                   &raw);
        if (e != CWB_OK) return e;
        ++report->table_reads;
        uint16_t expected = (uint16_t)samples[i];
        uint16_t lane0 = (uint16_t)(raw & 0xffffu);
        uint16_t lane1 = (uint16_t)(raw >> 16);
        if (lane0 != expected || lane1 != expected) {
            ++report->readback_mismatches;
            if (report->readback_mismatches == 1u)
                report->first_mismatch = i;
        }
    }
    report->readback_ok = report->readback_mismatches == 0u;
    return report->readback_ok ? CWB_OK : CWB_ERR_READBACK;
}

enum cwb_error cwb_disable(struct cwb_backend *backend) {
    if (!backend || !backend->io.set_dac_words) return CWB_ERR_ARGUMENT;
    if (backend->io.set_dac_words(backend->io.context, backend->tuning_word,
                                  cwb_custom_control_word(false)) != 0)
        return CWB_ERR_ENABLE;
    backend->active = false;
    return CWB_OK;
}

enum cwb_error cwb_apply_package(struct cwb_backend *backend,
                                 const uint8_t *package, size_t package_length,
                                 struct cwb_report *report) {
    int16_t samples[CWG_TABLE_LEN];
    struct cwg_header header;
    uint32_t status = 0, receipt_before = 0, receipt_after = 0;
    uint32_t ftw;
    enum cwb_error e;
    enum cwg_error package_error;

    if (!backend || !package || !report || !backend->io.set_dac_words)
        return CWB_ERR_ARGUMENT;
    memset(report, 0, sizeof(*report));
    package_error = cwg_unpack(package, package_length, &header, samples,
                               CWG_TABLE_LEN);
    if (package_error != CWG_OK) return CWB_ERR_PACKAGE;
    e = cwb_frequency_to_ftw(header.frequency_hz, &ftw);
    if (e != CWB_OK) return e;

    /* Do not write a candidate package through an unrecognized AXI window. */
    {
        uint32_t abi = 0;
        e = io_read(backend, CWB_REG_READBACK_ABI, &abi);
        if (e != CWB_OK) return e;
        if (abi != CWB_READBACK_ABI) return CWB_ERR_ABI;
    }
    /* Never leave an old table driving the DAC while a new table is written. */
    e = cwb_disable(backend);
    if (e != CWB_OK) return e;
    e = check_ready(backend, CWB_STATUS_ONLINE | CWB_STATUS_WRITE_READY,
                    &status);
    if (e != CWB_OK) return e;
    report->status_before = status;
    e = read_receipt(backend, &receipt_before);
    if (e != CWB_OK) return e;

    /* Metadata is written before samples; commit is the only activation edge. */
    e = io_write(backend, CWB_REG_TRANSACTION, header.transaction);
    if (e != CWB_OK) return e;
    e = io_write(backend, CWB_REG_FREQUENCY_HZ, header.frequency_hz);
    if (e != CWB_OK) return e;
    e = io_write(backend, CWB_REG_AMPLITUDE,
                 (uint32_t)(uint16_t)header.amplitude_q15);
    if (e != CWB_OK) return e;
    e = io_write(backend, CWB_REG_OFFSET, (uint32_t)(uint16_t)header.offset_q15);
    if (e != CWB_OK) return e;
    for (uint32_t i = 0; i < CWG_TABLE_LEN; ++i) {
        e = io_write(backend, CWB_TABLE_BASE + i * CWB_TABLE_WORD_STRIDE,
                     (uint32_t)(uint16_t)samples[i]);
        if (e != CWB_OK) return e;
        ++report->table_writes;
    }
    e = check_ready(backend, CWB_STATUS_ONLINE | CWB_STATUS_COMMIT_READY,
                    &status);
    if (e != CWB_OK) return e;

    /* Read every written word through the 522 bridge before committing. */
    e = verify_table(backend, samples, report);
    if (e != CWB_OK) return e;
    /* Recheck the live bridge state after the long readback loop.  This
     * closes the race where the bridge went offline after the first status
     * read but a stale/zero read value happened to match a zero sample. */
    e = check_ready(backend, CWB_STATUS_ONLINE | CWB_STATUS_COMMIT_READY,
                    &status);
    if (e != CWB_OK) return e;

    e = io_write(backend, CWB_REG_CONTROL, CWB_CONTROL_COMMIT);
    if (e != CWB_OK) return e;

    /* Receipt is the transaction boundary; bounded polling avoids sleeps.
     * If a prior receipt was valid, require the next sequence value rather
     * than merely any changed value.  If it was invalid, the first valid
     * receipt is necessarily newer than the observed state. */
    const bool before_valid = (receipt_before & CWB_RECEIPT_VALID) != 0u;
    const uint8_t expected_seq =
        (uint8_t)(((uint8_t)receipt_before + 1u) & 0xffu);
    bool receipt_accepted = false;
    for (uint32_t poll = 0; poll < backend->poll_limit; ++poll) {
        e = read_receipt(backend, &receipt_after);
        if (e != CWB_OK) return e;
        if ((receipt_after & CWB_RECEIPT_VALID) != 0u &&
            ((!before_valid && (uint8_t)receipt_after !=
                                  (uint8_t)receipt_before) ||
             (before_valid && (uint8_t)receipt_after == expected_seq))) {
            receipt_accepted = true;
            break;
        }
        if (poll + 1u == backend->poll_limit) return CWB_ERR_TIMEOUT;
    }
    if (!receipt_accepted) return CWB_ERR_RECEIPT;
    e = io_read(backend, CWB_REG_STATUS, &status);
    if (e != CWB_OK) return e;
    if ((status & CWB_STATUS_ONLINE) == 0u) return CWB_ERR_OFFLINE;
    e = backend->io.set_dac_words(
            backend->io.context, ftw, cwb_custom_control_word(true)) == 0
            ? CWB_OK : CWB_ERR_ENABLE;
    if (e != CWB_OK) return e;

    report->header = header;
    report->ftw = ftw;
    report->status_after = status;
    report->receipt_before = receipt_before;
    report->receipt_after = receipt_after;
    report->receipt_before_valid = (receipt_before & CWB_RECEIPT_VALID) != 0u;
    report->receipt_after_valid = (receipt_after & CWB_RECEIPT_VALID) != 0u;
    report->committed = true;
    report->enabled = true;
    backend->tuning_word = ftw;
    backend->active = true;
    return CWB_OK;
}
