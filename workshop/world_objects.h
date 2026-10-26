#pragma once
#include "map_editor.h"
namespace Workshop {
QString mapObjectName(int shape, int value = 0, int aux = 0);
class WorldObjects {
  public:
    static QString resource(int world);
    static QByteArray effective(const Project &, int world);
    static bool overridden(const Project &, int world);
    static QByteArray change(const Project &, int world, int slot, const QByteArray &record);
    static void validate(const QString &, const QByteArray &, const QByteArray &original);
};
using TypedObjectCommit =
    std::function<bool(const QString &, const QByteArray &, bool, int, unsigned)>;
using ObjectCommit = std::function<bool(const QString &, const QByteArray &, bool)>;
QWidget *worldObjectEditor(Project *, int world, ObjectCommit, QMap<QString, int> *,
                           std::function<bool()> *, MapCanvas *sharedCanvas = nullptr,
                           std::function<void(QPoint)> locate = {},
                           TypedObjectCommit typedCommit = {});
} // namespace Workshop
