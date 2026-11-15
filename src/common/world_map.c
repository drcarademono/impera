#include "common.h"
#include "world_map.h"
#include "engine_settings.h"
#include "world_map_palette.h"
#include "file.h"
#include "funcs.h"
#include "mod/types.h"
#include "mod/package.h"
#include "graphics/widescreen.h"
#include "macros.h"
#include "debug.h"
#include "graphics/grap_buf.h"
#include "graphics/grap_sdl.h"
#include "key/key.h"
#include "key/mouse.h"
#include "mod/maps.h"
#include "mod/runtime.h"
#include "time/time.h"
#include "vars.h"
#include <SDL3/SDL.h>
#include <string.h>

extern void KEY_SDL_ClearInput(void);

/* Interior coordinates are local to the building/dungeon, not Britannia. */
static bool partyPosition(int* x, int* y, int* resultWorld)
{
    int world = D_5895_map_level;
    unsigned map = D_5893_map_id;
    *x = D_5896_map_x; *y = D_5897_map_y;
    if(map) {
        if(!MAPS_ReturnPoint(map, &world, x, y)) {
            const unsigned char* r=MAPS_Record(MAPS_IsDungeon(map)?4:1,map,0);
            if(r && (int8_t)r[14]!=-2) {
                world=(int8_t)r[14]; *x=r[15]; *y=r[16];
            } else {
                if(map > 40) return false;
                *x = MOD_TableValue(7, map-1, D_1e8a[map-1]);
                *y = MOD_TableValue(8, map-1, D_1eb2[map-1]);
                world = map==25 || map==40 ? -1 : 0;
            }
        }
    }
    *resultWorld=world? -1:0;
    return true;
}

int WORLD_MAP_MarkerPosition(int spell,int* x,int* y,int* world)
{
    if(!partyPosition(x,y,world)) return 0;
    if(spell) {
        if(MAPS_IsDungeon(D_5893_map_id)) *world=0;
        return 1;
    }
    return D_57bc && !D_5893_map_id && !*world && (D_587f<=5 || D_587f>=19);
}

/* Keep the terrain as last observed, not fresh terrain from unseen locations. */
static struct { byte known[8192]; byte terrain[65536]; } explored;
static int exploredWidth,exploredHeight;
static unsigned explorationEpoch;
unsigned WORLD_MAP_ExplorationEpoch(void) { return explorationEpoch; }
void WORLD_MAP_ResetExploration(void)
{
    memset(&explored,0,sizeof(explored)); exploredWidth=exploredHeight=0;++explorationEpoch;
}
static void explorationSize(void)
{
    int w=MAPS_ActiveWidth(0,-1),h=MAPS_ActiveHeight(0,-1);
    if(w!=exploredWidth || h!=exploredHeight) {
        WORLD_MAP_ResetExploration(); exploredWidth=w; exploredHeight=h;
    }
}
static void remember(int x,int y,unsigned tile)
{
    x=MAPS_Wrap(x,exploredWidth);y=MAPS_Wrap(y,exploredHeight);
    TypeCell cell={0};cell.floor=-1;cell.x=x;cell.y=y;
    TypeDef def;
    if(TYPES_Get(TYPES_At(cell,tile),&def) && !def.kind) tile=def.base;
    int index=y*256+x;
    explored.known[index/8]|=1u<<(index%8);explored.terrain[index]=tile;
}
void WORLD_MAP_RecordGem(int x,int y,int columns,int rows)
{
    if(D_5893_map_id || !D_5895_map_level) return;
    explorationSize();
    for(int row=0;row<rows;row++) for(int col=0;col<columns;col++)
        remember(x+col,y+row,*ULTIMA_4402_GetTileAddr(x+col,y+row));
}
void WORLD_MAP_RecordView(int columns,int rows,int mapY,int clipTop,int clipBottom)
{
    if(D_5893_map_id || !D_5895_map_level || !D_58a4) return;
    explorationSize();
    for(int row=0;row<rows;row++) for(int col=0;col<columns;col++) {
        if(mapY+row*16>=clipBottom || mapY+(row+1)*16<=clipTop) continue;
        int dx=col-columns/2,dy=row-rows/2;
        if(abs(dx)<=5 && abs(dy)<=5 && GetMapViewport(dx+5,dy+5)==255) continue;
        if(!WIDE_Visible(dx,dy)) continue;
        unsigned tile=WIDE_MapTile(dx,dy);
        if(tile==255) continue;
        remember(D_5896_map_x+dx,D_5897_map_y+dy,tile);
    }
}
int WORLD_MAP_WriteExploration(FILE* file)
{
    bool any=false;
    for(unsigned i=0;i<sizeof(explored.known);i++) any|=explored.known[i]!=0;
    if(!any) return 1;
    unsigned char header[16]="IUWMAP01";
    header[8]=exploredWidth;header[9]=exploredWidth>>8;
    header[10]=exploredHeight;header[11]=exploredHeight>>8;
    TYPES_Put32(header+12,MOD_Crc32(&explored,sizeof(explored)));
    return fwrite(header,1,sizeof(header),file)==sizeof(header) &&
           fwrite(&explored,1,sizeof(explored),file)==sizeof(explored);
}
int WORLD_MAP_ReadExploration(FILE* file,int apply)
{
    long start=ftell(file);unsigned char header[16];
    size_t n=fread(header,1,sizeof(header),file);
    if(n<8 || memcmp(header,"IUWMAP01",8)) {
        if(ferror(file) || start<0 || fseek(file,start,SEEK_SET)) return 0;
        if(apply) WORLD_MAP_ResetExploration();
        return 1; /* Old saves have no exploration section. */
    }
    if(n!=sizeof(header)) return 0;
    int w=MAPS_Word(header+8),h=MAPS_Word(header+10);
    if(w<1 || w>256 || h<1 || h>256) return 0;
    void* next=malloc(sizeof(explored));if(!next) return 0;
    int ok=fread(next,1,sizeof(explored),file)==sizeof(explored) &&
           MOD_Crc32(next,sizeof(explored))==TYPES_U32(header+12);
    if(ok && apply) { memcpy(&explored,next,sizeof(explored));exploredWidth=w;exploredHeight=h;++explorationEpoch; }
    free(next);return ok;
}

