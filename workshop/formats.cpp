#include "formats.h"
#include "mod/package.h"
#include <QHash>
#include <QMap>
#include <QSet>
#include <algorithm>
#include <stdexcept>
namespace U5 {
void require(bool condition, const QString &message) {
    if (!condition)
        throw std::runtime_error(message.toStdString());
}
unsigned byte(const QByteArray &b, int o) {
    require(o >= 0 && o < b.size(), "Resource is truncated");
    return (unsigned char)b[o];
}
unsigned word(const QByteArray &b, int o) { return byte(b, o) | (byte(b, o + 1) << 8); }
static unsigned dword(const QByteArray &b, int o) { return word(b, o) | (word(b, o + 2) << 16); }
void setWord(QByteArray &b, int o, unsigned v) {
    require(o >= 0 && o + 1 < b.size() && v <= 65535, "Invalid 16-bit value or offset");
    b[o] = char(v);
    b[o + 1] = char(v >> 8);
}
static void appendWord(QByteArray &b, unsigned v) {
    require(v <= 65535, "Image offset exceeds DOS 16-bit format");
    b.append(char(v));
    b.append(char(v >> 8));
}
static void appendDword(QByteArray &b, unsigned v) {
    for (int i = 0; i < 4; i++)
        b.append(char(v >> (i * 8)));
}
QByteArray decompress(const QByteArray &b) {
    unsigned wanted = dword(b, 0);
    require(wanted <= MOD_MAX_RESOURCE, "Compressed resource is too large");
    QVector<QByteArray> table;
    auto reset = [&] {
        table.clear();
        for (int i = 0; i < 256; i++)
            table.append(QByteArray(1, char(i)));
        table.append(QByteArray());
        table.append(QByteArray());
    };
    reset();
    int width = 9, next = 258;
    qsizetype pos = 32;
    QByteArray out, prev;
    out.reserve(wanted);
    while (out.size() < wanted) {
        require(pos + width <= b.size() * 8, "Truncated LZW stream");
        unsigned code = 0;
        for (int i = 0; i < width; i++)
            code |= ((byte(b, int((pos + i) / 8)) >> ((pos + i) % 8)) & 1) << i;
        pos += width;
        if (code == 256) {
            reset();
            width = 9;
            next = 258;
            prev.clear();
            continue;
        }
        require(code != 257, "LZW ended before declared size");
        QByteArray entry;
        if (code < unsigned(table.size()) && !table[code].isEmpty())
            entry = table[code];
        else if (code == unsigned(next) && !prev.isEmpty())
            entry = prev + prev.left(1);
        else
            require(false, "Invalid LZW dictionary code");
        out.append(entry.left(wanted - out.size()));
        if (!prev.isEmpty() && next < 4096) {
            table.append(prev + entry.left(1));
            if (++next == (1 << width) && width < 12)
                ++width;
        }
        prev = entry;
    }
    return out;
}
QByteArray compress(const QByteArray &raw) {
    require(raw.size() <= MOD_MAX_RESOURCE, "Resource exceeds size limit");
    QByteArray out;
    appendDword(out, raw.size());
    unsigned bits = 0;
    int nbits = 0, width = 9, nextDecoder = 258;
    bool previous = false;
    auto writeCode = [&](int code) {
        bits |= unsigned(code) << nbits;
        nbits += width;
        while (nbits >= 8) {
            out.append(char(bits));
            bits >>= 8;
            nbits -= 8;
        }
        if (code == 256) {
            width = 9;
            nextDecoder = 258;
            previous = false;
        } else if (code != 257) {
            if (previous && nextDecoder < 4096 && ++nextDecoder == (1 << width) && width < 12)
                ++width;
            previous = true;
        }
    };
    QHash<QByteArray, int> table;
    auto reset = [&] {
        table.clear();
        for (int i = 0; i < 256; i++)
            table.insert(QByteArray(1, char(i)), i);
    };
    reset();
    int next = 258;
    QByteArray current;
    writeCode(256);
    for (char value : raw) {
        QByteArray combined = current + value;
        if (table.contains(combined)) {
            current = combined;
            continue;
        }
        writeCode(table.value(current));
        if (next < 4096)
            table.insert(combined, next++);
        else {
            writeCode(256);
            reset();
            next = 258;
        }
        current = QByteArray(1, value);
    }
    if (!current.isEmpty())
        writeCode(table.value(current));
    writeCode(257);
    if (nbits)
        out.append(char(bits));
    return out;
}
QVector<QRgb> palette() {
    return {qRgb(0, 0, 0),     qRgb(0, 0, 170),    qRgb(0, 170, 0),    qRgb(0, 170, 170),
            qRgb(170, 0, 0),   qRgb(170, 0, 170),  qRgb(170, 85, 0),   qRgb(170, 170, 170),
            qRgb(85, 85, 85),  qRgb(85, 85, 255),  qRgb(85, 255, 85),  qRgb(85, 255, 255),
            qRgb(255, 85, 85), qRgb(255, 85, 255), qRgb(255, 255, 85), qRgb(255, 255, 255)};
}
Graphics readGraphics(const QString &name, const QByteArray &b) {
    Graphics g;
    g.tiles = name == "TILES.16";
    g.original = b;
    QByteArray raw = g.tiles && b.size() == 65536 ? b : decompress(b);
    auto colors = palette();
    unsigned count = g.tiles ? 512 : word(raw, 0);
    require(count > 0 && count <= 4096, "Invalid image table");
    if (g.tiles)
        require(raw.size() == 65536, "TILES.16 must have 512 tiles");
    else
        require(2 + count * 4 <= unsigned(raw.size()), "Truncated image table");
    auto dims = [&](int off) {
        require(off >= int(2 + count * 4) && off + 4 <= raw.size(), "Image offset outside table");
        int w = word(raw, off), h = word(raw, off + 2);
        require(w > 0 && h > 0 && uint64_t(w) * h <= MOD_MAX_RESOURCE, "Invalid image dimensions");
        return QSize(w, h);
    };
    uint64_t totalPixels = 0;
    for (unsigned i = 0; i < count; i++) {
        int co = g.tiles ? i * 128 : word(raw, 2 + i * 4), mo = g.tiles ? 0 : word(raw, 4 + i * 4);
        if (!g.tiles && !co) {
            require(!mo, "Mask without image");
            g.images.append(QImage());
            continue;
        }
        QSize d = g.tiles ? QSize(16, 16) : dims(co);
        totalPixels += uint64_t(d.width()) * d.height();
        require(totalPixels <= MOD_MAX_RESOURCE, "Total decoded image pixels exceed limit");
        int stride = ((d.width() + 7) / 8) * 4, ms = (d.width() + 7) / 8, start = co + (g.tiles ? 0 : 4);
        require(start + stride * d.height() <= raw.size(), "Truncated color image");
        if (mo) {
            require(dims(mo) == d, "Mask dimensions differ");
            require(mo + 4 + ms * d.height() <= raw.size(), "Truncated mask");
        }
        QImage img(d, QImage::Format_ARGB32);
        require(!img.isNull(), "Cannot allocate image");
        for (int y = 0; y < d.height(); y++)
            for (int x = 0; x < d.width(); x++) {
                unsigned v = byte(raw, start + y * stride + x / 2);
                v = x % 2 ? v & 15 : v >> 4;
                QRgb color = colors[v];
                if (mo && (byte(raw, mo + 4 + y * ms + x / 8) & (128 >> (x % 8))))
                    color &= 0xffffff;
                img.setPixel(x, y, color);
            }
        g.images.append(img);
    }
    g.originals = g.images;
    return g;
}
QByteArray writeGraphics(const Graphics &g) {
    require(g.images.size() == g.originals.size(), "Changing image count is unsupported");
    if (g.images == g.originals)
        return g.original;
    QByteArray raw = g.tiles ? QByteArray() : QByteArray(2 + g.images.size() * 4, 0);
    if (!g.tiles)
        setWord(raw, 0, g.images.size());
    auto colors = palette();
    for (int i = 0; i < g.images.size(); i++) {
        const QImage &img = g.images[i];
        if (img.isNull())
            continue;
        require(img.size() == g.originals[i].size(), "Imported image dimensions must match the slot");
        int w = img.width(), h = img.height(), stride = ((w + 7) / 8) * 4, ms = (w + 7) / 8;
        QByteArray pixels(stride * h, 0), mask(ms * h, 0);
        bool transparent = false;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                QRgb c = img.pixel(x, y);
                int alpha = qAlpha(c);
                require(alpha == 0 || alpha == 255,
                        "DOS graphics require fully opaque or transparent pixels");
                require(!g.tiles || alpha == 255, "TILES.16 cannot represent PNG transparency");
                int index = 0;
                if (alpha) {
                    index = colors.indexOf(qRgb(qRed(c), qGreen(c), qBlue(c)));
                    require(index >= 0, "PNG must use the exact 16-color EGA palette");
                } else {
                    transparent = true;
                    mask[y * ms + x / 8] = char(byte(mask, y * ms + x / 8) | (128 >> (x % 8)));
                }
                pixels[y * stride + x / 2] =
                    char(byte(pixels, y * stride + x / 2) | (index << (x % 2 ? 0 : 4)));
            }
        if (!g.tiles) {
            setWord(raw, 2 + i * 4, raw.size());
            appendWord(raw, w);
            appendWord(raw, h);
        }
        raw.append(pixels);
        if (!g.tiles && transparent) {
            setWord(raw, 4 + i * 4, raw.size());
            appendWord(raw, w);
            appendWord(raw, h);
            raw.append(mask);
        }
    }
    return compress(raw);
}
QVector<Conversation> readDialogue(const QByteArray &b) {
    unsigned count = word(b, 0);
    require(count <= 127 && 2 + count * 4 <= unsigned(b.size()), "Invalid conversation header");
    QVector<Conversation> out;
    QSet<unsigned> ids;
    int previous = 2 + count * 4;
    for (unsigned i = 0; i < count; i++) {
        unsigned id = word(b, 2 + i * 4);
        int off = word(b, 4 + i * 4), end = i + 1 < count ? word(b, 8 + i * 4) : b.size();
        require(off >= previous && off < end && end <= b.size() && !ids.contains(id),
                "Duplicate NPC or invalid dialogue offset");
        ids.insert(id);
        previous = end;
        out.append({id, b.mid(off, end - off)});
    }
    return out;
}
QByteArray writeDialogue(const QVector<Conversation> &entries) {
    require(entries.size() <= 127, "Conversation table exceeds engine header");
    QByteArray out;
    appendWord(out, entries.size());
    int offset = 2 + entries.size() * 4;
    QSet<unsigned> ids;
    for (const auto &e : entries) {
        require(!ids.contains(e.id) && e.id <= 65535 && offset <= 32767 && !e.bytes.isEmpty() &&
                    e.bytes.size() <= 1024,
                "Invalid NPC, conversation size or signed DOS offset");
        ids.insert(e.id);
        appendWord(out, e.id);
        appendWord(out, offset);
        offset += e.bytes.size();
    }
    for (const auto &e : entries)
        out.append(e.bytes);
    return out;
}

