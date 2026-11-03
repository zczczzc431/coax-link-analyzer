#ifndef CUSTOM513_UI_MODEL_H
#define CUSTOM513_UI_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CUI_MAX_EXPR 256u
#define CUI_PACKAGE_CAPACITY (40u + 1024u * sizeof(int16_t))

enum cui_state { CUI_EMPTY = 0, CUI_EDITING, CUI_READY, CUI_ERROR };

/* The UI model knows nothing about formula parsing or MMIO.  A caller supplies
 * the already-reviewed formula->package callback from custom511. */
typedef int (*cui_build_package_fn)(void *context, const char *expression,
                                    uint32_t frequency_hz, int32_t amplitude_q15,
                                    int16_t offset_q15, uint32_t transaction,
                                    uint8_t *package, size_t capacity,
                                    size_t *used);

struct cui_model {
    char expression[CUI_MAX_EXPR + 1u];
    size_t expression_length;
    uint32_t frequency_hz;
    int32_t amplitude_q15;
    int16_t offset_q15;
    uint32_t transaction;
    enum cui_state state;
    int build_error;
    uint8_t package[CUI_PACKAGE_CAPACITY];
    size_t package_used;
};

void cui_init(struct cui_model *model);
bool cui_append(struct cui_model *model, char token);
bool cui_backspace(struct cui_model *model);
void cui_clear(struct cui_model *model);
bool cui_set_frequency(struct cui_model *model, uint32_t frequency_hz);
bool cui_set_amplitude(struct cui_model *model, int32_t amplitude_q15);
bool cui_set_offset(struct cui_model *model, int32_t offset_q15);
int cui_build(struct cui_model *model, cui_build_package_fn build,
              void *context);

#endif
