#include "project.h"
#include "mod/package.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
using U5::require;
static QByteArray readLimited(const QString &path, qint64 cap) {
    QFile f(path);
    require(f.open(QIODevice::ReadOnly), "Cannot open " + path + ": " + f.errorString());
    require(f.size() <= cap, "File exceeds supported size: " + path);
    QByteArray bytes = f.readAll();
    require(bytes.size() == f.size(), "Failed reading " + path);
    return bytes;
}
void Project::openGame(const QString &directory) {
    Project next;
    next.sourceDirectory = QDir(directory).absolutePath();
    QDir dir(next.sourceDirectory);
    require(dir.exists(), "Game folder does not exist");
    for (const auto &name : dir.entryList(QDir::Files, QDir::Name)) {
        QString upper = name.toUpper();
        if (!MOD_AllowedResource(upper.toLatin1().constData()))
            continue;
        require(!next.resources.contains(upper), "Ambiguous filename case in game folder: " + name);
        QByteArray b = readLimited(dir.filePath(name), MOD_MAX_RESOURCE);
        next.resources.insert(upper, {b, b});
    }
    require(next.resources.contains("TILES.16") && next.resources.contains("INIT.GAM"),
            "Select a DOS Ultima 5 folder containing TILES.16 and INIT.GAM");
    U5::readGraphics("TILES.16", next.data("TILES.16"));
    require(next.data("INIT.GAM").size() == 4192, "INIT.GAM must use the 4192-byte DOS layout");
    *this = next;
}
const QByteArray &Project::data(const QString &name) const {
    auto it = resources.constFind(name);
    require(it != resources.cend(), "Required companion resource is missing: " + name);
    return it->edited;
}
QStringList Project::changed() const {
    QStringList list;
    for (auto it = resources.begin(); it != resources.end(); ++it)
        if (it->edited != it->original)
            list << it.key();
    return list;
}
void Project::validate() const {
    require(!title.trimmed().isEmpty() && title.toUtf8().size() <= 256 && !title.contains(QChar(0)),
            "Mod name must contain 1–256 UTF-8 bytes");
    for (const auto &name : changed()) {
        const auto &b = data(name);
        require(!b.isEmpty() && b.size() <= MOD_MAX_RESOURCE, "Invalid resource size: " + name);
        if (name.endsWith(".16"))
            U5::readGraphics(name, b);
        else if (name.endsWith(".TLK")) {
            auto entries = U5::readDialogue(b);
            for (auto e : entries)
                U5::validateText(e.bytes);
            U5::writeDialogue(entries);
        } else if (name.endsWith(".NPC"))
            require(b.size() == 4608, "NPC schedules must be 4608 bytes");
        else if (name == "INIT.GAM")
            require(b.size() == 4192, "INIT.GAM must be 4192 bytes");
        else if (name == "STORY.DAT")
            U5::storyPages(b);
        else if (name == "BRIT.DAT" || name == "DATA.OVL")
            U5::worldMap("BRIT.DAT", data("BRIT.DAT"), data("DATA.OVL"));
        else if (name == "UNDER.DAT")
            U5::worldMap(name, b, QByteArray());
        else if (name.endsWith(".CBT") ||
                 QStringList{"TOWNE.DAT", "KEEP.DAT", "CASTLE.DAT", "DWELLING.DAT"}.contains(name))
            U5::mapPages(name, b);
    }
}
static void append32(QByteArray &b, unsigned v) {
    for (int i = 0; i < 4; i++)
        b.append(char(v >> (8 * i)));
}
QByteArray Project::package() const {
    validate();
    auto list = changed();
    require(!list.isEmpty(), "There are no changes to export");
    require(list.size() <= MOD_MAX_ENTRIES, "Too many modified resources");
    QByteArray out("IMOD0001", 8), nameBytes = title.toUtf8();
    append32(out, nameBytes.size());
    append32(out, list.size());
    out += nameBytes;
    size_t totalOutput = 0;
    for (auto name : list) {
        totalOutput += resources[name].edited.size();
        require(totalOutput <= MOD_MAX_PACKAGE, "Decoded package exceeds 32 MiB");
        const auto &r = resources[name];
        QByteArray spans;
        unsigned count = 0;
        for (int i = 0; i < r.edited.size();) {
            if (i < r.original.size() && r.edited[i] == r.original[i]) {
                ++i;
                continue;
            }
            int begin = i++;
            while (i < r.edited.size() && (i >= r.original.size() || r.edited[i] != r.original[i]))
                ++i;
            append32(spans, begin);
            append32(spans, i - begin);
            spans += r.edited.mid(begin, i - begin);
            ++count;
        }
        QByteArray n = name.toLatin1();
        append32(out, n.size());
        out += n;
        append32(out, r.original.size());
        append32(out, r.edited.size());
        append32(out, MOD_Crc32(r.original.constData(), r.original.size()));
        append32(out, MOD_Crc32(r.edited.constData(), r.edited.size()));
        append32(out, count);
        out += spans;
    }
    require(out.size() <= MOD_MAX_PACKAGE, "Package exceeds 32 MiB");
    return out;
}
static int readBase(void *context, const char *name, unsigned char **data, size_t *size) {
    auto p = static_cast<Project *>(context);
    auto it = p->resources.constFind(QString::fromLatin1(name));
    *data = nullptr;
    *size = 0;
    if (it == p->resources.cend())
        return 0;
    *size = it->original.size();
    *data = static_cast<unsigned char *>(malloc(*size ? *size : 1));
    if (!*data)
        return 0;
    memcpy(*data, it->original.constData(), *size);
    return 1;
}
void Project::importPackage(const QByteArray &bytes) {
    ModPackage decoded;
    char error[256];
    int ok = MOD_Decode(bytes.constData(), bytes.size(), readBase, this, &decoded, error, sizeof(error));
    require(ok, QString::fromUtf8(error));
    Project next = *this;
    next.title = QString::fromUtf8(decoded.title);
    for (unsigned i = 0; i < decoded.count; i++) {
        auto &e = decoded.entries[i];
        next.resources[QString::fromLatin1(e.name)].edited =
            QByteArray(reinterpret_cast<char *>(e.data), e.size);
    }
    MOD_Free(&decoded);
    next.validate();
    *this = next;
}
void Project::save(const QString &path) {
    validate();
    QJsonObject root{{"format", "impera-workshop-1"}, {"source", sourceDirectory}, {"title", title}};
    QJsonArray edits;
    for (auto name : changed()) {
        const auto &r = resources[name];
        edits.append(QJsonObject{{"name", name},
                                 {"baseSize", r.original.size()},
                                 {"baseCrc", double(MOD_Crc32(r.original.constData(), r.original.size()))},
                                 {"bytes", QString::fromLatin1(r.edited.toBase64())}});
    }
    root["edits"] = edits;
    QSaveFile f(path);
    require(f.open(QIODevice::WriteOnly), f.errorString());
    auto json = QJsonDocument(root).toJson();
    require(f.write(json) == json.size() && f.commit(), "Cannot save project: " + f.errorString());
    projectPath = path;
}
void Project::load(const QString &path) {
    auto bytes = readLimited(path, 64 * 1024 * 1024);
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(bytes, &error);
    require(error.error == QJsonParseError::NoError && doc.isObject(), "Invalid Workshop project JSON");
    auto root = doc.object();
    require(root["format"] == "impera-workshop-1", "Unsupported Workshop project version");
    Project next;
    next.openGame(root["source"].toString());
    next.title = root["title"].toString();
    QSet<QString> names;
    require(root["edits"].isArray() && root["edits"].toArray().size() <= MOD_MAX_ENTRIES,
            "Invalid project edit list");
    for (auto value : root["edits"].toArray()) {
        auto edit = value.toObject();
        QString name = edit["name"].toString();
        require(next.resources.contains(name) && !names.contains(name),
                "Missing or duplicate resource in project");
        names.insert(name);
        auto &r = next.resources[name];
        require(edit["baseSize"].toInt(-1) == r.original.size() &&
                    edit["baseCrc"].toDouble(-1) == MOD_Crc32(r.original.constData(), r.original.size()),
                "Original files changed since project creation: " + name);
        auto decoded = QByteArray::fromBase64Encoding(edit["bytes"].toString().toLatin1(),
                                                      QByteArray::AbortOnBase64DecodingErrors);
        require(bool(decoded) && decoded.decoded.size() <= MOD_MAX_RESOURCE,
                "Invalid project resource bytes");
        r.edited = decoded.decoded;
    }
    next.validate();
    next.projectPath = path;
    *this = next;
}
