#include "spectrum_qualification654.h"
#include <float.h>
#include <math.h>
#include <string.h>

void sm654_defaults(struct sm654_policy *p) {
 if(p) {p->radius=32;p->guard=3;p->peak_radius=2;p->min_training=8;p->contrast_db=12;}
}
static double db654(double p) {return p>0?10*log10(p):-300;}
static unsigned distance654(unsigned a,unsigned b) {return a>b?a-b:b-a;}
int sm654_select(const struct sm612_config *c,const struct sm654_policy *p,
                 const double *power,size_t bins,struct sm654_selection *out) {
 if(!c || !p || !power || !out) return -1;
 memset(out,0,sizeof *out);
 if(bins<5 || bins>SM612_MAX_BINS || c->first_bin<1 || c->last_bin>=bins ||
    c->first_bin>c->last_bin || !isfinite(c->floor_db) || c->floor_db<-200 || c->floor_db>0 ||
    !isfinite(c->lower_signal_margin_db) || c->lower_signal_margin_db<0 || c->lower_signal_margin_db>120 ||
    p->radius<1 || p->radius>=SM612_MAX_BINS || p->guard>=p->radius ||
    p->peak_radius<1 || p->peak_radius>p->guard || p->min_training<1 ||
    p->min_training>2*p->radius || !isfinite(p->contrast_db) || p->contrast_db<0 || p->contrast_db>120)
  return -1;
 for(size_t k=0;k<SM612_MAX_BINS;k++) if(c->exclude[k]>1) return -1;
 for(size_t k=0;k<bins;k++) {
  if(!isfinite(power[k]) || power[k]<0) return -1;
  out->baseline_db[k]=db654(power[k]);out->background_db[k]=-300;
 }
 unsigned char seed[SM612_MAX_BINS]={0},guarded[SM612_MAX_BINS]={0};
 double peak=-DBL_MAX;unsigned included=0;
 const double absolute_gate=c->floor_db+c->lower_signal_margin_db;
 for(unsigned k=c->first_bin;k<=c->last_bin;k++) {
  if(c->exclude[k]) {out->coverage[k]=SM654_EXCLUDED;continue;}
  included++;
  if(out->baseline_db[k]>peak) {peak=out->baseline_db[k];out->dominant_bin=k;}
  if(out->baseline_db[k]<absolute_gate) {out->coverage[k]=SM654_BELOW_ABSOLUTE;continue;}
  unsigned lo=k>p->peak_radius?k-p->peak_radius:0,hi=k+p->peak_radius;
  if(lo<c->first_bin) lo=c->first_bin;
  if(hi>c->last_bin) hi=c->last_bin;
  int maximum=1;
  for(unsigned j=lo;j<=hi;j++) {
   if(c->exclude[j]) continue;
   if(power[j]>power[k] || (j<k && power[j]==power[k])) {maximum=0;break;}
  }
  if(!maximum) {out->coverage[k]=SM654_NOT_PEAK;continue;}
  seed[k]=1;out->seeds++;
 }
 if(!included) return -1;
 /* Remove ALL initially identified peaks and their guard regions from the
  * reference pool. Otherwise a nearby strong fundamental hides a real weak
  * harmonic in the background estimate. Decisions never use temporal spread.
  */
 for(unsigned k=c->first_bin;k<=c->last_bin;k++) if(seed[k]) {
  unsigned lo=k>p->guard?k-p->guard:0,hi=k+p->guard;
  if(lo<c->first_bin) lo=c->first_bin;
  if(hi>c->last_bin) hi=c->last_bin;
  for(unsigned j=lo;j<=hi;j++) guarded[j]=1;
 }
 for(unsigned k=c->first_bin;k<=c->last_bin;k++) if(seed[k]) {
  unsigned lo=k>p->radius?k-p->radius:0,hi=k+p->radius,count=0;
  if(lo<c->first_bin) lo=c->first_bin;
  if(hi>c->last_bin) hi=c->last_bin;
  double mean=0;
  for(unsigned j=lo;j<=hi;j++) {
   if(c->exclude[j] || guarded[j] || distance654(j,k)<=p->guard) continue;
   count++;mean+=(power[j]-mean)/count;
  }
  out->training_count[k]=count;
  if(!isfinite(mean) || mean<0) return -1;
  out->background_db[k]=db654(mean);
  if(count<p->min_training) {
   out->coverage[k]=SM654_INSUFFICIENT_REFERENCE;out->insufficient++;continue;
  }
  double reference=fmax(c->floor_db,out->background_db[k]);
  if(out->baseline_db[k]<reference+p->contrast_db) {
   out->coverage[k]=SM654_LOW_CONTRAST;out->low_contrast++;continue;
  }
  out->coverage[k]=SM654_SUPPORTED;out->qualified++;
 }
 return 0;
}
