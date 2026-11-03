#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define N 2048
#define FS 400000000.0
#define PI 3.14159265358979323846
static unsigned checks;
static void ck(int ok,const char *msg){checks++;if(!ok){fprintf(stderr,"FFT_PHYSICS_FAIL %s\n",msg);exit(1);}}
static void fft(double *re,double *im){
 for(int i=1,j=0;i<N;i++){int bit=N>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j){double t=re[i];re[i]=re[j];re[j]=t;t=im[i];im[i]=im[j];im[j]=t;}}
 for(int len=2;len<=N;len<<=1){double a=-2*PI/len,wr0=cos(a),wi0=sin(a);for(int b=0;b<N;b+=len){double wr=1,wi=0;for(int j=0;j<len/2;j++){int p=b+j,q=p+len/2;double tr=wr*re[q]-wi*im[q],ti=wr*im[q]+wi*re[q];re[q]=re[p]-tr;im[q]=im[p]-ti;re[p]+=tr;im[p]+=ti;double nw=wr*wr0-wi*wi0;wi=wr*wi0+wi*wr0;wr=nw;}}}
}
int main(void){
 double w[N],sum=0,sumsq=0,re[N],im[N];for(int i=0;i<N;i++){w[i]=.5-.5*cos(2*PI*i/(N-1));sum+=w[i];sumsq+=w[i]*w[i];re[i]=im[i]=0;}
 ck(fabs(sum-(N-1)/2.0)<1e-12,"Hann coherent gain");ck(fabs((N*sumsq)/(sum*sum)-1.50073277967758)<1e-12,"Hann ENBW");
 const int bin=50;const double amp=1234.0,dc=317.0;for(int i=0;i<N;i++)re[i]=(dc+amp*sin(2*PI*bin*i/N))*w[i];fft(re,im);
 double one=2*hypot(re[bin],im[bin])/sum;ck(fabs(one-amp)<1.0,"one-sided amplitude");ck(fabs(re[0]/sum-dc)<1.0,"DC not doubled");
 double et=0,ef=0;for(int i=0;i<N;i++){double x=(dc+amp*sin(2*PI*bin*i/N))*w[i];et+=x*x;}for(int k=0;k<N;k++)ef+=re[k]*re[k]+im[k]*im[k];ck(fabs(ef/N-et)<1e-10*et,"Parseval of actual windowed samples");
 memset(re,0,sizeof(re));memset(im,0,sizeof(im));for(int i=0;i<N;i++)re[i]=amp*sin(2*PI*(bin+.37)*i/N)*w[i];fft(re,im);double skirt=0,peak=0;int pk=0;for(int k=1;k<=N/2;k++){double v=hypot(re[k],im[k]);if(v>peak){peak=v;pk=k;}}for(int k=1;k<=N/2;k++)if(k!=pk)skirt=fmax(skirt,hypot(re[k],im[k]));
 ck(pk==bin||pk==bin+1,"off-bin peak");ck(skirt>0&&skirt<peak,"off-bin leakage");ck(fabs(FS/N-195312.5)<1e-6,"bin spacing");
 memset(re,0,sizeof(re));memset(im,0,sizeof(im));for(int i=0;i<N;i++)re[i]=1000*w[i];fft(re,im);ck(hypot(re[N/2],im[N/2])<1e-6,"DC not Nyquist");
 printf("FFT_PHYSICS_PASS checks=%u N=%d FS=%.0f BIN_HZ=%.6f ENBW_HZ=%.6f\n",checks,N,FS,FS/N,1.50073277967758*FS/N);return 0;
}
