#include "dialogue_links.h"
#include "event/event.h"
#include "graphics/grap_buf.h"
#include "graphics/grap_sdl.h"
#include "graphics/tileset.h"
#include "key.h"
#include "text/dialogue_runtime.h"
#include "vars.h"
#include <string.h>

typedef struct LinkCell {
    char topic[16];
    byte glyph[8], foreground;
} LinkCell;
typedef struct WordCell {
    int x, y;
    byte glyph[8], foreground;
} WordCell;
static LinkCell cells[25][40];
static WordCell pending[64];
static char token[65];
static byte conversation[1024];
static size_t conversationSize;
static int length;
static bool active, enabled, reading, prompt, dirty, overlong;
static bool visible(void) { return !GRAP_SDL_PixelUI() && !EVT_ImmediateExitEnabled(); }
bool DLINK_Enabled(void) { return enabled; }
void DLINK_Enable(bool on) {
    enabled = on;
    dirty = true;
    GRAP_BUF_MarkDirty();
}
void DLINK_Refresh(void) {
    if (!dirty || !visible() || !g_linearEgaBuffer0)
        return;
    for (int y = 0; y < 25; y++)
        for (int x = 0; x < 40; x++) {
            LinkCell *c = &cells[y][x];
            if (!c->topic[0])
                continue;
            for (int row = 0; row < 8; row++)
                for (int col = 0; col < 8; col++)
                    if (c->glyph[row] & (128 >> col)) {
                        byte *pixel = g_linearEgaBuffer0 + (y * 8 + row) * 320 + x * 8 + col;
                        if (enabled)
                            TILESET_RecordRGBA(pixel, 0xff676fffu);
                        else {
                            TILESET_Copy(pixel, NULL, 1);
                            *pixel = c->foreground;
                        }
                    }
        }
    dirty = false;
}
void DLINK_Flush(void) {
    while (length && (token[length - 1] == '\'' || token[length - 1] == '-'))
        --length;
    token[length] = 0;
    if (length && !overlong && IMPERA_TalkIsTopic(conversation, conversationSize, token)) {
        for (int i = 0; i < length; i++) {
            WordCell *p = &pending[i];
            if (p->x < 0 || p->x >= 40 || p->y < 0 || p->y >= 25)
                continue;
            LinkCell *c = &cells[p->y][p->x];
            size_t n = (size_t)length < 15 ? (size_t)length : 15;
            memcpy(c->topic, token, n);
            c->topic[n] = 0;
            memcpy(c->glyph, p->glyph, 8);
            c->foreground = p->foreground;
        }
        dirty = true;
        GRAP_BUF_MarkDirty();
    }
    length = 0;
    overlong = false;
}
void DLINK_Begin(const byte *data, size_t size) {
    DLINK_End();
    if (!data || !size)
        return;
    if (size > sizeof(conversation))
        size = sizeof(conversation);
    memcpy(conversation, data, size);
    conversationSize = size;
    active = true;
}
void DLINK_End(void) {
    DLINK_Flush();
    bool on = enabled;
    enabled = false;
    dirty = true;
    DLINK_Refresh();
    enabled = on;
    memset(cells, 0, sizeof(cells));
    length = 0;
    overlong = false;
    active = reading = prompt = false;
    GRAP_BUF_MarkDirty();
}
void DLINK_Reading(bool on) {
    DLINK_Flush();
    reading = on;
}
void DLINK_Prompt(bool on) {
    DLINK_Flush();
    prompt = on;
}
static bool letter(int ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
}
void DLINK_Character(int ch, const byte *glyph, byte foreground, int x, int y) {
    if (!active || !visible() || !D_538e || D_52ba_vdp._52d8_page != 0 || x < 0 || x >= 40 ||
        y < 0 || y >= 25)
        return;
    const TextWindow *w = &D_535e_textWindows[2];
    if (w->left < 24 || w->top < 10 || x < w->left || x > w->right || y < w->top || y > w->bottom)
        return;
    memset(&cells[y][x], 0, sizeof(cells[y][x]));
    if (reading)
        return;
    if (!letter(ch) && !((ch == '\'' || ch == '-') && length)) {
        DLINK_Flush();
        return;
    }
    if (length == 64) {
        overlong = true;
        return;
    }
    token[length] = (char)(ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch);
    pending[length].x = x;
    pending[length].y = y;
    pending[length].foreground = foreground;
    memcpy(pending[length].glyph, glyph, 8);
    ++length;
}
void DLINK_Clear(int left, int top, int right, int bottom) {
    if (!active || !visible())
        return;
    for (int y = 0; y < 25; y++)
        for (int x = 0; x < 40; x++)
            if (x * 8 <= right && x * 8 + 7 >= left && y * 8 <= bottom && y * 8 + 7 >= top)
                memset(&cells[y][x], 0, sizeof(cells[y][x]));
    for (int i = 0; i < length; i++)
        if (pending[i].x * 8 >= left && pending[i].x * 8 <= right && pending[i].y * 8 >= top &&
            pending[i].y * 8 <= bottom) {
            length = 0;
            overlong = false;
            break;
        }
}
void DLINK_Scroll(int left, int top, int right, int bottom, int amount) {
    if (!active || !visible() || amount % 8 || left % 8 || top % 8 || right % 8 != 7 ||
        bottom % 8 != 7)
        return;
    int first = top / 8, last = bottom / 8, shift = amount / 8;
    if (first < 0 || last >= 25 || left < 0 || right >= 320)
        return;
    for (int n = 0; n <= last - first; n++) {
        int y = shift < 0 ? first + n : last - n, source = y - shift;
        for (int x = left / 8; x <= right / 8; x++) {
            if (source >= first && source <= last)
                cells[y][x] = cells[source][x];
            else
                memset(&cells[y][x], 0, sizeof(cells[y][x]));
        }
    }
    for (int i = 0; i < length; i++)
        if (pending[i].x >= left / 8 && pending[i].x <= right / 8 && pending[i].y >= first &&
            pending[i].y <= last) {
            pending[i].y += shift;
            if (pending[i].y < first || pending[i].y > last)
                pending[i].y = -1;
        }
    dirty = true;
}
bool DLINK_Click(float x, float y) {
    float ux, uy;
    byte ignored;
    if (!active || !enabled || !prompt || !reading || !visible() ||
        !GRAP_SDL_MouseUIPoint(x, y, &ux, &uy) || ux < 0 || ux >= 320 || uy < 0 || uy >= 200 ||
        GRAP_BUF_TextHistoryPixel((int)ux, (int)uy, &ignored))
        return false;
    LinkCell *c = &cells[(int)uy / 8][(int)ux / 8];
    if (!c->topic[0])
        return false;
    KEY_SDL_SubmitText(c->topic);
    GRAP_BUF_EndTextScroll();
    return true;
}
