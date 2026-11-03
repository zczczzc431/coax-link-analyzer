#include "scope_precision682.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"PRECISION682_FAIL line=%d\n",__LINE__); exit(1); } } while(0)
int main(void) {
    int fit=99;
    CHECK(sp682_frequency_mhz(1,1,1.2,1000,&fit)==1.0 && fit==1);
    double periods[]={1250,1000.0/1.137,1000.0/1.5};
    for(unsigned j=0;j<3;j++) {
        double f=sp682_frequency_mhz(1,1,1.2,periods[j],&fit);
        CHECK(fabs(f*periods[j]-1000.0)<1e-10 && fit);
    }
    CHECK(sp682_frequency_mhz(0,1,1.2,1000,&fit)==1.2 && !fit);
    CHECK(sp682_frequency_mhz(1,0,0,1000,&fit)==0 && !fit);
    CHECK(sp682_frequency_mhz(1,1,1.2,NAN,&fit)==1.2 && !fit);
    CHECK(sp682_frequency_mhz(1,1,1.2,0,&fit)==1.2 && !fit);
    CHECK(sp682_frequency_mhz(1,1,1.2,INFINITY,&fit)==1.2 && !fit);
    CHECK(isnan(sp682_frequency_mhz(1,1,NAN,1000,&fit)) && !fit);
    uint32_t a[64],copy[64]; double offset=99;
    for(int falling=0;falling<2;falling++) for(unsigned index=3;index<61;index+=7) {
        for(unsigned phase=0;phase<20;phase++) {
            for(unsigned i=0;i<64;i++) a[i]=i<index?6000:10000;
            /* Crossing sweeps 0.025..0.975 of the local sample interval. */
            a[index-1]=8190-phase*20; a[index]=8590-phase*20;
            if(falling) for(unsigned i=0;i<64;i++) a[i]=16000-a[i];
            memcpy(copy,a,sizeof a);
            int level=falling?7800:8200;
            CHECK(sp682_edge_shift(a,64,3,index,level,falling,6000,10000,1,&offset));
            double expect=-1.0+(10.0+phase*20)/400.0;
            CHECK(fabs(offset-expect)<1e-10 && memcmp(copy,a,sizeof a)==0);
            /* Applying x(sample-offset) puts the crossing at integer index. */
            CHECK(fabs(((double)index+expect-offset)-(double)index)<1e-10);
            CHECK(sp682_plot_x(45,800,40,(int)index,offset)==
                  45+(int)floor(((double)index-expect)*20.0));
        }
    }
    for(unsigned i=0;i<64;i++) a[i]=i<32?6000:10000;
    CHECK(sp682_edge_shift(a,64,3,32,8000,0,6000,10000,1,&offset) && offset==-0.5);
    const unsigned flags[]={0,1,2,7};
    for(unsigned j=0;j<4;j++) CHECK(!sp682_edge_shift(a,64,flags[j],32,8000,0,6000,10000,1,&offset) && offset==0);
    CHECK(!sp682_edge_shift(a,64,3,32,8000,1,6000,10000,1,&offset));
    CHECK(!sp682_edge_shift(a,64,3,20,8000,0,6000,10000,1,&offset));
    CHECK(!sp682_edge_shift(a,64,3,32,8000,0,6000,10000,0,&offset));
    CHECK(!sp682_edge_shift(a,64,3,32,8000,0,0,16383,1,&offset));
    CHECK(!sp682_edge_shift(a,64,3,32,6001,0,6000,10000,1,&offset));
    CHECK(!sp682_edge_shift(a,64,3,1,8000,0,6000,10000,1,&offset));
    CHECK(!sp682_edge_shift(a,64,3,62,8000,0,6000,10000,1,&offset));
    CHECK(!sp682_edge_shift(NULL,64,3,32,8000,0,6000,10000,1,&offset));
    a[31]=7998;a[32]=8002;
    CHECK(!sp682_edge_shift(a,64,3,32,8000,0,6000,10000,1,&offset));
    a[30]=7000;a[31]=9000;a[32]=7000;a[33]=9000;
    CHECK(!sp682_edge_shift(a,64,3,32,8000,0,6000,10000,1,&offset));
    for(unsigned i=0;i<64;i++) a[i]=i<31?6000:10000;
    CHECK(!sp682_edge_shift(a,64,3,32,8000,0,6000,10000,1,&offset) && offset==0);
    a[30]=6000;a[31]=8668; /* delta about -1.25 but outside: reject */
    CHECK(!sp682_edge_shift(a,64,3,32,8000,0,6000,10000,1,&offset));
    a[31]=8660; /* delta -1.248.. inside budget: accept */
    CHECK(sp682_edge_shift(a,64,3,32,8000,0,6000,10000,1,&offset));
    const int visible[]={40,80,200,400,800,2000};
    for(unsigned k=0;k<6;k++) for(int i=0;i<visible[k];i++)
        CHECK(sp682_plot_x(45,730,visible[k],i,0)==45+i*730/visible[k]);
    CHECK(sp682_plot_x(45,730,0,1,0)==45);
    CHECK(sp682_plot_x(45,730,40,1,NAN)==45);
    CHECK(sp682_plot_x(45,800,40,10,-0.5)==255);
    CHECK(sp682_plot_x(45,800,40,10,0.5)==235);
    printf("PRECISION682_UNIT_PASS checks=%u DISPLAY_ONLY NO_MMIO\n",checks);
    return 0;
}
