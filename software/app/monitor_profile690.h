#ifndef MONITOR_PROFILE690_H
#define MONITOR_PROFILE690_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
/* Host observation only. Nested stage totals must NOT be summed. No I/O until STOP. */
enum p690_stage {P690_WAIT,P690_GAP,P690_COPY,P690_MEASURE,P690_MASK,
 P690_FFT,P690_AUDIT,P690_BIND,P690_CLOSED,P690_CHECKPOINT,P690_PAINT,P690_LOOP,P690_FRAME,P691_FLUSH,P690_STAGES};
#define P690_BUCKETS 11
struct p690_stat {uint64_t n,total,max,hist[P690_BUCKETS];};
struct p690_profile {
 struct p690_stat stage[P690_STAGES];
 uint64_t bad,request_ns,last_done_ns;uint32_t request_token;
 int enabled,have_done,seen_request;
};
static struct p690_profile p690;
static int p690_selected;
#ifndef P690_CLOCK
#define P690_CLOCK(t) clock_gettime(CLOCK_MONOTONIC,(t))
#endif
static inline void p690_bad(void) {if(p690.bad<UINT64_MAX)p690.bad++;}
static inline uint64_t p690_now(void) {
 if(!p690.enabled)return UINT64_MAX;
 struct timespec t;
 if(P690_CLOCK(&t)!=0 || t.tv_sec<0 || t.tv_nsec<0 || t.tv_nsec>=1000000000L ||
    (uint64_t)t.tv_sec>(UINT64_MAX-(uint64_t)t.tv_nsec)/UINT64_C(1000000000)) {
  p690_bad();return UINT64_MAX;
 }
 return (uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;
}
static inline void p690_span_at(unsigned stage,uint64_t start,uint64_t end) {
 static const uint64_t upper_ns[P690_BUCKETS-1]={1000,10000,100000,500000,1000000,
  5000000,10000000,50000000,200000000,1000000000};
 if(!p690.enabled)return;
 if(stage>=P690_STAGES || start==UINT64_MAX || end==UINT64_MAX || end<start) {p690_bad();return;}
 struct p690_stat *s=&p690.stage[stage];uint64_t d=end-start;unsigned b=0;
 while(b<P690_BUCKETS-1 && d>upper_ns[b])b++;
 if(s->n==UINT64_MAX || s->hist[b]==UINT64_MAX || s->total>UINT64_MAX-d) {p690_bad();return;}
 s->n++;s->total+=d;s->hist[b]++;if(d>s->max)s->max=d;
}
static inline void p690_end(unsigned stage,uint64_t start) {
 if(start!=UINT64_MAX)p690_span_at(stage,start,p690_now());
}
static inline void p690_start(void) {
 memset(&p690,0,sizeof p690);p690.enabled=p690_selected!=0;
}
static inline void p690_request_at(uint32_t token,uint64_t now) {
 if(!p690.enabled)return;
 if(!token || now==UINT64_MAX) {p690_bad();return;}
 if(p690.request_token)p690_bad();
 p690.request_token=token;p690.request_ns=now;p690.seen_request=1;
}
static inline void p690_done_at(uint32_t token,uint64_t now) {
 if(!p690.enabled)return;
 if(!p690.seen_request)return; /* An already in-flight pre-ARM frame is not timed. */
 if(!token || token!=p690.request_token || now==UINT64_MAX) {p690_bad();return;}
 p690_span_at(P690_WAIT,p690.request_ns,now);
 if(p690.have_done)p690_span_at(P690_GAP,p690.last_done_ns,now);
 p690.last_done_ns=now;p690.have_done=1;p690.request_token=0;
}
static inline void p690_report(FILE *f,const char *reason) {
 static const char *names[P690_STAGES]={"REQUEST_TO_DONE_OBS","DONE_GAP","RAW_COPY","MEASURE",
 "MASK_TOTAL","FFT","AUDIT","BIND","CLOSED_SAVE","CHECKPOINT_SAVE","PAINT","LOOP","FRAME_TOTAL","FB_FLUSH691"};
 if(!p690.enabled)return;
 p690.enabled=0; /* Printing cannot add itself to the measured session. */
 fprintf(f,"PROFILE690_BEGIN REASON=%s BAD=%llu INFLIGHT=%u UNIT=NS HOST_ONLY=1 NESTED=1\n",
  reason,(unsigned long long)p690.bad,p690.request_token);
 for(unsigned i=0;i<P690_STAGES;i++) {
  const struct p690_stat *s=&p690.stage[i];
  fprintf(f,"PROFILE690 STAGE=%s N=%llu TOTAL_NS=%llu MAX_NS=%llu HIST=",names[i],
   (unsigned long long)s->n,(unsigned long long)s->total,(unsigned long long)s->max);
  for(unsigned b=0;b<P690_BUCKETS;b++)fprintf(f,"%s%llu",b?",":"",(unsigned long long)s->hist[b]);
  fputc('\n',f);
 }
 fprintf(f,"PROFILE690_END BUCKET_UPPER_NS=1000,10000,100000,500000,1000000,5000000,10000000,50000000,200000000,1000000000,INF\n");
}
#endif
