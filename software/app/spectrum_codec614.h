#ifndef SPECTRUM_CODEC614_H
#define SPECTRUM_CODEC614_H
#include "spectrum_evidence613.h"
#define SM614_BIN_MAX 300000u
#define SM614_CSV_MAX 300000u
typedef int (*sm614_emit)(void *user,const void *data,size_t bytes);
/* Predicate: 1=valid CLOSED SM613 event, 0=invalid; no output on invalid input.
 * Encoders return 0 only after every callback returned 0; otherwise -1.
 * Output is canonical, explicit fields, not compiler struct layout. */
int sm614_event_valid(const struct sm613_event *event);
int sm614_encode_bin(const struct sm613_event *event,sm614_emit emit,void *user);
int sm614_encode_csv(const struct sm613_event *event,sm614_emit emit,void *user);
#endif
