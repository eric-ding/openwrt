#include "njit_managed.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/resource.h>
int Authentication(const char *,const char *,const char *);
int main(int argc,char **argv){
    struct rlimit zero={0,0};
    if(setrlimit(RLIMIT_CORE,&zero))return 74;
    struct njit_credentials config;
    if(getuid()!=0 || geteuid()!=0){fputs("njit: root required\n",stderr);return 77;}
    if(argc==3 && !strcmp(argv[1],"--check-config")){
        if(njit_read_credentials(argv[2],&config)){fputs("njit: invalid private configuration\n",stderr);return 78;}
        njit_erase(&config,sizeof(config));puts("configuration valid");return 0;
    }
    if(argc!=5 || strcmp(argv[1],"--config") || strcmp(argv[2],NJIT_CREDENTIALS) ||
       strcmp(argv[3],"--interface") || strcmp(argv[4],"wan")){
        fputs("njit: use managed configuration and WAN interface\n",stderr);return 64;
    }
    if(njit_read_credentials(NJIT_CREDENTIALS,&config)){
        fputs("njit: invalid private configuration\n",stderr);return 78;
    }
    if(njit_runtime_init()){njit_erase(&config,sizeof(config));fputs("njit: runtime unavailable or already running\n",stderr);return 73;}
    int result=0;
    while(!njit_stopping()){
        if(njit_wait_link())break;
        result=Authentication(config.username,config.password,argv[4]);
        if(njit_stopping() || njit_retry_pause())break;
    }
    njit_erase(&config,sizeof(config));njit_runtime_close();
    return result;
}
