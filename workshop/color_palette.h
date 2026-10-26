#pragma once
#include "formats.h"
#include "mod/tileset_resources.h"
#include <QtWidgets>
#include <algorithm>
#include <functional>
#include <climits>

namespace Workshop {
inline QVector<QRgb> artworkColors(const U5::Graphics &graphics) {
    if (MOD_IsTilesetMask(graphics.pngResource.toLatin1().constData()))
        return {qRgb(0, 0, 0), qRgb(255, 255, 255)};
    if (graphics.pngResource.isEmpty())
        return U5::palette();
    QSet<QRgb> unique;
    for (const auto &image : graphics.images)
        for (int y = 0; y < image.height(); y++)
            for (int x = 0; x < image.width(); x++)
                unique.insert(image.pixel(x, y));
    auto colors = unique.values().toVector();
    std::sort(colors.begin(), colors.end());
    return colors;
}
inline QRgb paletteColor(QRgb rgba, const QVector<QRgb> &colors) {
    if (!qAlpha(rgba))
        return 0;
    if (colors.isEmpty())
        return rgba;
    QRgb best = colors.first();
    int distance = INT_MAX;
    for (auto color : colors) {
        int r = qRed(rgba) - qRed(color), g = qGreen(rgba) - qGreen(color),
            b = qBlue(rgba) - qBlue(color);
        int next = r * r + g * g + b * b;
        if (next < distance) {
            distance = next;
            best = color;
        }
    }
    return qRgba(qRed(best), qGreen(best), qBlue(best), qAlpha(rgba));
}
inline QImage paletteImage(const QImage &source, const QVector<QRgb> &colors) {
    auto result = source.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < result.height(); y++)
        for (int x = 0; x < result.width(); x++)
            result.setPixel(x, y, paletteColor(result.pixel(x, y), colors));
    return result;
}
// PNG, GIMP GPL, JASC-PAL and plain hex/RGB palettes. Import never changes artwork.
inline QVector<QRgb> readPalette(const QString &path) {
    QVector<QRgb> colors;
    auto append = [&](QRgb color) {
        if (!colors.contains(color))
            colors.append(color);
        U5::require(colors.size() <= 1024, "A palette may contain at most 1024 colors");
    };
    if (QFileInfo(path).suffix().compare("png", Qt::CaseInsensitive) == 0) {
        QImageReader reader(path);
        auto size = reader.size();
        U5::require(size.isValid() && size.width() <= 4096 && size.height() <= 4096,
                    "Palette PNG must be no larger than 4096 × 4096 pixels");
        auto image = reader.read();
        U5::require(!image.isNull(), "Cannot read palette PNG");
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                append(image.pixelColor(x, y).rgba());
    } else {
        QFile file(path);
        U5::require(file.open(QIODevice::ReadOnly) && file.size() <= 1024 * 1024,
                    "Cannot read palette file, or file exceeds 1 MiB");
        const auto lines = QString::fromUtf8(file.readAll()).split('\n');
        bool jasc = !lines.isEmpty() && lines[0].trimmed() == "JASC-PAL";
        int expected = jasc && lines.size() > 2 ? lines[2].trimmed().toInt() : 0;
        QRegularExpression rgb("^([0-9]{1,3})\\s+([0-9]{1,3})\\s+([0-9]{1,3})(?:\\s.*)?$");
        QRegularExpression hex("^#?(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$");
        int entries = 0;
        for (int i = jasc ? 3 : 0; i < lines.size(); ++i) {
            auto line = lines[i].trimmed();
            if (line.isEmpty() || line.startsWith("GIMP Palette") || line.startsWith("Name:") ||
                line.startsWith("Columns:"))
                continue;
            if (hex.match(line).hasMatch()) {
                QColor color(line.startsWith('#') ? line : "#" + line);
                U5::require(color.isValid(), "Invalid palette color");
                append(color.rgba());
            } else if (line.startsWith('#')) {
                continue;
            } else {
                auto match = rgb.match(line);
                U5::require(match.hasMatch(),
                            "Use a PNG, GIMP GPL, JASC-PAL, or hex/RGB text palette");
                int r = match.captured(1).toInt(), g = match.captured(2).toInt(),
                    b = match.captured(3).toInt();
                U5::require(r <= 255 && g <= 255 && b <= 255,
                            "Palette RGB components must be between 0 and 255");
                append(qRgb(r, g, b));
            }
            ++entries;
        }
        U5::require(!jasc ||
                        (lines.size() > 2 && lines[1].trimmed() == "0100" && expected == entries),
                    "Invalid JASC palette header or color count");
    }
    U5::require(!colors.isEmpty(), "Palette contains no colors");
    return colors;
}
// Both artwork workspaces use the complete palette of the selected source sheet.
class ColorPalette : public QWidget {
    QGridLayout *grid;
    QVector<QToolButton *> swatches;
    QRgb selected = qRgb(255, 255, 255);
    int columns;
    QWidget *customTools;
    QVector<QRgb> customColors;
    bool editable = false;
    void rememberCustomColors() {
        QStringList values;
        for (auto color : customColors)
            values.append(QColor::fromRgba(color).name(QColor::HexArgb));
        QSettings().setValue("artwork/customPalette", values);
    }

