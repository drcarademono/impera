#ifndef IMPERA_MOD_RUNTIME_H
#define IMPERA_MOD_RUNTIME_H
#include <stdio.h>
#include "cutscene_placement.h"
#include <stdint.h>
/* directory supplies original game resources; packages come from application/Mods.
 * U5D_MODS_DIR selects a private Workshop/test session instead. */
void MOD_MountDirectory(const char *directory);
FILE *MOD_OpenResource(const char *name, int *found);
unsigned MOD_LoadedCount(void);
unsigned MOD_FrameColor(void);
#define MOD_CATALOG_LIMIT 128
#define MOD_FILENAME_LIMIT 1024
typedef struct ModInfo {
    char filename[MOD_FILENAME_LIMIT], title[257], author[257], description[4097], error[256];
    unsigned char *image;
    size_t imageSize;
    int enabled, startupEnabled, active;
} ModInfo;
unsigned MOD_AvailableCount(void);
const ModInfo *MOD_Available(unsigned index);
int MOD_SetEnabled(unsigned index, int enabled);
int MOD_ChangesPending(void);
unsigned MOD_Revision(void);
const char *MOD_LocationName(unsigned map, const char *fallback);
unsigned MOD_TableValue(unsigned table, unsigned index, unsigned fallback);
/* Snapshot resets on each mount; never mutates the original compiled index. */
const uint8_t *MOD_BritanniaIndex(void);
/* Build fresh worlds from resources, never from persisted SAVEGAME lists. */
int MOD_InitializeWorldObjects(void *worlds512);
const char *MOD_NarrativeText(const char *resource, unsigned offset);
const char *MOD_SceneMessage(const char *resource, unsigned offset, const char *fallback);
const void *MOD_CutsceneComposition(int scene, size_t *size);
void MOD_CutscenePlacement(int scene, unsigned page, U5CutscenePlacement *placement);
unsigned MOD_CutsceneCount(int scene);
const char *MOD_CutscenePage(int scene, unsigned page, unsigned *layout);
#endif
