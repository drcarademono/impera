#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "common/common.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#if defined(TARGET_SDL)
#include <SDL3/SDL.h>
static SDL_Mutex* s_lock;
#endif

static FILE* s_log;
static int s_initialized;
static unsigned long s_sequence;
#if defined(OS_LINUX) || defined(OS_MACOS)
#include <signal.h>
#include <unistd.h>
#include <errno.h>
static volatile sig_atomic_t s_crashFd=-1;
static void DEBUG_SignalWrite(int fd,const char* text,size_t length)
{
    while(length) {
        ssize_t written=write(fd,text,length);
        if(written>0) { text+=written;length-=(size_t)written; }
        else if(written<0 && errno==EINTR) continue;
        else break;
    }
}
static void DEBUG_Crash(int number)
{
    /* No stdio, allocation, SDL, or locks in a signal handler. Preserve the
     * default termination/core-dump behavior after recording a final marker. */
    static const char marker[]="FATAL: process terminated by signal ";
    char digits[12];int count=0,value=number;
    do { digits[count++]=(char)('0'+value%10);value/=10; } while(value);
    for(int i=0;i<count/2;i++) { char c=digits[i];digits[i]=digits[count-1-i];digits[count-1-i]=c; }
    digits[count++]='\n';
    int fd=s_crashFd;
    if(fd>=0) { DEBUG_SignalWrite(fd,marker,sizeof(marker)-1);DEBUG_SignalWrite(fd,digits,(size_t)count); }
    DEBUG_SignalWrite(STDERR_FILENO,marker,sizeof(marker)-1);DEBUG_SignalWrite(STDERR_FILENO,digits,(size_t)count);
    struct sigaction action={0};action.sa_handler=SIG_DFL;sigemptyset(&action.sa_mask);
    sigaction(number,&action,NULL);raise(number);
}
#endif

static void DEBUG_Write(const char* message, int error)
{
#if defined(TARGET_SDL)
    if(s_lock) SDL_LockMutex(s_lock);
#endif
    if (error) fprintf(stderr, "Error: %s\n", message);
    if (s_log) {
        /* Bound disk use during long sessions. Preserve the immediately prior
         * segment, including the previous run until this segment fills up. */
        if(ftell(s_log)>=4L*1024*1024) {
#if defined(OS_LINUX) || defined(OS_MACOS)
            s_crashFd=-1;
#endif
            fclose(s_log);s_log=NULL;
            remove("LOG.PREV.TXT");rename("LOG.TXT","LOG.PREV.TXT");
            s_log=fopen("LOG.TXT","wb");
#if defined(OS_LINUX) || defined(OS_MACOS)
            if(s_log) s_crashFd=fileno(s_log);
#endif
        }
        if(s_log) {
            time_t now=time(NULL);
            fprintf(s_log,"[%lu time=%ld] %s%s",++s_sequence,(long)now,error?"ERROR: ":"",message);
            size_t length=strlen(message);
            if(!length || message[length-1]!='\n') fputc('\n',s_log);
            fflush(s_log); /* Keep the last completed record on abnormal exit. */
        }
    }
#if defined(TARGET_SDL)
    if(s_lock) SDL_UnlockMutex(s_lock);
#endif
}

static void DEBUG_Close(void)
{
    DEBUG_Write("Clean application shutdown",0);
#if defined(OS_LINUX) || defined(OS_MACOS)
    s_crashFd=-1;
#endif
    if (s_log) fclose(s_log);
    s_log = NULL;
}

#if defined(OS_WINDOWS)
#include <strsafe.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <DbgHelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

void CDECL debug(const char* str, ...)
{
    char debugBuffer[2048];

    va_list args;
    va_start(args, str);

#if defined(TARGET_DOS16)
    if (vsprintf(debugBuffer, str, args) < 0)
#else
    if (vsnprintf(debugBuffer, sizeof(debugBuffer), str, args) < 0)
#endif
    {
        debugBuffer[0] = 0;
    }

    va_end(args);

#if defined(OS_WINDOWS)
    puts(debugBuffer);
#endif
    DEBUG_Write(debugBuffer, 0);
}

void CDECL DEBUG_Error(const char* str, ...)
{
    char message[512];
    va_list args;
    va_start(args, str);
#if defined(TARGET_DOS16)
    vsprintf(message, str, args);
#else
    vsnprintf(message, sizeof(message), str, args);
#endif
    va_end(args);
    DEBUG_Write(message, 1);
}

#if defined(OS_WINDOWS)
static LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS* exceptionPointers);
#endif

void DEBUG_Initialize(void)
{
    if(s_initialized) return;
    s_initialized=1;
#if defined(TARGET_SDL)
    s_lock=SDL_CreateMutex();
#endif
    /* Runtime diagnostics are always enabled, including first-launch setup. */
    FILE* previous=fopen("LOG.TXT","rb");
    if(previous) { fclose(previous);remove("LOG.PREV.TXT");rename("LOG.TXT","LOG.PREV.TXT"); }
    s_log=fopen("LOG.TXT","wb");
    if(s_log) {
#if defined(OS_LINUX) || defined(OS_MACOS)
        s_crashFd=fileno(s_log);
        const int signals[]={SIGABRT,SIGSEGV,SIGILL,SIGFPE,SIGBUS};
        struct sigaction action={0};action.sa_handler=DEBUG_Crash;sigemptyset(&action.sa_mask);
        for(size_t i=0;i<sizeof(signals)/sizeof(signals[0]);i++) sigaction(signals[i],&action,NULL);
#endif
        atexit(DEBUG_Close);
        debug("Impera runtime diagnostics; timestamps are Unix seconds; segment limit=4 MiB");
#if defined(TARGET_SDL)
        debug("Platform=%s SDL=%d compiled=%d revision=%s",SDL_GetPlatform(),SDL_GetVersion(),SDL_VERSION,SDL_GetRevision());
#endif
        debug("Engine base version=%s; build compiled %s %s; U5D_DEBUG is no longer required",U5_VERSION,__DATE__,__TIME__);
    } else fputs("Unable to create LOG.TXT; errors will be written to stderr.\n",stderr);

#if defined(OS_WINDOWS)
    SetUnhandledExceptionFilter(UnhandledExceptionHandler);
#endif
}

#if defined(OS_WINDOWS)

static void WriteCrashDump(EXCEPTION_POINTERS* exceptionPointers)
{
    SYSTEMTIME time;
    GetLocalTime(&time);

    wchar_t path[MAX_PATH];

    StringCchPrintfW(path, MAX_PATH, L"crash_%04u%02u%02u_%02u%02u%02u.dmp", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);

    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (file == INVALID_HANDLE_VALUE)
        return;

    MINIDUMP_EXCEPTION_INFORMATION exceptionInfo = { 0, };
    exceptionInfo.ThreadId = GetCurrentThreadId();
    exceptionInfo.ExceptionPointers = exceptionPointers;
    exceptionInfo.ClientPointers = FALSE;

    MINIDUMP_TYPE dumpType = (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);

    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, dumpType, exceptionPointers != NULL ? &exceptionInfo : NULL, NULL, NULL);

    CloseHandle(file);
}

static LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS* exceptionPointers)
{
    WriteCrashDump(exceptionPointers);

    return EXCEPTION_EXECUTE_HANDLER;
}

#endif
