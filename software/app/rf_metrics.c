#include "rf_metrics.h"
#include <math.h>
#include <string.h>
static double db20(double x){ return 20.0*log10(x>1e-12?x:1e-12); }
int rf_metrics_analyze(const double *code,size_t bins,double fs_hz,
                       unsigned fundamental_bin,struct rf_metrics_result *out){
    if(!code||!out||bins<8||!isfinite(fs_hz)||fs_hz<=0||fundamental_bin<2||
       fundamental_bin>=bins/3||!isfinite(code[fundamental_bin])||code[fundamental_bin]<=0) return -1;
    memset(out,0,sizeof(*out)); out->bin_hz=fs_hz/(2.0*(bins-1));
    out->fundamental_bin=fundamental_bin; out->fundamental_code=code[fundamental_bin];
    out->second_bin=2*fundamental_bin; out->third_bin=3*fundamental_bin;
    double fund=out->fundamental_code,h2=code[out->second_bin],h3=code[out->third_bin];
    out->hd2_db=db20(h2/fund); out->hd3_db=db20(h3/fund);
    double harmonic_power=0,noise_power=0,max_spur=0; unsigned spur=0;
    for(size_t k=1;k<bins;k++) {
        double v=isfinite(code[k])&&code[k]>=0?code[k]:0;
        int near_f=fabs((double)k-(double)fundamental_bin)<=2.0;
        int harmonic=(k==out->second_bin||k==out->third_bin);
        if(harmonic) harmonic_power+=v*v;
        if(!near_f && !harmonic) {
            noise_power+=v*v;
            if(v>max_spur){max_spur=v;spur=(unsigned)k;}
        }
    }
    out->spur_bin=spur; out->sfdr_db=db20(fund/(max_spur>0?max_spur:1e-12));
    out->thd_percent=100.0*sqrt(harmonic_power)/(fund>0?fund:1.0);
    out->snr_db=10.0*log10((fund*fund)/(noise_power>1e-24?noise_power:1e-24));
    size_t noise_bins=bins-1-5-2; if(noise_bins<1) noise_bins=1;
    out->noise_floor_db=db20(sqrt(noise_power/(double)noise_bins)/(fund>0?fund:1.0));
    double threshold=fund*pow(10.0,-20.0/20.0); size_t lo=fundamental_bin,hi=fundamental_bin;
    while(lo>1 && code[lo-1]>=threshold) --lo;
    while(hi+1<bins && code[hi+1]>=threshold) ++hi;
    out->occupied_bw_hz=(double)(hi-lo)*out->bin_hz; out->valid=1; return 0;
}
