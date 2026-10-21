#define _GNU_SOURCE
#include "njit_managed.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopping;
static int directory=-1,lockfd=-1;
static unsigned sequence,retries;
static void stop_signal(int sig){(void)sig;stopping=1;}
int njit_stopping(void){return stopping!=0;}
static int permitted_event(const char *s){
    static const char *const names[]={"starting","authenticating","authenticated","authentication_failed",
        "credentials_rejected","waiting_retry","interface_error","stopped","waiting_link","manual_required"};
    for(size_t i=0;i<sizeof(names)/sizeof(*names);i++)if(!strcmp(s,names[i]))return 1;
    return 0;
}
void njit_event(const char *event){
    if(directory<0 || !permitted_event(event))return;
    if(!strcmp(event,"authenticated"))retries=0;
    struct timespec now;if(clock_gettime(CLOCK_MONOTONIC,&now))return;
    char record[256];int n=snprintf(record,sizeof(record),
        "{\"state\":\"%s\",\"pid\":%ld,\"sequence\":%u,\"monotonic_seconds\":%lld}\n",
        event,(long)getpid(),++sequence,(long long)now.tv_sec);
    unlinkat(directory,"state.new",0);
    int fd=openat(directory,"state.new",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0)return;
    struct stat st;
    if(fstat(fd,&st) || st.st_uid!=0 || st.st_nlink!=1 || !S_ISREG(st.st_mode)) {close(fd);return;}
    if(write(fd,record,(size_t)n)==n && !fsync(fd))renameat(directory,"state.new",directory,"state.json");
    close(fd);
}
int njit_runtime_init(void){
    struct rlimit zero={0,0};if(setrlimit(RLIMIT_CORE,&zero))return -1;
    if(mkdir(NJIT_RUNDIR,0700) && errno!=EEXIST)return -1;
    directory=open(NJIT_RUNDIR,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    struct stat st;
    if(directory<0 || fstat(directory,&st) || st.st_uid!=0 || (st.st_mode&077))return -1;
    lockfd=openat(directory,"lock",O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
    if(lockfd<0 || fstat(lockfd,&st) || st.st_uid!=0 || !S_ISREG(st.st_mode) || st.st_nlink!=1)return -1;
    struct flock lock={.l_type=F_WRLCK,.l_whence=SEEK_SET};
    if(fcntl(lockfd,F_SETLK,&lock))return -1;
    struct sigaction sa={0};sa.sa_handler=stop_signal;sigemptyset(&sa.sa_mask);
    if(sigaction(SIGTERM,&sa,0)||sigaction(SIGINT,&sa,0))return -1;
    njit_event("starting");return 0;
}
#ifndef NJIT_CARRIER
#define NJIT_CARRIER "/sys/class/net/wan/carrier"
#endif
int njit_carrier(void){
    int fd=open(NJIT_CARRIER,O_RDONLY|O_CLOEXEC);if(fd<0)return -1;
    char c=0;ssize_t n=read(fd,&c,1);close(fd);
    return n==1 && c=='1'?1:(n==1 && c=='0'?0:-1);
}
static int manual_pause(void){
    njit_event("manual_required");
    while(!stopping && !njit_can_reconnect())sleep(1);
    return stopping?-1:0;
}
int njit_wait_link(void){
    while(!stopping && njit_carrier()==0){
        njit_event("waiting_link");
        if(!njit_can_reconnect() && manual_pause())return -1;
        sleep(1);
    }
    return stopping?-1:0;
}
int njit_retry_pause(void){
    if(!njit_can_reconnect())return manual_pause();
    unsigned delay=5U << (retries<6?retries++:6);
    if(delay>300)delay=300;
    njit_event("waiting_retry");
    while(delay-- && !stopping){
        if(!njit_can_reconnect())return manual_pause();
        sleep(1);
    }
    return stopping?-1:0;
}
void njit_wait_for_manual_retry(void){
    njit_event("credentials_rejected");
    while(!stopping)sleep(1);
}
void njit_runtime_close(void){
    if(stopping)njit_event("stopped");
    if(lockfd>=0)close(lockfd);
    if(directory>=0)close(directory);
    directory=lockfd=-1;
}
