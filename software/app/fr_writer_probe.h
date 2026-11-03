#ifndef FR_WRITER_PROBE_H
#define FR_WRITER_PROBE_H
#include <stdint.h>
#include <sys/types.h>

enum fr_writer_error {
    FR_WRITER_ARGS=1u, FR_WRITER_IDENTITY=2u, FR_WRITER_PROC=4u,
    FR_WRITER_LOCK=8u, FR_WRITER_CLOSE=16u, FR_WRITER_CLOCK=32u,
    FR_WRITER_TIMEOUT=64u
};
struct fr_writer_probe_config {
    /* Borrowed, hash-verified regular ELF FDs; never closed by the probe.
     * Hash verification is NOT performed here. reader_fd identifies self.
     * Production root must be /proc with full visibility; override is only
     * for offline fixtures, not a user-facing configurable measurement. */
    int tool_fd,reader_fd;
    const char *tool_path,*proc_root,*lock_path;
    pid_t self_pid;
};
struct fr_writer_snapshot {
    unsigned errors,known_writers,processes;
    pid_t first_writer;
    int self_seen,lock_was_free;
};
/* Reads proc/executable metadata and briefly tries an EXISTING advisory lock.
 * No create/unlink/write, exec, signal, device access or sleep. Never makes
 * DAC OFF, never holds a session lock, never reaps another process.
 * Matching inode catches pre-exec reader children and deleted executables;
 * name checks also catch ordinary dacctl/scope copies. This is not a security
 * scanner for arbitrary renamed root MMIO writers or hidden PID namespaces.
 * Syscalls may block in the kernel; this is a startup/diagnostic helper only,
 * NOT an interactive per-frame callback or a hard real-time guarantee. */
int fr_writer_probe(struct fr_writer_probe_config,struct fr_writer_snapshot *);

/* Separate startup helper: never called by the read-only observer or UI.
 * dirfd is a borrowed trusted local directory (production /run/lock, uid 0).
 * It must not be writable by group/other. Caller verifies its absolute path
 * and excludes other startup writers before permitting FIRST creation.
 * allow_first_create=1 is not a measurement permit and must never be used to
 * repair a missing/replaced lock during a session. Existing objects are never
 * truncated, chmod'ed, unlinked or replaced. New file mode is 0600 & ~umask.
 * Returns an owned CLOEXEC fd, UNLOCKED; close once at lifecycle end. A held
 * parent flock would block the existing dacctl's independent open/flock.
 * Success pins identity only, NOT exclusivity, writer absence or DAC OFF.
 * Production binding remains disabled; fixtures supply their own directory.
 */
int fr_writer_lock_open(int dirfd,uid_t owner,int allow_first_create,int *created);
/* Verify pinned descriptor still names the same permitted inode. No flock,
 * create or repair. Caller treats failure as sticky for the current session.
 * Trusted privileged code must not mutate lock directories concurrently;
 * metadata snapshots cannot defend against an arbitrary root writer. */
int fr_writer_lock_check(int dirfd,uid_t owner,int pinned_fd);

enum fr_writer_observation { FR_WRITER_FAILED=-1,FR_WRITER_WAIT=0,
                             FR_WRITER_QUIET_OBSERVED=1 };
struct fr_writer_watch {
    uint64_t deadline_us,quiet_us,last_us,quiet_since_us;
    unsigned errors;
    int seeded,quiet_started;
};
int fr_writer_watch_init(struct fr_writer_watch *,uint64_t now,
                          uint64_t budget_us,uint64_t quiet_us);
/* >=2 clean observations separated by quiet_us. This is NOT proof of a
 * continuously idle device: each probe releases its lock before returning.
 * Output is only evidence for a later owner/admission/recovery transaction,
 * not hardware permission. START remains disabled; ADC/exclusivity/tool
 * hash/PL/frontend/OFF requirements remain separate and mandatory. */
int fr_writer_watch_step(struct fr_writer_watch *,
                          const struct fr_writer_snapshot *,uint64_t now);
#endif
