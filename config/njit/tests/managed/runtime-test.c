#define _GNU_SOURCE
#include "njit_managed.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
int main(int argc,char **argv){
 (void)argv;
 if(argc>1)return njit_runtime_init()==0?1:0;
 assert(njit_runtime_init()==0);njit_event("authenticated");
 pid_t pid=fork();assert(pid>=0);if(pid==0){execl("/proc/self/exe","runtime-test","second",NULL);_exit(2);}
 int status;assert(waitpid(pid,&status,0)==pid);assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);
 assert(raise(SIGTERM)==0);assert(njit_stopping());njit_wait_for_manual_retry();njit_runtime_close();
 FILE *f=fopen(NJIT_RUNDIR "/state.json","r");assert(f);char data[256];assert(fgets(data,sizeof(data),f));fclose(f);
 puts(data);return 0;
}
