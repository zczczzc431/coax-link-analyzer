#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifndef TEST699_ROOT
#error "TEST699_ROOT must be the fresh approved test directory"
#endif
static int fake_gen699(int,int,int,int);
#define GEN_CONTROL_EXEC fake_gen699
#define gen_control_exec fake_gen699
#define DIAG_STORAGE_DIR TEST699_ROOT "/data"
#define REFLECT_STORAGE_DIR TEST699_ROOT "/reflect"
#define SCOPE_VIEW_PATH TEST699_ROOT "/scope.view"
#define REFLECT_RAW_PAGE 1
#define main scope699_program_main
#include "adc_scope_gen_display.c"
#undef main
#undef gen_control_exec
static unsigned checks699,gen_calls699;
#define CHECK699(x,label) do { checks699++; if(!(x)){fprintf(stderr,"HOLD699_FAIL line=%d %s\n",__LINE__,label);exit(1);} } while(0)
static int fake_gen699(int on,int frequency,int wave,int amplitude) {
 (void)on;(void)frequency;(void)wave;(void)amplitude;gen_calls699++;return 0;
}
static struct diag_monitor_config cfg699(uint64_t hold,unsigned exit_frames) {
 struct diag_monitor_config c={16,0.65,0.82,0.20,2,exit_frames,hold,16.0};return c;
}
static void fixture699(const char *text699) {
 FILE *f=fopen(MONITOR_CONFIG_PATH,"w");
 CHECK699(f!=NULL,"config fixture open");
 CHECK699(fputs(text699,f)>=0,"config fixture write");
 CHECK699(fclose(f)==0,"config fixture close");
}
static size_t read699(const char *path,char *data,size_t cap) {
 FILE *f=fopen(path,"rb");CHECK699(f!=NULL,"read open");
 size_t n=fread(data,1,cap-1,f);CHECK699(!ferror(f)&&feof(f),"read complete");
 CHECK699(fclose(f)==0,"read close");data[n]=0;return n;
}
static void cycle699(void) {
 const uint64_t p[]={10,20,50,100,250,500,750,1000,1500,2000,3000,5000};
 CHECK699(MONITOR_HOLD_MIN_MS==10 && MONITOR_HOLD_MAX_MS==5000,"public range");
 for(unsigned i=0;i<sizeof p/sizeof p[0];i++)
  CHECK699(diag_next_recovery_hold_ms(p[i])==p[(i+1)%(sizeof p/sizeof p[0])],"exact12 preset cycle");
 CHECK699(diag_next_recovery_hold_ms(15)==20,"nonpreset15 rounds to20");
 CHECK699(diag_next_recovery_hold_ms(0)==10,"below minimum selects10");
 CHECK699(diag_next_recovery_hold_ms(4999)==5000,"nonpreset4999 selects5000");
 CHECK699(diag_next_recovery_hold_ms(UINT64_MAX)==10,"above range wraps10");
 struct diag_monitor m;diag_monitor_init(&m,cfg699(750,3));
 CHECK699(m.cfg.recovery_hold_ms==750,"default750 retained");
 diag_monitor_init(&m,cfg699(0,3));CHECK699(m.cfg.recovery_hold_ms==750,"zero core default unchanged");
 puts("HOLD699_CYCLE_PASS exact12 nonpreset15_to20 default750");
}
static void config699(void) {
 const uint64_t accepted[]={10,5000,250,750,15};
 for(unsigned i=0;i<sizeof accepted/sizeof accepted[0];i++) {
  char text699[160];snprintf(text699,sizeof text699,"sensitivity=1\nrecovery_hold_ms=%llu\n",(unsigned long long)accepted[i]);
  fixture699(text699);struct diag_monitor_config c=cfg699(750,3);
  diag_load_monitor_config(&c);
  CHECK699(c.recovery_hold_ms==accepted[i]&&diag_sensitivity_level(&c)==1,"accepted inclusive range and old values");
  CHECK699(diag_save_monitor_config(&c)==0,"actual atomic durable config save");
  struct diag_monitor_config loaded=cfg699(1000,4);diag_load_monitor_config(&loaded);
  CHECK699(loaded.recovery_hold_ms==accepted[i]&&diag_sensitivity_level(&loaded)==1,"save reload exact value");
 }
 const char *bad[]={"0","1","9","5001","-1","18446744073709551616","999999999999999999999999999999999","10junk","10.5",""};
 for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++) {
  char text699[200];snprintf(text699,sizeof text699,"sensitivity=2\nrecovery_hold_ms=%s\n",bad[i]);fixture699(text699);
  struct diag_monitor_config c=cfg699(250,4),before;memcpy(&before,&c,sizeof c);diag_load_monitor_config(&c);
  CHECK699(!memcmp(&before,&c,sizeof c),"invalid hold rejects transaction including changed sensitivity");
 }
 const char *bad_lines[]={"recovery_hold_ms=10\nunknown=1\n","sensitivity=3\nrecovery_hold_ms=10\n","not_a_key_value_line\nrecovery_hold_ms=10\n"};
 for(unsigned i=0;i<sizeof bad_lines/sizeof bad_lines[0];i++) {
  fixture699(bad_lines[i]);struct diag_monitor_config c=cfg699(250,4),before;memcpy(&before,&c,sizeof c);
  diag_load_monitor_config(&c);CHECK699(!memcmp(&before,&c,sizeof c),"malformed or unknown config rejects transaction");
 }
 puts("HOLD699_CONFIG_PASS boundaries10_5000 legacy250_750 persistent_roundtrip illegal_transactional");
}
static int feed699(struct diag_monitor *m,uint64_t ms,int healthy,struct diag_monitor_event *e) {
 struct diag_monitor_sample s={ms,healthy?100.0:40.0,healthy?200.0:80.0,1.0,1};
 return diag_monitor_update(m,&s,e);
}
static void fault699(struct diag_monitor *m,unsigned exit_frames) {
 struct diag_monitor_config c=cfg699(10,exit_frames);c.baseline_frames=1;diag_monitor_init(m,c);
 struct diag_monitor_event e;
 CHECK699(feed699(m,1,1,&e)==0&&m->state==DIAG_MONITOR_NORMAL,"real one frame fixture baseline");
 CHECK699(feed699(m,100,0,&e)==0&&m->state==DIAG_MONITOR_NORMAL,"entry needs bad frame confirmation");
 CHECK699(feed699(m,101,0,&e)==0&&m->state==DIAG_MONITOR_FAULT,"second bad enters fault");
}
static void core699(void) {
 struct diag_monitor m;struct diag_monitor_event e;
 fault699(&m,2);CHECK699(feed699(&m,200,1,&e)==0&&m.exit_count==1,"first good starts recovery");
 CHECK699(feed699(&m,209,1,&e)==0&&m.state==DIAG_MONITOR_FAULT&&m.exit_count==2,"two good but9ms not enough");
 CHECK699(feed699(&m,210,1,&e)==1&&m.state==DIAG_MONITOR_NORMAL,"10ms and frames closes");
 CHECK699(e.start_ms==100&&e.end_ms==200&&e.duration_ms==100,"end timestamp is first good not delayed closure");
 fault699(&m,2);CHECK699(feed699(&m,200,1,&e)==0,"two frame case first good");
 CHECK699(feed699(&m,210,1,&e)==1&&e.end_ms==200&&m.exit_count==0,"exactly two good frames with10ms gap closes");
 fault699(&m,3);CHECK699(feed699(&m,200,1,&e)==0,"three frame case first good");
 CHECK699(feed699(&m,210,1,&e)==0&&m.exit_count==2,"elapsed10ms does not bypass three frame gate");
 CHECK699(feed699(&m,211,1,&e)==1&&e.end_ms==200,"third good closes with original healthy onset");
 fault699(&m,2);CHECK699(feed699(&m,200,1,&e)==0&&feed699(&m,209,1,&e)==0,"interrupted recovery begins");
 CHECK699(feed699(&m,210,0,&e)==0&&m.exit_count==0&&m.recovery_start_ms==0,"bad resets good streak and timer");
 CHECK699(feed699(&m,211,1,&e)==0&&feed699(&m,220,1,&e)==0,"new streak9ms still fault");
 CHECK699(feed699(&m,221,1,&e)==1&&e.end_ms==211,"new full10ms closes with new recovery onset");
 puts("HOLD699_CORE_PASS 10ms_and_good_frames independent_gates bad_resets first_good_end");
}
static void ui699(void) {
 const uint64_t p[]={10,20,50,100,250,500,750,1000,1500,2000,3000,5000};
 struct touch touch699;memset(&touch699,0,sizeof touch699);
 diag_ui_init(&diag_ui_state);diag_ui_state.mode=DIAG_UI_DIAG;diag_ui_state.page=DIAG_PAGE_MONITOR;
 mask617_panel=0;sm612_defaults(&mask617_config);
 struct sm612_config mask_before;memcpy(&mask_before,&mask617_config,sizeof mask_before);
 uint64_t source=mask617_source_revision,command=mask617_command_revision;
 int authorized=mask655_authorized,pending=mask655_pending;unsigned sequence=17;
 for(unsigned event=0;event<2;event++) {
  diag_monitor_init(&diag_monitor_state,cfg699(5000,3));diag_monitor_state.state=DIAG_MONITOR_NORMAL;
  diag_monitor_state.event_sequence=sequence;diag_have_event=(int)event;
  for(unsigned i=0;i<sizeof p/sizeof p[0];i++) {
   CHECK699(drag_press(&touch699,720,286,800,480)==1,"actual HOLD touch handled in both layouts");
   CHECK699(diag_monitor_state.cfg.recovery_hold_ms==p[i],"actual touch follows12 cycle");
   struct diag_monitor_config loaded=cfg699(750,3);diag_load_monitor_config(&loaded);
   CHECK699(loaded.recovery_hold_ms==p[i],"touch durable config reload");
   CHECK699(diag_monitor_state.event_sequence==sequence,"HOLD touch preserves event numbering");
  }
 }
 diag_monitor_state.cfg.recovery_hold_ms=15;CHECK699(drag_press(&touch699,720,286,800,480)==1&&diag_monitor_state.cfg.recovery_hold_ms==20,"actual touch15_to20");
 diag_monitor_state.cfg.recovery_hold_ms=250;CHECK699(diag_save_monitor_config(&diag_monitor_state.cfg)==0,"fault test starting persisted config");
 char before[4096],after[4096];size_t before_n=read699(MONITOR_CONFIG_PATH,before,sizeof before);
 diag_monitor_state.state=DIAG_MONITOR_FAULT;
 CHECK699(drag_press(&touch699,720,286,800,480)==1&&diag_monitor_state.cfg.recovery_hold_ms==250,"FAULT rejects HOLD touch");
 size_t after_n=read699(MONITOR_CONFIG_PATH,after,sizeof after);
 CHECK699(before_n==after_n&&!memcmp(before,after,before_n),"FAULT causes no config write");
 CHECK699(!memcmp(&mask_before,&mask617_config,sizeof mask_before)&&source==mask617_source_revision&&command==mask617_command_revision&&authorized==mask655_authorized&&pending==mask655_pending,"HOLD is independent of MASK config audit and revisions");
 diag_monitor_state.state=DIAG_MONITOR_NORMAL;diag_have_event=0;
 puts("HOLD699_UI_PASS actual_drag_press both_layouts cycle persistence FAULT_mask_independence");
}
static void ppm699(const struct screen *s,const char *name) {
 char path[PATH_MAX];int n=snprintf(path,sizeof path,"%s/%s",TEST699_ROOT,name);CHECK699(n>0&&(size_t)n<sizeof path,"render path bounded");
 FILE *f=fopen(path,"wb");CHECK699(f!=NULL,"render output open");
 CHECK699(fprintf(f,"P6\n%d %d\n255\n",s->w,s->h)>0,"ppm header");
 for(int i=0;i<s->w*s->h;i++) {uint32_t c=s->rgb[i];unsigned char px[3]={(unsigned char)(c>>16),(unsigned char)(c>>8),(unsigned char)c};if(fwrite(px,1,3,f)!=3) CHECK699(0,"ppm pixel");}
 CHECK699(fclose(f)==0,"render close");
}
static void report_render699(void) {
 char report[65536];
 diag_monitor_state.cfg.recovery_hold_ms=10;CHECK699(diag_export_report()==0,"actual report10 export");
 read699(REPORT_RESULT_PATH,report,sizeof report);
 CHECK699(strstr(report,"recovery_hold_ms,10\n")&&strstr(report,"recovery_hold_min_ms,10\n")&&strstr(report,"recovery_hold_max_ms,5000\n"),"report actual value and inclusive bounds");
 CHECK699(strstr(report,"recovery_hold_semantics,RMS_VPP_FREQUENCY_RECOVERY_NOT_SAMPLE_PERIOD\n")&&strstr(report,"|hold_ms=10|"),"report recovery semantics and signature");
 struct screen screen699;memset(&screen699,0,sizeof screen699);screen699.w=800;screen699.h=480;
 screen699.rgb=calloc(800u*480u,sizeof *screen699.rgb);CHECK699(screen699.rgb!=NULL,"RAM rendering allocation");
 diag_have_sample=0;diag_have_event=0;diag_event_history_count=0;
 diag_ui_state.page=DIAG_PAGE_MONITOR;render_diag_panel(&screen699,NULL,0,0);ppm699(&screen699,"monitor_hold10_noevent.ppm");
 diag_monitor_state.cfg.recovery_hold_ms=5000;diag_have_event=1;diag_last_event=(struct diag_monitor_event){1,100,200,100,0.4,100,40,200,80,1,2};
 memset(screen699.rgb,0,800u*480u*sizeof *screen699.rgb);render_diag_panel(&screen699,NULL,0,0);ppm699(&screen699,"monitor_hold5000_event.ppm");
 diag_have_event=0;diag_ui_state.page=DIAG_PAGE_SELFTEST;memset(screen699.rgb,0,800u*480u*sizeof *screen699.rgb);render_diag_panel(&screen699,NULL,0,0);ppm699(&screen699,"selftest_hold5000.ppm");
 diag_monitor_state.cfg.recovery_hold_ms=10;diag_ui_state.page=DIAG_PAGE_REPORT;memset(screen699.rgb,0,800u*480u*sizeof *screen699.rgb);render_diag_panel(&screen699,NULL,0,0);ppm699(&screen699,"report_hold10.ppm");
 free(screen699.rgb);
 diag_monitor_state.cfg.recovery_hold_ms=5000;CHECK699(diag_export_report()==0,"actual report5000 export");read699(REPORT_RESULT_PATH,report,sizeof report);
 CHECK699(strstr(report,"recovery_hold_ms,5000\n")&&strstr(report,"|hold_ms=5000|"),"report upper bound value and signature");
 puts("HOLD699_REPORT_RENDER_PASS real_report_markers RAM800x480 MONITOR2_REPORT_SELFTEST no_LCD");
}
int main(void) {
 CHECK699(mkdir(DIAG_STORAGE_DIR,0700)==0,"fresh test data directory");
 CHECK699(mkdir(REFLECT_STORAGE_DIR,0700)==0,"fresh test reflect directory");
 cycle699();config699();core699();ui699();report_render699();
 CHECK699(gen_calls699==0,"no generator control invoked");
 printf("HOLD699_PASS checks=%u ACTUAL_UI_AND_CORE SYNTHETIC_TIME_NO_HARDWARE\n",checks699);return 0;
}
