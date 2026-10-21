#define _GNU_SOURCE
#include "njit_managed.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

void njit_erase(void *p, size_t n) { volatile unsigned char *q=p; while(n--) *q++=0; }
/* Literal key=value records, never shell or JSON evaluation. No newlines in values.
 * UTF-8 bytes, quotes, spaces, = and shell metacharacters remain literal. */
int njit_read_credentials(const char *path, struct njit_credentials *out) {
    char dir[PATH_MAX], data[1025]={0}, username[89]={0}, domain[89]={0}, password[112]={0};
    int d=-1,fd=-1,ok=-1; struct stat st; size_t used=0; unsigned seen=0;
    memset(out,0,sizeof(*out));
    if(!path || strlen(path)>=sizeof(dir)) goto end;
    strcpy(dir,path); char *leaf=strrchr(dir,'/');
    if(!leaf || !leaf[1]) goto end;
    *leaf++=0;
    d=open(*dir?dir:"/",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(d<0 || fstat(d,&st) || st.st_uid!=0 || (st.st_mode & 077)!=0) goto end;
    fd=openat(d,leaf,O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0 || fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_uid!=0 ||
       (st.st_mode & 07777)!=0600 || st.st_nlink!=1 || st.st_size<1 || st.st_size>1024) goto end;
    for(;;) {
        ssize_t n=read(fd,data+used,1024-used);
        if(n<0 && errno==EINTR) continue;
        if(n<0) goto end;
        if(n==0) break;
        if(memchr(data+used,0,(size_t)n)) goto end;
        used+=(size_t)n;
        if(used==1024) { char extra; if(read(fd,&extra,1)!=0) goto end; break; }
    }
    data[used]=0;
    char *line=data;
    while(*line) {
        char *next=strchr(line,'\n'); if(next) *next++=0;
        if(strchr(line,'\r')) goto end;
        char *eq=strchr(line,'='); if(!eq) goto end; *eq++=0;
        unsigned bit; char *dest; size_t cap;
        if(!strcmp(line,"username")){bit=1;dest=username;cap=sizeof(username);}
        else if(!strcmp(line,"password")){bit=2;dest=password;cap=sizeof(password);}
        else if(!strcmp(line,"domain")){bit=4;dest=domain;cap=sizeof(domain);}
        else goto end;
        if((seen&bit) || strlen(eq)>=cap) goto end;
        seen|=bit; strcpy(dest,eq);
        if(!next) break;
        line=next;
    }
    if((seen&3)!=3 || !*username || !*password) goto end;
    size_t u=strlen(username),g=strlen(domain);
    if(u+(g?g+1:0)>88) goto end;
    memcpy(out->username,username,u+1);
    if(g){out->username[u]='@';memcpy(out->username+u+1,domain,g+1);}
    strcpy(out->password,password);ok=0;
end:
    if(fd>=0) close(fd);
    if(d>=0)close(d);
    njit_erase(data,sizeof(data));njit_erase(username,sizeof(username));
    njit_erase(password,sizeof(password));njit_erase(domain,sizeof(domain));
    if(ok)njit_erase(out,sizeof(*out));
    return ok;
}
