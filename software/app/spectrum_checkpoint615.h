#ifndef SPECTRUM_CHECKPOINT615_H
#define SPECTRUM_CHECKPOINT615_H
#include "spectrum_store614.h"
#define SM615_NAME_MAX 80u
/* Immutable, single-owner view between feed calls. No core state is persisted.
 * observed_* are the latest ACCEPTED ADC observation, not the newest retained
 * raw frame (post slots and WORST may stop changing). snapshot_us is host time.
 * Caller supplies a new generation; exact same-generation retries are allowed. */
struct sm615_view {
 const struct sm613_event *event;
 uint64_t generation,snapshot_us;
 uint32_t observed_token;
 uint64_t observed_us;
};
struct sm615_store { struct sm614_store files; };
struct sm615_found {
 uint64_t generation,snapshot_us;
 uint32_t observed_token;
 uint64_t observed_us;
 char bin_name[SM615_NAME_MAX],csv_name[SM615_NAME_MAX],index_name[SM615_NAME_MAX];
};
/* Predicate 1 valid ACTIVE snapshot, 0 invalid. Encoding 0 success/-1 failure.
 * CLOSED events remain exclusive to SM614. No output if view is invalid. */
int sm615_view_valid(const struct sm615_view *view);
int sm615_encode_bin(const struct sm615_view *view,sm614_emit emit,void *user);
int sm615_encode_csv(const struct sm615_view *view,sm614_emit emit,void *user);
/* Dedicated owned 0700 root, different from CLOSED store. max_snapshots counts
 * committed generations, NOT confirmed/closed events. Same 614 status/faults.
 * No mkdir, deletion, promotion, automatic acquisition or checkpoint cadence. */
int sm615_open(struct sm615_store *store,const char *directory,
 unsigned max_snapshots,uint64_t max_bytes);
void sm615_close(struct sm615_store *store);
void sm615_inject(struct sm615_store *store,enum sm614_fault fault,uint64_t after);
int sm615_scan(struct sm615_store *store,struct sm614_scan *out);
int sm615_save(struct sm615_store *store,const struct sm615_view *view);
/* Verify WHOLE directory first. 1=latest complete hash-verified generation,
 * 0=none, -1=failure. Returns filenames/metadata, never a runnable core state.
 * Incomplete higher generations are ignored, corrupt committed ones fail
 * closed (no silent fallback). No automatic ARM or fake close on reboot. */
int sm615_latest(struct sm615_store *store,uint64_t session,uint64_t capture,
 struct sm615_found *out);
#endif
