/* A generated, original sine-wave SF2 bank: no external sample licensing. */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static unsigned char sfdata[16000];
static size_t sfpos;
static void sfbytes(const void *p, size_t n) {
    memcpy(sfdata + sfpos, p, n);
    sfpos += n;
}
static void sfword(unsigned n) {
    unsigned char p[2] = {(unsigned char)n, (unsigned char)(n >> 8)};
    sfbytes(p, 2);
}
static void sfdword(unsigned n) {
    sfword(n);
    sfword(n >> 16);
}
static size_t sfchunk(const char *name) {
    sfbytes(name, 4);
    size_t p = sfpos;
    sfdword(0);
    return p;
}
static void sfend(size_t p) {
    unsigned n = (unsigned)(sfpos - p - 4);
    for (int i = 0; i < 4; i++)
        sfdata[p + i] = (unsigned char)(n >> (8 * i));
}
static void sfname(const char *name) {
    char text[20] = {0};
    strncpy(text, name, 19);
    sfbytes(text, 20);
}
static void sfph(const char *name, unsigned program, unsigned bag) {
    sfname(name);
    sfword(program);
    sfword(0);
    sfword(bag);
    sfdword(0);
    sfdword(0);
    sfdword(0);
}
static void sfsh(const char *name, unsigned end) {
    sfname(name);
    sfdword(0);
    sfdword(end);
    sfdword(0);
    sfdword(end);
    sfdword(48000);
    sfbytes("\x45\0", 2);
    sfword(0);
    sfword(1);
}
static void soundfontFixture(const char *path) {
    sfpos = 0;
    size_t riff = sfchunk("RIFF");
    sfbytes("sfbk", 4);
    size_t list = sfchunk("LIST");
    sfbytes("INFO", 4);
    size_t c = sfchunk("ifil");
    sfword(2);
    sfword(1);
    sfend(c);
    sfend(list);
    list = sfchunk("LIST");
    sfbytes("sdta", 4);
    c = sfchunk("smpl");
    for (int i = 0; i < 4800; i++)
        sfword((unsigned)(int16_t)(sin(i * 2 * 3.141592653589793 * 440 / 48000) * 16000));
    for (int i = 0; i < 46; i++)
        sfword(0);
    sfend(c);
    sfend(list);
    list = sfchunk("LIST");
    sfbytes("pdta", 4);
    c = sfchunk("phdr");
    sfph("Test Harpsichord", 6, 0);
    sfph("Test Lute", 24, 1);
    sfph("EOP", 0, 2);
    sfend(c);
    c = sfchunk("pbag");
    for (int i = 0; i < 3; i++) {
        sfword(i);
        sfword(0);
    }
    sfend(c);
    c = sfchunk("pmod");
    for (int i = 0; i < 10; i++)
        sfbytes("\0", 1);
    sfend(c);
    c = sfchunk("pgen");
    sfword(41);
    sfword(0);
    sfword(41);
    sfword(0);
    sfword(0);
    sfword(0);
    sfend(c);
    c = sfchunk("inst");
    sfname("Sine");
    sfword(0);
    sfname("EOI");
    sfword(1);
    sfend(c);
    c = sfchunk("ibag");
    sfword(0);
    sfword(0);
    sfword(2);
    sfword(0);
    sfend(c);
    c = sfchunk("imod");
    for (int i = 0; i < 10; i++)
        sfbytes("\0", 1);
    sfend(c);
    c = sfchunk("igen");
    sfword(54);
    sfword(1);
    sfword(53);
    sfword(0);
    sfword(0);
    sfword(0);
    sfend(c);
    c = sfchunk("shdr");
    sfsh("Sine", 4800);
    sfsh("EOS", 0);
    sfend(c);
    sfend(list);
    sfend(riff);
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fwrite(sfdata, 1, sfpos, f) == sfpos);
    assert(!fclose(f));
}
