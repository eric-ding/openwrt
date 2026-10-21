#define _GNU_SOURCE
/* rpcd exec plugin. Fixed operations only; secrets enter through stdin. */
#include "njit_managed.h"
#include <json-c/json.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifndef NJIT_CONFIG_DIR
#define NJIT_CONFIG_DIR "/etc/njitclient"
#endif
#ifndef NJIT_UCI_DIR
#define NJIT_UCI_DIR "/etc/config"
#endif
#ifndef NJIT_UI_RUNDIR
#define NJIT_UI_RUNDIR "/var/run/njitclient-ui"
#endif
#ifndef NJIT_SERVICE
#define NJIT_SERVICE "/etc/init.d/njitclient"
#endif
#ifndef NJIT_BINARY
#define NJIT_BINARY "/usr/sbin/njit-client"
#endif
#ifndef NJIT_UBUS
#define NJIT_UBUS "/bin/ubus"
#endif
#ifndef NJIT_CARRIER
#define NJIT_CARRIER "/sys/class/net/wan/carrier"
#endif

static json_object *reply;
static void boolean(json_object *o,const char *k,int v){json_object_object_add(o,k,json_object_new_boolean(v));}
static void string(json_object *o,const char *k,const char *v){json_object_object_add(o,k,json_object_new_string(v));}
static void failure(const char *code){boolean(reply,"ok",0);string(reply,"error",code);}
static json_object *get(json_object *o,const char *key){json_object *v=NULL;if(o)json_object_object_get_ex(o,key,&v);return v;}
static long long monotonic_ms(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
/* Only fixed, non-secret arguments reach execv; no shell. Output is bounded. */
static int command(char *const argv[],char *output,size_t cap){
 int pipes[2];if(pipe2(pipes,O_CLOEXEC|O_NONBLOCK))return -1;
 pid_t pid=fork();if(pid<0){close(pipes[0]);close(pipes[1]);return -1;}
 if(pid==0){
  int fd=open("/dev/null",O_RDWR);if(fd<0)_exit(127);
  if(dup2(fd,STDIN_FILENO)<0 || dup2(fd,STDERR_FILENO)<0 || dup2(pipes[1],STDOUT_FILENO)<0)_exit(127);
  int flags=fcntl(STDOUT_FILENO,F_GETFL);fcntl(STDOUT_FILENO,F_SETFL,flags&~O_NONBLOCK);
  close(fd);close(pipes[0]);close(pipes[1]);execv(argv[0],argv);_exit(127);
 }
 close(pipes[1]);size_t used=0;int status=0,done=0,bad=0;long long end=monotonic_ms()+1500;
 for(;;){
  char buffer[512];ssize_t n=read(pipes[0],buffer,sizeof(buffer));
  if(n>0){if((size_t)n>=cap-used){bad=1;break;}memcpy(output+used,buffer,(size_t)n);used+=(size_t)n;}
  else if(n==0 && done)break;
  else if(n<0 && errno!=EAGAIN && errno!=EINTR){bad=1;break;}
  if(!done){pid_t r=waitpid(pid,&status,WNOHANG);if(r==pid)done=1;else if(r<0){bad=1;break;}}
  if(monotonic_ms()>=end){bad=1;break;}
  if(n<=0){struct pollfd p={.fd=pipes[0],.events=POLLIN};poll(&p,1,10);}
 }
 if(!done){kill(pid,SIGKILL);while(waitpid(pid,&status,0)<0 && errno==EINTR){}}
 close(pipes[0]);output[used]=0;
 return !bad && WIFEXITED(status) && WEXITSTATUS(status)==0?0:-1;
}
static json_object *ubus(const char *object,const char *method,const char *args){
 char data[16384];char *argv[]={NJIT_UBUS,"-t","1","call",(char *)object,(char *)method,(char *)args,NULL};
 if(command(argv,data,sizeof(data)))return NULL;
 json_object *o=json_tokener_parse(data);if(o && !json_object_is_type(o,json_type_object)){json_object_put(o);o=NULL;}return o;
}
/* Never infer process health from a historical status file alone. */
static int service_status(int *running,pid_t *pid){
 *running=0;*pid=0;
 json_object *o=ubus("service","list","{\"name\":\"njitclient\"}");if(!o)return -1;
 json_object *instance=get(get(get(o,"njitclient"),"instances"),"main");
 json_object *active=get(instance,"running"),*id=get(instance,"pid");
 if(active && json_object_get_boolean(active) && id && json_object_is_type(id,json_type_int)){
  long long value=json_object_get_int64(id);
  if(value>1 && value<=INT_MAX){
   char proc[64];struct stat a,b;snprintf(proc,sizeof(proc),"/proc/%lld/exe",value);
   if(!stat(proc,&a) && !stat(NJIT_BINARY,&b) && a.st_dev==b.st_dev && a.st_ino==b.st_ino){*running=1;*pid=(pid_t)value;}
   else{json_object_put(o);return -1;}
  }else{json_object_put(o);return -1;}
 }
 json_object_put(o);return 0;
}
static int directory(const char *path,int create,int private){
 if(create && mkdir(path,0700) && errno!=EEXIST)return -1;
 int fd=open(path,O_DIRECTORY|O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct stat st;
 if(fd<0)return -1;
 if(fstat(fd,&st) || st.st_uid!=0 || (private?(st.st_mode&077):(st.st_mode&022))){close(fd);return -1;}return fd;
}
static int operation_lock(void){
 int d=directory(NJIT_UI_RUNDIR,1,1);if(d<0)return -1;
 int fd=openat(d,"lock",O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);close(d);struct stat st;
 if(fd<0)return -1;
 if(fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_uid!=0 || st.st_nlink!=1 || (st.st_mode&077)){close(fd);return -1;}
 struct flock lock={.l_type=F_WRLCK,.l_whence=SEEK_SET};if(fcntl(fd,F_SETLK,&lock)){close(fd);return -1;}return fd;
}
static int atomic_write(int d,const char *leaf,const char *text,size_t len){
 /* Root-private lock serializes writers; never follow a temporary symlink. */
 const char *temp=".njitclient-new";unlinkat(d,temp,0);
 int fd=openat(d,temp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return -1;
 size_t done=0;int result=-1;
 while(done<len){ssize_t n=write(fd,text+done,len-done);if(n<0 && errno==EINTR)continue;if(n<=0)goto end;done+=(size_t)n;}
 if(fsync(fd) || renameat(d,temp,d,leaf) || fsync(d))goto end;
 result=0;
end:
 close(fd);if(result)unlinkat(d,temp,0);return result;
}
static int enable(int on){
 int d=directory(NJIT_UCI_DIR,0,0);if(d<0)return -1;
 char data[160];int n=snprintf(data,sizeof(data),"config njitclient 'main'\n\toption enabled '%d'\n\toption device 'wan'\n",on?1:0);
 int r=atomic_write(d,"njitclient",data,(size_t)n);close(d);return r;
}
static int service(const char *action){char output[1024];char *argv[]={NJIT_SERVICE,(char *)action,NULL};return command(argv,output,sizeof(output));}
static int valid_text(json_object *o,size_t max,int empty){
 if(!o || !json_object_is_type(o,json_type_string))return 0;
 const char *s=json_object_get_string(o);size_t n=(size_t)json_object_get_string_len(o);
 if(n>max || (!empty && n==0) || strlen(s)!=n || memchr(s,'\n',n) || memchr(s,'\r',n))return 0;
 return 1;
}
static int allowed(json_object *args,const char *method){
 json_object_object_foreach(args,key,value){
  if(!strcmp(key,"ubus_rpc_session") && valid_text(value,64,0))continue;
  if(!strcmp(method,"save") && (!strcmp(key,"username") || !strcmp(key,"password") || !strcmp(key,"domain")))continue;
  if(!strcmp(method,"save_options") && (!strcmp(key,"autostart") || !strcmp(key,"reconnect")))continue;
  if(!strcmp(method,"control") && !strcmp(key,"action"))continue;
  return 0;
 }
 return 1;
}
static void settings(void){
 struct njit_credentials c;int configured=njit_read_credentials(NJIT_CREDENTIALS,&c)==0;
 boolean(reply,"configured",configured);boolean(reply,"password_set",configured);
 string(reply,"username",configured?c.account:"");string(reply,"domain",configured?c.domain:"");
 int a,r;boolean(reply,"options_valid",njit_read_options(&a,&r)==0);
 string(reply,"device","wan");boolean(reply,"autostart",a);boolean(reply,"reconnect",r);njit_erase(&c,sizeof(c));
}
static void save(json_object *args){
 json_object *user=get(args,"username"),*password=get(args,"password"),*domain=get(args,"domain");
 if(!valid_text(user,88,0) || !valid_text(password,111,1) || !valid_text(domain,88,1)){failure("INVALID_INPUT");return;}
 size_t u=(size_t)json_object_get_string_len(user),g=(size_t)json_object_get_string_len(domain);
 if(u+(g?g+1:0)>88){failure("INVALID_INPUT");return;}
 int running;pid_t pid;if(service_status(&running,&pid)){failure("SERVICE_UNAVAILABLE");return;}
 if(running){failure("STOP_BEFORE_SAVE");return;}
 struct njit_credentials old;int have=njit_read_credentials(NJIT_CREDENTIALS,&old)==0;
 const char *pw=json_object_get_string(password);
 if(!*pw){if(!have){njit_erase(&old,sizeof(old));failure("PASSWORD_REQUIRED");return;}pw=old.password;}
 char content[400];int n=snprintf(content,sizeof(content),"username=%s\npassword=%s\ndomain=%s\n",json_object_get_string(user),pw,json_object_get_string(domain));
 int d=directory(NJIT_CONFIG_DIR,1,1),r=-1;
 if(d>=0){r=atomic_write(d,"credentials",content,(size_t)n);close(d);}
 njit_erase(content,sizeof(content));njit_erase(&old,sizeof(old));
 if(r)failure("SAVE_FAILED");
}
static void save_options(json_object *args){
 json_object *a=get(args,"autostart"),*r=get(args,"reconnect");
 if(!a || !r || !json_object_is_type(a,json_type_boolean) || !json_object_is_type(r,json_type_boolean)){failure("INVALID_INPUT");return;}
 int old_a,old_r;if(njit_read_options(&old_a,&old_r)){old_a=old_r=0;}
 int d=directory(NJIT_CONFIG_DIR,1,1);if(d<0){failure("SAVE_FAILED");return;}
 char data[80];int n=snprintf(data,sizeof(data),"{\"autostart\":%s,\"reconnect\":%s}\n",json_object_get_boolean(a)?"true":"false",json_object_get_boolean(r)?"true":"false");
 if(atomic_write(d,"options.json",data,(size_t)n))failure("SAVE_FAILED");
 else if(service(json_object_get_boolean(a)?"enable":"disable")){
  n=snprintf(data,sizeof(data),"{\"autostart\":%s,\"reconnect\":%s}\n",old_a?"true":"false",old_r?"true":"false");
  atomic_write(d,"options.json",data,(size_t)n);failure("SAVE_FAILED");
 }
 close(d);
}
static void clear(void){
 int running;pid_t pid;if(service_status(&running,&pid)){failure("SERVICE_UNAVAILABLE");return;}
 if(running){failure("STOP_BEFORE_SAVE");return;}
 int d=directory(NJIT_CONFIG_DIR,0,1);if(d<0){if(errno!=ENOENT)failure("CLEAR_FAILED");return;}
 if(unlinkat(d,"credentials",0) && errno!=ENOENT)failure("CLEAR_FAILED");
 else if(fsync(d))failure("CLEAR_FAILED");
 close(d);
}
static void control(json_object *args){
 json_object *v=get(args,"action");if(!valid_text(v,7,0)){failure("INVALID_ACTION");return;}
 const char *a=json_object_get_string(v);
 if(strcmp(a,"start") && strcmp(a,"stop") && strcmp(a,"restart")){failure("INVALID_ACTION");return;}
 int running;pid_t pid;if(service_status(&running,&pid)){failure("SERVICE_UNAVAILABLE");return;}
 if(!strcmp(a,"stop") || !strcmp(a,"restart")){
  if(enable(0) || service("stop")){failure("CONTROL_FAILED");return;}
  if(!strcmp(a,"stop"))return;
  if(service_status(&running,&pid) || running){failure("WAIT_FOR_STOP");return;}
 }
 if(!strcmp(a,"start") && running)return;
 struct njit_credentials c;if(njit_read_credentials(NJIT_CREDENTIALS,&c)){failure("CONFIG_REQUIRED");return;}njit_erase(&c,sizeof(c));
 if(enable(1)){failure("CONTROL_FAILED");return;}
 if(service("start")){enable(0);failure("CONTROL_FAILED");}
}
static int event_name(const char *s){
 const char *names[]={"starting","authenticating","authenticated","authentication_failed","credentials_rejected","waiting_retry","interface_error","stopped","waiting_link","manual_required"};
 for(size_t i=0;i<sizeof(names)/sizeof(*names);i++)if(!strcmp(s,names[i]))return 1;
 return 0;
}
static void status(void){
 int running=0;pid_t pid=0;int known=service_status(&running,&pid)==0;
 boolean(reply,"service_known",known);boolean(reply,"running",running);string(reply,"device","wan");
 int fd=open(NJIT_CARRIER,O_RDONLY|O_CLOEXEC);char carrier=0;if(fd>=0){if(read(fd,&carrier,1)!=1)carrier=0;close(fd);}
 boolean(reply,"carrier_known",carrier=='0' || carrier=='1');boolean(reply,"carrier",carrier=='1');
 struct njit_credentials c;boolean(reply,"configured",njit_read_credentials(NJIT_CREDENTIALS,&c)==0);njit_erase(&c,sizeof(c));
 /* Bound reads; whitelist fields and correlate event PID with the verified service. */
 int d=directory(NJIT_RUNDIR,0,1);if(d>=0){
  fd=openat(d,"state.json",O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);close(d);struct stat st;
  if(fd>=0){char data[512];ssize_t n=-1;if(!fstat(fd,&st) && S_ISREG(st.st_mode) && st.st_uid==0 && !(st.st_mode&077) && st.st_size<=(off_t)sizeof(data)-1)n=read(fd,data,sizeof(data)-1);close(fd);
   if(n>0){data[n]=0;json_object *e=json_tokener_parse(data);json_object *name=get(e,"state"),*id=get(e,"pid"),*when=get(e,"monotonic_seconds");
    if(known && running && name && json_object_is_type(name,json_type_string) && event_name(json_object_get_string(name)) && id && json_object_get_int64(id)==pid && when && json_object_is_type(when,json_type_int)){
     long long age=monotonic_ms()/1000-json_object_get_int64(when);
     if(age>=0){string(reply,"event",json_object_get_string(name));json_object_object_add(reply,"event_age_seconds",json_object_new_int64(age));}
    }if(e)json_object_put(e);
   }
  }
 }
 json_object *wan=ubus("network.interface.wan","status","{}");boolean(reply,"wan_known",wan!=NULL);
 if(wan){boolean(reply,"wan_up",json_object_get_boolean(get(wan,"up")));
  json_object *ips=get(wan,"ipv4-address"),*addresses=json_object_new_array();
  if(ips && json_object_is_type(ips,json_type_array))for(size_t i=0;i<json_object_array_length(ips) && i<8;i++){
   json_object *v=get(json_object_array_get_idx(ips,i),"address");struct in_addr ip;
   if(v && json_object_is_type(v,json_type_string) && inet_pton(AF_INET,json_object_get_string(v),&ip)==1)json_object_array_add(addresses,json_object_new_string(json_object_get_string(v)));
  }json_object_object_add(reply,"ipv4_addresses",addresses);json_object_put(wan);
 }
}
int main(int argc,char **argv){
 struct rlimit zero={0,0};if(setrlimit(RLIMIT_CORE,&zero) || getuid()!=0 || geteuid()!=0)return 77;
 if(argc==2 && !strcmp(argv[1],"options")){
  int a,r;njit_read_options(&a,&r);
  printf("{\"autostart\":%s,\"reconnect\":%s}\n",a?"true":"false",r?"true":"false");return 0;
 }
 if(argc==2 && !strcmp(argv[1],"list")){
  puts("{\"status\":{},\"get_settings\":{},\"save\":{\"username\":\"\",\"password\":\"\",\"domain\":\"\"},\"clear\":{},\"save_options\":{\"autostart\":true,\"reconnect\":true},\"control\":{\"action\":\"\"}}");return 0;
 }
 reply=json_object_new_object();boolean(reply,"ok",1);
 if(argc!=3 || strcmp(argv[1],"call")){failure("INVALID_METHOD");goto done;}
 char input[4097];size_t used=0;for(;;){ssize_t n=read(STDIN_FILENO,input+used,sizeof(input)-1-used);if(n<0 && errno==EINTR)continue;if(n<0){failure("INVALID_INPUT");goto done;}if(n==0)break;used+=(size_t)n;if(used==sizeof(input)-1){failure("INVALID_INPUT");goto done;}}
 input[used]=0;struct json_tokener *tok=json_tokener_new();json_tokener_set_flags(tok,JSON_TOKENER_STRICT);
 json_object *args=json_tokener_parse_ex(tok,input,(int)used);size_t consumed=json_tokener_get_parse_end(tok);
 int valid=json_tokener_get_error(tok)==json_tokener_success && args && json_object_is_type(args,json_type_object);
 json_tokener_free(tok);while(consumed<used && isspace((unsigned char)input[consumed]))consumed++;
 if(!valid || consumed!=used || !allowed(args,argv[2])){if(args)json_object_put(args);njit_erase(input,sizeof(input));failure("INVALID_INPUT");goto done;}
 if(!strcmp(argv[2],"status"))status();
 else if(!strcmp(argv[2],"get_settings"))settings();
 else if(!strcmp(argv[2],"save") || !strcmp(argv[2],"save_options") || !strcmp(argv[2],"clear") || !strcmp(argv[2],"control")){
  int lock=operation_lock();if(lock<0)failure("BUSY");
  else{if(!strcmp(argv[2],"save"))save(args);else if(!strcmp(argv[2],"save_options"))save_options(args);else if(!strcmp(argv[2],"clear"))clear();else control(args);close(lock);}
 }else failure("INVALID_METHOD");
 /* json-c owns decoded copies of secrets; erase them before release as well. */
 json_object *password=get(args,"password");if(password && json_object_is_type(password,json_type_string))njit_erase((void *)json_object_get_string(password),(size_t)json_object_get_string_len(password));
 json_object_put(args);njit_erase(input,sizeof(input));
done:
 puts(json_object_to_json_string_ext(reply,JSON_C_TO_STRING_PLAIN));json_object_put(reply);return 0;
}
