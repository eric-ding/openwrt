#define _GNU_SOURCE
#include "njit_managed.h"
#include <json-c/json.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef NJIT_CONFIG_DIR
#define NJIT_CONFIG_DIR "/etc/njitclient"
#endif
/* Non-secret switches, atomically written by the restricted RPC helper. */
int njit_read_options(int *autostart,int *reconnect){
 *autostart=*reconnect=0;
 int d=open(NJIT_CONFIG_DIR,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(d<0)return errno==ENOENT?0:-1;
 struct stat st;if(fstat(d,&st) || st.st_uid!=0 || (st.st_mode&077)){close(d);return -1;}
 int fd=openat(d,"options.json",O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);close(d);if(fd<0)return errno==ENOENT?0:-1;
 if(fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_uid!=0 || (st.st_mode&07777)!=0600 || st.st_nlink!=1 || st.st_size<2 || st.st_size>128){close(fd);return -1;}
 char data[129];ssize_t n=read(fd,data,sizeof(data)-1);close(fd);if(n!=st.st_size)return -1;data[n]=0;
 struct json_tokener *tok=json_tokener_new();json_tokener_set_flags(tok,JSON_TOKENER_STRICT);
 json_object *o=json_tokener_parse_ex(tok,data,(int)n);int valid=json_tokener_get_error(tok)==json_tokener_success && json_object_is_type(o,json_type_object);
 size_t consumed=json_tokener_get_parse_end(tok);
 while(consumed<(size_t)n && isspace((unsigned char)data[consumed]))consumed++;
 valid=valid && consumed==(size_t)n;
 json_tokener_free(tok);json_object *a=NULL,*r=NULL;
 if(valid)valid=json_object_object_length(o)==2 && json_object_object_get_ex(o,"autostart",&a) && json_object_object_get_ex(o,"reconnect",&r) && json_object_is_type(a,json_type_boolean) && json_object_is_type(r,json_type_boolean);
 if(valid){*autostart=json_object_get_boolean(a);*reconnect=json_object_get_boolean(r);}
 if(o)json_object_put(o);
 return valid?0:-1;
}
int njit_can_reconnect(void){int a,r;return njit_read_options(&a,&r)==0 && r;}
