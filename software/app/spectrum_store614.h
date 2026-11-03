#ifndef SPECTRUM_STORE614_H
#define SPECTRUM_STORE614_H
#include "spectrum_codec614.h"
enum sm614_status { SM614_OK,SM614_BAD_EVENT,SM614_IO,SM614_CONFLICT,
 SM614_QUOTA,SM614_CORRUPT,SM614_LOCKED,SM614_BAD_ARGUMENT };
enum sm614_fault { SM614_FAULT_NONE,SM614_FAULT_WRITE,SM614_FAULT_FILE_SYNC,
 SM614_FAULT_LINK,SM614_FAULT_DIR_SYNC,SM614_FAULT_INDEX_WRITE,
 SM614_FAULT_INDEX_SYNC,SM614_FAULT_INDEX_LINK,SM614_FAULT_INDEX_DIR_SYNC };
struct sm614_scan { uint64_t bytes; unsigned files,complete_events,incomplete_files; };
struct sm614_store {
 int dirfd,lockfd;
 unsigned max_events,max_files;
 uint64_t max_bytes,temporary_seq;
 enum sm614_status status;
 enum sm614_fault fault;
 uint64_t fail_after; /* WRITE: newly written bin+csv bytes in this save;
                       INDEX_WRITE: newly written idx bytes. Reused published
                       bytes consume no budget. Each save starts at zero. */
};
/* Existing private owned 0700 directory, same local POSIX filesystem, supporting
 * hard-link no-replace publication, flock and file/directory fsync. No mkdir,
 * deletion/rotation, symlink following, installation or production file access.
 * One serialized owner; inputs must remain immutable during save. */
int sm614_open(struct sm614_store *store,const char *directory,
 unsigned max_events,uint64_t max_bytes);
void sm614_close(struct sm614_store *store);
/* Validates every complete index and both payload digests. Incomplete regular
 * files are counted, NEVER considered complete and NEVER auto-deleted.
 * Returns -1/SM614_CORRUPT for invalid committed records or unsafe entries. */
int sm614_scan(struct sm614_store *store,struct sm614_scan *out);
/* Direct SM613 sink: 0=durably accepted exact CLOSED event, -1=failure.
 * Repeated identical session/capture succeeds without replacing any file.
 * Same name/different content returns CONFLICT; idx published LAST. */
int sm614_save(void *store,const struct sm613_event *event);
/* Explicit offline failure injection, default NONE; never exposed in UI. */
void sm614_inject(struct sm614_store *store,enum sm614_fault fault,uint64_t after);
#endif
