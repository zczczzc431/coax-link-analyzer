#ifndef MASK_AUDIT655_H
#define MASK_AUDIT655_H
#include "spectrum_mask612.h"
#include "spectrum_evidence613.h"

#define SM655_AUDIT_FORMAT UINT32_C(0x06550001)
#define SM655_AUDIT_NAME_MAX 128u
#define SM655_AUDIT_MAX_FILES 64u
#define SM655_AUDIT_MAX_BYTES UINT64_C(33554432)

struct sm655_receipt {
 int valid;
 uint32_t format,learn_token;
 uint64_t learn_command,bytes;
 unsigned upper_count,lower_count,qualified_count,dominant_bin;
 char basename[SM655_AUDIT_NAME_MAX],sha256[65],template_sha256[65];
};
/* Serialized caller. rootDir is an absolute POSIX path to a dedicated private
 * directory. Only its last component may be created. Every path component is
 * opened without following symlinks. No existing evidence is overwritten or
 * removed. SHA establishes integrity/provenance, not authentication.
 * write accepts a complete READY dual-limit654 template and clears out on ANY
 * failure. learn_token is the frozen LEARN-end token, never a live watermark.
 */
int sm655_audit_write(const char *rootDir,const struct sm612_model *model,
 uint64_t learn_command,uint32_t learn_end_token,struct sm655_receipt *out);
/* Rebuilds canonical bytes from immutable learning fields and frozen receipt
 * command/token, then verifies the exact on-disk content. Runtime state,
 * reason, frame watermarks, timestamps and event counters are not provenance.
 * Return 0 is required before using a receipt; valid alone is insufficient.
 */
int sm655_audit_check(const char *rootDir,const struct sm612_model *model,
 const struct sm655_receipt *receipt);
/* Must succeed BEFORE the corresponding old-format event/checkpoint save.
 * The capture key remains stable while candidate/event_id changes. */
int sm655_audit_bind(const char *rootDir,const struct sm613_event *event,
 const struct sm655_receipt *receipt);

#ifdef MASK655_TESTING
enum sm655_test_failure { SM655_FAIL_NONE,SM655_FAIL_FILE_FSYNC,
 SM655_FAIL_FILE_CLOSE,SM655_FAIL_LINK,SM655_FAIL_UNLINK,SM655_FAIL_DIR_FSYNC,
 SM655_FAIL_PARENT_FSYNC,SM655_CRASH_PRELINK,SM655_CRASH_POSTLINK,SM655_CRASH_AFTER_CREATE };
void sm655_audit_test_fail(enum sm655_test_failure stage);
#endif
#endif
