#include "custom_wave/package/custom_wavegen_package.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
static void ck(int ok,const char *name) {
    ++checks;
    if(!ok) { fprintf(stderr,"CUSTOM515_MATH_FAIL %s\n",name); exit(1); }
}
int main(void) {
    const double pi=acos(-1.0);
    struct cwg_table table; struct cf_table normalized;
    struct cwg_header header;
    uint8_t packet[CWG_HEADER_BYTES+CWG_PAYLOAD_BYTES];
    int16_t samples[CWG_TABLE_LEN]; size_t used=0;
    ck(cwg_generate("sin(2*pi*t)",16384,-1000,&table)==CWG_OK,"sine generate");
    for(unsigned i=0;i<CWG_TABLE_LEN;i++)
        ck(fabs(table.q15[i]-(16384*sin(2*pi*i/CWG_TABLE_LEN)-1000))<=1.1,"independent sine sample reference");
    ck(cf_generate("t",&normalized)==CF_ERR_PERIODIC,"nonclosing period refused");
    ck(cf_generate("sin(2*pi*t)/0",&normalized)==CF_ERR_DIVZERO,"division by zero refused");
    ck(cf_generate("clamp(sin(2*pi*t),1,-1)",&normalized)==CF_ERR_RANGE,"inverted clamp refused");
    ck(cwg_generate("sin(2*pi*t)",32767,32767,&table)==CWG_ERR_CLIP,"offset plus amplitude clipping refused");
    ck(cwg_generate("sin(2*pi*t)+0.25*cos(6*pi*t)",12000,0,&table)==CWG_OK,"mixed harmonic generate");
    double s1=0,c1=0,s3=0,c3=0;
    for(unsigned i=0;i<CWG_TABLE_LEN;i++) {
        double phase=2*pi*i/CWG_TABLE_LEN;
        s1+=table.q15[i]*sin(phase); c1+=table.q15[i]*cos(phase);
        s3+=table.q15[i]*sin(3*phase); c3+=table.q15[i]*cos(3*phase);
    }
    ck(fabs(hypot(s3,c3)/hypot(s1,c1)-0.25)<0.0001,"harmonic amplitude ratio preserved after common normalization");
    ck(cwg_pack(&table,42,10000000,12000,0,packet,sizeof(packet),&used)==CWG_OK,"pack");
    ck(cwg_unpack(packet,used,&header,samples,CWG_TABLE_LEN)==CWG_OK,"CRC unpack");
    ck(header.frequency_hz==10000000u && header.transaction==42u,"integer Hz and transaction");
    ck(!memcmp(samples,table.q15,sizeof(samples)),"exact signed sample roundtrip");
    packet[CWG_HEADER_BYTES+1u]^=1u;
    ck(cwg_unpack(packet,used,&header,samples,CWG_TABLE_LEN)==CWG_ERR_CRC,"corruption refused");
    ck(cwg_pack(&table,42,0,12000,0,packet,sizeof(packet),&used)==CWG_ERR_FREQUENCY,"zero Hz refused");
    ck(cwg_pack(&table,42,100000001u,12000,0,packet,sizeof(packet),&used)==CWG_ERR_FREQUENCY,"software range refused");
    printf("CUSTOM515_MATH_PASS checks=%u digital_table_only_not_analog_bandwidth\n",checks);
    return 0;
}
