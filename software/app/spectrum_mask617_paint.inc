/* Rendering only: never calculates FFT, feeds, or writes files. */
static const char *mask617_state_name(enum sm612_state state) {
 const char *names[]={"NO TEMPLATE","LEARNING","READY","ARMED","CANDIDATE","ALARM","INVALID","SUSPENDED"};
 return (unsigned)state<sizeof names/sizeof names[0]?names[state]:"UNKNOWN";
}
static void render_mask617(struct screen *s) {
 const uint32_t ink=0xe4edf5,accent=0xffb75e,wave=0x00e0aa;char b[160];
 text(s,20,46,"SPECTRAL MASK - RAW FRAME EVIDENCE",2,ink);
 power_box(s,535,42,115,26,"100 MHZ",mask617_config.last_bin==512?accent:ink);
 power_box(s,660,42,115,26,"120 MHZ",mask617_config.last_bin==614?accent:ink);
 power_box(s,535,74,115,24,"STD -60",mask617_config.floor_db==-60?accent:ink);
 power_box(s,660,74,115,24,"RAW -100",mask617_config.floor_db==-100?accent:ink);
 const struct sm612_model *m=&mask617_runtime.capture.core;
 snprintf(b,sizeof b,"STATE %s  REASON %u  LEARN %u/%u",mask655_pending?"AUDIT ERROR":mask617_state_name(m->state),
  (unsigned)m->reason,m->learned,mask617_config.learn_frames);
 text(s,20,76,b,2,m->state==SM612_ALARM?accent:ink);
 text(s,20,104,mask617_notice,1,accent);
 snprintf(b,sizeof b,"STORE %s  ACTIVE PENDING %d  CLOSED BLOCKED %d  SAVED %u",mask617_ready?"OPEN":"UNAVAILABLE",
  mask617_runtime.pending,mask617_runtime.capture.blocked,mask617_closed_count);
#ifdef MONITOR_ASYNC694
 if(mask694_selected) snprintf(b,sizeof b,"ASYNC QUEUED %u/%u WRITE %u FAIL %u  SAVED %u  CHECKPOINTS %u",
  mask694_store_state.queue.occupied,mask694_store_state.queue.capacity,mask694_store_state.queue.inflight,
  mask694_store_state.queue.failed,mask617_closed_count,mask694_checkpoints_saved);
#endif
 int store_bad694=mask617_runtime.pending || mask617_runtime.capture.blocked || !mask617_ready;
#ifdef MONITOR_ASYNC694
 if(mask694_selected && (mask694_error || mask694_store_state.producer_blocked ||
    mask694_store_state.queue.failed))store_bad694=1;
#endif
 text(s,20,119,b,1,store_bad694?accent:wave);
 const int left=45,right=s->w-25,top=145,bottom=275;
 line(s,left,top,left,bottom,ink);line(s,left,bottom,right,bottom,ink);
 for(int x=left+1;x<=right;x++) {
  unsigned k=(unsigned)((uint64_t)(x-left)*(N/2)/(right-left));
  unsigned p=(unsigned)((uint64_t)(x-left-1)*(N/2)/(right-left));
  if(k>N/2) k=N/2;
  double db[3]={spectrum_cache_valid?spectrum_cache.db[k]:NAN,m->upper_db[k],m->lower_db[k]};
  double old[3]={spectrum_cache_valid?spectrum_cache.db[p]:NAN,m->upper_db[p],m->lower_db[p]};
  const uint32_t color[]={wave,0xffb75e,0x58aaff};
  for(unsigned j=0;j<3;j++) {
   if(j==2) continue; /* Sparse lower coverage is drawn as explicit ticks below. */
   if((j && (!m->template_valid || k<m->config.first_bin || k>m->config.last_bin ||
       m->config.exclude[k] || (j==2 && !m->lower_valid[k]))) || !isfinite(db[j]) || !isfinite(old[j])) continue;
   int y=top+(int)lround(-fmax(-120.0,fmin(0.0,db[j]))*(bottom-top)/120.0);
   int py=top+(int)lround(-fmax(-120.0,fmin(0.0,old[j]))*(bottom-top)/120.0);
   plot_line(s,x-1,py,x,y,color[j],left,top,right,bottom);
  }
 }
 if(m->template_valid) for(unsigned k=m->config.first_bin;k<=m->config.last_bin;k++) {
  if(m->config.exclude[k] || !m->lower_valid[k]) continue;
  int x=left+(int)((uint64_t)k*(right-left)/(N/2));
  int y=top+(int)lround(-fmax(-120.0,fmin(0.0,m->lower_db[k]))*(bottom-top)/120.0);
  plot_line(s,x-2,y,x+2,y,0x58aaff,left,top,right,bottom);
 }
 text(s,45,282,"0 MHZ      RAW GREEN / UPPER AMBER / LOWER BLUE              200 MHZ",1,ink);
 snprintf(b,sizeof b,"LIMIT BAND %.3f-%.3f MHZ  UPPER/LOWER %.1f/%.1f DB  HYST %.1f DB",
  mask617_config.first_bin*FFT_BIN_HZ/1e6,mask617_config.last_bin*FFT_BIN_HZ/1e6,
  mask617_config.upper_margin_db,mask617_config.lower_margin_db,mask617_config.hysteresis_db);
 text(s,20,301,b,1,ink);
 unsigned upper=0,lower=0;
 if(m->template_valid) for(unsigned k=m->config.first_bin;k<=m->config.last_bin;k++) {
  if(m->config.exclude[k]) continue;
  upper++;lower+=m->lower_valid[k]!=0;
 }
 snprintf(b,sizeof b,"LOCAL CONTRAST V1  UPPER %u BINS / LOWER %u BINS  AUDIT %s",upper,lower,
  mask655_pending?"ERROR":mask655_authorized?"CURRENT":"STALE/NEED LEARN");
 text(s,20,316,b,1,lower?ink:accent);
 text(s,20,331,"OTHER BINS: UPPER ONLY / WEAK HARMONIC LOSS MAY NOT BE COVERED",1,accent);
 text(s,20,346,"SINGLE-FRAME TRANSIENTS RETAINED / SPARSE SNAPSHOTS / GAPS MAY MISS EVENTS",1,ink);
 snprintf(b,sizeof b,"BAND <= %.3f MHZ / MIN %.0f DBFS / OUTSIDE OR BELOW: NOT TESTED / RELEARN",
  mask617_config.last_bin*FFT_BIN_HZ/1e6,mask617_config.floor_db);
 text(s,20,360,b,1,accent);
 const char *buttons[]={"LEARN","ARM","STOP","RETRY","BACK"};
 for(int j=0;j<5;j++) power_box(s,20+j*152,375,146,45,buttons[j],j==2?accent:ink);
 text(s,20,439,"LEARN -> READY -> ARM. SOURCE/OWNER CHANGE REQUIRES EXPLICIT ACTION.",1,ink);
 text(s,20,454,"NO AUTOMATIC REARM. NO SIGNAL IS STILL A VALID LOWER-LIMIT TEST.",1,ink);
}