// Canonical DOS token indices: src/vars.c D_24ea; covered by compatibility tests.
static const char *words[] = {
    "the",   "thou",    "of",    "to",    "and",         "that",  "for",          "",
    "in",    "is",      "have",  "with",  "thee",        "this",  "not",          "my",
    "it",    "me",      "but",   "dost",  "know",        "be",    "was",          "Blackthorn",
    "from",  "thy",     "one",   "",      "are",         "here",  "many",         "Lord",
    "am",    "we",      "they",  "he",    "would",       "art",   "on",           "young",
    "what",  "see",     "like",  "only",  "by",          "there", "Blackthorn's", "good",
    "been",  "",        "must",  "his",   "British",     "fine",  "an",           "great",
    "thee,", "our",     "who",   "name",  "heard",       "as",    "at",           "has",
    "",      "through", "",      "once",  "can",         "",      "him",          "",
    "",      "",        "",      "ye",    "Shadowlords", "tell",  "some",         "believe",
    "all",   "their",   "upon",  "even",  "'tis",        "find",  "if",           "about",
    "don't", "before",  "these", "just",  "make",        "will",  "when",         "three",
    "Great", "might",   "those", "old",   "hast",        "ask",   "unto",         "wish",
    "man",   "so",      "knows", "still", "Mantra",      "out",   "help",         "well",
    "shall", "think",   "where", "named", "talking",     "more",  "such",         "very",
    "may",   "lives",   "canst", "which", "since",       "need",  "I've",         "work"};
