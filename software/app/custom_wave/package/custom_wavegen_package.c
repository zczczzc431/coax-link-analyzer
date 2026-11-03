#include "custom_wavegen_package.h"

#include <string.h>

static uint32_t header_crc(const struct cwg_header *header) {
    struct cwg_header copy = *header;
    copy.header_crc32 = 0;
    return cf_crc32(&copy, sizeof copy);
}

static int32_t round_q15_product(int16_t value, int16_t gain) {
    int64_t product = (int64_t)value * (int64_t)gain;
    if (product >= 0) return (int32_t)((product + 16383) / 32767);
    return (int32_t)((product - 16383) / 32767);
}

const char *cwg_error_string(enum cwg_error error) {
    static const char *names[] = {
        "ok", "argument", "frequency", "amplitude", "offset", "clip",
        "length", "magic", "version", "crc", "transaction"
    };
    return (unsigned)error < sizeof(names) / sizeof(names[0])
               ? names[error] : "unknown";
}

enum cwg_error cwg_generate(const char *expression, int32_t amplitude_q15,
                            int16_t offset_q15, struct cwg_table *table) {
    struct cf_table normalized;
    enum cf_error formula_error;
    if (!expression || !table) return CWG_ERR_ARGUMENT;
    if (amplitude_q15 < 0 || amplitude_q15 > 32767) return CWG_ERR_AMPLITUDE;
    formula_error = cf_generate(expression, &normalized);
    if (formula_error != CF_OK) return CWG_ERR_ARGUMENT;
    memset(table, 0, sizeof(*table));
    table->minimum = 32767;
    table->maximum = -32768;
    for (size_t i = 0; i < CWG_TABLE_LEN; ++i) {
        int32_t value = round_q15_product(normalized.q15[i], amplitude_q15);
        value += offset_q15;
        if (value < -32768 || value > 32767) {
            table->clipped = true;
            return CWG_ERR_CLIP;
        }
        table->q15[i] = (int16_t)value;
        if (table->q15[i] < table->minimum) table->minimum = table->q15[i];
        if (table->q15[i] > table->maximum) table->maximum = table->q15[i];
    }
    table->crc32 = cf_crc32(table->q15, CWG_PAYLOAD_BYTES);
    return CWG_OK;
}

enum cwg_error cwg_pack(const struct cwg_table *table, uint32_t transaction,
                        uint32_t frequency_hz, int32_t amplitude_q15,
                        int16_t offset_q15, uint8_t *buffer, size_t capacity,
                        size_t *used) {
    struct cwg_header header;
    if (!table || !buffer || !used) return CWG_ERR_ARGUMENT;
    if (!transaction) return CWG_ERR_TRANSACTION;
    if (frequency_hz < CWG_MIN_HZ || frequency_hz > CWG_MAX_HZ)
        return CWG_ERR_FREQUENCY;
    if (amplitude_q15 < 0 || amplitude_q15 > 32767)
        return CWG_ERR_AMPLITUDE;
    if (capacity < CWG_HEADER_BYTES + CWG_PAYLOAD_BYTES)
        return CWG_ERR_LENGTH;
    memset(&header, 0, sizeof(header));
    header.magic = CWG_MAGIC;
    header.version = CWG_VERSION;
    header.header_bytes = CWG_HEADER_BYTES;
    header.table_len = CWG_TABLE_LEN;
    header.payload_bytes = CWG_PAYLOAD_BYTES;
    header.transaction = transaction;
    header.frequency_hz = frequency_hz;
    header.amplitude_q15 = (int16_t)amplitude_q15;
    header.offset_q15 = offset_q15;
    header.payload_crc32 = cf_crc32(table->q15, CWG_PAYLOAD_BYTES);
    header.header_crc32 = header_crc(&header);
    memcpy(buffer, &header, sizeof(header));
    memcpy(buffer + sizeof(header), table->q15, CWG_PAYLOAD_BYTES);
    *used = sizeof(header) + CWG_PAYLOAD_BYTES;
    return CWG_OK;
}

enum cwg_error cwg_unpack(const uint8_t *buffer, size_t length,
                          struct cwg_header *header, int16_t *samples,
                          size_t sample_capacity) {
    struct cwg_header candidate;
    if (!buffer || !header || !samples) return CWG_ERR_ARGUMENT;
    if (length < sizeof(candidate)) return CWG_ERR_LENGTH;
    memcpy(&candidate, buffer, sizeof(candidate));
    if (candidate.magic != CWG_MAGIC) return CWG_ERR_MAGIC;
    if (candidate.version != CWG_VERSION ||
        candidate.header_bytes != CWG_HEADER_BYTES)
        return CWG_ERR_VERSION;
    if (candidate.table_len != CWG_TABLE_LEN ||
        candidate.payload_bytes != CWG_PAYLOAD_BYTES ||
        candidate.frequency_hz < CWG_MIN_HZ ||
        candidate.frequency_hz > CWG_MAX_HZ ||
        !candidate.transaction ||
        sample_capacity < CWG_TABLE_LEN ||
        length < sizeof(candidate) + candidate.payload_bytes)
        return CWG_ERR_LENGTH;
    if (candidate.header_crc32 != header_crc(&candidate) ||
        candidate.payload_crc32 !=
            cf_crc32(buffer + sizeof(candidate), candidate.payload_bytes))
        return CWG_ERR_CRC;
    memcpy(header, &candidate, sizeof(candidate));
    memcpy(samples, buffer + sizeof(candidate), candidate.payload_bytes);
    return CWG_OK;
}
