#ifndef CUSTOM_FORMULA_ENGINE_H
#define CUSTOM_FORMULA_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#define CF_MAX_EXPR 256u
#define CF_TABLE_LEN 1024u

enum cf_error {
    CF_OK = 0, CF_ERR_EMPTY, CF_ERR_CHAR, CF_ERR_SYNTAX, CF_ERR_DEPTH,
    CF_ERR_NODES, CF_ERR_DIVZERO, CF_ERR_NONFINITE, CF_ERR_RANGE,
    CF_ERR_PERIODIC, CF_ERR_SLOPE
};

struct cf_table {
    int16_t q15[CF_TABLE_LEN];
    uint32_t crc32;
    double minimum;
    double maximum;
    double scale;
};

const char *cf_error_string(enum cf_error error);
enum cf_error cf_generate(const char *expression, struct cf_table *table);
uint32_t cf_crc32(const void *data, size_t length);

#endif
