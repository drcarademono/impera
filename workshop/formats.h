#pragma once
#include <QByteArray>
#include <QImage>
#include <QStringList>
#include <QVector>
namespace U5 {
QByteArray decompress(const QByteArray &bytes);
QByteArray compress(const QByteArray &raw);
struct Graphics {
    bool tiles = false;
    QByteArray original;
    QVector<QImage> images, originals;
};
Graphics readGraphics(const QString &name, const QByteArray &bytes);
QByteArray writeGraphics(const Graphics &graphics);
QVector<QRgb> palette();
struct Conversation {
    unsigned id;
    QByteArray bytes;
};
QVector<Conversation> readDialogue(const QByteArray &bytes);
QByteArray writeDialogue(const QVector<Conversation> &entries);
QString decodeText(const QByteArray &bytes);
QByteArray encodeText(const QString &text);
void validateText(const QByteArray &bytes);
QStringList textTags();
QVector<int> storyOffsets();
QStringList storyPages(const QByteArray &bytes);
QByteArray changeStory(const QByteArray &bytes, int page, const QString &text);
struct MapPage {
    QString name;
    int side, offset, stride, settlement = -1, floor = 0;
};
QVector<MapPage> mapPages(const QString &name, const QByteArray &bytes);
QByteArray worldMap(const QString &name, const QByteArray &bytes, const QByteArray &overlay);
void writeWorld(const QString &name, const QByteArray &world, QByteArray &bytes, QByteArray &overlay);
unsigned byte(const QByteArray &b, int offset);
unsigned word(const QByteArray &b, int offset);
void setWord(QByteArray &b, int offset, unsigned value);
void require(bool condition, const QString &message);
} // namespace U5
