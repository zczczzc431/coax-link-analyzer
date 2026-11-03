#ifndef MASK_LEARN_DIAG687_H
#define MASK_LEARN_DIAG687_H
/* Diagnostic observer only. No MMIO, threshold changes or per-frame disk I/O.
 * Snapshot RAW before the existing classifier; emit only after learning exits.
 * Allocation/export failure must never change the classifier's decision. */
#include "spectrum_mask612.h"
#include "spectrum_qualification654.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define SD687_FRAMES 128u
#define SD687_SAMPLES 2048u
struct sd687_frame {
 uint32_t token; uint64_t us; size_t samples;
 uint32_t raw[SD687_SAMPLES];
};
struct sd687_buffer {
 unsigned count,dropped;
 struct sd687_frame frame[SD687_FRAMES];
};
static struct sd687_buffer *sd687;
static void sd687_clear(void) {free(sd687);sd687=NULL;}
static void sd687_begin(void) {
 sd687_clear();sd687=calloc(1,sizeof *sd687);
 if(!sd687) printf("MASK687_DISABLED REASON=DIAGNOSTIC_ALLOCATION_ONLY\n");
}
static void sd687_capture(const uint32_t *raw,size_t n,uint32_t token,uint64_t us) {
 if(!sd687) return;
 if(!raw || n<8 || n>SD687_SAMPLES || sd687->count>=SD687_FRAMES) {sd687->dropped++;return;}
 struct sd687_frame *f=&sd687->frame[sd687->count++];
 f->token=token;f->us=us;f->samples=n;memcpy(f->raw,raw,n*sizeof *raw);
}
static int sd687_write(FILE *out,const struct sm612_model *m) {
 if(!out || !m || !sd687 || !m->learned || m->bins>SM612_MAX_BINS) return -1;
 struct sm654_policy policy;struct sm654_selection sel;
 double mean[SM612_MAX_BINS];
 sm654_defaults(&policy);
 for(size_t k=0;k<m->bins;k++) mean[k]=m->sum_power[k]/m->learned;
 int selected=sm654_select(&m->config,&policy,mean,m->bins,&sel);
 fprintf(out,"MASK687_BEGIN STATE=%u REASON=%u LEARNED=%u STORED=%u DROPPED=%u SELECT_RC=%d\n",
  (unsigned)m->state,(unsigned)m->reason,m->learned,sd687->count,sd687->dropped,selected);
 fprintf(out,"MASK687_ID SESSION=%llu INPUT=%llu SOURCE=%llu FS=%.17g FFT=%u FORMAT=%u LAST_TOKEN=%u LAST_US=%llu\n",
  (unsigned long long)m->identity.session,(unsigned long long)m->identity.input_signature,
  (unsigned long long)m->identity.source_signature,m->identity.fs_hz,m->identity.fft_size,m->identity.format,
  m->last_token,(unsigned long long)m->last_us);
 fprintf(out,"MASK687_CONFIG FIRST=%u LAST=%u FRAMES=%u SPREAD_DB=%.17g FLOOR_DB=%.17g LOWER_MARGIN=%.17g UPPER_DB=%.17g LOWER_DB=%.17g\n",
  m->config.first_bin,m->config.last_bin,m->config.learn_frames,m->config.max_learn_spread_db,
  m->config.floor_db,m->config.lower_signal_margin_db,m->config.upper_margin_db,m->config.lower_margin_db);
 if(selected==0) for(size_t k=0;k<m->bins;k++) {
  int fail=sel.coverage[k]==SM654_SUPPORTED &&
   m->max_db[k]-m->min_db[k]>m->config.max_learn_spread_db;
  fprintf(out,"MASK687_BIN %zu %.17g %.17g %.17g %.17g %u %u %d\n",k,
   sel.baseline_db[k],m->min_db[k],m->max_db[k],sel.background_db[k],
   (unsigned)sel.coverage[k],(unsigned)m->config.exclude[k],fail);
 }
 static const char hex[]="0123456789abcdef";
 char encoded[SD687_SAMPLES*8u+1u];
 for(unsigned j=0;j<sd687->count;j++) {
  const struct sd687_frame *f=&sd687->frame[j];
  for(size_t i=0;i<f->samples;i++) for(unsigned h=0;h<8;h++)
   encoded[i*8u+h]=hex[(f->raw[i]>>(28u-4u*h))&15u];
  encoded[f->samples*8u]=0;
  fprintf(out,"MASK687_RAW %u %llu %zu %s\n",f->token,(unsigned long long)f->us,f->samples,encoded);
 }
 fprintf(out,"MASK687_END RAW_UNCHANGED=1 DIAGNOSTIC_ONLY=1\n");
 return ferror(out)?-1:0;
}
static void sd687_finish(const struct sm612_model *m) {
 if(sd687 && m && m->state==SM612_INVALID) {
  if(sd687_write(stdout,m)!=0) printf("MASK687_EXPORT_FAILED DIAGNOSTIC_ONLY=1\n");
 }
 sd687_clear();
}
#endif
