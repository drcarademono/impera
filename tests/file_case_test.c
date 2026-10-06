#undef NDEBUG
#include "common/common.h"
#include "common/file.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

static void put(const char* name, const char* content)
{
    FILE* fp = fopen(name, "wb");
    assert(fp);
    assert(fputs(content, fp) >= 0);
    assert(fclose(fp) == 0);
}

int main(void)
{
    FILE* fp;
    char resolved[FILE_PATH_SIZE];
    char value[8] = {0};
    char absolute[FILE_PATH_SIZE];
    put("MiXeD/SubDir/DaTa.BiN", "before");
    fp = FILE_Open("mixed/subdir/data.bin", "rb");
    assert(fp && fread(value, 1, 6, fp) == 6);
    fclose(fp);
    assert(strcmp(value, "before") == 0);
    fp = FILE_Open("MIXED\\SUBDIR\\DATA.BIN", "wb");
    assert(fp && fputs("after", fp) >= 0);
    fclose(fp);
    fp = fopen("MiXeD/SubDir/DaTa.BiN", "rb");
    assert(fp && fgetc(fp) == 'a'); fclose(fp);
    assert(fopen("MiXeD/SubDir/DATA.BIN", "rb") == NULL);
    fp = FILE_Open("mixed/subdir/New.Bin", "wb");
    assert(fp); fclose(fp);
    assert(FILE_ResolvePath("mixed/subdir/new.bin", resolved, sizeof(resolved), 0) == 0);
    assert(strcmp(resolved, "MiXeD/SubDir/New.Bin") == 0);
    assert(FILE_ResolvePath("mixed/subdir/new.bin", resolved, strlen("MiXeD/SubDir/New.Bin") + 1, 0) == 0);
    assert(getcwd(absolute, sizeof(absolute)));
    strcat(absolute, "/MIXED/SUBDIR/DATA.BIN");
    fp = FILE_Open(absolute, "rb");
    assert(fp); fclose(fp);
    fp = FILE_Open("mixed/subdir/../SUBDIR/data.bin", "rb");
    assert(fp); fclose(fp);
    put("MiXeD/SubDir/With Spaces.Bin", "space");
    fp = FILE_Open("mixed/subdir/with spaces.BIN", "rb");
    assert(fp); fclose(fp);
    assert(FILE_ResolvePath("MiXeD/SubDir/DaTa.BiN", resolved, 4, 0) == -1);
    assert(errno == ENAMETOOLONG);
    assert(FILE_Open("mixed/missing/data.bin", "wb") == NULL);
    assert(FILE_Open("mixed/subdir/absent.bin", "rb") == NULL);

    /* Resource reads request buffer capacity, not necessarily file size. */
    unsigned char resource[3000];memset(resource,0xa5,sizeof(resource));
    FILE* resourceFile=fopen("BRITISH.PTH","wb");assert(resourceFile);
    for(int i=0;i<2783;i++) assert(fputc(i%256,resourceFile)!=EOF);
    assert(fclose(resourceFile)==0);
    errno=EAGAIN; /* A stale error must not turn ordinary EOF into failure. */
    assert(FILE_ReadFile("BRITISH.PTH",resource,sizeof(resource),0)==0);
    for(int i=0;i<2783;i++) assert(resource[i]==(unsigned char)(i%256));
    for(int i=2783;i<3000;i++) assert(resource[i]==0xa5);
    memset(resource,0xa5,sizeof(resource));
    assert(FILE_ReadFile("BRITISH.PTH",resource,3000,2780)==0);
    assert(resource[0]==(unsigned char)(2780%256) && resource[3]==0xa5);
    assert(FILE_ReadFile("BRITISH.PTH",resource,3000,-1)==-1);
    assert(errno==EINVAL);

    put("SaVeGaMe/SaVeD.GaM", "save");
    assert(FILE_ReadFile("saved.gam", value, 4, 0) == 0);
    assert(memcmp(value, "save", 4) == 0);
    assert(FILE_WriteFile("SAVED.GAM", "new", 3, 0) == 0);
    assert(fopen("SaVeGaMe/SAVED.GAM", "rb") == NULL);
    put("u4SaVe/PaRtY.SaV", "u4");
    assert(FILE_ReadFile("PARTY.SAV", value, 2, 0) == 0);
    assert(memcmp(value, "u4", 2) == 0);
    put("bGm/01.OgG", "audio");
    assert(FILE_ResolvePath("BGM/01.ogg", resolved, sizeof(resolved), 0) == 0);
    assert(strcmp(resolved, "bGm/01.OgG") == 0);

    put("MiXeD/SubDir/ClAsH", "one");
    put("MiXeD/SubDir/cLaSh", "two");
    assert(FILE_Open("mixed/subdir/CLASH", "rb") == NULL && errno == EEXIST);
    assert(FILE_Open("mixed/subdir/CLASH", "wb") == NULL && errno == EEXIST);
    fp = FILE_Open("MiXeD/SubDir/ClAsH", "rb");
    assert(fp && fgetc(fp) == 'o'); fclose(fp);
    puts("Case-insensitive paths, save routing, writes, and ambiguity tests passed.");
    return 0;
}
