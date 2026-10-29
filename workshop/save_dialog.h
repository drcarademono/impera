#pragma once
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>

namespace Workshop {
// A format filter alone does not consistently add extensions on every platform.
inline QString saveFileName(QWidget *owner, const QString &title, const QString &initial,
                            const QString &filter, const QString &suffix) {
    QFileDialog dialog(owner, title, initial, filter);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setDefaultSuffix(suffix);
    while (dialog.exec() == QDialog::Accepted) {
        const auto files = dialog.selectedFiles();
        if (files.isEmpty() || files.first().isEmpty())
            return {};
        const QString selected = files.first();
        QString path = selected;
        if (!path.endsWith("." + suffix, Qt::CaseInsensitive))
            path += "." + suffix;
        // The native dialog already checked selected. If normalization changed
        // the target, confirm replacement of the actual file we will write.
        if (path != selected && QFileInfo::exists(path) &&
            QMessageBox::question(owner, "Replace file?",
                                  "The file already exists:\n" + path + "\nReplace it?",
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
                QMessageBox::Yes) {
            dialog.selectFile(path);
            continue;
        }
        return path;
    }
    return {};
}
} // namespace Workshop