static SDL_Surface* generate(int world)
{
    int w=MAPS_ActiveWidth(0,world), h=MAPS_ActiveHeight(0,world);
    SDL_Surface* image=SDL_CreateSurface(w,h,SDL_PIXELFORMAT_ARGB8888);
    if(!image) return NULL;
    if(world) {
        explorationSize();
        for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
            int index=y*256+x;
            Uint32* row=(Uint32*)((byte*)image->pixels+y*image->pitch);
            row[x]=0xff000000u;
            if(explored.known[index/8] & (1u<<(index%8)))
                row[x]|=world_map_colors[explored.terrain[index]];
        }
        return image;
    }
    const unsigned char* extended=MAPS_ActiveCell(0,0,0,0);
    FILE* file=extended?NULL:FILE_Open("BRIT.DAT","rb");
    if(!extended && !file) { SDL_DestroySurface(image); return NULL; }
    unsigned char blocks[256][256],loaded[256]={0};
    const uint8_t* index=MOD_BritanniaIndex();
    for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
        unsigned tile;
        if(extended) tile=*MAPS_ActiveCell(0,0,x,y);
        else {
            int block=(y/16)*16+x/16, resource=index[block];
            if(!loaded[block]) {
                if(resource==255) memset(blocks[block],1,256);
                else if(fseek(file,resource*256,SEEK_SET) || fread(blocks[block],1,256,file)!=256) {
                    fclose(file); SDL_DestroySurface(image); return NULL;
                }
                loaded[block]=1;
            }
            tile=blocks[block][(y%16)*16+x%16];
            /* Include live native terrain changes in the resident world blocks. */
            if(D_58a4 && !D_5893_map_id && !D_5895_map_level &&
               ((x-D_589b)&255)<32 && ((y-D_589c)&255)<32)
                tile=*ULTIMA_4402_GetTileAddr(x,y);
        }
        TypeCell cell={0}; cell.x=x; cell.y=y;
        uint32_t type=TYPES_At(cell,tile);
        TypeDef def;
        if(TYPES_Get(type,&def) && !def.kind) tile=def.base;
        Uint32* row=(Uint32*)((unsigned char*)image->pixels+y*image->pitch);
        row[x]=0xff000000u | world_map_colors[tile&255];
    }
    if(file) fclose(file);
    return image;
}

