#ifndef MEASUREMENT_QUALITY_H
#define MEASUREMENT_QUALITY_H
#include <stddef.h>
#include <stdint.h>
enum measurement_quality_label { MQ_INVALID=0, MQ_PASS, MQ_WARNING, MQ_FAIL };
enum measurement_quality_flag { MQF_NONE=0, MQF_LOW_SIGNAL=1u<<0, MQF_CLIPPED=1u<<1, MQF_STALE=1u<<2, MQF_BAD_FREQUENCY=1u<<3, MQF_BASELINE_MISSING=1u<<4 };
struct measurement_quality_config { double min_rms, clip_fraction, warn_delta_db, fail_delta_db; uint64_t max_age_ms; };
struct measurement_quality_input { double rms, clipped_fraction, delta_db; double frequency_mhz; uint64_t age_ms; int baseline_valid; };
/* score is relative deviation severity: 0=no deviation, 1=fail threshold.
 * Score is not meaningful when label is INVALID. */
struct measurement_quality_result { enum measurement_quality_label label; unsigned flags; double score; char reason[64]; };
struct measurement_quality_config measurement_quality_default(void);
int measurement_quality_evaluate(const struct measurement_quality_config *, const struct measurement_quality_input *, struct measurement_quality_result *);
const char *measurement_quality_label_name(enum measurement_quality_label);
#endif
