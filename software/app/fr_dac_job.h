#ifndef FR_DAC_JOB_H
#define FR_DAC_JOB_H
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>

#define FR_DAC_C827_SHA256 "c82787f92394269364c24b75e68670bca9cdf757930a15c0e318747324897e12"
#define FR_DAC_IMAGE_MAX_BYTES 1048576u
struct fr_dac_image {
    int fd,valid; /* Borrowed: caller retains fd through OFF/cleanup. */
    struct stat metadata;
    char sha256[65];
};
/* Startup only: bounded regular-file pread, preserves offset, no shell/exec.
 * On failure digest[0]=0. Cannot promise a hard kernel I/O deadline. */
int fr_dac_digest_fd(int fd,char digest[65]);
/* Verifies expected lowercase SHA and owner against this same ELF fd.
 * Zero-init a fresh output, never overwrite an active session's image.
 * Caller controls inode content and FD lifetime; no defense against arbitrary
 * concurrent root modifications or forged internal C structs. */
int fr_dac_image_verify(int fd,uid_t owner,const char *expected,struct fr_dac_image *);
int fr_dac_image_check(const struct fr_dac_image *);

#define FR_DAC_TIMEOUT_US 2000000u
#define FR_DAC_KILL_GRACE_US 100000u
enum fr_dac_error {
    FR_DAC_ARGS=1u, FR_DAC_SPAWN=2u, FR_DAC_WAIT=4u, FR_DAC_EXIT=8u,
    FR_DAC_TIMEOUT=16u, FR_DAC_CANCEL=32u, FR_DAC_CLOCK=64u,
    FR_DAC_SIGNAL=128u
};
struct fr_dac_command { int on; uint32_t frequency_hz; double amplitude_pct; };
struct fr_dac_job {
    pid_t pid;
    uint64_t last_us, deadline_us, kill_us;
    unsigned errors;
    int status, term_sent, kill_sent, started;
};
/* Zero-initialize ONCE. No shell, no waitpid(...,0), no sleep. Caller polls
 * from its event loop until this exact child is reaped, including after STOP.
 * permit=1 is caller's verified ownership/hardware permission, NOT detection.
 * Command success is not proof of physical output, and killing dacctl is NOT
 * DAC OFF. Before an OFF job, drain the preceding job; never overlap writers.
 * Hardware use is disabled by default; the guarded async RAW/PS contract must
 * explicitly confirm this job before sampling and drain it before OFF. */
int fr_dac_job_start(struct fr_dac_job *, struct fr_dac_command,
                     int permit, uint64_t now_us);
/* FD-bound variant for later platform admission. Caller must hash-verify the
 * SAME borrowed read-only CLOEXEC ELF fd, control its owner/content and keep it valid
 * until this call returns. No pathname fallback; fexecve executes this inode.
 * This helper checks ELF magic/metadata, NOT SHA or hardware ownership.
 * The forked child inherits its own reference; caller may close after return.
 * Trusted root must not rewrite executable bytes concurrently. Dynamic loader
 * and libraries remain trusted platform dependencies. Platform admission must
 * supply this FD; ordinary execution remains disabled without that admission. */
int fr_dac_job_start_fd(struct fr_dac_job *,struct fr_dac_command,
                        int permit,uint64_t now_us,int verified_elf_fd);
int fr_dac_job_start_image(struct fr_dac_job *,struct fr_dac_command,
                           int permit,uint64_t now_us,const struct fr_dac_image *);
/* 0=in progress, 1=reaped success, -1=terminal error/unconfirmed child.
 * An error while pid>0 still requires polling: no early ownership release. */
int fr_dac_job_poll(struct fr_dac_job *, uint64_t now_us);
int fr_dac_job_cancel(struct fr_dac_job *, uint64_t now_us);
int fr_dac_job_busy(const struct fr_dac_job *);
/* Pure formatting, preserving existing sine <=30MHz / NCO >30MHz mapping,
 * 0.001MHz resolution and fractional digital amplitude. REFLECT only. */
int fr_dac_command_format(struct fr_dac_command, char frequency[16],
                          char amplitude[24], const char **wave);
#endif
