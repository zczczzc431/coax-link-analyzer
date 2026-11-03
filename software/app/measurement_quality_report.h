#ifndef MEASUREMENT_QUALITY_REPORT_H
#define MEASUREMENT_QUALITY_REPORT_H
#include "measurement_quality.h"
#include <stdio.h>
int measurement_quality_csv(FILE *, const struct measurement_quality_result *);
#endif
