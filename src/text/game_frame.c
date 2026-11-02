#include "game_frame.h"
void IMPERA_DrawGameFrame(const ImperaGameFrame *s) {
    int extra = s->partyRows>6 ? 16 : 0;
    s->fill(s->host, 0, 0, 0, 319, 199);
    s->fill(s->host, s->blue, 0, 0, 319, 6);
    s->fill(s->host, s->blue, 0, 185, 191, 191);
    s->fill(s->host, s->blue, 0, 0, 6, 191);
    s->fill(s->host, s->blue, 185, 0, 191, 191);
    s->fill(s->host, s->blue, 313, 0, 319, 87+extra);
    s->fill(s->host, s->blue, 192, 80+extra, 312, 87+extra);
    s->fill(s->host, s->blue, 192, 57+extra, 312, 63+extra);
    s->corner(s->host, s->blue, 0, 0, 0x7b);
    s->corner(s->host, s->blue, 39, 0, 0x7c);
    s->corner(s->host, s->blue, 0, 23, 0x7d);
    s->line(s->host, s->white, 7, 7, 7, 184);
    s->line(s->host, s->white, 7, 184, 184, 184);
    s->line(s->host, s->white, 184, 184, 184, 7);
    s->line(s->host, s->white, 184, 7, 7, 7);
    s->line(s->host, s->white, 191, 191, 191, 87+extra);
    s->line(s->host, s->white, 191, 87+extra, 319, 87+extra);
    s->line(s->host, s->white, 191, 7, 312, 7);
    s->line(s->host, s->white, 312, 7, 312, 56+extra);
    s->line(s->host, s->white, 312, 56+extra, 191, 56+extra);
    s->line(s->host, s->white, 191, 56+extra, 191, 7);
    s->line(s->host, s->white, 191, 63+extra, 312, 63+extra);
    s->line(s->host, s->white, 312, 63+extra, 312, 80+extra);
    s->line(s->host, s->white, 312, 80+extra, 191, 80+extra);
    s->line(s->host, s->white, 191, 80+extra, 191, 63+extra);
}
