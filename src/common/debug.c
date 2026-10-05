#include "common/common.h"
#include "common/file.h"

#include <stdarg.h>
#include <stdio.h>

static FILE* s_log;
static int s_verbose;

static void DEBUG_Write(const char* message, int error)
{
    if (error)
        fprintf(stderr, "Error: %s\n", message);
    if (s_log)
    {
        fprintf(s_log, "%s%s\n", error ? "ERROR: " : "", message);
        fflush(s_log);
    }
}

static void DEBUG_Close(void)
{
    if (s_log)
        fclose(s_log);
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
    char debugBuffer[256];

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
    if (s_verbose)
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
    const char* verbose = getenv("U5D_DEBUG");
    s_verbose = verbose != NULL && verbose[0] == '1';
#if defined(OS_WINDOWS)
    s_verbose = 1;
#endif
    s_log = FILE_Open("LOG.TXT", "wb");
    if (s_log)
    {
        fputs("Ultima V runtime log. Set U5D_DEBUG=1 for verbose tracing.\n", s_log);
        fflush(s_log);
        atexit(DEBUG_Close);
    }
    else
        fputs("Unable to create LOG.TXT; errors will be written to stderr.\n", stderr);

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
