#ifndef FR_PLATFORM_FILES_H
#define FR_PLATFORM_FILES_H
#include "fr_dac_job.h"
enum fr_files_error {FR_FILES_PATH=1u,FR_FILES_TOOL=2u,FR_FILES_LOCK=4u,FR_FILES_CLOSE=8u};
struct fr_platform_files {
    int started,ready;
    unsigned errors;
    int root_fd,bin_fd,lockdir_fd,tool_fd,lock_fd;
    uid_t owner;
    struct fr_dac_image image;
};
/* Zero-init ONCE. Fixed root-owned /usr/local/bin/dacctl and /run/lock.
 * Startup only: SHA + filesystem I/O, not a per-frame/touch callback.
 * No create, chmod, flock, exec, MMIO or GPIO; no hardware permission.
 * Refuses symlink directory components and group/other writable directories.
 * Failure closes ONLY descriptors acquired here, leaves existing files alone.
 * A ready result pins files, NOT writer absence or session exclusivity. */
int fr_platform_files_open(struct fr_platform_files *);
int fr_platform_files_check(struct fr_platform_files *);
/* Caller must establish that owner/ADC/OFF cleanup no longer borrows the
 * descriptors. borrowers_done!=1 refuses closure without modifying state.
 * Never retries close or deletes lock files. Closed contexts are not reopened. */
int fr_platform_files_close(struct fr_platform_files *,int borrowers_done);
#ifdef FR_PLATFORM_FILES_TEST
/* Isolated fixtures only; not compiled into the production ARM. */
int fr_platform_files_test_open(struct fr_platform_files *,int rootfd,uid_t owner,
                                 const char *expected_sha);
#endif
#endif
