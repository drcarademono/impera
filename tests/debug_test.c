#undef NDEBUG
#include "common/common.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#if defined(OS_LINUX) || defined(OS_MACOS)
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/resource.h>
#endif
static void contains(const char* file,const char* expected)
{
    FILE* f=fopen(file,"rb");assert(f);char line[4096];bool found=false;
    while(fgets(line,sizeof(line),f)) if(strstr(line,expected)) found=true;
    fclose(f);assert(found);
}
int main(void)
{
    FILE* f=fopen("LOG.TXT","wb");assert(f);fputs("previous player run\n",f);fclose(f);
    DEBUG_Initialize();DEBUG_Initialize();
    debug("diagnostics enabled without environment flag");
    DEBUG_Error("test error with context map=7");
    contains("LOG.PREV.TXT","previous player run");
    contains("LOG.TXT","diagnostics enabled without environment flag");
    contains("LOG.TXT","ERROR: test error with context map=7");
    char payload[1900];memset(payload,'x',sizeof(payload)-1);payload[sizeof(payload)-1]=0;
    for(int i=0;i<2300;i++) debug("%s",payload);
    debug("after rotation");contains("LOG.TXT","after rotation");
    contains("LOG.PREV.TXT","xxxxxxxx");
#if defined(OS_LINUX) || defined(OS_MACOS)
    pid_t child=fork();assert(child>=0);
    if(child==0) { struct rlimit limit={0,0};setrlimit(RLIMIT_CORE,&limit);raise(SIGABRT);_exit(99); }
    int status;assert(waitpid(child,&status,0)==child);
    assert(WIFSIGNALED(status) && WTERMSIG(status)==SIGABRT);
    contains("LOG.TXT","FATAL: process terminated by signal");
#endif
    return 0;
}
