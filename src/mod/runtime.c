#include "metadata.h"
#include "interface.h"
#include "location_names.h"
#include "maps.h"
#include "runtime.h"
#include "common/common.h"
#include "common/file.h"
#include "contents.h"
#include "cutscenes.h"
#include "engine_tables.h"
#include "narrative.h"
#include "package.h"
#include "types.h"
#include "world_resources.h"
#include <SDL3/SDL.h>
#include <string.h>

/* Startup snapshot: no extraction, no changes to game assets or writable saves. */
static ModPackage *packages[128];
static unsigned loaded;
static ModInfo catalog[MOD_CATALOG_LIMIT];
static unsigned catalogCount;
static char preferences[FILE_PATH_SIZE];
static char disabled[MOD_CATALOG_LIMIT][MOD_FILENAME_LIMIT];
static unsigned disabledCount;
unsigned MOD_AvailableCount(void) { return catalogCount; }
const ModInfo *MOD_Available(unsigned index) { return index < catalogCount ? &catalog[index] : NULL; }
int MOD_ChangesPending(void) {
    for (unsigned i = 0; i < catalogCount; ++i)
        if (catalog[i].enabled != catalog[i].startupEnabled) return 1;
    return 0;
}
static int disabledIndex(const char *name) {
    for (unsigned i = 0; i < disabledCount; ++i)
        if (!strcmp(disabled[i], name)) return (int)i;
    return -1;
}
static int hexValue(char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
static void loadPreferences(void) {
    disabledCount = 0;
    FILE *f = fopen(preferences, "rb");
    if (!f) return;
    char line[MOD_FILENAME_LIMIT * 2 + 2];
    if (!fgets(line, sizeof(line), f) || strcmp(line, "IMODS001\n")) { fclose(f); return; }
    while (disabledCount < MOD_CATALOG_LIMIT && fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        if (!n || line[n - 1] != '\n' || (n - 1) % 2 || n <= 1) continue;
        --n;
        if (n / 2 >= MOD_FILENAME_LIMIT) continue;
        char name[MOD_FILENAME_LIMIT]; int ok = 1;
        for (size_t i = 0; i < n / 2; ++i) {
            int a = hexValue(line[i * 2]), b = hexValue(line[i * 2 + 1]);
            if (a < 0 || b < 0 || !(a || b)) { ok = 0; break; }
            name[i] = (char)(a * 16 + b);
        }
        name[n / 2] = 0;
        if (ok && disabledIndex(name) < 0) SDL_strlcpy(disabled[disabledCount++], name, MOD_FILENAME_LIMIT);
    }
    fclose(f);
}
static int savePreferences(void) {
    if (!*preferences) return 0;
    char temporary[FILE_PATH_SIZE];
    if (SDL_snprintf(temporary, sizeof(temporary), "%s.tmp", preferences) >= (int)sizeof(temporary)) return 0;
    FILE *f = fopen(temporary, "wb");
    if (!f) return 0;
    int ok = fputs("IMODS001\n", f) >= 0;
    for (unsigned i = 0; ok && i < disabledCount; ++i) {
        for (const unsigned char *p = (const unsigned char *)disabled[i]; *p; ++p)
            if (fprintf(f, "%02x", *p) < 0) { ok = 0; break; }
        if (fputc('\n', f) == EOF) ok = 0;
    }
    if (fclose(f)) ok = 0;
    if (ok) ok = SDL_RenamePath(temporary, preferences);
    if (!ok) remove(temporary);
    return ok;
}
int MOD_SetEnabled(unsigned index, int enabled) {
    if (index >= catalogCount) return 0;
    ModInfo *info = &catalog[index]; enabled = !!enabled;
    if (info->enabled == enabled) return 1;
    int found = disabledIndex(info->filename);
    if (!enabled) {
        if (disabledCount == MOD_CATALOG_LIMIT) return 0;
        SDL_strlcpy(disabled[disabledCount++], info->filename, MOD_FILENAME_LIMIT);
    } else if (found >= 0) {
        memmove(disabled[found], disabled[found + 1], (disabledCount - found - 1) * MOD_FILENAME_LIMIT);
        --disabledCount;
    }
    if (!savePreferences()) { loadPreferences(); return 0; }
    info->enabled = enabled;
    return 1;
}

static unsigned revision;
unsigned MOD_Revision(void) { return revision; }
static uint8_t britanniaIndex[256];
static int customIndex;
const uint8_t *MOD_BritanniaIndex(void) {
    return customIndex ? britanniaIndex : U5_BritanniaDefaultIndex;
}
static const char *baseDirectory;
static int readBase(void *context, const char *name, unsigned char **data, size_t *size) {
    char full[FILE_PATH_SIZE], resolved[FILE_PATH_SIZE];
    (void)context;
    *data = NULL;
    *size = 0;
    if (SDL_snprintf(full, sizeof(full), "%s/%s", baseDirectory, name) >= (int)sizeof(full) ||
        FILE_ResolvePath(full, resolved, sizeof(resolved), 0))
        return 0;
    FILE *f = fopen(resolved, "rb");
    if (!f)
        return 0;
    if (fseek(f, 0, SEEK_END)) {
        fclose(f);
        return 0;
    }
    long length = ftell(f);
    if (length < 0 || length > MOD_MAX_RESOURCE || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return 0;
    }
    *data = malloc(length ? (size_t)length : 1);
    *size = (size_t)length;
    int ok = *data && fread(*data, 1, *size, f) == *size;
    fclose(f);
    if (!ok) {
        free(*data);
        *data = NULL;
    }
    return ok;
}
static const ModResource *resourceIn(const ModPackage *p, const char *name) {
    if (p) for (unsigned i = 0; i < p->count; ++i)
        if (FILE_NameEqual(p->entries[i].name, name)) return &p->entries[i];
    return NULL;
}
static const ModResource *mountedResource(const char *name) {
    for (unsigned i = 0; i < loaded; ++i) {
        const ModResource *r = resourceIn(packages[i], name);
        if (r) return r;
    }
    return NULL;
}
/* Validate against the effective pair, including earlier packages. No partial mount. */
static int validateWorlds(const ModPackage *candidate, char *error, size_t capacity) {
    const char *objects[] = {"INIT.OOL", "BRIT.OOL", "UNDER.OOL"};
    for (unsigned i = 0; i < 3; ++i) {
        const ModResource *r = resourceIn(candidate, objects[i]);
        if (r && r->size != U5_WORLD_OBJECT_SIZE) {
            SDL_snprintf(error, capacity, "%s must contain exactly 32 eight-byte world objects", objects[i]);
            return 0;
        }
    }
    if (!resourceIn(candidate, "DATA.OVL") && !resourceIn(candidate, "BRIT.DAT")) return 1;
    const ModResource *overlay = resourceIn(candidate, "DATA.OVL");
    if (!overlay) overlay = mountedResource("DATA.OVL");
    const uint8_t *index = U5_BritanniaDefaultIndex;
    if (overlay) {
        if (overlay->size < U5_WORLD_INDEX_OFFSET + U5_WORLD_INDEX_SIZE) {
            SDL_strlcpy(error, "DATA.OVL is missing the Britannia chunk index", capacity);
            return 0;
        }
        index = overlay->data + U5_WORLD_INDEX_OFFSET;
    }
    const ModResource *map = resourceIn(candidate, "BRIT.DAT");
    if (!map) map = mountedResource("BRIT.DAT");
    unsigned char *base = NULL;
    size_t size = map ? map->size : 0;
    if (!map && !readBase(NULL, "BRIT.DAT", &base, &size)) {
        SDL_strlcpy(error, "Cannot validate Britannia: BRIT.DAT is missing or unreadable", capacity);
        return 0;
    }
    int ok = U5_ValidBritanniaIndex(index, size);
    free(base);
    if (!ok) SDL_strlcpy(error, "Britannia chunk index references missing/truncated BRIT.DAT chunks", capacity);
    return ok;
}
static int compareNames(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}
unsigned MOD_LoadedCount(void) { return loaded; }
void MOD_MountDirectory(const char *directory) {
    for (unsigned i = 0; i < catalogCount; ++i) free(catalog[i].image);
    memset(catalog, 0, sizeof(catalog)); catalogCount = 0;
    preferences[0] = 0; disabledCount = 0;
    MAPS_Mount(NULL, 0);
    TYPES_Mount(NULL, 0, NULL, 0);
    for (unsigned i = 0; i < loaded; i++) {
        MOD_Free(packages[i]);
        free(packages[i]);
    }
    CONTENTS_Unmount();
    ++revision;
    loaded = 0;
    customIndex = 0;
    memcpy(britanniaIndex, U5_BritanniaDefaultIndex, sizeof(britanniaIndex));
    baseDirectory = directory;
    size_t totalMemory = 0;
    if (!directory || !*directory)
        return;
    char full[FILE_PATH_SIZE], mods[FILE_PATH_SIZE];
    const char *override = SDL_getenv("U5D_MODS_DIR");
    const char *appimage = SDL_getenv("APPIMAGE");
    if (override && *override) {
        if (SDL_strlcpy(full, override, sizeof(full)) >= sizeof(full))
            return;
    } else {
        const char *base = appimage && *appimage ? appimage : SDL_GetBasePath();
        if (!base || SDL_strlcpy(full, base, sizeof(full)) >= sizeof(full))
            return;
        if (appimage && *appimage) {
            char *slash = strrchr(full, '/');
            if (!slash)
                return;
            *slash = 0;
        }
#if defined(OS_MACOS)
        char *bundle = strstr(full, ".app/Contents/");
        if (bundle) {
            *bundle = 0;
            char *slash = strrchr(full, '/');
            if (!slash)
                return;
            *slash = 0;
        }
#endif
        if (SDL_strlcat(full, "/Mods", sizeof(full)) >= sizeof(full))
            return;
    }
    if (FILE_ResolvePath(full, mods, sizeof(mods), 0))
        return;
    SDL_PathInfo folder;
    if (!SDL_GetPathInfo(mods, &folder) || folder.type != SDL_PATHTYPE_DIRECTORY)
        return;
    if (SDL_snprintf(preferences, sizeof(preferences), "%s/mods.cfg", mods) >= (int)sizeof(preferences))
        preferences[0] = 0;
    loadPreferences();
    int count = 0;
    char **names = SDL_GlobDirectory(mods, "*.imperamod", SDL_GLOB_CASEINSENSITIVE, &count);
    if (!names) {
        DEBUG_Error("Cannot scan Mods folder: %s", SDL_GetError());
        return;
    }
    qsort(names, count, sizeof(char *), compareNames);
    for (int i = 0; i < count && catalogCount < MOD_CATALOG_LIMIT; i++) {
        char path[FILE_PATH_SIZE], error[256];
        SDL_PathInfo info;
        if (SDL_snprintf(path, sizeof(path), "%s/%s", mods, names[i]) >= (int)sizeof(path) ||
            !SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE)
            continue;
        if (strlen(names[i]) >= MOD_FILENAME_LIMIT) continue;
        ModInfo *item = &catalog[catalogCount++];
        SDL_strlcpy(item->filename, names[i], sizeof(item->filename));
        SDL_strlcpy(item->title, names[i], sizeof(item->title));
        item->enabled = item->startupEnabled = disabledIndex(names[i]) < 0;
        if (loaded == 128 || info.size > MOD_MAX_PACKAGE) {
            SDL_strlcpy(item->error, "Package exceeds the mod size limit", sizeof(item->error));
            DEBUG_Error("Skipping mod %s: package limit exceeded", names[i]);
            continue;
        }
        FILE *f = fopen(path, "rb");
        if (!f) {
            SDL_strlcpy(item->error, "Cannot read this mod file", sizeof(item->error));
            DEBUG_Error("Cannot open mod %s", names[i]);
            continue;
        }
        unsigned char *bytes = malloc(info.size ? (size_t)info.size : 1);
        ModPackage *p = calloc(1, sizeof(*p));
        int ok = bytes && p && fread(bytes, 1, (size_t)info.size, f) == info.size;
        fclose(f);
        if (ok)
            ok = MOD_Decode(bytes, (size_t)info.size, readBase, NULL, p, error, sizeof(error));
        else
            SDL_strlcpy(error, "Cannot read package or allocate memory", sizeof(error));
        free(bytes);
        if (ok) {
            SDL_strlcpy(item->title, p->title, sizeof(item->title));
            const ModResource *meta = resourceIn(p, "MODINFO.DAT");
            if (meta) {
                unsigned text = MODINFO_U32(meta->data + 8), image = MODINFO_U32(meta->data + 12);
                memcpy(item->description, meta->data + MODINFO_Header(meta->data), text);
                memcpy(item->author, meta->data + MODINFO_Header(meta->data) + text, MODINFO_AuthorSize(meta->data));
                if (image) {
                    item->image = malloc(image);
                    if (item->image) {
                        memcpy(item->image, meta->data + MODINFO_ImageOffset(meta->data), image);
                        item->imageSize = image;
                    }
                }
            }
        }
        if (!item->enabled) {
            if (!ok) SDL_strlcpy(item->error, error, sizeof(item->error));
            if (p) { MOD_Free(p); free(p); }
            continue;
        }
        if (ok)
            for (unsigned j = 0; j < loaded; j++)
                for (unsigned a = 0; a < p->count; a++)
                    for (unsigned b = 0; b < packages[j]->count; b++)
                        if (!strcmp(p->entries[a].name, packages[j]->entries[b].name) &&
                            strcmp(p->entries[a].name, "CONTENTS.OVR") &&
                            strcmp(p->entries[a].name, "MODINFO.DAT")) {
                            ok = 0;
                            SDL_snprintf(error, sizeof(error), "Resource %s already overridden by %s",
                                         p->entries[a].name, packages[j]->title);
                        }
        if (ok) ok = validateWorlds(p, error, sizeof(error));
        const ModResource *contents = resourceIn(p, "CONTENTS.OVR");
        if (ok && contents && !CONTENTS_CanMount(contents->data, contents->size)) {
            ok = 0;
            SDL_strlcpy(error,
                        "Conflicting map contents target/identity or contents limit exceeded",
                        sizeof(error));
        }
        size_t packageMemory = 0;
        if (ok)
            for (unsigned j = 0; j < p->count; j++)
                packageMemory += p->entries[j].size;
        if (ok && packageMemory > 64u * 1024u * 1024u - totalMemory) {
            ok = 0;
            SDL_strlcpy(error, "Total mod memory limit (64 MiB) exceeded", sizeof(error));
        }
        const ModResource *maps = resourceIn(p, "MAPS.OVR");
        if (ok && maps && !MAPS_Mount(maps->data, maps->size)) {
            ok = 0;
            SDL_strlcpy(error, "Cannot mount extended maps", sizeof(error));
        }
        if (ok) {
            totalMemory += packageMemory;
            packages[loaded++] = p;
            item->active = 1;
            if (contents)
                CONTENTS_Mount(contents->data, contents->size);
            const ModResource *types = resourceIn(p, "TYPES.OVR"),
                              *typeMap = resourceIn(p, "TYPEMAP.OVR");
            if (types)
                TYPES_Mount(types->data, types->size, typeMap ? typeMap->data : NULL,
                            typeMap ? typeMap->size : 0);
            const ModResource *overlay = resourceIn(p, "DATA.OVL");
            if (overlay) {
                memcpy(britanniaIndex, overlay->data + U5_WORLD_INDEX_OFFSET, sizeof(britanniaIndex));
                customIndex = 1;
                debug("Using modded Britannia chunk index");
            }
            debug("Loaded mod %s (%s), %u resources", names[i], p->title, p->count);
        } else {
            SDL_strlcpy(item->error, error, sizeof(item->error));
            DEBUG_Error("Skipping mod %s: %s", names[i], error);
            if (p) {
                MOD_Free(p);
                free(p);
            }
        }
    }
    SDL_free(names);
    baseDirectory = NULL;
}
FILE *MOD_OpenResource(const char *name, int *found) {
    *found = 0;
    for (unsigned i = 0; i < loaded; i++)
        for (unsigned j = 0; j < packages[i]->count; j++) {
            ModResource *r = &packages[i]->entries[j];
            if (!FILE_NameEqual(r->name, name))
                continue;
            *found = 1;
            FILE *f = tmpfile();
            if (!f) {
                DEBUG_Error("Cannot open temporary mod resource %s", name);
                return NULL;
            }
            if (fwrite(r->data, 1, r->size, f) != r->size || fseek(f, 0, SEEK_SET)) {
                fclose(f);
                DEBUG_Error("Cannot prepare mod resource %s", name);
                return NULL;
            }
            return f;
        }
    return NULL;
}

int MOD_InitializeWorldObjects(void *worlds512) {
    unsigned char fresh[512] = {0};
    FILE *f = FILE_Open("INIT.OOL", "rb");
    if (!f) return 0;
    int ok = fread(fresh + 256, 1, 256, f) == 256 && fgetc(f) == EOF && !ferror(f);
    fclose(f);
    if (!ok) {
        DEBUG_Error("Cannot initialize new game: INIT.OOL must be 256 bytes");
        return 0;
    }
    const char *names[] = {"BRIT.OOL", "UNDER.OOL"};
    for (unsigned i = 0; i < 2; ++i) {
        const ModResource *r = mountedResource(names[i]);
        if (r) {
            if (r->size != 256) return 0;
            memcpy(fresh + i * 256, r->data, 256);
        }
    }
    memcpy(worlds512, fresh, sizeof(fresh));
    debug("Initialized fresh world objects: Britannia=%s Underworld=%s",
          mountedResource("BRIT.OOL") ? "mod override" : "normal empty start",
          mountedResource("UNDER.OOL") ? "mod override" : "INIT.OOL");
    return 1;
}

const char *MOD_LocationName(unsigned map, const char *fallback) {
    const ModResource *r = mountedResource("LOCATIONS.OVR");
    const char *name = r ? LOCATIONS_Name(r->data, r->size, map) : NULL;
    if (name)
        return name;
    const unsigned char *extended = MAPS_Record(MAPS_IsDungeon(map) ? 4 : 1, map, 0);
    return extended && MAPS_NewLocation(map) ? (const char *)extended + 24 : fallback;
}
const char *MOD_NarrativeText(const char *resource, unsigned offset) {
    const ModResource *r = mountedResource("TEXT.OVR");
    if (!r) return NULL;
    const U5NarrativeEntry *entry = U5_NarrativeEntry(resource, offset);
    int valid;
    return entry && r ? U5_FindNarrativeText(r->data, r->size, entry->id, &valid) : NULL;
}

const char *MOD_SceneMessage(const char *resource, unsigned offset, const char *fallback) {
    const char *text = MOD_NarrativeText(resource, offset);
    return text ? text : fallback;
}

unsigned MOD_FrameColor(void) {
    const ModResource *r = mountedResource("INTERFACE.OVR");
    return r && INTERFACE_Validate(r->data, r->size) ? r->data[8] : 1;
}

unsigned MOD_TableValue(unsigned table, unsigned index, unsigned fallback) {
    const ModResource *r = mountedResource("RULES.TBL");
    return r ? U5_TableValue(r->data, r->size, table, index, fallback) : fallback;
}

unsigned MOD_CutsceneCount(int scene) {
    const ModResource *r=mountedResource("CUTSCENE.OVR");
    unsigned count=0;
    if(r) U5_CutsceneRead(r->data,r->size,scene,0,NULL,NULL,&count);
    return count;
}
const char *MOD_CutscenePage(int scene, unsigned page, unsigned *layout) {
    const ModResource *r=mountedResource("CUTSCENE.OVR");
    const char *text=NULL;
    if(r) U5_CutsceneRead(r->data,r->size,scene,page,layout,&text,NULL);
    return text;
}

void MOD_CutscenePlacement(int scene, unsigned page, U5CutscenePlacement *placement) {
    const ModResource *r=mountedResource("CUTSCENE.OVR");
    memset(placement,0,sizeof(*placement));
    if(r) U5_CutsceneReadPlacement(r->data,r->size,scene,page,NULL,NULL,NULL,placement);
}

const void *MOD_CutsceneComposition(int scene, size_t *size) {
    const ModResource *r=mountedResource("CUTSCENE.OVR");
    if(!r||r->size<20||memcmp(r->data,"ICUT0003",8)||!MOD_CutsceneCount(scene)) return NULL;
    *size=r->size;return r->data;
}
