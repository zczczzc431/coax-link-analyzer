#include "measurement_quality_report.h"
int measurement_quality_csv(FILE *fp,const struct measurement_quality_result *r){
 if(!fp||!r||r->label<MQ_INVALID||r->label>MQ_FAIL)return -1;
 if(fprintf(fp,"section,measurement_quality\nlabel,flags,score,reason\n%s,%u,%.6f,%s\n",measurement_quality_label_name(r->label),r->flags,r->score,r->reason)<0)return -1;
 return ferror(fp)?-1:0;
}
