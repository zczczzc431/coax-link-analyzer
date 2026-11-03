#define _GNU_SOURCE
#include "spectrum_store614.h"
#include "spectrum_checkpoint615.h"
#include "fr_sha256_local.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* Private directory, serialized caller. SHA is integrity, not authentication. */
struct sha614 { uint32_t h[8]; uint64_t bytes; size_t used; unsigned char block[64]; };
struct digest614 { uint64_t bytes; char hex[65]; };
struct record614 {
    uint64_t session,capture,generation,snapshot_us,observed_us;
    uint32_t observed_token;
    int checkpoint;
    struct digest614 bin,csv;
};
struct measure614 { struct sha614 sha; uint64_t limit; };
struct writer614 { struct sm614_store *s; int fd,index; uint64_t *written; };

static void sha_init(struct sha614 *a) {
    static const uint32_t initial[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memcpy(a->h,initial,sizeof initial); a->bytes=0; a->used=0;
}
static void sha_update(struct sha614 *a,const void *data,size_t n) {
    const unsigned char *p=data; a->bytes+=(uint64_t)n;
    while(n) {
        size_t k=64-a->used; if(k>n)k=n;
        memcpy(a->block+a->used,p,k); a->used+=k; p+=k; n-=k;
        if(a->used==64) {fr_sha_block(a->h,a->block); a->used=0;}
    }
}
static void sha_final(struct sha614 *a,struct digest614 *d) {
    static const char digits[]="0123456789abcdef";
    uint64_t bits=a->bytes*8; d->bytes=a->bytes;
    a->block[a->used++]=0x80;
    if(a->used>56) {memset(a->block+a->used,0,64-a->used);fr_sha_block(a->h,a->block);a->used=0;}
    memset(a->block+a->used,0,56-a->used);
    for(unsigned k=0;k<8;k++)a->block[63-k]=(unsigned char)(bits>>(8*k));
    fr_sha_block(a->h,a->block);
    for(unsigned k=0;k<32;k++) {
        unsigned char b=(unsigned char)(a->h[k/4]>>(24-8*(k%4)));
        d->hex[2*k]=digits[b>>4]; d->hex[2*k+1]=digits[b&15];
    }
    d->hex[64]=0;
}
static int fail(struct sm614_store *s,enum sm614_status status) {s->status=status;return -1;}
static int measure(void *user,const void *data,size_t n) {
    struct measure614 *m=user;
    if(n>m->limit-m->sha.bytes)return -1;
    sha_update(&m->sha,data,n);return 0;
}
static void names(const struct record614 *r,char *bin,char *csv,char *idx) {
    char stem[64];
    if(r->checkpoint)
        (void)snprintf(stem,sizeof stem,"a%016" PRIx64 "_c%016" PRIx64 "_g%016" PRIx64,
            r->session,r->capture,r->generation);
    else (void)snprintf(stem,sizeof stem,"s%016" PRIx64 "_c%016" PRIx64,r->session,r->capture);
    (void)snprintf(bin,SM615_NAME_MAX,"%s.bin",stem); (void)snprintf(csv,SM615_NAME_MAX,"%s.csv",stem);
    (void)snprintf(idx,SM615_NAME_MAX,"%s.%s",stem,r->checkpoint?"cpi":"idx");
}
static int index_text(const struct record614 *r,char *out,size_t cap) {
    int n;
    if(r->checkpoint)n=snprintf(out,cap,"SM615_CHECKPOINT 1\nSESSION %016" PRIx64 "\nCAPTURE %016" PRIx64
        "\nGENERATION %016" PRIx64 "\nSNAPSHOT %" PRIu64 "\nOBSERVED %" PRIu32 " %" PRIu64
        "\nBIN %" PRIu64 " %s\nCSV %" PRIu64 " %s\nEND\n",r->session,r->capture,
        r->generation,r->snapshot_us,r->observed_token,r->observed_us,
        r->bin.bytes,r->bin.hex,r->csv.bytes,r->csv.hex);
    else n=snprintf(out,cap,"SM614_INDEX 1\nSESSION %016" PRIx64 "\nCAPTURE %016" PRIx64
        "\nBIN %" PRIu64 " %s\nCSV %" PRIu64 " %s\nEND\n",r->session,r->capture,
        r->bin.bytes,r->bin.hex,r->csv.bytes,r->csv.hex);
    return n>0 && (size_t)n<cap?n:-1;
}
static int lower_hex(const char *p,size_t n) {
    for(size_t k=0;k<n;k++)if(!((p[k]>='0' && p[k]<='9')||(p[k]>='a' && p[k]<='f')))return 0;
    return 1;
}
/* 0=regular opened, 1=absent, -1=IO, -2=unsafe entry. */
static int open_regular(struct sm614_store *s,const char *name,int *fd,struct stat *st) {
    *fd=openat(s->dirfd,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    if(*fd<0)return errno==ENOENT?1:(errno==ELOOP?-2:-1);
    if(fstat(*fd,st)<0) {close(*fd);return -1;}
    if(!S_ISREG(st->st_mode)||st->st_size<0||st->st_uid!=geteuid()) {close(*fd);return -2;}
    return 0;
}
/* Exact bounded payload verifier. 2=wrong content, 1=absent. */
static int payload(struct sm614_store *s,const char *name,const struct digest614 *want) {
    int fd,rc; struct stat st; struct sha614 sha; struct digest614 actual;
    unsigned char buffer[8192];
    rc=open_regular(s,name,&fd,&st);if(rc)return rc;
    if((uint64_t)st.st_size!=want->bytes) {close(fd);return 2;}
    sha_init(&sha);
    for(;;) {
        ssize_t n=read(fd,buffer,sizeof buffer);
        if(n<0) {if(errno==EINTR)continue;close(fd);return -1;}
        if(n==0)break;
        if((uint64_t)n>want->bytes-sha.bytes) {close(fd);return 2;}
        sha_update(&sha,buffer,(size_t)n);
    }
    if(close(fd)<0)return -1;
    sha_final(&sha,&actual);
    return actual.bytes==want->bytes && !strcmp(actual.hex,want->hex)?0:2;
}
/* Read a canonical small index. 2=malformed, 1=absent. */
static int read_index(struct sm614_store *s,const char *name,struct record614 *r,char *out,size_t *bytes,int checkpoint) {
    int fd,rc,fields,nconsumed=0;struct stat st;size_t got=0;char canonical[512];
    rc=open_regular(s,name,&fd,&st);if(rc)return rc;
    if(st.st_size<=0 || st.st_size>=512) {close(fd);return 2;}
    while(got<(size_t)st.st_size) {
        ssize_t n=read(fd,out+got,(size_t)st.st_size-got);
        if(n<0) {if(errno==EINTR)continue;close(fd);return -1;}
        if(!n) {close(fd);return 2;} got+=(size_t)n;
    }
    {char extra;ssize_t n;do {n=read(fd,&extra,1);}while(n<0 && errno==EINTR);
     if(n!=0) {close(fd);return n<0?-1:2;}}
    if(close(fd)<0)return -1;
    out[got]=0;
    r->checkpoint=checkpoint;
    if(checkpoint)fields=sscanf(out,"SM615_CHECKPOINT 1\nSESSION %16" SCNx64 "\nCAPTURE %16" SCNx64
        "\nGENERATION %16" SCNx64 "\nSNAPSHOT %" SCNu64 "\nOBSERVED %" SCNu32 " %" SCNu64
        "\nBIN %" SCNu64 " %64[0-9a-f]\nCSV %" SCNu64 " %64[0-9a-f]\nEND\n%n",
        &r->session,&r->capture,&r->generation,&r->snapshot_us,&r->observed_token,&r->observed_us,
        &r->bin.bytes,r->bin.hex,&r->csv.bytes,r->csv.hex,&nconsumed);
    else fields=sscanf(out,"SM614_INDEX 1\nSESSION %16" SCNx64 "\nCAPTURE %16" SCNx64
        "\nBIN %" SCNu64 " %64[0-9a-f]\nCSV %" SCNu64 " %64[0-9a-f]\nEND\n%n",
        &r->session,&r->capture,&r->bin.bytes,r->bin.hex,&r->csv.bytes,r->csv.hex,&nconsumed);
    if(fields!=(checkpoint?10:6) ||
        nconsumed!=(int)got || strlen(r->bin.hex)!=64 || strlen(r->csv.hex)!=64 ||
        !lower_hex(r->bin.hex,64)||!lower_hex(r->csv.hex,64)||
        !r->session||!r->capture||!r->bin.bytes||!r->csv.bytes||
        r->bin.bytes>SM614_BIN_MAX||r->csv.bytes>SM614_CSV_MAX)return 2;
    if(checkpoint && (!r->generation||r->snapshot_us<r->observed_us))return 2;
    rc=index_text(r,canonical,sizeof canonical);
    if(rc!=(int)got || memcmp(out,canonical,got))return 2;
    *bytes=got;return 0;
}
static int sync_file(struct sm614_store *s,const char *name,int index) {
    int fd,rc;struct stat st;
    rc=open_regular(s,name,&fd,&st);
    if(rc)return fail(s,SM614_IO);
    if(s->fault==(index?SM614_FAULT_INDEX_SYNC:SM614_FAULT_FILE_SYNC)) {close(fd);return fail(s,SM614_IO);}
    do {rc=fsync(fd);}while(rc<0 && errno==EINTR);
    if(close(fd)<0)rc=-1;
    return rc?fail(s,SM614_IO):0;
}
static int sync_dir(struct sm614_store *s,int index) {
    int rc;
    if(s->fault==(index?SM614_FAULT_INDEX_DIR_SYNC:SM614_FAULT_DIR_SYNC))return fail(s,SM614_IO);
    do {rc=fsync(s->dirfd);}while(rc<0 && errno==EINTR);
    return rc?fail(s,SM614_IO):0;
}

int sm614_open(struct sm614_store *s,const char *directory,unsigned max_events,uint64_t max_bytes) {
    struct stat st;int rc;
    if(!s)return -1;
    memset(s,0,sizeof *s);s->dirfd=-1;s->lockfd=-1;
    if(!directory||!max_events||max_events>(UINT_MAX-16)/3||!max_bytes)return fail(s,SM614_BAD_ARGUMENT);
    s->dirfd=open(directory,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(s->dirfd<0)return fail(s,SM614_IO);
    if(fstat(s->dirfd,&st)<0||st.st_uid!=geteuid()||(st.st_mode&0777)!=0700) {
        sm614_close(s);return fail(s,SM614_BAD_ARGUMENT);
    }
    s->lockfd=openat(s->dirfd,".sm614.lock",O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK,0600);
    if(s->lockfd<0) {sm614_close(s);return fail(s,SM614_IO);}
    if(fstat(s->lockfd,&st)<0||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&077)!=0) {
        sm614_close(s);return fail(s,SM614_BAD_ARGUMENT);
    }
    rc=flock(s->lockfd,LOCK_EX|LOCK_NB);
    if(rc<0) {enum sm614_status status=(errno==EWOULDBLOCK||errno==EAGAIN)?SM614_LOCKED:SM614_IO;
        sm614_close(s);return fail(s,status);}
    s->max_events=max_events;s->max_files=3*max_events+16;s->max_bytes=max_bytes;s->status=SM614_OK;
    return 0;
}
void sm614_close(struct sm614_store *s) {
    if(!s)return;
    if(s->lockfd>=0)close(s->lockfd);
    if(s->dirfd>=0)close(s->dirfd);
    s->lockfd=-1;s->dirfd=-1;
}
void sm614_inject(struct sm614_store *s,enum sm614_fault fault,uint64_t after) {
    if(s) {s->fault=fault;s->fail_after=after;}
}
static int scan_kind(struct sm614_store *s,struct sm614_scan *out,int checkpoint) {
    DIR *dir;int fd;struct dirent *entry;struct sm614_scan found={0,0,0,0};
    if(!s)return -1;
    if(!out||s->dirfd<0||s->lockfd<0)return fail(s,SM614_BAD_ARGUMENT);
    memset(out,0,sizeof *out);
    {struct stat path_lock,held_lock;
     if(fstatat(s->dirfd,".sm614.lock",&path_lock,AT_SYMLINK_NOFOLLOW)<0 ||
        fstat(s->lockfd,&held_lock)<0)return fail(s,SM614_IO);
     if(!S_ISREG(path_lock.st_mode)||path_lock.st_uid!=geteuid()||
        (path_lock.st_mode&077)!=0||path_lock.st_dev!=held_lock.st_dev||
        path_lock.st_ino!=held_lock.st_ino)return fail(s,SM614_CORRUPT);}
    /* openat '.', unlike dup(dirfd), has an independent directory offset. */
    fd=openat(s->dirfd,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(fd<0)return fail(s,SM614_IO);
    dir=fdopendir(fd);if(!dir) {close(fd);return fail(s,SM614_IO);}
    for(;;) {
        struct stat st;size_t len;int rc;
        errno=0;entry=readdir(dir);
        if(!entry) {if(errno) {closedir(dir);return fail(s,SM614_IO);}break;}
        if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,"..")||!strcmp(entry->d_name,".sm614.lock"))continue;
        if(fstatat(s->dirfd,entry->d_name,&st,AT_SYMLINK_NOFOLLOW)<0) {closedir(dir);return fail(s,SM614_IO);}
        if(!S_ISREG(st.st_mode)||st.st_size<0||st.st_uid!=geteuid()) {closedir(dir);return fail(s,SM614_CORRUPT);}
        if(found.files==UINT_MAX || (uint64_t)st.st_size>UINT64_MAX-found.bytes) {closedir(dir);return fail(s,SM614_CORRUPT);}
        found.files++;found.bytes+=(uint64_t)st.st_size;len=strlen(entry->d_name);
        if(len>=4 && (!strcmp(entry->d_name+len-4,".idx") || !strcmp(entry->d_name+len-4,".cpi"))) {
            struct record614 r;char content[512],bin[SM615_NAME_MAX],csv[SM615_NAME_MAX],idx[SM615_NAME_MAX];size_t bytes;
            if(strcmp(entry->d_name+len-4,checkpoint?".cpi":".idx")) {closedir(dir);return fail(s,SM614_CORRUPT);}
            memset(&r,0,sizeof r);
            rc=read_index(s,entry->d_name,&r,content,&bytes,checkpoint);
            if(rc) {closedir(dir);return fail(s,rc==-1?SM614_IO:SM614_CORRUPT);}
            names(&r,bin,csv,idx);
            if(strcmp(entry->d_name,idx)) {closedir(dir);return fail(s,SM614_CORRUPT);}
            rc=payload(s,bin,&r.bin);if(!rc)rc=payload(s,csv,&r.csv);
            if(rc) {closedir(dir);return fail(s,rc==-1?SM614_IO:SM614_CORRUPT);}
            if(found.complete_events==UINT_MAX) {closedir(dir);return fail(s,SM614_CORRUPT);}
            found.complete_events++;
        }
    }
    if(closedir(dir)<0)return fail(s,SM614_IO);
    if(found.complete_events>found.files/3)return fail(s,SM614_CORRUPT);
    found.incomplete_files=found.files-3*found.complete_events;*out=found;s->status=SM614_OK;return 0;
}
int sm614_scan(struct sm614_store *s,struct sm614_scan *out) {return scan_kind(s,out,0);}

/* Directory already fully verified. This returns metadata only, not core state. */
static int latest_record(struct sm614_store *s,uint64_t session,uint64_t capture,struct record614 *latest) {
    DIR *dir;struct dirent *entry;int fd,found=0;
    memset(latest,0,sizeof *latest);
    fd=openat(s->dirfd,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(fd<0)return fail(s,SM614_IO);
    dir=fdopendir(fd);if(!dir) {close(fd);return fail(s,SM614_IO);}
    for(;;) {
        size_t len;
        errno=0;entry=readdir(dir);
        if(!entry) {if(errno) {closedir(dir);return fail(s,SM614_IO);}break;}
        len=strlen(entry->d_name);
        if(len>=4 && !strcmp(entry->d_name+len-4,".cpi")) {
            struct record614 r;char content[512];size_t bytes;int rc;
            memset(&r,0,sizeof r);rc=read_index(s,entry->d_name,&r,content,&bytes,1);
            if(rc) {closedir(dir);return fail(s,rc==-1?SM614_IO:SM614_CORRUPT);}
            if(r.session==session && r.capture==capture && (!found||r.generation>latest->generation)) {
                *latest=r;found=1;
            }
        }
    }
    if(closedir(dir)<0)return fail(s,SM614_IO);
    return found;
}

static int encode_bin(const struct sm613_event *event,const struct sm615_view *view,sm614_emit emit,void *user) {
    return view?sm615_encode_bin(view,emit,user):sm614_encode_bin(event,emit,user);
}
static int encode_csv(const struct sm613_event *event,const struct sm615_view *view,sm614_emit emit,void *user) {
    return view?sm615_encode_csv(view,emit,user):sm614_encode_csv(event,emit,user);
}

static int write_emit(void *user,const void *data,size_t bytes) {
    struct writer614 *w=user;const unsigned char *p=data;size_t allowed=bytes;
    enum sm614_fault injected=w->index?SM614_FAULT_INDEX_WRITE:SM614_FAULT_WRITE;
    if(w->s->fault==injected) {
        uint64_t remaining=*w->written<w->s->fail_after?w->s->fail_after-*w->written:0;
        if(remaining<allowed)allowed=(size_t)remaining;
    }
    {size_t done=0;while(done<allowed) {
        ssize_t n=write(w->fd,p+done,allowed-done);
        if(n<0) {if(errno==EINTR)continue;return -1;}
        if(!n)return -1;
        done+=(size_t)n;*w->written+=(uint64_t)n;
    }}
    return allowed==bytes?0:-1;
}
/* Only our own completed staging link is removed; failures retain partials. */
static int publish(struct sm614_store *s,const char *name,const struct digest614 *digest,
        const struct sm613_event *event,const struct sm615_view *view,
        const char *text,size_t text_bytes,int kind,uint64_t *written) {
    int rc,fd;char temporary[128];struct writer614 writer;
    rc=payload(s,name,digest);
    if(rc==0)return sync_file(s,name,kind==2)||sync_dir(s,kind==2)?-1:0;
    if(rc!=1)return fail(s,rc==-1?SM614_IO:SM614_CONFLICT);
    for(;;) {
        int n;
        if(s->temporary_seq==UINT64_MAX)return fail(s,SM614_IO);
        s->temporary_seq++;
        n=snprintf(temporary,sizeof temporary,"%s.part.%ld.%" PRIu64,name,(long)getpid(),s->temporary_seq);
        if(n<0||(size_t)n>=sizeof temporary)return fail(s,SM614_IO);
        fd=openat(s->dirfd,temporary,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        if(fd>=0)break;
        if(errno!=EEXIST)return fail(s,SM614_IO);
    }
    writer.s=s;writer.fd=fd;writer.index=kind==2;writer.written=written;
    if(kind==0)rc=encode_bin(event,view,write_emit,&writer);
    else if(kind==1)rc=encode_csv(event,view,write_emit,&writer);
    else rc=write_emit(&writer,text,text_bytes);
    if(rc) {close(fd);return fail(s,SM614_IO);}
    if(s->fault==(kind==2?SM614_FAULT_INDEX_SYNC:SM614_FAULT_FILE_SYNC)) {close(fd);return fail(s,SM614_IO);}
    do {rc=fsync(fd);}while(rc<0&&errno==EINTR);
    if(close(fd)<0)rc=-1;
    if(rc)return fail(s,SM614_IO);
    if(s->fault==(kind==2?SM614_FAULT_INDEX_LINK:SM614_FAULT_LINK))return fail(s,SM614_IO);
    rc=linkat(s->dirfd,temporary,s->dirfd,name,0);
    if(rc<0) {
        if(errno!=EEXIST)return fail(s,SM614_IO);
        rc=payload(s,name,digest);
        if(rc)return fail(s,rc==-1?SM614_IO:SM614_CONFLICT);
        if(sync_file(s,name,kind==2))return -1;
    }
    if(unlinkat(s->dirfd,temporary,0)<0)return fail(s,SM614_IO);
    return sync_dir(s,kind==2);
}
static int save_kind(struct sm614_store *s,const struct sm613_event *event,const struct sm615_view *view) {
    struct record614 r,existing;struct measure614 m;
    char bin[SM615_NAME_MAX],csv[SM615_NAME_MAX],idx[SM615_NAME_MAX],index[512],old_index[512];size_t old_bytes=0;
    struct sm614_scan inventory;int n,rc;uint64_t total,written=0;struct digest614 index_digest;struct sha614 sha;
    if(!s)return -1;
    if(s->dirfd<0||s->lockfd<0)return fail(s,SM614_BAD_ARGUMENT);
    if(view?!sm615_view_valid(view):!sm614_event_valid(event))return fail(s,SM614_BAD_EVENT);
    /* An exact retry must not bypass startup/inventory corruption checks. */
    if(scan_kind(s,&inventory,view!=NULL))return -1;
    memset(&r,0,sizeof r);r.session=event->limits.identity.session;r.capture=event->capture_id;
    if(view) {
        r.checkpoint=1;r.generation=view->generation;r.snapshot_us=view->snapshot_us;
        r.observed_token=view->observed_token;r.observed_us=view->observed_us;
    }
    sha_init(&m.sha);m.limit=SM614_BIN_MAX;
    if(encode_bin(event,view,measure,&m))return fail(s,SM614_BAD_EVENT);
    sha_final(&m.sha,&r.bin);sha_init(&m.sha);m.limit=SM614_CSV_MAX;
    if(encode_csv(event,view,measure,&m))return fail(s,SM614_BAD_EVENT);
    sha_final(&m.sha,&r.csv);names(&r,bin,csv,idx);n=index_text(&r,index,sizeof index);
    if(n<0)return fail(s,SM614_BAD_EVENT);
    memset(&existing,0,sizeof existing);rc=read_index(s,idx,&existing,old_index,&old_bytes,view!=NULL);
    if(rc==0) {
        if(old_bytes!=(size_t)n || memcmp(index,old_index,(size_t)n))return fail(s,SM614_CONFLICT);
        rc=payload(s,bin,&r.bin);if(!rc)rc=payload(s,csv,&r.csv);
        if(rc)return fail(s,rc==-1?SM614_IO:SM614_CORRUPT);
        /* Exact retries do real persistence work; visible != durable. */
        if(sync_file(s,bin,0)||sync_file(s,csv,0)||sync_dir(s,0)||sync_file(s,idx,1)||sync_dir(s,1))return -1;
        s->status=SM614_OK;return 0;
    }
    if(rc!=1)return fail(s,rc==-1?SM614_IO:SM614_CONFLICT);
    if(view) {
        struct record614 latest;
        rc=latest_record(s,r.session,r.capture,&latest);if(rc<0)return -1;
        if(rc && (r.generation<=latest.generation || r.observed_us<latest.observed_us ||
            r.snapshot_us<latest.snapshot_us || r.observed_token-latest.observed_token>INT32_MAX ||
            ((r.observed_us==latest.observed_us)!=(r.observed_token==latest.observed_token))))
            return fail(s,SM614_CONFLICT);
    }
    /* Detect conflicting uncommitted payloads before quota or any new output. */
    rc=payload(s,bin,&r.bin);if(rc!=0&&rc!=1)return fail(s,rc==-1?SM614_IO:SM614_CONFLICT);
    rc=payload(s,csv,&r.csv);if(rc!=0&&rc!=1)return fail(s,rc==-1?SM614_IO:SM614_CONFLICT);
    total=r.bin.bytes+r.csv.bytes+(uint64_t)n;
    if(inventory.complete_events>=s->max_events || inventory.files>s->max_files ||
        s->max_files-inventory.files<6 || inventory.bytes>s->max_bytes ||
        total>(s->max_bytes-inventory.bytes)/2)return fail(s,SM614_QUOTA);
    if(publish(s,bin,&r.bin,event,view,NULL,0,0,&written)||publish(s,csv,&r.csv,event,view,NULL,0,1,&written))return -1;
    sha_init(&sha);sha_update(&sha,index,(size_t)n);sha_final(&sha,&index_digest);written=0;
    if(publish(s,idx,&index_digest,event,view,index,(size_t)n,2,&written))return -1;
    s->status=SM614_OK;return 0;
}
int sm614_save(void *store,const struct sm613_event *event) {return save_kind(store,event,NULL);}
int sm615_open(struct sm615_store *s,const char *directory,unsigned maximum,uint64_t bytes) {
    return s?sm614_open(&s->files,directory,maximum,bytes):-1;
}
void sm615_close(struct sm615_store *s) {if(s)sm614_close(&s->files);}
void sm615_inject(struct sm615_store *s,enum sm614_fault fault,uint64_t after) {
    if(s)sm614_inject(&s->files,fault,after);
}
int sm615_scan(struct sm615_store *s,struct sm614_scan *out) {return s?scan_kind(&s->files,out,1):-1;}
int sm615_save(struct sm615_store *s,const struct sm615_view *v) {
    if(!s)return -1;
    if(!v)return fail(&s->files,SM614_BAD_EVENT);
    return save_kind(&s->files,v->event,v);
}
int sm615_latest(struct sm615_store *s,uint64_t session,uint64_t capture,struct sm615_found *out) {
    struct sm614_scan inventory;struct record614 latest;int rc;
    if(out)memset(out,0,sizeof *out);
    if(!s)return -1;
    if(!out||!session||!capture)return fail(&s->files,SM614_BAD_ARGUMENT);
    if(scan_kind(&s->files,&inventory,1))return -1;
    rc=latest_record(&s->files,session,capture,&latest);if(rc<=0)return rc;
    out->generation=latest.generation;out->snapshot_us=latest.snapshot_us;
    out->observed_token=latest.observed_token;out->observed_us=latest.observed_us;
    names(&latest,out->bin_name,out->csv_name,out->index_name);return 1;
}
