#define _POSIX_C_SOURCE 200809L
#include "fr_platform_files.h"
#include "fr_writer_probe.h"
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

static int trusted_dir(int fd,uid_t uid) {
    struct stat s;
    if(fd<0 || fstat(fd,&s)!=0 || !S_ISDIR(s.st_mode) || s.st_uid!=uid)
        return 0;
    /* /run/lock on this image is the standard root-owned sticky directory
     * (01777).  Sticky ownership prevents an unprivileged user from deleting
     * or renaming the root-owned 0600 lock inode; accept this exact safe form,
     * while continuing to reject ordinary group/other-writable directories. */
    if(!(s.st_mode&0022)) return 1;
    return (s.st_mode&01000)!=0 && (s.st_mode&0022)==0022;
}
static void drop(int *fd,unsigned *errors) {
    int old=*fd;*fd=-1;
    if(old>=0 && close(old)<0)*errors|=FR_FILES_CLOSE;
}
static int walk(int root,uid_t uid,const char *const *parts,unsigned n,unsigned *errors) {
    if(!trusted_dir(root,uid))return -1;
    int dir=fcntl(root,F_DUPFD_CLOEXEC,3);
    if(dir<0)return -1;
    for(unsigned i=0;i<n;i++) {
        int next=openat(dir,parts[i],O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        drop(&dir,errors);
        if(!trusted_dir(next,uid) || (*errors&FR_FILES_CLOSE)) {
            drop(&next,errors);return -1;
        }
        dir=next;
    }
    return dir;
}
static const char *const bin_parts[]={"usr","local","bin"};
static const char *const lock_parts[]={"run","lock"};
static int same_inode(int x,int y) {
    struct stat a,b;
    return fstat(x,&a)==0 && fstat(y,&b)==0 && a.st_dev==b.st_dev && a.st_ino==b.st_ino;
}
int fr_platform_files_close(struct fr_platform_files *p,int done) {
    if(!p)return -1;
    if(!p->started)return 0;
    if(done!=1)return -1;
    p->ready=0;p->image.valid=0;
    drop(&p->lock_fd,&p->errors);drop(&p->tool_fd,&p->errors);
    drop(&p->lockdir_fd,&p->errors);drop(&p->bin_fd,&p->errors);drop(&p->root_fd,&p->errors);
    return p->errors&FR_FILES_CLOSE?-1:0;
}
int fr_platform_files_check(struct fr_platform_files *p) {
    if(!p || !p->ready || p->errors)return -1;
    int bin=walk(p->root_fd,p->owner,bin_parts,3,&p->errors);
    int locks=walk(p->root_fd,p->owner,lock_parts,2,&p->errors);
    if(bin<0 || locks<0 || !same_inode(bin,p->bin_fd) || !same_inode(locks,p->lockdir_fd))
        p->errors|=FR_FILES_PATH;
    struct stat name;
    if(fr_dac_image_check(&p->image)<0 ||
       fstatat(p->bin_fd,"dacctl",&name,AT_SYMLINK_NOFOLLOW)<0 || !S_ISREG(name.st_mode) ||
       name.st_dev!=p->image.metadata.st_dev || name.st_ino!=p->image.metadata.st_ino)
        p->errors|=FR_FILES_TOOL;
    if(fr_writer_lock_check(p->lockdir_fd,p->owner,p->lock_fd)<0)p->errors|=FR_FILES_LOCK;
    drop(&bin,&p->errors);drop(&locks,&p->errors);
    if(p->errors) {p->ready=0;return -1;}
    return 0;
}
static int prepare(struct fr_platform_files *p,int root,uid_t uid,const char *sha,int allow_create) {
    if(!p || p->started)return -1;
    memset(p,0,sizeof(*p));p->started=1;p->owner=uid;
    p->root_fd=p->bin_fd=p->lockdir_fd=p->tool_fd=p->lock_fd=-1;
    if(!trusted_dir(root,uid) || (p->root_fd=fcntl(root,F_DUPFD_CLOEXEC,3))<0) {
        p->errors|=FR_FILES_PATH;goto fail;
    }
    p->bin_fd=walk(p->root_fd,uid,bin_parts,3,&p->errors);
    p->lockdir_fd=walk(p->root_fd,uid,lock_parts,2,&p->errors);
    if(p->bin_fd<0 || p->lockdir_fd<0) {p->errors|=FR_FILES_PATH;goto fail;}
    struct stat s;
    if(fstatat(p->bin_fd,"dacctl",&s,AT_SYMLINK_NOFOLLOW)<0 || !S_ISREG(s.st_mode)) {
        p->errors|=FR_FILES_TOOL;goto fail;
    }
    p->tool_fd=openat(p->bin_fd,"dacctl",O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
    if(fr_dac_image_verify(p->tool_fd,uid,sha,&p->image)<0) {p->errors|=FR_FILES_TOOL;goto fail;}
    int created=0;
    p->lock_fd=fr_writer_lock_open(p->lockdir_fd,uid,allow_create,&created);
    if(p->lock_fd<0) {p->errors|=FR_FILES_LOCK;goto fail;}
    p->ready=1;
    if(fr_platform_files_check(p)==0)return 0;
fail:
    (void)fr_platform_files_close(p,1);return -1;
}
int fr_platform_files_open(struct fr_platform_files *p) {
    if(!p || p->started)return -1;
    int root=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    int rc=prepare(p,root,0,FR_DAC_C827_SHA256,1);
    drop(&root,&p->errors);
    if(p->errors) {(void)fr_platform_files_close(p,1);return -1;}
    return rc;
}
#ifdef FR_PLATFORM_FILES_TEST
int fr_platform_files_test_open(struct fr_platform_files *p,int root,uid_t owner,const char *sha) {
    return prepare(p,root,owner,sha,0);
}
#endif
