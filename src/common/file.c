#include "common/common.h"
#include "common/file.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#ifndef ENAMETOOLONG
#define ENAMETOOLONG ERANGE
#endif
#if defined(OS_LINUX) || defined(OS_MACOS)
#include <dirent.h>
#include <sys/stat.h>
#endif

static char s_dataDirectory[FILE_PATH_SIZE];
void FILE_SetDataDirectory(const char* directory)
{
    if(!directory || strlen(directory)>=sizeof(s_dataDirectory)) s_dataDirectory[0]=0;
    else strcpy(s_dataDirectory,directory);
}
int FILE_DataPath(const char* path,char* resolved,size_t capacity)
{
    char full[FILE_PATH_SIZE];
    if(!*s_dataDirectory || !path || !*path || path[0]=='/' || path[0]=='\\' || strchr(path,':')) return -1;
    if(snprintf(full,sizeof(full),"%s/%s",s_dataDirectory,path)>=(int)sizeof(full)) return -1;
    return FILE_ResolvePath(full,resolved,capacity,0);
}

static unsigned char FILE_Fold(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

int FILE_NameEqual(const char* left, const char* right)
{
    while (*left && *right)
    {
        if (FILE_Fold((unsigned char)*left++) != FILE_Fold((unsigned char)*right++))
            return 0;
    }
    return *left == *right;
}

int FILE_ResolvePath(const char* path, char* resolved, size_t capacity, int allowMissingLeaf)
{
    if (!path || !*path || !resolved || capacity == 0)
    {
        errno = EINVAL;
        return -1;
    }
#if defined(OS_LINUX) || defined(OS_MACOS)
    {
        const char* cursor = path;
        size_t used = 0;
        if (*cursor == '/' || *cursor == '\\')
        {
            if (capacity < 2) { errno = ENAMETOOLONG; return -1; }
            resolved[used++] = '/';
        }
        resolved[used] = '\0';
        while (*cursor)
        {
            const char* start;
            size_t length;
            size_t parentLength;
            struct stat info;
            char component[FILE_PATH_SIZE];
            char match[FILE_PATH_SIZE];
            DIR* directory;
            struct dirent* entry;
            int matches = 0;
            while (*cursor == '/' || *cursor == '\\') cursor++;
            if (!*cursor) break;
            start = cursor;
            while (*cursor && *cursor != '/' && *cursor != '\\') cursor++;
            length = (size_t)(cursor - start);
            if (length >= sizeof(component) ||
                used + (used && resolved[used - 1] != '/' ? 1 : 0) + length + 1 > capacity)
            { errno = ENAMETOOLONG; return -1; }
            memcpy(component, start, length);
            component[length] = '\0';
            parentLength = used;
            if (used && resolved[used - 1] != '/') resolved[used++] = '/';
            memcpy(resolved + used, component, length + 1);
            if (stat(resolved, &info) == 0)
            {
                used += length;
                continue;
            }
            if (errno != ENOENT) return -1;
            resolved[parentLength] = '\0';
            directory = opendir(parentLength ? resolved : ".");
            if (!directory) return -1;
            errno = 0;
            while ((entry = readdir(directory)) != NULL)
            {
                if (FILE_NameEqual(component, entry->d_name))
                {
                    /* Also preserve an exact dangling symlink's spelling. */
                    if (strcmp(component, entry->d_name) == 0)
                    {
                        strcpy(match, entry->d_name);
                        matches = 1;
                        break;
                    }
                    if (matches == 0) strcpy(match, entry->d_name);
                    matches++;
                }
            }
            {
                int scanError = errno;
                closedir(directory);
                if (scanError) { errno = scanError; return -1; }
            }
            if (matches > 1) { errno = EEXIST; return -1; }
            if (matches == 0)
            {
                if (!allowMissingLeaf || *cursor) { errno = ENOENT; return -1; }
                strcpy(match, component);
            }
            length = strlen(match);
            used = parentLength;
            if (used + (used && resolved[used - 1] != '/' ? 1 : 0) + length + 1 > capacity)
            { errno = ENAMETOOLONG; return -1; }
            if (used && resolved[used - 1] != '/') resolved[used++] = '/';
            memcpy(resolved + used, match, length + 1);
            used += length;
        }
        return 0;
    }
#else
    /* Windows and DOS retain their native case-insensitive lookup. */
    (void)allowMissingLeaf;
    if (strlen(path) >= capacity) { errno = ENAMETOOLONG; return -1; }
    strcpy(resolved, path);
    return 0;
#endif
}

FILE* FILE_Open(const char* path, const char* mode)
{
    /* Immutable assets come from the selected source; writes stay local. */
    if(mode[0]=='r' && !strchr(mode,'+') && !strchr(path,'/') && !strchr(path,'\\')) {
        char asset[FILE_PATH_SIZE];
        if(FILE_DataPath(path,asset,sizeof(asset))==0) return fopen(asset,mode);
    }

#if defined(OS_LINUX) || defined(OS_MACOS)
    char resolved[FILE_PATH_SIZE];
    if (FILE_ResolvePath(path, resolved, sizeof(resolved), mode[0] == 'w' || mode[0] == 'a') != 0) {
        if(mode[0]=='r' && !strchr(mode,'+') && strncmp(path,"U4SAVE/",7)==0 && FILE_DataPath(path,resolved,sizeof(resolved))==0)
            return fopen(resolved,mode);
        return NULL;
    }
    FILE* stream=fopen(resolved,mode);
    if(!stream && mode[0]=='r' && !strchr(mode,'+') && strncmp(path,"U4SAVE/",7)==0 && FILE_DataPath(path,resolved,sizeof(resolved))==0)
        stream=fopen(resolved,mode);
    return stream;
#else
    return fopen(path, mode);
#endif
}

int FILE_ReadU32LE(FILE* fp, u32* out)
{
    u8 b[4];

    if (fread(b, 1, 4, fp) != 4)
        return 0;

    *out = (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24);

    return 1;
}

int FILE_ReadU16LE(FILE* fp, u16* out)
{
    u8 b[2];

    if (fread(b, 1, 2, fp) != 2)
        return 0;

    *out = (u16)b[0] | ((u16)b[1] << 8);

    return 1;
}

int FILE_ReadU8(FILE* fp, u8* out)
{
    int c = fgetc(fp);
    if (c == EOF)
        return 0;

    *out = (u8)c;

    return 1;
}

int FILE_WriteU32LE(FILE* fp, u32 in)
{
    u8 b[4];

    b[0] = (u8)in;
    b[1] = (u8)(in >> 8);
    b[2] = (u8)(in >> 16);
    b[3] = (u8)(in >> 24);

    if (fwrite(b, 1, 4, fp) != 4)
        return 0;

    return 1;
}

int FILE_WriteU16LE(FILE* fp, u16 in)
{
    u8 b[2];

    b[0] = (u8)in;
    b[1] = (u8)(in >> 8);

    if (fwrite(b, 1, 2, fp) != 2)
        return 0;

    return 1;
}

int FILE_WriteU8(FILE* fp, u8 in)
{
    int ret = fputc(in, fp);
    if (ret == EOF)
        return 0;

    return 1;
}

#if defined(TARGET_DOS16) || defined(TARGET_DOS32)
#define PATH_SEPARATOR "\\"
#else
#define PATH_SEPARATOR "/"
#endif

int FILE_ReadFile(char* fileName, void* buffer, uint size, int offset)
{
	FILE* stream;

    char buf[256];
    if (FILE_NameEqual(fileName, "BRIT.OOL") || FILE_NameEqual(fileName, "UNDER.OOL") || FILE_NameEqual(fileName, "SAVED.OOL") ||
        FILE_NameEqual(fileName, "SAVED.GAM"))
    {
        sprintf(buf, "SAVEGAME" PATH_SEPARATOR "%s", fileName);
        fileName = buf;
    }
    else if (FILE_NameEqual(fileName, "party.sav"))
    {
        sprintf(buf, "U4SAVE" PATH_SEPARATOR "%s", fileName);
        fileName = buf;
    }

	debug("Read file=%s bytes=%u offset=%d",fileName,size,offset);
	stream = FILE_Open(fileName, "rb");
	if (stream == 0)
	{
		DEBUG_Error("Cannot read %s: %s",fileName,strerror(errno));
		return -1;
	}

    errno=0;
    bool ok=fseek(stream,offset,SEEK_SET)==0;
    size_t got=ok?fread(buffer,1,size,stream):0;
    /* Original callers often request the capacity of a resource buffer, not
     * its file length (for example .PTH and conversation data). EOF is valid;
     * preserve unread bytes exactly as the original loader did. */
    bool failed=!ok || ferror(stream);
    int failure=failed?(errno?errno:EIO):0;
    fclose(stream);
    if(failed) {
        DEBUG_Error("Failed read %s requested=%u actual=%lu offset=%d: %s",fileName,size,(unsigned long)got,offset,strerror(failure));
        errno=failure;return -1;
    }
    if(got!=size) debug("Resource read reached EOF file=%s requested=%u actual=%lu offset=%d",fileName,size,(unsigned long)got,offset);
    return 0;
}

int FILE_WriteFile(char* fileName, void* buffer, uint size, int offset)
{
    FILE* stream;

    char buf[256];
    if (FILE_NameEqual(fileName, "BRIT.OOL") || FILE_NameEqual(fileName, "UNDER.OOL") || FILE_NameEqual(fileName, "SAVED.OOL") ||
        FILE_NameEqual(fileName, "SAVED.GAM"))
    {
        sprintf(buf, "SAVEGAME" PATH_SEPARATOR "%s", fileName);
        fileName = buf;
    }

    debug("Write file=%s bytes=%u offset=%d",fileName,size,offset);
    stream = FILE_Open(fileName, "wb");
    if (stream == 0)
    {
        DEBUG_Error("Cannot write %s: %s",fileName,strerror(errno));
        return -1;
    }

    bool ok=fseek(stream,offset,SEEK_SET)==0 && fwrite(buffer,1,size,stream)==size;
    if(fclose(stream)!=0) ok=false;
    if(!ok) DEBUG_Error("Failed write %s bytes=%u offset=%d: %s",fileName,size,offset,strerror(errno));
    return ok?0:-1;
}
