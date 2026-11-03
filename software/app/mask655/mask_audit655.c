#define _GNU_SOURCE
#include "mask_audit655.h"
#include "spectrum_qualification654.h"
#include "fr_sha256_local.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/file.h>
#include <unistd.h>

#if DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024
#error "mask655 canonical doubles require IEEE-754 binary64"
#endif
_Static_assert(sizeof(double)==sizeof(uint64_t),"binary64 required");
#define AUDIT_CAP655 (512u*1024u)
struct sha655 { uint32_t h[8];uint64_t bytes;size_t used;unsigned char block[64]; };
struct text655 { char *data;size_t used; };
struct canonical655 { struct text655 text;struct sm655_receipt receipt; };
#ifdef MASK655_TESTING
static enum sm655_test_failure test_failure655;
void sm655_audit_test_fail(enum sm655_test_failure stage) {test_failure655=stage;}
static int injected655(enum sm655_test_failure stage) {
 if(test_failure655!=stage) return 0;
 test_failure655=SM655_FAIL_NONE;errno=EIO;return 1;
}
#define INJECT655(stage) injected655(stage)
#else
#define INJECT655(stage) 0
#endif
static void sha_init655(struct sha655 *s) {
 static const uint32_t initial[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
  0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
 memcpy(s->h,initial,sizeof initial);s->bytes=0;s->used=0;
}
static void sha_add655(struct sha655 *s,const void *data,size_t n) {
 const unsigned char *p=data;s->bytes+=(uint64_t)n;
 while(n) {
  size_t k=64-s->used;if(k>n) k=n;
  memcpy(s->block+s->used,p,k);s->used+=k;p+=k;n-=k;
  if(s->used==64) {fr_sha_block(s->h,s->block);s->used=0;}
 }
}
static void sha_end655(struct sha655 *s,char hex[65]) {
 static const char digits[]="0123456789abcdef";
 uint64_t bits=s->bytes*8;s->block[s->used++]=0x80;
 if(s->used>56) {memset(s->block+s->used,0,64-s->used);fr_sha_block(s->h,s->block);s->used=0;}
 memset(s->block+s->used,0,56-s->used);
 for(unsigned k=0;k<8;k++) s->block[63-k]=(unsigned char)(bits>>(8*k));
 fr_sha_block(s->h,s->block);
 for(unsigned k=0;k<32;k++) {
  unsigned char b=(unsigned char)(s->h[k/4]>>(24-8*(k%4)));
  hex[2*k]=digits[b>>4];hex[2*k+1]=digits[b&15];
 }
 hex[64]=0;
}
static uint64_t bits655(double value) {uint64_t out;memcpy(&out,&value,sizeof out);return out;}
static int add655(struct text655 *t,const char *format,...) {
 va_list ap;va_start(ap,format);
 int n=vsnprintf(t->data+t->used,AUDIT_CAP655-t->used,format,ap);va_end(ap);
 if(n<0 || (size_t)n>=AUDIT_CAP655-t->used) return -1;
 t->used+=(size_t)n;return 0;
}
static int name655(char *out,size_t cap,uint64_t session,uint64_t command,uint32_t token) {
 int n=snprintf(out,cap,"s%016" PRIx64 "_l%016" PRIx64 "_t%08" PRIx32 ".mask655",session,command,token);
 return n>0 && (size_t)n<cap?0:-1;
}
static int template655(struct text655 *t,const struct sm612_identity *id,const struct sm612_config *c,
 size_t bins,const double *baseline,const double *upper,const double *lower,const unsigned char *valid) {
 if(add655(t,"TEMPLATE655 1\nID %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %u %08" PRIx32
  "\nCFG %u %u %u %u %u %" PRIu64 " %d %d\nDB %016" PRIx64 " %016" PRIx64 " %016" PRIx64
  " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\nBINS %zu\n",
  id->session,id->input_signature,id->source_signature,bits655(id->fs_hz),id->fft_size,id->format,
  c->first_bin,c->last_bin,c->learn_frames,c->enter_frames,c->recover_frames,c->max_gap_us,c->upper_enabled,c->lower_enabled,
  bits655(c->upper_margin_db),bits655(c->lower_margin_db),bits655(c->floor_db),bits655(c->lower_signal_margin_db),
  bits655(c->min_learn_peak_db),bits655(c->max_learn_spread_db),bits655(c->hysteresis_db),bins)!=0) return -1;
 for(size_t k=0;k<SM612_MAX_BINS;k++) if(add655(t,"%u",(unsigned)c->exclude[k])!=0) return -1;
 if(add655(t,"\n")!=0) return -1;
 for(size_t k=0;k<bins;k++) if(add655(t,"%zu %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %u\n",
   k,bits655(baseline[k]),bits655(upper[k]),bits655(lower[k]),(unsigned)valid[k])!=0) return -1;
 return 0;
}
static void digest655(const struct text655 *t,char hex[65]) {
 struct sha655 sha;sha_init655(&sha);sha_add655(&sha,t->data,t->used);sha_end655(&sha,hex);
}
static int config655(const struct sm612_model *m) {
 const struct sm612_identity *id=&m->identity;const struct sm612_config *c=&m->config;
 if(!m->template_valid || !id->session || !isfinite(id->fs_hz) || id->fs_hz<=0 ||
    id->format!=SM612_FORMAT || id->fft_size<8 || id->fft_size>2048 ||
    (id->fft_size&(id->fft_size-1)) || m->bins!=id->fft_size/2+1u ||
    c->first_bin<1 || c->last_bin>=m->bins || c->first_bin>c->last_bin ||
    c->learn_frames<2 || c->learn_frames>128 || m->learned!=c->learn_frames ||
    c->enter_frames<1 || c->enter_frames>32 || c->recover_frames<1 || c->recover_frames>32 ||
    !c->max_gap_us || c->upper_enabled!=1 || c->lower_enabled!=1) return -1;
 const double values[]={c->upper_margin_db,c->lower_margin_db,c->floor_db,
  c->lower_signal_margin_db,c->min_learn_peak_db,c->max_learn_spread_db,c->hysteresis_db};
 for(size_t k=0;k<sizeof values/sizeof values[0];k++) if(!isfinite(values[k])) return -1;
 if(c->upper_margin_db<0 || c->upper_margin_db>120 || c->lower_margin_db<0 || c->lower_margin_db>120 ||
    c->floor_db<-200 || c->floor_db>0 || c->lower_signal_margin_db<0 || c->lower_signal_margin_db>120 ||
    c->min_learn_peak_db<-200 || c->min_learn_peak_db>0 || c->max_learn_spread_db<=0 ||
    c->max_learn_spread_db>120 || c->hysteresis_db<0 || c->hysteresis_db>c->upper_margin_db ||
    c->hysteresis_db>c->lower_margin_db) return -1;
 return 0;
}
static int canonical655(const struct sm612_model *m,uint64_t command,uint32_t token,struct canonical655 *out) {
 memset(out,0,sizeof *out);
 if(!m || !command || config655(m)!=0) return -1;
 struct sm654_policy policy;struct sm654_selection selection;double mean[SM612_MAX_BINS];
 sm654_defaults(&policy);
 for(size_t k=0;k<m->bins;k++) {
  if(!isfinite(m->sum_power[k]) || m->sum_power[k]<0) return -1;
  mean[k]=m->sum_power[k]/m->learned;
 }
 if(sm654_select(&m->config,&policy,mean,m->bins,&selection)!=0 || !selection.qualified ||
    selection.coverage[selection.dominant_bin]!=SM654_SUPPORTED) return -1;
 struct sm655_receipt *r=&out->receipt;const struct sm612_config *c=&m->config;
 r->format=SM655_AUDIT_FORMAT;r->learn_command=command;r->learn_token=token;
 r->qualified_count=selection.qualified;r->dominant_bin=selection.dominant_bin;
 double peak=-DBL_MAX;
 for(size_t k=0;k<m->bins;k++) {
  int included=k>=c->first_bin && k<=c->last_bin && !c->exclude[k];
  int supported=selection.coverage[k]==SM654_SUPPORTED;
  double baseline=selection.baseline_db[k],upper=fmax((supported?baseline:m->max_db[k])+c->upper_margin_db,c->floor_db);
  double lower=baseline-c->lower_margin_db;
  int lower_valid=included && supported && lower>=c->floor_db;
  if(!isfinite(m->min_db[k]) || !isfinite(m->max_db[k]) || m->min_db[k]>m->max_db[k] ||
     !isfinite(m->baseline_db[k]) || !isfinite(m->upper_db[k]) || !isfinite(m->lower_db[k]) ||
     m->baseline_db[k]!=baseline || m->upper_db[k]!=upper || m->lower_db[k]!=lower ||
     m->lower_valid[k]!=(unsigned char)lower_valid ||
     (included && supported && m->max_db[k]-m->min_db[k]>c->max_learn_spread_db)) return -1;
  if(included) {r->upper_count++;if(baseline>peak) peak=baseline;}
  if(lower_valid) r->lower_count++;
 }
 if(!r->lower_count || peak<c->min_learn_peak_db || name655(r->basename,sizeof r->basename,m->identity.session,command,token)!=0) return -1;
 out->text.data=malloc(AUDIT_CAP655);if(!out->text.data) return -1;
 struct text655 *t=&out->text;const struct sm612_identity *id=&m->identity;
 if(template655(t,id,c,m->bins,m->baseline_db,m->upper_db,m->lower_db,m->lower_valid)!=0) goto fail;
 digest655(t,r->template_sha256);t->used=0;
 if(add655(t,"SM655_LEARN_AUDIT 1\nDOUBLE IEEE754_BINARY64_HEX\nPOLICY %u R%u G%u PEAK%u MIN%u CONTRAST %016" PRIx64
  "\nLEARN %016" PRIx64 " %08" PRIx32 "\nIDENTITY %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %u %08" PRIx32
  "\nCONFIG %u %u %u %u %u %" PRIu64 " %d %d\nMARGINS %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64
  " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\nLEARNED %u BINS %zu\nCOVERAGE U%u L%u Q%u D%u SEEDS%u INSUFFICIENT%u LOW_CONTRAST%u\nEXCLUDE ",
  SM654_POLICY_VERSION,policy.radius,policy.guard,policy.peak_radius,policy.min_training,bits655(policy.contrast_db),command,token,
  id->session,id->input_signature,id->source_signature,bits655(id->fs_hz),id->fft_size,id->format,c->first_bin,c->last_bin,
  c->learn_frames,c->enter_frames,c->recover_frames,c->max_gap_us,c->upper_enabled,c->lower_enabled,
  bits655(c->upper_margin_db),bits655(c->lower_margin_db),bits655(c->floor_db),bits655(c->lower_signal_margin_db),
  bits655(c->min_learn_peak_db),bits655(c->max_learn_spread_db),bits655(c->hysteresis_db),m->learned,m->bins,
  r->upper_count,r->lower_count,r->qualified_count,r->dominant_bin,selection.seeds,selection.insufficient,selection.low_contrast)!=0) goto fail;
 for(size_t k=0;k<SM612_MAX_BINS;k++) if(add655(t,"%u",(unsigned)c->exclude[k])!=0) goto fail;
 if(add655(t,"\nBIN BASELINE UPPER LOWER LOWER_VALID SUM_POWER MIN MAX QUALIFICATION REFERENCES BACKGROUND\n")!=0) goto fail;
 for(size_t k=0;k<m->bins;k++) {
  if(add655(t,"%zu %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %u %016" PRIx64 " %016" PRIx64 " %016" PRIx64
   " %u %u %016" PRIx64 "\n",k,bits655(m->baseline_db[k]),bits655(m->upper_db[k]),bits655(m->lower_db[k]),
   (unsigned)m->lower_valid[k],bits655(m->sum_power[k]),bits655(m->min_db[k]),bits655(m->max_db[k]),
   (unsigned)selection.coverage[k],selection.training_count[k],bits655(selection.background_db[k]))!=0) goto fail;
 }
 if(add655(t,"TEMPLATE_SHA256 %s\nORIGINAL_LEARN_RAW_SAVED 0\nEND\n",r->template_sha256)!=0) goto fail;
 struct sha655 sha;sha_init655(&sha);sha_add655(&sha,t->data,t->used);sha_end655(&sha,r->sha256);
 r->bytes=(uint64_t)t->used;r->valid=1;return 0;
fail:
 free(out->text.data);out->text.data=NULL;return -1;
}
/* All components are resolved relative to already-open directories, so neither
 * the last component nor a symlinked ancestor can redirect the destination. */
static int root655(const char *path,int create) {
 if(!path || path[0]!='/' || !path[1] || strlen(path)>=PATH_MAX) return -1;
 int fd=open("/",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(fd<0) return -1;
 const char *p=path+1;
 while(*p) {
  const char *slash=strchr(p,'/');size_t n=slash?(size_t)(slash-p):strlen(p);char part[NAME_MAX+1];
  if(!n || n>NAME_MAX || (n==1 && p[0]=='.') || (n==2 && p[0]=='.' && p[1]=='.') || (slash && !slash[1])) goto fail;
  memcpy(part,p,n);part[n]=0;
  if(!slash && create) {
   if(mkdirat(fd,part,0700)!=0) {if(errno!=EEXIST) goto fail;}
   if(INJECT655(SM655_FAIL_PARENT_FSYNC) || fsync(fd)!=0) goto fail;
  }
  int next=openat(fd,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
  if(next<0) goto fail;
  if(close(fd)!=0) {close(next);return -1;}fd=next;
  if(!slash) {
   struct stat st;
   if(fstat(fd,&st)!=0 || !S_ISDIR(st.st_mode) || st.st_uid!=geteuid() || (st.st_mode&07777)!=0700) goto fail;
   return fd;
  }
  p=slash+1;
 }
fail:
 close(fd);return -1;
}
static int file_stat655(int fd,struct stat *st) {
 return fstat(fd,st)!=0 || !S_ISREG(st->st_mode) || st->st_uid!=geteuid() ||
  (st->st_mode&07777)!=0600 || st->st_nlink!=1 || st->st_size<=0?-1:0;
}
/* 0=exact content, 1=absent, -1=unsafe/mismatch/IO. */
static int payload655(int dirfd,const struct canonical655 *want) {
 int fd=openat(dirfd,want->receipt.basename,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
 if(fd<0) return errno==ENOENT?1:-1;
 struct stat st;int rc=-1;
 if(file_stat655(fd,&st)!=0 || (uint64_t)st.st_size!=want->receipt.bytes) goto done;
 struct sha655 sha;sha_init655(&sha);size_t offset=0;unsigned char buffer[4096];
 for(;;) {
  ssize_t n=read(fd,buffer,sizeof buffer);
  if(n<0) {if(errno==EINTR) continue;goto done;}
  if(!n) break;
  if((size_t)n>want->text.used-offset || memcmp(buffer,want->text.data+offset,(size_t)n)) goto done;
  offset+=(size_t)n;sha_add655(&sha,buffer,(size_t)n);
 }
 char hex[65];sha_end655(&sha,hex);
 if(offset==want->text.used && !strcmp(hex,want->receipt.sha256) && fsync(fd)==0) rc=0;
done:
 if(close(fd)!=0) rc=-1;
 return rc;
}
static int entry_name655(const char *name) {
 uint64_t session=0,command=0;uint32_t token=0;int used=0;char canonical[SM655_AUDIT_NAME_MAX];
 if(sscanf(name,"s%16" SCNx64 "_l%16" SCNx64 "_t%8" SCNx32 ".mask655%n",&session,&command,&token,&used)!=3 ||
    !used || name[used] || !session || !command || name655(canonical,sizeof canonical,session,command,token)!=0) return 0;
 return !strcmp(name,canonical);
}
static int temp_name655(const char *name) {
 long pid=0;unsigned serial=0;int used=0;char canonical[96];
 if(sscanf(name,".mask655-tmp-%ld-%u%n",&pid,&serial,&used)!=2 || !used || name[used] || pid<=0) return 0;
 int n=snprintf(canonical,sizeof canonical,".mask655-tmp-%ld-%u",pid,serial);
 return n>0 && (size_t)n<sizeof canonical && !strcmp(name,canonical);
}
/* Recover ONLY duplicate temporary hardlinks of the exact published payload.
 * Every link must be accounted for in this private directory. The final file
 * remains in place; pre-publication incomplete files are retained and counted.
 */
static int recover_links655(int dirfd,const struct canonical655 *want) {
 int fd=openat(dirfd,want->receipt.basename,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
 if(fd<0) return errno==ENOENT?0:-1;
 struct stat target;int rc=-1;
 if(fstat(fd,&target)!=0 || !S_ISREG(target.st_mode) || target.st_uid!=geteuid() ||
    (target.st_mode&07777)!=0600 || (uint64_t)target.st_size!=want->receipt.bytes || target.st_nlink<1) goto done;
 if(target.st_nlink==1) {rc=0;goto done;}
 if(target.st_nlink>65) goto done;
 unsigned char buffer[4096];size_t offset=0;
 for(;;) {
  ssize_t n=read(fd,buffer,sizeof buffer);
  if(n<0) {if(errno==EINTR) continue;goto done;}
  if(!n) break;
  if((size_t)n>want->text.used-offset || memcmp(buffer,want->text.data+offset,(size_t)n)) goto done;
  offset+=(size_t)n;
 }
 if(offset!=want->text.used) goto done;
 char aliases[64][96];unsigned count=0;
 int copy=openat(dirfd,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(copy<0) goto done;
 DIR *dir=fdopendir(copy);if(!dir) {close(copy);goto done;}
 struct dirent *entry;errno=0;
 while((entry=readdir(dir))!=NULL) {
  if(!temp_name655(entry->d_name)) {errno=0;continue;}
  struct stat st;
  if(fstatat(dirfd,entry->d_name,&st,AT_SYMLINK_NOFOLLOW)!=0) {closedir(dir);goto done;}
  if(st.st_dev==target.st_dev && st.st_ino==target.st_ino) {
   if(count>=64 || strlen(entry->d_name)>=sizeof aliases[0]) {closedir(dir);goto done;}
   strcpy(aliases[count++],entry->d_name);
  }
  errno=0;
 }
 int read_error=errno;if(closedir(dir)!=0 || read_error || count+1!=target.st_nlink) goto done;
 for(unsigned i=0;i<count;i++) {
  struct stat st;
  if(fstatat(dirfd,aliases[i],&st,AT_SYMLINK_NOFOLLOW)!=0 || st.st_dev!=target.st_dev || st.st_ino!=target.st_ino ||
     unlinkat(dirfd,aliases[i],0)!=0) goto done;
 }
 if(fsync(dirfd)!=0) goto done;
 rc=0;
done:
 if(close(fd)!=0) rc=-1;
 return rc;
}
static int ref_name655(const char *name) {
 uint64_t session=0,capture=0;int used=0;char canonical[128];
 if(sscanf(name,"s%16" SCNx64 "_c%16" SCNx64 ".ref655%n",&session,&capture,&used)!=2 ||
    !used || name[used] || !session || !capture) return 0;
 int n=snprintf(canonical,sizeof canonical,"s%016" PRIx64 "_c%016" PRIx64 ".ref655",session,capture);
 return n>0 && (size_t)n<sizeof canonical && !strcmp(canonical,name);
}
static int lock655(int dirfd) {
 int fd=openat(dirfd,".mask655.lock",O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK,0600);
 if(fd<0) return -1;
 struct stat st;
 if(fstat(fd,&st)!=0 || !S_ISREG(st.st_mode) || st.st_uid!=geteuid() ||
    (st.st_mode&07777)!=0600 || st.st_nlink!=1 || flock(fd,LOCK_EX|LOCK_NB)!=0) {close(fd);return -1;}
 return fd;
}
/* Quota accounts for incomplete temporary files, including a zero-byte file
 * or all locally explained crash hardlinks from an OLD session. They remain
 * incomplete evidence, never valid receipts. No deletion is needed for a new
 * session. Strict payload reads still demand one complete regular final file.
 */
static int quota_stat655(int dirfd,const char *name,int fd,struct stat *st) {
 if(fstat(fd,st)!=0 || !S_ISREG(st->st_mode) || st->st_uid!=geteuid() ||
    (st->st_mode&07777)!=0600 || st->st_nlink<1 || st->st_nlink>65 || st->st_size<0 ||
    (!temp_name655(name) && st->st_size==0)) return -1;
 if(st->st_nlink==1) return 0;
 int copy=openat(dirfd,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(copy<0) return -1;
 DIR *dir=fdopendir(copy);if(!dir) {close(copy);return -1;}
 unsigned links=0;int rc=-1;struct dirent *entry;errno=0;
 while((entry=readdir(dir))!=NULL) {
  struct stat alias;
  if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")) {errno=0;continue;}
  if(fstatat(dirfd,entry->d_name,&alias,AT_SYMLINK_NOFOLLOW)!=0) goto done;
  if(alias.st_dev==st->st_dev && alias.st_ino==st->st_ino) {
   if(!entry_name655(entry->d_name) && !ref_name655(entry->d_name) && !temp_name655(entry->d_name)) goto done;
   links++;
  }
  errno=0;
 }
 if(!errno && links==st->st_nlink) rc=0;
done:
 if(closedir(dir)!=0) rc=-1;
 return rc;
}
static int quota655(int dirfd,uint64_t new_bytes) {
 int copy=openat(dirfd,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(copy<0) return -1;
 DIR *dir=fdopendir(copy);if(!dir) {close(copy);return -1;}
 unsigned files=0;uint64_t bytes=0;int rc=-1;struct dirent *entry;
 errno=0;
 while((entry=readdir(dir))!=NULL) {
  if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")) {errno=0;continue;}
  if(!strcmp(entry->d_name,".mask655.lock")) {errno=0;continue;}
  if(!entry_name655(entry->d_name) && !ref_name655(entry->d_name) && !temp_name655(entry->d_name)) goto done;
  int fd=openat(dirfd,entry->d_name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
  if(fd<0) goto done;
  struct stat st;int okay=quota_stat655(dirfd,entry->d_name,fd,&st);if(close(fd)!=0) okay=-1;
  if(okay!=0 || files>=SM655_AUDIT_MAX_FILES || (uint64_t)st.st_size>SM655_AUDIT_MAX_BYTES-bytes) goto done;
  files++;bytes+=(uint64_t)st.st_size;errno=0;
 }
 if(errno || files>=SM655_AUDIT_MAX_FILES || new_bytes>SM655_AUDIT_MAX_BYTES-bytes) goto done;
 rc=0;
done:
 if(closedir(dir)!=0) rc=-1;
 return rc;
}
static int receipt_equal655(const struct sm655_receipt *a,const struct sm655_receipt *b) {
 return a->valid==1 && a->format==b->format && a->learn_token==b->learn_token && a->learn_command==b->learn_command &&
  a->bytes==b->bytes && a->upper_count==b->upper_count && a->lower_count==b->lower_count &&
  a->qualified_count==b->qualified_count && a->dominant_bin==b->dominant_bin &&
  !memcmp(a->basename,b->basename,sizeof a->basename) && !memcmp(a->sha256,b->sha256,sizeof a->sha256) &&
  !memcmp(a->template_sha256,b->template_sha256,sizeof a->template_sha256);
}
int sm655_audit_check(const char *root,const struct sm612_model *m,const struct sm655_receipt *receipt) {
 if(!receipt || receipt->valid!=1) return -1;
 struct canonical655 want;
 if(canonical655(m,receipt->learn_command,receipt->learn_token,&want)!=0) return -1;
 int rc=-1,dirfd=-1,lockfd=-1;
 if(receipt_equal655(receipt,&want.receipt) && (dirfd=root655(root,0))>=0 && (lockfd=lock655(dirfd))>=0 && payload655(dirfd,&want)==0) rc=0;
 if(lockfd>=0 && close(lockfd)!=0) rc=-1;
 if(dirfd>=0 && close(dirfd)!=0) rc=-1;
 free(want.text.data);return rc;
}
static int commit655(const char *root,const struct canonical655 *source) {
 const struct canonical655 want=*source;
 int rc=-1,dirfd=root655(root,1),fd=-1,lockfd=-1,temp_exists=0;char temp[96]={0};
 if(dirfd<0) goto done;
 lockfd=lock655(dirfd);if(lockfd<0) goto done;
 if(recover_links655(dirfd,&want)!=0) goto done;
 int existing=payload655(dirfd,&want);
 if(existing==0) {if(!INJECT655(SM655_FAIL_DIR_FSYNC) && fsync(dirfd)==0) rc=0;goto done;}
 if(existing<0 || quota655(dirfd,want.receipt.bytes)!=0) goto done;
 static unsigned serial;
 for(unsigned attempt=0;attempt<64;attempt++) {
  int n=snprintf(temp,sizeof temp,".mask655-tmp-%ld-%u",(long)getpid(),++serial);
  if(n<0 || (size_t)n>=sizeof temp) goto done;
  fd=openat(dirfd,temp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
  if(fd>=0) {temp_exists=1;break;}
  if(errno!=EEXIST) goto done;
 }
 if(fd<0) goto done;
 /* fchmod makes the specified private mode exact even under a restrictive umask. */
 if(fchmod(fd,0600)!=0) goto done;
 if(INJECT655(SM655_CRASH_AFTER_CREATE)) _exit(73);
 size_t written=0;
 while(written<want.text.used) {
  ssize_t n=write(fd,want.text.data+written,want.text.used-written);
  if(n<0) {if(errno==EINTR) continue;goto done;}
  if(!n) goto done;
  written+=(size_t)n;
 }
 if(INJECT655(SM655_FAIL_FILE_FSYNC) || fsync(fd)!=0) goto done;
 {int closed=close(fd);fd=-1;if(closed!=0 || INJECT655(SM655_FAIL_FILE_CLOSE)) goto done;}
 if(INJECT655(SM655_CRASH_PRELINK)) _exit(71);
 if(INJECT655(SM655_FAIL_LINK)) goto done;
 if(linkat(dirfd,temp,dirfd,want.receipt.basename,0)!=0) goto done;
 if(INJECT655(SM655_CRASH_POSTLINK)) _exit(72);
 /* A post-link failure leaves recoverable evidence but never a valid receipt.
  * An exact retry can complete directory durability; no final is rolled back. */
 if(INJECT655(SM655_FAIL_UNLINK) || unlinkat(dirfd,temp,0)!=0) goto done;
 temp_exists=0;
 if(INJECT655(SM655_FAIL_DIR_FSYNC) || fsync(dirfd)!=0) goto done;
 if(payload655(dirfd,&want)!=0) goto done;
 rc=0;
done:
 if(fd>=0 && close(fd)!=0) rc=-1;
 if(temp_exists && dirfd>=0 && unlinkat(dirfd,temp,0)!=0) rc=-1;
 if(lockfd>=0 && close(lockfd)!=0) rc=-1;
 if(dirfd>=0 && close(dirfd)!=0) rc=-1;
 return rc;
}
int sm655_audit_write(const char *root,const struct sm612_model *m,uint64_t command,uint32_t token,struct sm655_receipt *out) {
 if(!out) return -1;
 memset(out,0,sizeof *out);
 if(!m || m->state!=SM612_READY) return -1;
 struct canonical655 want;
 if(canonical655(m,command,token,&want)!=0) return -1;
 int rc=commit655(root,&want);
 if(rc==0) *out=want.receipt;
 free(want.text.data);return rc;
}
int sm655_audit_bind(const char *root,const struct sm613_event *e,const struct sm655_receipt *r) {
 if(!e || !r || r->valid!=1 || r->format!=SM655_AUDIT_FORMAT || !e->capture_id ||
    e->limits.bins<5 || e->limits.bins>SM612_MAX_BINS ||
    strnlen(r->basename,sizeof r->basename)>=sizeof r->basename || !entry_name655(r->basename) ||
    strnlen(r->sha256,65)!=64 || strnlen(r->template_sha256,65)!=64) return -1;
 struct canonical655 want={0};want.text.data=malloc(AUDIT_CAP655);if(!want.text.data) return -1;
 const struct sm613_template *t=&e->limits;
 int rc=-1;
 if(template655(&want.text,&t->identity,&t->config,t->bins,t->baseline_db,t->upper_db,t->lower_db,t->lower_valid)!=0) goto done;
 char hex[65];digest655(&want.text,hex);if(strcmp(hex,r->template_sha256)) goto done;
 want.text.used=0;
 if(add655(&want.text,"SM655_CAPTURE_REF 1\nSESSION %016" PRIx64 "\nCAPTURE %016" PRIx64
   "\nLEARN %016" PRIx64 "\nAUDIT %s\nAUDIT_SHA256 %s\nTEMPLATE_SHA256 %s\nEND\n",
   t->identity.session,e->capture_id,r->learn_command,r->basename,r->sha256,r->template_sha256)!=0) goto done;
 int n=snprintf(want.receipt.basename,sizeof want.receipt.basename,"s%016" PRIx64 "_c%016" PRIx64 ".ref655",t->identity.session,e->capture_id);
 if(n<=0 || (size_t)n>=sizeof want.receipt.basename) goto done;
 want.receipt.bytes=want.text.used;digest655(&want.text,want.receipt.sha256);
 rc=commit655(root,&want);
done:
 free(want.text.data);return rc;
}
