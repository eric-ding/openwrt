#define _DEFAULT_SOURCE
/* auth.c comes from the hash-verified archive plus our exact package patch. */
#include "auth.c"
static unsigned sent;
int __wrap_pcap_sendpacket(pcap_t *handle,const unsigned char *packet,int size){
 (void)handle;assert(size>=18 && size<=256);assert(packet[12]==0x88 && packet[13]==0x8e);
 if(packet[15]==0){unsigned n=((unsigned)packet[16]<<8)|packet[17];assert(n==(unsigned)size-18);}
 sent++;return 0;
}
void GetIpFromDevice(uint8_t ip[4],const char *device){(void)device;memset(ip,0,4);}
int main(void){
 uint8_t request[64]={0},eth[14]={0},ip[4]={0};char user[89],password[112];
 memset(user,'u',88);user[88]=0;memset(password,'p',111);password[111]=0;
 eth[12]=0x88;eth[13]=0x8e;request[18]=REQUEST;
 for(unsigned n=1;n<=88;n++){
  user[n]=0;request[22]=IDENTITY;SendFirstResponseIdentity(NULL,request,eth,ip,user);
  SendResponseIdentity(NULL,request,eth,ip,user);request[22]=AVAILABLE;SendResponseAvailable(NULL,request,eth,ip,user);
  request[22]=MD5;SendResponseMD5(NULL,request,eth,user,password);user[n]='u';
 }
 assert(sent==352);puts("352 offline response boundary checks passed");return 0;
}
