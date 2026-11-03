#ifndef CUSTOM_WAVEGEN_PACKAGE_H
#define CUSTOM_WAVEGEN_PACKAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../formula/formula_engine.h"

#define CWG_MAGIC 0x32475743u /* "CWG2" in little-endian byte order */
#define CWG_VERSION 2u
#define CWG_TABLE_LEN CF_TABLE_LEN
#define CWG_PAYLOAD_BYTES (CWG_TABLE_LEN * sizeof(int16_t))
#define CWG_HEADER_BYTES 40u
#define CWG_MIN_HZ 1u
#define CWG_MAX_HZ 100000000u

enum cwg_error {
    CWG_OK = 0,
    CWG_ERR_ARGUMENT,
    CWG_ERR_FREQUENCY,
    CWG_ERR_AMPLITUDE,
    CWG_ERR_OFFSET,
    CWG_ERR_CLIP,
    CWG_ERR_LENGTH,
    CWG_ERR_MAGIC,
    CWG_ERR_VERSION,
    CWG_ERR_CRC,
    CWG_ERR_TRANSACTION
};

/* The package is a software-side candidate ABI. It is not a claim about the
 * current production AXI register map. All multi-byte fields are native
 * little-endian values on the Zynq target, and the header size is asserted. */
struct cwg_header {
    uint32_t magic;
    uint16_t version;
    uint16_t header_bytes;
    uint32_t table_len;
    uint32_t payload_bytes;
    uint32_t transaction;
    uint32_t frequency_hz;
    int16_t amplitude_q15;
    int16_t offset_q15;
    uint32_t payload_crc32;
    uint32_t header_crc32;
    uint32_t reserved;
};

_Static_assert(sizeof(struct cwg_header) == CWG_HEADER_BYTES,
               "custom waveform package header ABI changed");

struct cwg_table {
    int16_t q15[CWG_TABLE_LEN];
    int16_t minimum;
    int16_t maximum;
    uint32_t crc32;
    bool clipped;
};

const char *cwg_error_string(enum cwg_error error);

/* Generate a normalized formula and apply signed Q15 amplitude/offset. The
 * operation rejects clipping instead of silently changing the requested
 * waveform. */
enum cwg_error cwg_generate(const char *expression, int32_t amplitude_q15,
                            int16_t offset_q15, struct cwg_table *table);

enum cwg_error cwg_pack(const struct cwg_table *table, uint32_t transaction,
                        uint32_t frequency_hz, int32_t amplitude_q15,
                        int16_t offset_q15, uint8_t *buffer, size_t capacity,
                        size_t *used);

enum cwg_error cwg_unpack(const uint8_t *buffer, size_t length,
                          struct cwg_header *header, int16_t *samples,
                          size_t sample_capacity);

#endif
