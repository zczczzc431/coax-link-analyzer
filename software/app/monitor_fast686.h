#ifndef MONITOR_FAST686_H
#define MONITOR_FAST686_H
#include <stdint.h>
/* Host scheduling only. Neither a sample clock nor a no-blind-time guarantee. */
struct m686_cadence { uint64_t count,last_us,max_gap_us; uint32_t token; };
static inline int m686_eligible(int page,int paused,int single,int sweep,int reflect,int borrowed) {
    return page && !paused && !single && !sweep && !reflect && !borrowed;
}
static inline unsigned m686_delay_us(int fast) { return fast?0u:50000u; }
/* MONITOR must not wait for a disappearing edge before seeing an outage.
 * FREE is an existing mailbox mode; SCOPE/SINGLE/borrowed captures unchanged. */
static inline int m686_capture_mode(int fast,int ordinary_mode) { return fast?0:ordinary_mode; }
static inline int m686_observe(struct m686_cadence *c,uint32_t token,uint64_t now) {
    if(!c || !token || (c->count && (token==c->token || now<=c->last_us))) return -1;
    if(c->count && now-c->last_us>c->max_gap_us) c->max_gap_us=now-c->last_us;
    c->last_us=now;c->token=token;c->count++;return 0;
}
#endif
