#define _POSIX_C_SOURCE 200809L
/* Candidate PS CLI. Dry-run unless --apply --confirm-map 0x41230000 is given.
 * Only run after the matching PL bitstream is proven active; never auto-start.
 * AXI GPIO output registers cannot be used as a trustworthy state readback.
 */
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

enum { BASE=0x41230000, GPIO_DATA=0, GPIO2_DATA=8 };
static int number(const char *s,double *out) {
    char *end; errno=0; *out=strtod(s,&end);
    return errno==0 && end!=s && *end==0 && isfinite(*out);
}
static uint32_t tuning(double mhz,int mode) {
    double clock_mhz=mode==3?1600.0:400.0;
    return (uint32_t)llround(mhz*(4294967296.0/clock_mhz));
}
static uint32_t control(int enabled,int mode,double percent) {
    uint32_t amplitude=(uint32_t)llround(percent*32767.0/100.0);
    return ((uint32_t)enabled<<30)|((uint32_t)mode<<28)|amplitude;
}
static int selftest(void) {
    if(tuning(10,0)!=0x06666666U || tuning(20,0)!=0x0ccccccdU ||
       tuning(30,0)!=0x13333333U || tuning(30,3)!=0x04cccccdU ||
       tuning(100,3)!=0x10000000U ||
       tuning(32,1)!=0x147ae148U ||
       tuning(150,3)!=0x18000000U || tuning(200,3)!=0x20000000U ||
       tuning(250,3)!=0x28000000U ||
       (control(1,0,25)&0xc000ffffU)!=0x40002000U ||
       (control(1,3,20)&0x7000ffffU)!=0x70001999U ||
       (control(0,2,50)&0x7000ffffU)!=0x20004000U) return 1;
    puts("DACCTL_SELFTEST_PASS tuning/control math; no MMIO access"); return 0;
}
static void delay_ms(long ms) {
    struct timespec t={ms/1000,(ms%1000)*1000000L};
    while(nanosleep(&t,&t)<0 && errno==EINTR) {}
}
int main(int argc,char **argv) {
    double freq=10,percent=25; int mode=0,enabled=1,apply=0,confirm=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--selftest")) return selftest();
        if(!strcmp(argv[i],"--freq") && i+1<argc) {
            if(!number(argv[++i],&freq)) return 2;
        } else if(!strcmp(argv[i],"--amp-percent") && i+1<argc) {
            if(!number(argv[++i],&percent)) return 2;
        } else if(!strcmp(argv[i],"--wave") && i+1<argc) {
            const char *v=argv[++i];
            if(!strcmp(v,"sine")) mode=0;
            else if(!strcmp(v,"square")) mode=1;
            else if(!strcmp(v,"triangle")) mode=2;
            else if(!strcmp(v,"nco")) mode=3;
            else return 2;
        } else if(!strcmp(argv[i],"--off")) enabled=0;
        else if(!strcmp(argv[i],"--apply")) apply=1;
        else if(!strcmp(argv[i],"--confirm-map") && i+1<argc) {
            confirm=!strcmp(argv[++i],"0x41230000");
        } else {
            fprintf(stderr,"Usage: %s [--freq 1..32 --wave sine|square|triangle | --freq 30..250 --wave nco] [--amp-percent 0..50] [--off] [--apply --confirm-map 0x41230000]\n",argv[0]);
            return 2;
        }
    }
    if(((mode==3) && (freq<30 || freq>250)) ||
       ((mode!=3) && (freq<1 || freq>32)) || percent<0 || percent>50) {
        fprintf(stderr,"Invalid candidate frequency or digital-amplitude range\n"); return 2;
    }
    uint32_t step=tuning(freq,mode),cfg=control(enabled,mode,percent);
    printf("DAC_CANDIDATE %s FREQ=%.6fMHz WAVE=%s AMP_DIGITAL=%.1f%% STEP=0x%08x CTRL=0x%08x VOLTS=UNCALIBRATED\n",
           apply?"APPLY":"DRY_RUN",freq,mode==0?"SINE":mode==1?"SQUARE":mode==2?"TRIANGLE":"NCO_SINE",percent,step,cfg);
    if(!apply) return 0;
    if(!confirm || geteuid()!=0) {
        fprintf(stderr,"Refusing MMIO: matching candidate bitstream and root must be verified first\n"); return 2;
    }
    int lock=open("/run/lock/dac_control.lock",O_RDWR|O_CREAT,0600);
    if(lock<0 || flock(lock,LOCK_EX|LOCK_NB)<0) { perror("DAC control lock"); return 1; }
    int fd=open("/dev/mem",O_RDWR|O_SYNC);
    if(fd<0) { perror("/dev/mem"); close(lock); return 1; }
    volatile uint32_t *gpio=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,BASE);
    if(gpio==MAP_FAILED) { perror("DAC GPIO mmap"); close(fd); close(lock); return 1; }
    // PG144: 0x0 GPIO_DATA, 0x8 GPIO2_DATA. Outputs are configured in hardware.
    gpio[GPIO2_DATA/4]=cfg; __sync_synchronize(); delay_ms(2); // deassert old pulse
    gpio[GPIO_DATA/4]=step; __sync_synchronize(); delay_ms(2); // stable 32-bit bus
    gpio[GPIO2_DATA/4]=cfg|0x80000000U; __sync_synchronize(); delay_ms(2);
    gpio[GPIO2_DATA/4]=cfg; __sync_synchronize(); delay_ms(2);
    munmap((void *)gpio,4096); close(fd); close(lock);
    puts("DAC_COMMAND_SENT; verify actual output on OUT1->IN1 frame (not register readback)");
    return 0;
}
