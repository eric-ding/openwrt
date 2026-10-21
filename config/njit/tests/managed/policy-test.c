#define _DEFAULT_SOURCE
#include "njit_managed.h"
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define CONFIG "/tmp/njit-policy-config"
static void options(int boot,int reconnect){
 char data[80];int n=snprintf(data,sizeof(data),"{\"autostart\":%s,\"reconnect\":%s}\n",boot?"true":"false",reconnect?"true":"false");
 int fd=open(CONFIG "/new",O_CREAT|O_WRONLY|O_TRUNC,0600);assert(fd>=0);assert(write(fd,data,(size_t)n)==n);assert(close(fd)==0);assert(rename(CONFIG "/new",CONFIG "/options.json")==0);
}
static void carrier(char c){int fd=open(CONFIG "/carrier",O_CREAT|O_WRONLY|O_TRUNC,0600);assert(fd>=0);assert(write(fd,&c,1)==1);close(fd);}
static void wait_child(pid_t pid){int status;assert(waitpid(pid,&status,0)==pid);assert(WIFEXITED(status) && WEXITSTATUS(status)==0);}
int main(void){
 assert(mkdir(CONFIG,0700)==0);assert(njit_runtime_init()==0);int a,r;
 for(int i=0;i<4;i++){options(i&1,(i>>1)&1);assert(njit_read_options(&a,&r)==0);assert(a==(i&1));assert(r==((i>>1)&1));assert(njit_can_reconnect()==r);}
 options(0,0);carrier('1');
 /* Disabled retries block until the user enables them; they do not return to auth. */
 pid_t pid=fork();assert(pid>=0);if(pid==0){usleep(200000);options(0,1);_exit(0);}
 assert(njit_retry_pause()==0);wait_child(pid);assert(njit_can_reconnect());
 /* Reconnect enabled still waits for carrier, without sending frames. */
 carrier('0');pid=fork();assert(pid>=0);if(pid==0){usleep(200000);carrier('1');_exit(0);}
 assert(njit_wait_link()==0);wait_child(pid);
 /* Turning retry off while backing off must switch to manual waiting. */
 pid_t parent=getpid();pid=fork();assert(pid>=0);if(pid==0){usleep(200000);options(0,0);usleep(1200000);assert(kill(parent,SIGTERM)==0);_exit(0);}
 assert(njit_retry_pause()==-1);assert(njit_stopping());wait_child(pid);njit_runtime_close();
 puts("automatic policy checks passed: all flag combinations, manual pause, live opt-in, carrier wait and live retry disable");return 0;
}
