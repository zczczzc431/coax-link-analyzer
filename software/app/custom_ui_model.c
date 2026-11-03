#include "custom_ui_model.h"

#include <string.h>

static void mark_editing(struct cui_model *m) {
    if (!m) return;
    m->state = m->expression_length ? CUI_EDITING : CUI_EMPTY;
    m->build_error = 0;
    m->package_used = 0;
}

void cui_init(struct cui_model *model) {
    if (!model) return;
    memset(model, 0, sizeof(*model));
    (void)strcpy(model->expression, "sin(2*pi*t)");
    model->expression_length = strlen(model->expression);
    model->frequency_hz = 1000000u;
    model->amplitude_q15 = 16384;
    model->offset_q15 = 0;
    model->transaction = 1u;
    model->state = CUI_EDITING;
}

bool cui_append(struct cui_model *model, char token) {
    unsigned char c = (unsigned char)token;
    if (!model || c < 0x20u || c > 0x7eu ||
        model->expression_length >= CUI_MAX_EXPR) return false;
    model->expression[model->expression_length++] = token;
    model->expression[model->expression_length] = '\0';
    mark_editing(model);
    return true;
}

bool cui_backspace(struct cui_model *model) {
    if (!model || model->expression_length == 0u) return false;
    model->expression[--model->expression_length] = '\0';
    mark_editing(model);
    return true;
}

void cui_clear(struct cui_model *model) {
    if (!model) return;
    model->expression[0] = '\0';
    model->expression_length = 0u;
    mark_editing(model);
}

bool cui_set_frequency(struct cui_model *model, uint32_t frequency_hz) {
    if (!model || frequency_hz < 1u || frequency_hz > 100000000u) return false;
    model->frequency_hz = frequency_hz;
    mark_editing(model);
    return true;
}

bool cui_set_amplitude(struct cui_model *model, int32_t amplitude_q15) {
    if (!model || amplitude_q15 < 0 || amplitude_q15 > 32767) return false;
    model->amplitude_q15 = amplitude_q15;
    mark_editing(model);
    return true;
}

bool cui_set_offset(struct cui_model *model, int32_t offset_q15) {
    if (!model || offset_q15 < -32768 || offset_q15 > 32767) return false;
    model->offset_q15 = (int16_t)offset_q15;
    mark_editing(model);
    return true;
}

int cui_build(struct cui_model *model, cui_build_package_fn build,
              void *context) {
    int result;
    if (!model || !build || model->expression_length == 0u) return -1;
    result = build(context, model->expression, model->frequency_hz,
                   model->amplitude_q15, model->offset_q15, model->transaction,
                   model->package, sizeof(model->package), &model->package_used);
    model->build_error = result;
    if (result == 0) {
        model->state = CUI_READY;
        if (model->transaction != UINT32_MAX) ++model->transaction;
    } else {
        model->state = CUI_ERROR;
        model->package_used = 0u;
    }
    return result;
}
