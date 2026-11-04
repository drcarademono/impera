#pragma once
#include "project.h"
#include <QWidget>
#include <functional>
namespace Workshop {
struct AudioClip {
    int kind = 0, id = 0, gain = 100, start = 0, end = 0;
    bool loop = true;
    QString name;
    QByteArray bytes;
};
struct AudioRoute {
    unsigned scope, key, level, track;
};
struct AudioDocument {
    QVector<AudioClip> clips;
    QVector<AudioRoute> routes;
    static AudioDocument read(const Project &);
    QByteArray encode() const;
    AudioClip clip(int kind, int id) const;
    void put(const AudioClip &);
};
QString audioName(int kind, int id);
QByteArray audioSource(const Project &, const AudioClip &, QString *source = nullptr);
QByteArray audioWav(const QByteArray &);
QStringList exportSoundEffects(const Project &, const QString &directory);
QWidget *audioEditor(Project &, int kind,
                     std::function<void(const QByteArray &, const QString &)> edit,
                     QWidget *parent = nullptr, std::function<bool()> *flushDraft = nullptr);
} // namespace Workshop
