#include "njit_managed.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void base(unsigned char *p,unsigned code,unsigned len){memset(p,0,128);p[12]=0x88;p[13]=0x8e;p[17]=len;p[18]=code;p[21]=len;}
int main(void){
 unsigned char p[128];assert(!njit_frame_valid(NULL,0));
 for(size_t n=0;n<128;n++){unsigned char *q=malloc(n?n:1);memset(q,0,n);assert(!njit_frame_valid(q,n));free(q);}
 base(p,1,22);p[22]=4;p[23]=16;assert(njit_frame_valid(p,40));assert(!njit_frame_valid(p,39));p[23]=255;assert(!njit_frame_valid(p,40));
 base(p,3,4);assert(njit_frame_valid(p,22));base(p,4,4);assert(njit_frame_valid(p,22));
 base(p,4,11);p[22]=9;p[23]=5;assert(njit_frame_valid(p,29));p[23]=6;assert(!njit_frame_valid(p,29));
 base(p,10,9);p[26]=0x35;assert(!njit_frame_valid(p,27));base(p,10,41);p[26]=0x35;assert(njit_frame_valid(p,59));
 for(unsigned seed=0;seed<10000;seed++){size_t n=seed%128;unsigned char *q=malloc(n?n:1);for(size_t i=0;i<n;i++)q[i]=(unsigned char)(seed*31+i*17);(void)njit_frame_valid(q,n);free(q);}
 puts("frame checks passed");return 0;
}
