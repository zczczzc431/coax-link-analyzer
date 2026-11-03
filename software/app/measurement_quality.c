#include "measurement_quality.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
struct measurement_quality_config measurement_quality_default(void){return (struct measurement_quality_config){1.0,0.01,1.0,3.0,1000};}
const char *measurement_quality_label_name(enum measurement_quality_label x){switch(x){case MQ_PASS:return "PASS";case MQ_WARNING:return "WARNING";case MQ_FAIL:return "FAIL";default:return "INVALID";}}
int measurement_quality_evaluate(const struct measurement_quality_config *c,const struct measurement_quality_input *in,struct measurement_quality_result *out){
 if(!out)return -1;
 memset(out,0,sizeof(*out)); strcpy(out->reason,"INVALID_ARGUMENT");
 if(!c||!in||!isfinite(c->min_rms)||c->min_rms<0||!isfinite(c->clip_fraction)||c->clip_fraction<0||c->clip_fraction>1||!isfinite(c->warn_delta_db)||c->warn_delta_db<0||!isfinite(c->fail_delta_db)||c->fail_delta_db<=0||c->fail_delta_db<c->warn_delta_db||!c->max_age_ms||!isfinite(in->rms)||in->rms<0||!isfinite(in->clipped_fraction)||in->clipped_fraction<0||in->clipped_fraction>1||(in->baseline_valid!=0&&in->baseline_valid!=1)||!isfinite(in->delta_db))return -1;
 memset(out,0,sizeof(*out));
 if(!isfinite(in->frequency_mhz)||in->frequency_mhz<=0)out->flags|=MQF_BAD_FREQUENCY;
 if(!in->baseline_valid)out->flags|=MQF_BASELINE_MISSING;
 if(in->rms<c->min_rms)out->flags|=MQF_LOW_SIGNAL;
 if(in->clipped_fraction>c->clip_fraction)out->flags|=MQF_CLIPPED;
 if(in->age_ms>c->max_age_ms)out->flags|=MQF_STALE;
 if(out->flags){out->label=MQ_INVALID;snprintf(out->reason,sizeof(out->reason),"%s%s%s%s%s",(out->flags&MQF_BASELINE_MISSING)?"BASELINE ":"",(out->flags&MQF_LOW_SIGNAL)?"LOW_SIGNAL ":"",(out->flags&MQF_CLIPPED)?"CLIPPED ":"",(out->flags&MQF_STALE)?"STALE ":"",(out->flags&MQF_BAD_FREQUENCY)?"BAD_FREQUENCY":"");return 0;}
 double d=fabs(in->delta_db); out->score=d>=c->fail_delta_db?1.0:d/c->fail_delta_db;
 if(d>=c->fail_delta_db){out->label=MQ_FAIL;strcpy(out->reason,"DELTA_FAIL");}
 else if(d>=c->warn_delta_db){out->label=MQ_WARNING;strcpy(out->reason,"DELTA_WARNING");}
 else {out->label=MQ_PASS;strcpy(out->reason,"WITHIN_LIMIT");}
 return 0;
}
