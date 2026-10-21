#define _GNU_SOURCE
#include "njit_managed.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static const char *path="/tmp/njit-fixture/credentials";
static void fixture(const char *s,size_t n){unlink(path);int f=open(path,O_CREAT|O_WRONLY|O_TRUNC,0600);assert(f>=0);assert(write(f,s,n)==(ssize_t)n);assert(close(f)==0);}
static void check(const char *s,int valid){struct njit_credentials c;fixture(s,strlen(s));assert((njit_read_credentials(path,&c)==0)==valid);njit_erase(&c,sizeof(c));}
int main(void){
 assert(geteuid()==0);assert(mkdir("/tmp/njit-fixture",0700)==0);
 check("username=literal '$= user\npassword=private '$= value\ndomain=school\n",1);
 struct njit_credentials c;assert(njit_read_credentials(path,&c)==0);assert(!strcmp(c.username,"literal '$= user@school"));assert(!strcmp(c.password,"private '$= value"));njit_erase(&c,sizeof(c));
 check("username=u\npassword=p",1);
 check("username=\npassword=p\n",0);check("username=u\npassword=\n",0);
 check("username=u\npassword=p\npassword=q\n",0);check("username=u\npassword=p\nextra=x\n",0);
 check("username=u\r\npassword=p\n",0);check("username=u\n",0);
 char b[1200];memset(b,'x',sizeof(b));fixture(b,sizeof(b));assert(njit_read_credentials(path,&c)!=0);
 const char nul[]="username=u\npassword=p\0secret";fixture(nul,sizeof(nul)-1);assert(njit_read_credentials(path,&c)!=0);
 check("username=u\npassword=p\n",1);assert(chmod(path,0644)==0);assert(njit_read_credentials(path,&c)!=0);
 assert(chmod(path,0600)==0);assert(link(path,"/tmp/njit-fixture/hardlink")==0);assert(njit_read_credentials(path,&c)!=0);unlink("/tmp/njit-fixture/hardlink");
 unlink(path);assert(symlink("target",path)==0);assert(njit_read_credentials(path,&c)!=0);
 unlink(path);assert(mkfifo(path,0600)==0);assert(njit_read_credentials(path,&c)!=0);
 check("username=u\npassword=p\n",1);assert(chmod("/tmp/njit-fixture",0755)==0);assert(njit_read_credentials(path,&c)!=0);
 chmod("/tmp/njit-fixture",0700);unlink(path);rmdir("/tmp/njit-fixture");puts("credential checks passed");return 0;
}