SDL_Surface* WORLD_MAP_Generate(void)
{
    int world=0,x,y;partyPosition(&x,&y,&world);
    return generate(world);
}

static int show(int spell)
{
    if(!ENGINE_Get(ENGINE_WORLD_MAP)) return 0;
    int px=0,py=0,world=0;
    bool marker=WORLD_MAP_MarkerPosition(spell,&px,&py,&world);
    SDL_Surface* image=generate(world);
    if(!image) {
        DEBUG_Error("Cannot generate the active world map");
        KEY_SDL_ClearInput(); MOUSE_Cancel(); return 0;
    }
    int width=image->w,height=image->h;
    if(!GRAP_SDL_SetWorldMap(image)) { SDL_DestroySurface(image); return 0; }
    SDL_DestroySurface(image);

    float cx = marker ? px+0.5f : width/2.0f, cy = marker ? py+0.5f : height/2.0f;
    float zoom=1;
    bool pointer = MOUSE_PointerMode(), done=false, dragging=false;
    float dragX=0, dragY=0;
    KEY_SDL_ClearInput(); MOUSE_Cancel(); MOUSE_SetPointerMode(true);
    TIME_PauseGameClock(true);
    while(!done) {
        GRAP_SDL_WorldMapView(cx,cy,zoom,px+0.5f,py+0.5f,marker && (SDL_GetTicks()/400)%2==0);
        GRAP_BUF_MarkDirty(); GRAP_BUF_Present();
        SDL_Event e;
        while(!done && SDL_PollEvent(&e)) {
            if(e.type==SDL_EVENT_QUIT || (e.type==SDL_EVENT_KEY_DOWN &&
               e.key.key==SDLK_E && (e.key.mod & SDL_KMOD_CTRL))) exit(0);
            if(e.type==SDL_EVENT_KEY_DOWN) {
                switch(e.key.key) {
                case SDLK_TAB: if(!e.key.repeat) done=true; break;
                case SDLK_ESCAPE: done=true; break;
                case SDLK_LEFT: cx-=8/zoom; break;
                case SDLK_RIGHT: cx+=8/zoom; break;
                case SDLK_UP: cy-=8/zoom; break;
                case SDLK_DOWN: cy+=8/zoom; break;
                }
            }
            if(e.type==SDL_EVENT_MOUSE_WHEEL) {
                float direction=e.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-1:1;
                zoom=SDL_clamp(zoom*SDL_powf(1.25f,e.wheel.y*direction),0.25f,8.0f);
                cx-=e.wheel.x*direction*12/zoom;
            }
            if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button==SDL_BUTTON_LEFT)
                dragging=GRAP_SDL_WorldMapPoint(e.button.x,e.button.y,&dragX,&dragY);
            if(e.type==SDL_EVENT_MOUSE_BUTTON_UP && e.button.button==SDL_BUTTON_LEFT) dragging=false;
            if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST) { dragging=false; KEY_SDL_ClearInput(); }
            if(e.type==SDL_EVENT_MOUSE_MOTION && dragging) {
                float x,y;
                /* Keep dragging outside the viewport until button-up. */
                GRAP_SDL_WorldMapPoint(e.motion.x,e.motion.y,&x,&y);
                cx-=x-dragX; cy-=y-dragY; dragX=x; dragY=y;
            }
            cx=SDL_clamp(cx,0,width); cy=SDL_clamp(cy,0,height);
        }
        MOUSE_UpdateCursor();
        if(!done) SDL_Delay(16);
    }
    TIME_PauseGameClock(false);
    GRAP_SDL_SetWorldMap(NULL);
    KEY_SDL_ClearInput(); MOUSE_Cancel();
    SDL_FlushEvents(SDL_EVENT_KEY_DOWN,SDL_EVENT_KEY_UP);
    SDL_FlushEvents(SDL_EVENT_MOUSE_MOTION,SDL_EVENT_MOUSE_WHEEL);
    MOUSE_SetPointerMode(pointer);
    GRAP_BUF_MarkDirty(); GRAP_BUF_Present();
    return 1;
}
void WORLD_MAP_Show(void) { (void)show(0); }
int WORLD_MAP_ShowSpell(void) { return show(1); }
