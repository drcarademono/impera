#include "common.h"
#include "settings.h"

#include <stdio.h>
#include <errno.h>
#include <limits.h>

// TODO

#if defined(OS_WINDOWS)

// TODO: clean up
#undef ARRAYSIZE
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

int SETTINGS_GetString(char* section, char* key, char* defaultValue, char* outValue, int size)
{
    char fileName[] = ".\\ultima5.ini";

    return GetPrivateProfileStringA(section, key, defaultValue, outValue, size, fileName);
}

#else

#include <string.h>

int SETTINGS_GetString(char* section, char* key, char* defaultValue, char* outValue, int size)
{
    if (defaultValue == NULL)
        defaultValue = "";

    if (size <= 0)
        return 0;

    strncpy(outValue, defaultValue, size - 1);
    outValue[size - 1] = '\0';
    return 0;
}

#endif

int SETTINGS_GetInt(char* section, char* key, int defaultValue)
{
    char outValue[256] = { 0, };
    SETTINGS_GetString(section, key, "", outValue, 255);

    char* end;
    long val;
    errno = 0;
    val = strtol(outValue, &end, 10);
    if (errno != 0 || end == outValue || *end != '\0' || val < INT_MIN || val > INT_MAX)
    {
        return defaultValue;
    }

    return (int)val;
}
