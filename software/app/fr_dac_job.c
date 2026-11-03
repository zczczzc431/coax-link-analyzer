#define _POSIX_C_SOURCE 200809L
#include "fr_dac_job.h"
#include "fr_sha256_local.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef FR_DAC_EXEC_PATH
#define FR_DAC_EXEC_PATH "/usr/local/bin/dacctl"
#endif
int fr_dac_command_format(struct fr_dac_command c,char freq[16],
                          char amp[24],const char **wave) {
    if(!freq || !amp || !wave || (c.on!=0 && c.on!=1)) return -1;
    if(!c.on) { freq[0]=amp[0]=0; *wave=NULL; return 0; }
    if(c.frequency_hz<1000000u || c.frequency_hz>180000000u ||
       c.frequency_hz%1000u || !isfinite(c.amplitude_pct) ||
       c.amplitude_pct<0 || c.amplitude_pct>50) return -1;
    uint32_t khz=c.frequency_hz/1000u;
    snprintf(freq,16,"%u.%03u",khz/1000u,khz%1000u);
    snprintf(amp,24,"%.6f",c.amplitude_pct);
    *wave=khz>30000u?"nco":"sine";
    return 0;
}
int fr_dac_job_busy(const struct fr_dac_job *j) { return j && j->pid!=0; }
static void send_signal(struct fr_dac_job *j,int sig) {
    if(j->pid>0 && kill(j->pid,sig)<0 && errno!=ESRCH)
        j->errors|=FR_DAC_SIGNAL;
}
static void terminate(struct fr_dac_job *j,uint64_t now,int immediate) {
    if(j->pid<=0) return;
    if(immediate || now>UINT64_MAX-FR_DAC_KILL_GRACE_US) {
        if(!j->kill_sent) { send_signal(j,SIGKILL); j->kill_sent=1; }
    } else if(!j->term_sent) {
        send_signal(j,SIGTERM); j->term_sent=1;
        j->kill_us=now+FR_DAC_KILL_GRACE_US;
    }
}
static int executable_fd(int fd,struct stat *s) {
    unsigned char magic[4];int flags=fcntl(fd,F_GETFL),fdflags=fcntl(fd,F_GETFD);
    return flags>=0 && fdflags>=0 && (fdflags&FD_CLOEXEC) &&
        (flags&O_ACCMODE)==O_RDONLY && fstat(fd,s)==0 &&
        S_ISREG(s->st_mode) && s->st_size>=4 && (s->st_mode&0111) &&
        !(s->st_mode&06022) && pread(fd,magic,4,0)==4 &&
        !memcmp(magic,"\177ELF",4);
}
static int same_image(const struct stat *a,const struct stat *b) {
    return a->st_dev==b->st_dev && a->st_ino==b->st_ino &&
        a->st_size==b->st_size && a->st_mode==b->st_mode &&
        a->st_uid==b->st_uid && a->st_gid==b->st_gid &&
        a->st_mtim.tv_sec==b->st_mtim.tv_sec && a->st_mtim.tv_nsec==b->st_mtim.tv_nsec &&
        a->st_ctim.tv_sec==b->st_ctim.tv_sec && a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
int fr_dac_digest_fd(int fd,char digest[65]) {
    if(!digest)return -1;
    digest[0]=0;struct stat before,after;int flags=fcntl(fd,F_GETFL);
    if(flags<0 || (flags&O_ACCMODE)!=O_RDONLY || fstat(fd,&before)<0 ||
       !S_ISREG(before.st_mode) || before.st_size<0 ||
       (uint64_t)before.st_size>FR_DAC_IMAGE_MAX_BYTES)return -1;
    uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                   0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    unsigned char block[128]={0};off_t offset=0;
    while(before.st_size-offset>=64) {
        if(pread(fd,block,64,offset)!=64)return -1;
        fr_sha_block(h,block);offset+=64;
    }
    size_t left=(size_t)(before.st_size-offset);
    memset(block,0,sizeof(block));
    if(left && pread(fd,block,left,offset)!=(ssize_t)left)return -1;
    block[left]=0x80;size_t padded=left<56?64:128;
    uint64_t bits=(uint64_t)before.st_size*8;
    for(unsigned i=0;i<8;i++)block[padded-1-i]=(unsigned char)(bits>>(8*i));
    fr_sha_block(h,block);if(padded==128)fr_sha_block(h,block+64);
    if(fstat(fd,&after)<0 || !same_image(&before,&after))return -1;
    for(unsigned i=0;i<8;i++)snprintf(digest+8*i,9,"%08x",(unsigned)h[i]);
    return 0;
}
int fr_dac_image_verify(int fd,uid_t owner,const char *expected,struct fr_dac_image *out) {
    if(!out)return -1;
    memset(out,0,sizeof(*out));out->fd=-1;
    if(!expected || strlen(expected)!=64)return -1;
    for(unsigned i=0;i<64;i++)if(!strchr("0123456789abcdef",expected[i]))return -1;
    struct stat before,after;char digest[65];
    if(!executable_fd(fd,&before) || before.st_uid!=owner ||
       fr_dac_digest_fd(fd,digest)<0 || strcmp(digest,expected) ||
       !executable_fd(fd,&after) || !same_image(&before,&after))return -1;
    out->fd=fd;out->metadata=after;memcpy(out->sha256,digest,65);out->valid=1;
    return 0;
}
int fr_dac_image_check(const struct fr_dac_image *p) {
    struct stat s;
    return p && p->valid==1 && executable_fd(p->fd,&s) &&
        same_image(&p->metadata,&s)?0:-1;
}
static int start(struct fr_dac_job *j,struct fr_dac_command c,
                 int permit,uint64_t now,int use_fd,int fd,const struct fr_dac_image *image) {
    char freq[16],amp[24]; const char *wave;
    struct stat pinned;
    if(!j || j->pid!=0) return -1; /* Do not destroy an in-flight owner. */
    if(permit!=1 || fr_dac_command_format(c,freq,amp,&wave)<0) {
        j->errors=FR_DAC_ARGS; return -1;
    }
    if(use_fd && !executable_fd(fd,&pinned)) {
        j->errors=FR_DAC_ARGS;return -1;
    }
    if(image && (image->valid!=1 || !same_image(&image->metadata,&pinned))) {
        j->errors=FR_DAC_ARGS;return -1;
    }
    if(now>UINT64_MAX-FR_DAC_TIMEOUT_US) {
        j->errors=FR_DAC_CLOCK; return -1;
    }
    memset(j,0,sizeof(*j)); j->last_us=now;
    j->deadline_us=now+FR_DAC_TIMEOUT_US;
    pid_t pid=fork();
    if(pid<0) { j->errors=FR_DAC_SPAWN; return -1; }
    if(pid==0) {
        /* UI signal dispositions/masks must not stop this command child.
         * exec resets caught handlers, but ignored ones and masks survive. */
        struct sigaction sa; memset(&sa,0,sizeof(sa)); sa.sa_handler=SIG_DFL;
        sigemptyset(&sa.sa_mask);
        if(sigaction(SIGTERM,&sa,NULL)<0 || sigaction(SIGINT,&sa,NULL)<0 ||
           sigaction(SIGCHLD,&sa,NULL)<0) _exit(126);
        sigset_t mask; sigemptyset(&mask);
        if(sigprocmask(SIG_SETMASK,&mask,NULL)<0) _exit(126);
        if(use_fd) {
            struct stat again;
            if(!executable_fd(fd,&again) || !same_image(&pinned,&again))_exit(126);
        }
        char *const clean_env[]={"LC_ALL=C","PATH=/usr/bin:/bin",NULL};
        if(c.on) {
            char *const args[]={"dacctl","--freq",freq,"--wave",(char *)wave,
                "--amp-percent",amp,"--apply","--confirm-map","0x41230000",NULL};
            if(use_fd)fexecve(fd,args,clean_env);
            else execv(FR_DAC_EXEC_PATH,args);
        } else {
            char *const args[]={"dacctl","--off","--apply","--confirm-map",
                                "0x41230000",NULL};
            if(use_fd)fexecve(fd,args,clean_env);
            else execv(FR_DAC_EXEC_PATH,args);
        }
        _exit(127);
    }
    j->pid=pid; j->started=1; return 0;
}
int fr_dac_job_start(struct fr_dac_job *j,struct fr_dac_command c,
                     int permit,uint64_t now) {
    return start(j,c,permit,now,0,-1,NULL);
}
int fr_dac_job_start_fd(struct fr_dac_job *j,struct fr_dac_command c,
                        int permit,uint64_t now,int fd) {
    return start(j,c,permit,now,1,fd,NULL);
}
int fr_dac_job_start_image(struct fr_dac_job *j,struct fr_dac_command c,
                           int permit,uint64_t now,const struct fr_dac_image *image) {
    if(!image)return start(j,c,permit,now,1,-1,NULL);
    return start(j,c,permit,now,1,image->fd,image);
}
int fr_dac_job_poll(struct fr_dac_job *j,uint64_t now) {
    if(!j || j->pid<0) return -1;
    if(!j->pid) return j->started && !j->errors?1:-1;
    int bad_clock=now<j->last_us || now==UINT64_MAX;
    if(bad_clock) j->errors|=FR_DAC_CLOCK;
    else j->last_us=now;
    /* We cannot timestamp the child's actual exit. A first observation at
     * or after the limit conservatively fails, even if it has already exited. */
    if(now>=j->deadline_us && !j->term_sent && !j->kill_sent)
        j->errors|=FR_DAC_TIMEOUT;
    int status; pid_t rc=waitpid(j->pid,&status,WNOHANG);
    if(rc<0) {
        if(errno==EINTR) return 0;
        j->errors|=FR_DAC_WAIT;
        /* Do not signal a PID no longer confirmed to be our waitable child. */
        j->pid=-1; return -1;
    }
    if(rc>0) {
        j->pid=0; j->status=status;
        if(!WIFEXITED(status) || WEXITSTATUS(status)!=0) j->errors|=FR_DAC_EXIT;
        return j->errors?-1:1;
    }
    if(bad_clock) terminate(j,now,1);
    if(now>=j->deadline_us && !j->term_sent && !j->kill_sent) {
        j->errors|=FR_DAC_TIMEOUT; terminate(j,now,0);
    }
    if(j->term_sent && !j->kill_sent && now>=j->kill_us) {
        send_signal(j,SIGKILL); j->kill_sent=1;
    }
    return 0;
}
int fr_dac_job_cancel(struct fr_dac_job *j,uint64_t now) {
    if(!j || j->pid<0) return -1;
    if(!j->pid) return fr_dac_job_poll(j,now);
    j->errors|=FR_DAC_CANCEL;
    int bad_clock=now<j->last_us || now==UINT64_MAX;
    int rc=fr_dac_job_poll(j,now);
    if(rc!=0) return rc;
    terminate(j,now,bad_clock);
    return 0;
}
