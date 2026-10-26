#pragma once
#include <QtWidgets>

namespace Workshop {
class MapResizeDialog : public QDialog {
  public:
    QSpinBox width, height;
    QComboBox fill;

    MapResizeDialog(QWidget *parent, QSize size) : QDialog(parent) {
        setWindowTitle("Resize map");
        width.setObjectName("mapWidth");
        height.setObjectName("mapHeight");
        fill.setObjectName("mapFill");
        width.setRange(1, 256);
        height.setRange(1, 256);
        width.setValue(size.width());
        height.setValue(size.height());
        fill.setIconSize(QSize(32, 32));
        fill.setToolTip("Fill newly added cells.");
        auto form = new QFormLayout(this);
        form->addRow("Width (tiles)", &width);
        form->addRow("Height (tiles)", &height);
        form->addRow("Fill", &fill);
        auto crop = new QLabel(this);
        form->addRow(crop);
        auto preview = [this, size, crop] {
            const int count = size.width() * size.height() -
                              qMin(size.width(), width.value()) *
                                  qMin(size.height(), height.value());
            crop->setText(count ? QString("%1 cells will be cropped.").arg(count) : QString());
        };
        connect(&width, &QSpinBox::valueChanged, this, preview);
        connect(&height, &QSpinBox::valueChanged, this, preview);
        preview();
        auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }
};
} // namespace Workshop