static QMap<unsigned, QString> controls() {
    return {{129, "Avatar"},      {130, "End Conversation"},
            {131, "Pause"},       {132, "Join Party"},
            {133, "Gold"},        {134, "Change"},
            {135, "Or"},          {136, "Ask Name"},
            {137, "Karma + 1"},   {138, "Karma - 1"},
            {139, "Call Guards"}, {140, "Set Flag"},
            {141, "New Line"},    {142, "Rune"},
            {143, "Key Wait"},    {144, "Any"}};
}
static int operands(unsigned v) { return v == 133 ? 3 : v == 134 || v == 140 ? 1 : v == 254 ? 2 : 0; }
void validateText(const QByteArray &bytes) {
    for (int i = 0; i < bytes.size();) {
        int n = operands(byte(bytes, i));
        require(i + n < bytes.size(), "Truncated dialogue command");
        for (int j = 1; j <= n; j++)
            require(byte(bytes, i + j) != 0, "NUL in dialogue command operands");
        i += 1 + n;
    }
}
QString decodeText(const QByteArray &bytes) {
    validateText(bytes);
    QString out;
    auto ctrl = controls();
    for (int i = 0; i < bytes.size();) {
        unsigned v = byte(bytes, i++);
        int n = operands(v);
        if (v == 0)
            out += "<Entry>\n";
        else if (v <= 128 && v > 0 && *words[v - 1])
            out += "<" + QString::fromLatin1(words[v - 1]) + ">";
        else if (ctrl.contains(v))
            out += "<" + ctrl[v] + ">";
        else if (v >= 145 && v <= 159)
            out += QString("<Label %1>").arg(v - 144);
        else if (v >= 160 && v <= 253 && v != 188 && v != 190)
            out += QChar(v - 128);
        else
            out += QString("<Byte %1>").arg(v);
        for (int j = 0; j < n; j++)
            out += QString("<Byte %1>").arg(byte(bytes, i++));
    }
    return out;
}
QByteArray encodeText(const QString &text) {
    QByteArray out;
    auto ctrl = controls();
    for (int i = 0; i < text.size();) {
        QChar c = text[i++];
        if (c == '\n' || c == '\r')
            continue; // editor formatting; explicit New Line controls the game
        if (c == '<') {
            int end = text.indexOf('>', i);
            require(end >= i, "Unterminated dialogue tag");
            QString tag = text.mid(i, end - i);
            i = end + 1;
            int code = -1;
            if (tag == "Entry")
                code = 0;
            for (auto it = ctrl.begin(); it != ctrl.end(); ++it)
                if (it.value() == tag)
                    code = it.key();
            for (unsigned j = 0; j < 128; j++)
                if (tag == QString::fromLatin1(words[j]) && *words[j])
                    code = j + 1;
            if (tag.startsWith("Byte ")) {
                bool ok;
                int v = tag.mid(5).toInt(&ok);
                require(ok && v >= 0 && v <= 255, "Byte tag must be 0..255");
                code = v;
            }
            if (tag.startsWith("Label ")) {
                bool ok;
                int v = tag.mid(6).toInt(&ok);
                require(ok && v >= 1 && v <= 15, "Label must be 1..15");
                code = 144 + v;
            }
            require(code >= 0, "Unknown dialogue tag: " + tag);
            out.append(char(code));
        } else {
            require(c.unicode() >= 32 && c.unicode() <= 126, "Dialogue requires ASCII text");
            out.append(char(c.unicode() + 128));
        }
    }
    validateText(out);
    return out;
}
QStringList textTags() {
    QStringList tags;
    for (auto s : controls())
        tags << "<" + s + ">";
    tags << "<Entry>" << "<Byte 255>";
    for (int i = 1; i <= 15; i++)
        tags << QString("<Label %1>").arg(i);
    return tags;
}
QVector<int> storyOffsets() {
    return {0,      0x111,  0x3cb,  0x590,  0x803,  0xac4,  0xd6d,  0xe23,  0xfb6,  0x1173,
            0x132a, 0x14be, 0x162e, 0x192b, 0x1b9a, 0x1e7b, 0x2172, 0x244b, 0x2780, 0x2a97};
}
QStringList storyPages(const QByteArray &b) {
    auto starts = storyOffsets();
    QStringList pages;
    require(b.size() > starts.last(), "STORY.DAT is truncated");
    for (int i = 0; i < starts.size(); i++) {
        int end = i + 1 < starts.size() ? starts[i + 1] : b.size(), nul = b.indexOf(char(0), starts[i]);
        require(nul >= starts[i] && nul < end, "Unterminated story page");
        pages << QString::fromLatin1(b.mid(starts[i], nul - starts[i]));
    }
    return pages;
}
QByteArray changeStory(const QByteArray &b, int page, const QString &text) {
    auto pages = storyPages(b);
    auto starts = storyOffsets();
    require(page >= 0 && page < pages.size(), "Invalid story page");
    QByteArray encoded;
    for (QChar c : text) {
        require(c.unicode() > 0 && c.unicode() < 128, "Story requires ASCII, without NUL");
        encoded.append(char(c.unicode()));
    }
    int cap = pages[page].size();
    require(encoded.size() <= cap, QString("Page capacity is %1 bytes").arg(cap));
    QByteArray out = b;
    encoded.append(QByteArray(cap - encoded.size(), 0));
    out.replace(starts[page], cap, encoded);
    return out;
}
QVector<MapPage> mapPages(const QString &name, const QByteArray &b) {
    QVector<MapPage> out;
    if (name == "BRIT.DAT" || name == "UNDER.DAT")
        return {{name == "BRIT.DAT" ? "Britannia" : "Underworld", 256, 0, 256}};
    if (name.endsWith(".CBT")) {
        require(!b.isEmpty() && b.size() % 352 == 0, "Combat map must contain 352-byte blocks");
        for (int i = 0; i < b.size() / 352; i++)
            out.append({QString("Combat map %1").arg(i + 1), 11, i * 352, 32});
        return out;
    }
    QStringList names;
    QVector<int> levels, floors(8, 0);
    if (name == "TOWNE.DAT") {
        names = {"Moonglow", "Britain", "Jhelom", "Yew", "Minoc", "Trinsic", "Skara Brae", "New Magincia"};
        levels = {2, 2, 2, 2, 2, 2, 2, 2};
        floors[3] = -1;
    } else if (name == "CASTLE.DAT") {
        names = {"Lord British's Castle",
                 "Blackthorn's Castle",
                 "West Britanny",
                 "North Britanny",
                 "East Britanny",
                 "Paws",
                 "Cove",
                 "Buccaneer's Den"};
        levels = {5, 5, 1, 1, 1, 1, 1, 1};
        floors[0] = floors[1] = -1;
    } else if (name == "DWELLING.DAT") {
        names = {"Fogsbane",   "Stormcrow", "Greyhaven",       "Waveguide",
                 "Iolo's hut", "Spektran",  "Sin'Vraal's hut", "Grendel's hut"};
        levels = {3, 3, 3, 3, 1, 1, 1, 1};
    } else if (name == "KEEP.DAT") {
        names = {"Ararat",    "Bordermarch", "Farthing",     "Windemere",
                 "Stonegate", "Lycaeum",     "Empath Abbey", "Serpent's Hold"};
        levels = {2, 2, 1, 1, 1, 3, 3, 3};
        floors[7] = -1;
    } else
        require(false, "No visual map layout for this resource; use the resource inspector");
    int offset = 0;
    for (int i = 0; i < 8; i++)
        for (int l = 0; l < levels[i]; l++) {
            require(offset + 1024 <= b.size(), "Settlement map is truncated");
            int floor = floors[i] + l;
            out.append({names[i] + (floor < 0 ? " — Basement" : QString(" — Floor %1").arg(floor)), 32,
                        offset, 32, i, floor});
            offset += 1024;
        }
    return out;
}
QByteArray worldMap(const QString &name, const QByteArray &b, const QByteArray &overlay) {
    QByteArray world(65536, 1);
    require(b.size() % 256 == 0, "World map chunk data is truncated");
    if (name == "BRIT.DAT")
        require(overlay.size() >= 0x3986, "DATA.OVL is missing its chunk index");
    else
        require(b.size() >= 65536, "Underworld is truncated");
    for (int i = 0; i < 256; i++) {
        unsigned index = name == "BRIT.DAT" ? byte(overlay, 0x3886 + i) : i;
        if (index == 255 && name == "BRIT.DAT")
            continue;
        require((index + 1) * 256 <= unsigned(b.size()), "World map chunk index is invalid");
        for (int y = 0; y < 16; y++)
            world.replace((i / 16 * 16 + y) * 256 + i % 16 * 16, 16, b.mid(index * 256 + y * 16, 16));
    }
    return world;
}
void writeWorld(const QString &name, const QByteArray &world, QByteArray &b, QByteArray &overlay) {
    require(world.size() == 65536, "World map must be 256 by 256");
    QVector<QByteArray> blocks;
    for (int i = 0; i < 256; i++) {
        QByteArray chunk;
        for (int y = 0; y < 16; y++)
            chunk += world.mid((i / 16 * 16 + y) * 256 + i % 16 * 16, 16);
        blocks.append(chunk);
    }
    if (name == "UNDER.DAT") {
        QByteArray rebuilt;
        for (auto block : blocks)
            rebuilt += block;
        b.replace(0, 65536, rebuilt);
        return;
    }
    require(name == "BRIT.DAT" && overlay.size() >= 0x3986, "Britannia requires DATA.OVL");
    QVector<QByteArray> chunks;
    for (int i = 0; i < b.size() / 256; i++)
        chunks.append(b.mid(i * 256, 256));
    QByteArray mapping = overlay.mid(0x3886, 256);
    QVector<int> changed;
    for (int i = 0; i < 256; i++) {
        int old = byte(mapping, i);
        require(old == 255 || old < chunks.size(), "Invalid original chunk");
        if (blocks[i] != (old == 255 ? QByteArray(256, 1) : chunks[old])) {
            changed.append(i);
            mapping[i] = char(255);
        }
    }
    for (int i : changed) {
        if (blocks[i] == QByteArray(256, 1))
            continue;
        int index = chunks.indexOf(blocks[i]);
        if (index < 0) {
            for (int j = 0; j < chunks.size() && j < 255; j++)
                if (!mapping.contains(char(j))) {
                    index = j;
                    break;
                }
            if (index < 0) {
                require(chunks.size() < 255,
                        "Britannia cannot represent more than 255 distinct non-water chunks");
                index = chunks.size();
                chunks.append(blocks[i]);
            } else
                chunks[index] = blocks[i];
        }
        require(index < 255, "Invalid non-water chunk index");
        mapping[i] = char(index);
    }
    QByteArray rebuilt;
    for (auto chunk : chunks)
        rebuilt += chunk;
    b = rebuilt;
    overlay.replace(0x3886, 256, mapping);
}
} // namespace U5