  public:
    std::function<void(QRgb)> changed;
    explicit ColorPalette(int columnCount, QWidget *parent = nullptr)
        : QWidget(parent), columns(columnCount) {
        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        auto scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setMaximumHeight(118);
        scroll->setMinimumHeight(78);
        auto content = new QWidget;
        grid = new QGridLayout(content);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        scroll->setWidget(content);
        layout->addWidget(scroll);
        customTools = new QWidget;
        auto tools = new QHBoxLayout(customTools);
        tools->setContentsMargins(0, 0, 0, 0);
        auto color = new QToolButton;
        color->setText("+");
        color->setObjectName("customPaletteAddColor");
        color->setToolTip("Choose any drawing color and add it to your Custom palette. "
                          "Transparency is controlled by alpha, not by black.");
        auto import = new QToolButton;
        import->setText("Import palette…");
        import->setObjectName("customPaletteImport");
        import->setToolTip(
            "Load swatches from a PNG, GIMP GPL, JASC-PAL or hex/RGB text file. This replaces the "
            "Custom palette without recoloring artwork or restricting which colors you can use.");
        tools->addWidget(color);
        tools->addWidget(import);
        tools->addStretch();
        layout->addWidget(customTools);
        customTools->hide();
        for (const auto &value : QSettings().value("artwork/customPalette").toStringList()) {
            QColor c(value);
            if (c.isValid() && !customColors.contains(c.rgba()))
                customColors.append(c.rgba());
        }
        if (customColors.isEmpty())
            customColors = U5::palette();
        connect(color, &QToolButton::clicked, this, [this] {
            auto picked = QColorDialog::getColor(QColor::fromRgba(selected), this, "Drawing color",
                                                 QColorDialog::ShowAlphaChannel);
            if (!picked.isValid())
                return;
            addCustomColor(picked.rgba());
            if (changed)
                changed(picked.rgba());
        });
        connect(import, &QToolButton::clicked, this, [this] {
            auto path = QFileDialog::getOpenFileName(
                this, "Import palette", {},
                "Palettes (*.png *.gpl *.pal *.hex *.txt);;All files (*)");
            if (path.isEmpty())
                return;
            try {
                importPalette(path);
            } catch (const std::exception &e) {
                QMessageBox::warning(this, "Cannot import palette", QString::fromUtf8(e.what()));
            }
        });
    }
    void setEditable(bool enabled) {
        editable = enabled;
        customTools->setVisible(enabled);
        if (enabled)
            setColors(customColors, true);
    }
    void addCustomColor(QRgb rgba) {
        if (!editable)
            return;
        if (!customColors.contains(rgba)) {
            customColors.append(rgba);
            rememberCustomColors();
            setColors(customColors, true);
        }
        select(rgba);
    }
    void importPalette(const QString &path) {
        U5::require(editable, "Palette import is available for Custom artwork only");
        auto colors = readPalette(path);
        customColors = colors;
        rememberCustomColors();
        setColors(customColors, true);
        select(selected);
    }
    void select(QRgb rgba) {
        selected = rgba;
        for (auto b : swatches)
            b->setChecked(b->property("rgba").toUInt() == rgba);
    }
    void setColors(QVector<QRgb> colors, bool transparent) {
        while (auto item = grid->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        swatches.clear();
        if (transparent && !colors.contains(0))
            colors.append(0);
        int index = 0;
        for (QRgb rgba : colors) {
            auto b = new QToolButton;
            b->setCheckable(true);
            b->setFixedSize(30, 30);
            b->setProperty("rgba", quint32(rgba));
            b->setToolTip(
                qAlpha(rgba)
                    ? "Draw with this palette color (" + QColor::fromRgba(rgba).name() +
                          "). Black is opaque."
                    : "Draw transparent pixels to reveal the ground behind a transparent sprite.");
            b->setAccessibleName(b->toolTip());
            b->setStyleSheet(QString("QToolButton {background:%1;border:1px solid #8290a0;} "
                                     "QToolButton:checked {border:3px solid #ffffff;}")
                                 .arg(qAlpha(rgba) ? QColor::fromRgba(rgba).name() : "#333e4e"));
            if (!qAlpha(rgba))
                b->setText("×");
            grid->addWidget(b, index / columns, index % columns);
            index++;
            swatches.append(b);
            connect(b, &QToolButton::clicked, this, [=] {
                select(rgba);
                if (changed)
                    changed(rgba);
            });
        }
        select(selected);
    }
};
} // namespace Workshop
