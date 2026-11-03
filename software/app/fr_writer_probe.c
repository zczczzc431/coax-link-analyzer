#define _POSIX_C_SOURCE 200809L
#include "fr_writer_probe.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

/* Linux PF_KTHREAD, include/linux/sched.h (v4.9 and v6.1 checked).
 * proc stat field 9 exposes task flags. No executable is expected for a
 * kernel thread/zombie; a LIVE userspace task with missing exe is unknown. */
#define PROBE_PF_KTHREAD 0x00200000u
#define PROBE_MAX_PROCESSES 4096u
#define DAC_LOCK_NAME "dac_control.lock"
static int trusted_lock_dir(int fd,uid_t owner) {
    struct stat s;
    if(fd<0 || fstat(fd,&s)!=0 || !S_ISDIR(s.st_mode) || s.st_uid!=owner)
        return 0;
    /* The deployed image uses the standard root-owned sticky /run/lock
     * (01777).  Sticky ownership protects the root-owned 0600 inode from
     * unprivileged rename/unlink; accept only this exact exception and keep
     * rejecting ordinary group/other-writable directories. */
    if(!(s.st_mode&0022)) return 1;
    return (s.st_mode&01000)!=0 && (s.st_mode&0022)==0022;
}
static int permitted_lock(const struct stat *s,uid_t owner) {
    return S_ISREG(s->st_mode) && s->st_uid==owner && s->st_nlink==1 &&
           (s->st_mode&07777)==0600 && s->st_size==0;
}
int fr_writer_lock_check(int dir,uid_t owner,int fd) {
    struct stat a,b;
    if(!trusted_lock_dir(dir,owner) || fd<0 || fstat(fd,&a)<0 ||
       !permitted_lock(&a,owner) ||
       fstatat(dir,DAC_LOCK_NAME,&b,AT_SYMLINK_NOFOLLOW)<0 ||
       !permitted_lock(&b,owner) || a.st_dev!=b.st_dev || a.st_ino!=b.st_ino)
        return -1;
    return 0;
}
int fr_writer_lock_open(int dir,uid_t owner,int allow,int *created) {
    if(!created)return -1;
    *created=0;
    if((allow!=0 && allow!=1) || !trusted_lock_dir(dir,owner))return -1;
    struct stat before;
    int exists=fstatat(dir,DAC_LOCK_NAME,&before,AT_SYMLINK_NOFOLLOW)==0;
    if(!exists && (errno!=ENOENT || !allow))return -1;
    if(exists && !permitted_lock(&before,owner))return -1;
    int flags=O_RDWR|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW;
    /* EEXIST on a concurrent first creator fails, never silently switches
     * to another inode. No retry after disappearance or metadata mismatch. */
    int fd=openat(dir,DAC_LOCK_NAME,flags|(exists?0:O_CREAT|O_EXCL),0600);
    if(fd<0)return -1;
    *created=!exists;
    struct stat after;
    int ok=fr_writer_lock_check(dir,owner,fd)==0 && fstat(fd,&after)==0;
    if(ok && exists)ok=before.st_dev==after.st_dev && before.st_ino==after.st_ino;
    if(!ok) { (void)close(fd);return -1; } /* No retry, no unlink on failure. */
    return fd;
}
struct task_state { char comm[256],state;uint64_t flags,start; };
static int same_inode(const struct stat *a,const struct stat *b) {
    return a->st_dev==b->st_dev && a->st_ino==b->st_ino;
}
static int same_file(const struct stat *a,const struct stat *b) {
    return same_inode(a,b) && a->st_size==b->st_size && a->st_mode==b->st_mode &&
           a->st_mtim.tv_sec==b->st_mtim.tv_sec && a->st_mtim.tv_nsec==b->st_mtim.tv_nsec &&
           a->st_ctim.tv_sec==b->st_ctim.tv_sec && a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static void close_seen(int fd,struct fr_writer_snapshot *s) {
    if(close(fd)<0)s->errors|=FR_WRITER_CLOSE; /* Never retry stale fd. */
}
static int state_read(int dir,struct task_state *s,struct fr_writer_snapshot *o) {
    char buf[4096];int fd=openat(dir,"stat",O_RDONLY|O_CLOEXEC);
    if(fd<0)return -1;
    ssize_t n=read(fd,buf,sizeof(buf)-1);int read_error=errno;
    close_seen(fd,o);errno=read_error;
    if(n<=0 || n>=(ssize_t)sizeof(buf)-1)return -1;
    buf[n]=0;char *left=strchr(buf,'('),*right=strrchr(buf,')');
    if(!left || !right || right<=left || right[1]!=' ' ||
       (size_t)(right-left-1)>=sizeof(s->comm))return -1;
    memcpy(s->comm,left+1,(size_t)(right-left-1));s->comm[right-left-1]=0;
    char *save=NULL,*p=strtok_r(right+2," \n",&save);
    for(unsigned field=3;field<=22;field++,p=strtok_r(NULL," \n",&save)) {
        if(!p)return -1;
        if(field==3) {if(p[1] || !strchr("RSDTtZXxKWPI",p[0]))return -1;s->state=p[0];}
        if(field==9 || field==22) {
            if(*p<'0' || *p>'9')return -1;
            char *end=NULL;errno=0;unsigned long long x=strtoull(p,&end,10);
            if(errno || !end || *end)return -1;
            if(field==9)s->flags=x;else s->start=x;
        }
    }
    return 0;
}
static int numeric_pid(const char *name,pid_t *pid) {
    if(!*name)return 0;
    for(const char *p=name;*p;p++)if(*p<'0' || *p>'9')return 0;
    char *end=NULL;errno=0;unsigned long n=strtoul(name,&end,10);
    if(errno || !end || *end || n==0 || n>INT_MAX)return 0;
    *pid=(pid_t)n;return 1;
}
static int regular(int fd,struct stat *s) {
    return fd>=0 && fstat(fd,s)==0 && S_ISREG(s->st_mode);
}
static void scan(struct fr_writer_probe_config c,const struct stat *tool,
                 const struct stat *reader,struct fr_writer_snapshot *s) {
    DIR *d=opendir(c.proc_root);
    if(!d) {s->errors|=FR_WRITER_PROC;return;}
    for(;;) {
        errno=0;struct dirent *entry=readdir(d);
        if(!entry) {if(errno)s->errors|=FR_WRITER_PROC;break;}
        pid_t pid;if(!numeric_pid(entry->d_name,&pid))continue;
        if(++s->processes>PROBE_MAX_PROCESSES) {s->errors|=FR_WRITER_PROC;break;}
        int pd=openat(dirfd(d),entry->d_name,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
        if(pd<0) {
            if(errno!=ENOENT && errno!=ESRCH)s->errors|=FR_WRITER_PROC;
            continue;
        }
        struct task_state before={0},after={0};struct stat exe;
        if(state_read(pd,&before,s)<0) {s->errors|=FR_WRITER_PROC;close_seen(pd,s);continue;}
        if(before.state=='Z' || before.state=='X' || before.state=='x' || (before.flags&PROBE_PF_KTHREAD)) {
            close_seen(pd,s);continue;
        }
        int fd=openat(pd,"exe",O_RDONLY|O_CLOEXEC);
        if(!regular(fd,&exe)) {
            s->errors|=FR_WRITER_PROC;if(fd>=0)close_seen(fd,s);close_seen(pd,s);continue;
        }
        close_seen(fd,s);
        if(state_read(pd,&after,s)<0 || before.start!=after.start ||
           strcmp(before.comm,after.comm)) {
            s->errors|=FR_WRITER_PROC;close_seen(pd,s);continue;
        }
        /* exec need not change PID/starttime, and comm can remain identical. */
        struct stat exe_after;
        fd=openat(pd,"exe",O_RDONLY|O_CLOEXEC);
        if(!regular(fd,&exe_after) || !same_file(&exe,&exe_after))s->errors|=FR_WRITER_PROC;
        if(fd>=0)close_seen(fd,s);
        if(pid==c.self_pid) {
            if(same_inode(reader,&exe))s->self_seen=1;else s->errors|=FR_WRITER_IDENTITY;
        } else if(same_inode(tool,&exe) || same_inode(reader,&exe) ||
                  !strcmp(before.comm,"dacctl") || !strncmp(before.comm,"adc_scope_display",15)) {
            if(!s->known_writers)s->first_writer=pid;
            s->known_writers++;
        }
        close_seen(pd,s);
    }
    if(closedir(d)<0)s->errors|=FR_WRITER_CLOSE;
    if(!s->self_seen)s->errors|=FR_WRITER_PROC;
}
static void probe_lock(const char *path,struct fr_writer_snapshot *s) {
    /* Never create a fresh inode: that would bypass a deleted/replaced lock. */
    int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0) {s->errors|=FR_WRITER_LOCK;return;}
    struct stat before,after;
    if(!regular(fd,&before))s->errors|=FR_WRITER_LOCK;
    else {
        if(flock(fd,LOCK_EX|LOCK_NB)==0)s->lock_was_free=1;
        else if(errno!=EWOULDBLOCK && errno!=EAGAIN)s->errors|=FR_WRITER_LOCK;
        if(lstat(path,&after)<0 || !S_ISREG(after.st_mode) || !same_inode(&before,&after))
            s->errors|=FR_WRITER_LOCK;
    }
    close_seen(fd,s);
}
int fr_writer_probe(struct fr_writer_probe_config c,struct fr_writer_snapshot *s) {
    if(!s)return -1;
    memset(s,0,sizeof(*s));
    if(!c.proc_root || c.proc_root[0]!='/' || !c.lock_path || c.lock_path[0]!='/' ||
       !c.tool_path || c.tool_path[0]!='/' || c.self_pid!=getpid()) {
        s->errors=FR_WRITER_ARGS;return -1;
    }
    struct stat tool,reader,after,path;
    if(!regular(c.tool_fd,&tool) || !regular(c.reader_fd,&reader) ||
       same_inode(&tool,&reader) || stat(c.tool_path,&path)<0 || !same_file(&tool,&path)) {
        s->errors=FR_WRITER_IDENTITY;return -1;
    }
    scan(c,&tool,&reader,s);probe_lock(c.lock_path,s);
    if(!regular(c.tool_fd,&after) || !same_file(&tool,&after) ||
       stat(c.tool_path,&path)<0 || !same_file(&tool,&path) ||
       !regular(c.reader_fd,&after) || !same_file(&reader,&after))s->errors|=FR_WRITER_IDENTITY;
    return s->errors?-1:0;
}
int fr_writer_watch_init(struct fr_writer_watch *w,uint64_t now,
                          uint64_t budget,uint64_t quiet) {
    if(!w)return FR_WRITER_FAILED;
    memset(w,0,sizeof(*w));
    if(!quiet || budget<=quiet || now==UINT64_MAX || budget>UINT64_MAX-now) {
        w->errors=FR_WRITER_CLOCK;return FR_WRITER_FAILED;
    }
    w->deadline_us=now+budget;w->quiet_us=quiet;w->last_us=now;w->seeded=1;
    return FR_WRITER_WAIT;
}
int fr_writer_watch_step(struct fr_writer_watch *w,
                          const struct fr_writer_snapshot *s,uint64_t now) {
    if(!w)return FR_WRITER_FAILED;
    if(w->errors)return FR_WRITER_FAILED;
    if(!s || !w->seeded) {w->errors=FR_WRITER_ARGS;return FR_WRITER_FAILED;}
    if(now<w->last_us || now==UINT64_MAX) {w->errors=FR_WRITER_CLOCK;return FR_WRITER_FAILED;}
    w->last_us=now;
    if(now>=w->deadline_us) {w->errors=FR_WRITER_TIMEOUT;return FR_WRITER_FAILED;}
    if(s->errors || s->self_seen!=1 ||
       (s->lock_was_free!=0 && s->lock_was_free!=1)) {
        w->errors=s->errors?s->errors:FR_WRITER_ARGS;return FR_WRITER_FAILED;
    }
    if(s->known_writers || !s->lock_was_free) {w->quiet_started=0;return FR_WRITER_WAIT;}
    if(!w->quiet_started) {w->quiet_started=1;w->quiet_since_us=now;return FR_WRITER_WAIT;}
    return now-w->quiet_since_us>=w->quiet_us?FR_WRITER_QUIET_OBSERVED:FR_WRITER_WAIT;
}
