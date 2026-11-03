#include "measurement_quality.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void){
 struct measurement_quality_config c=measurement_quality_default();struct measurement_quality_input in={10,.0,.2,10,10,1};struct measurement_quality_result r;
 assert(measurement_quality_evaluate(&c,&in,&r)==0&&r.label==MQ_PASS);
 in.delta_db=1.2;assert(measurement_quality_evaluate(&c,&in,&r)==0&&r.label==MQ_WARNING);
 in.delta_db=3.2;assert(measurement_quality_evaluate(&c,&in,&r)==0&&r.label==MQ_FAIL);
 in.rms=.1;assert(measurement_quality_evaluate(&c,&in,&r)==0&&r.label==MQ_INVALID&&(r.flags&MQF_LOW_SIGNAL));in.rms=10;in.age_ms=1001;assert(measurement_quality_evaluate(&c,&in,&r)==0&&r.label==MQ_INVALID&&(r.flags&MQF_STALE));
 in.age_ms=0;in.baseline_valid=0;assert(measurement_quality_evaluate(&c,&in,&r)==0&&r.label==MQ_INVALID&&(r.flags&MQF_BASELINE_MISSING));
 in.baseline_valid=1;in.clipped_fraction=.02;assert(measurement_quality_evaluate(&c,&in,&r)==0&&r.label==MQ_INVALID&&(r.flags&MQF_CLIPPED));
 c.fail_delta_db=.1; c.warn_delta_db=1;assert(measurement_quality_evaluate(&c,&in,&r)==-1);in.frequency_mhz=NAN;assert(measurement_quality_evaluate(&c,&in,&r)==-1);
 c=measurement_quality_default();in=(struct measurement_quality_input){10,0,0,10,0,1};
 assert(measurement_quality_evaluate(&c,&in,&r)==0&&r.score==0);
 in.delta_db=3;assert(measurement_quality_evaluate(&c,&in,&r)==0&&r.score==1);
 in.delta_db=30;assert(measurement_quality_evaluate(&c,&in,&r)==0&&r.score==1);
 in.clipped_fraction=-0.1;assert(measurement_quality_evaluate(&c,&in,&r)==-1&&r.label==MQ_INVALID);
 in.clipped_fraction=1.1;assert(measurement_quality_evaluate(&c,&in,&r)==-1);
 in.clipped_fraction=0;in.frequency_mhz=NAN;assert(measurement_quality_evaluate(&c,&in,&r)==0&&(r.flags&MQF_BAD_FREQUENCY));
 in.frequency_mhz=10;c.warn_delta_db=0;c.fail_delta_db=0;assert(measurement_quality_evaluate(&c,&in,&r)==-1);
 puts("MEASUREMENT_QUALITY_PASS limits_invalid_provenance");return 0;
}
