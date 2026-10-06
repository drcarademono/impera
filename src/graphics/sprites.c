#include "common/common.h"
#include "common/file.h"
#include "sprites.h"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "third_party/stb_image.h"

static bool s_enabled;
static byte* s_images[256];
static const byte palette[16][3] = {
 {0,0,0},{0,0,170},{0,170,0},{0,170,170},{170,0,0},{170,0,170},{170,85,0},{170,170,170},
 {85,85,85},{85,85,255},{85,255,85},{85,255,255},{255,85,85},{255,85,255},{255,255,85},{255,255,255}
};
void SPRITES_SetEnabled(bool enabled) { s_enabled=enabled; }
void SPRITES_Cleanup(void)
{
    for (int i=0;i<256;i++) { stbi_image_free(s_images[i]); s_images[i]=NULL; }
}
void SPRITES_Load(void)
{
    SPRITES_Cleanup();
    if (!s_enabled) return;
    int loaded=0;
    for (int i=0;i<256;i++) {
        char name[96];
        snprintf(name,sizeof(name),"transparent-sprites/tile-%03x.png",256+i);
        FILE* fp=FILE_Open(name,"rb");
        if (!fp) continue;
        int width,height,channels;
        byte* pixels=stbi_load_from_file(fp,&width,&height,&channels,4);
        fclose(fp);
        if (!pixels || width!=16 || height!=16) {
            debug("Ignoring sprite PNG (requires 16x16): %s\n",name);
            stbi_image_free(pixels);
            continue;
        }
        s_images[i]=pixels; loaded++;
    }
    debug("Loaded %d transparent sprite overrides\n",loaded);
}
bool SPRITES_HasOverride(int tile)
{
    return tile>=256 && tile<512 && s_images[tile-256]!=NULL;
}
byte SPRITES_Composite(int tile,int x,int y,byte background,byte original)
{
    if (!SPRITES_HasOverride(tile)) return original;
    const byte* p=s_images[tile-256]+(y*16+x)*4;
    if (!p[3]) return background;
    int rgb[3];
    for (int i=0;i<3;i++) rgb[i]=(p[i]*p[3]+palette[background&15][i]*(255-p[3])+127)/255;
    int best=0,bestDistance=200000;
    for (int i=0;i<16;i++) {
        int distance=0;
        for (int j=0;j<3;j++) { int d=rgb[j]-palette[i][j]; distance+=d*d; }
        if (distance<bestDistance) { best=i; bestDistance=distance; }
    }
    return (byte)best;
}
u32 SPRITES_RGBA(int tile,int x,int y,u32 original)
{
    if (!SPRITES_HasOverride(tile)) return original;
    const byte* p=s_images[tile-256]+(y*16+x)*4;
    return ((u32)p[3]<<24)|((u32)p[0]<<16)|((u32)p[1]<<8)|p[2];
}
