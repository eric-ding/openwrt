#include "njit_managed.h"
int njit_frame_valid(const uint8_t *p,size_t n) {
    if(!p || n<22 || p[12]!=0x88 || p[13]!=0x8e || p[15]!=0) return 0;
    size_t ol=((size_t)p[16]<<8)|p[17],e=((size_t)p[20]<<8)|p[21];
    if(ol<4 || ol>n-18 || e<4 || e>ol) return 0;
    size_t end=18+e;
    switch(p[18]) {
    case 1: /* Request; all consumers read the type byte. */
        if(e<5) return 0;
        if(p[22]==4 && (e<22 || p[23]!=16))return 0;
        return 1;
    case 3: return 1; /* Success */
    case 4: /* Standard Failure is 4 bytes; vendor extension is optional. */
        if(e==4)return 1;
        if(e<6)return 0;
        return (size_t)p[23]<=end-24;
    case 10:
        if(e<9)return 0;
        return p[26]!=0x35 || e>=41;
    default: return 1; /* Only ID is used by the legacy unknown-code branch. */
    }
}
