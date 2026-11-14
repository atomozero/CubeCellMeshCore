#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sc.h"
void old_sc_reduce(unsigned char*); void old_sc_muladd(unsigned char*,const unsigned char*,const unsigned char*,const unsigned char*);
static unsigned long long st=88172645463325252ULL; static unsigned char rnd(){st^=st<<13;st^=st>>7;st^=st<<17;return st>>24;}
static void fill(unsigned char*p,int n,int mode){for(int i=0;i<n;i++)p[i]= mode==1?0xff: mode==2?0: rnd();}
int main(){ long fails=0,N=200000;
 for(long k=0;k<N;k++){ int mode = k<3? k : 3; if(k%7==3) mode=1;
  unsigned char a[64],b[64]; fill(a,64,mode); memcpy(b,a,64); sc_reduce(a); old_sc_reduce(b); if(memcmp(a,b,32)){fails++; if(fails<5)printf("reduce mismatch %ld\n",k);}
  unsigned char x[32],y[32],z[32],s1[32],s2[32]; fill(x,32,mode);fill(y,32,mode);fill(z,32,mode);
  if(k%5==0){x[31]&=0x1f;} /* scalars < 2^253 as in signing */
  sc_muladd(s1,x,y,z); old_sc_muladd(s2,x,y,z); if(memcmp(s1,s2,32)){fails++; if(fails<5)printf("muladd mismatch %ld\n",k);}
 }
 printf("%ld cases x2, fails=%ld\n",N,fails); return fails!=0; }
