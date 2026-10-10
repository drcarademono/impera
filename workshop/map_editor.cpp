#include "map_editor.h"
#include <cmath>
using U5::require;

bool MapDocument::supported(const QString &r) {
    return r.endsWith(".CBT") || QStringList{"BRIT.DAT",   "UNDER.DAT", "TOWNE.DAT",
                                             "CASTLE.DAT", "KEEP.DAT",  "DWELLING.DAT"}
                                     .contains(r);
}
MapDocument::MapDocument(Project *p, const QString &r) : project(p), resource(r) {
    pages = U5::mapPages(resource, project->data(resource));
}
QByteArray MapDocument::terrain(int index) const {
    const auto &p = pages.at(index);
    auto data = project->data(resource);
    if (p.side == 256)
        return U5::worldMap(resource, data,
                            resource == "BRIT.DAT" ? project->data("DATA.OVL") : QByteArray());
    QByteArray cells;
    for (int y = 0; y < p.side; ++y)
        cells += data.mid(p.offset + y * p.stride, p.side);
    require(cells.size() == p.side * p.side, "Map terrain is truncated");
    return cells;
}
QMap<QString, QByteArray> MapDocument::changes(int index, const QByteArray &cells) const {
    const auto &p = pages.at(index);
    require(cells.size() == p.side * p.side, "Map terrain dimensions changed");
    auto data = project->data(resource);
    QMap<QString, QByteArray> result;
    if (p.side == 256) {
        auto overlay = resource == "BRIT.DAT" ? project->data("DATA.OVL") : QByteArray();
        U5::writeWorld(resource, cells, data, overlay);
        if (resource == "BRIT.DAT")
            result["DATA.OVL"] = overlay;
    } else
        for (int y = 0; y < p.side; ++y)
            data.replace(p.offset + y * p.stride, p.side, cells.mid(y * p.side, p.side));
    result[resource] = data;
    return result;
}

MapCanvas::MapCanvas(QWidget *parent) : QWidget(parent) {
    setObjectName("mapCanvas");
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}
void MapCanvas::resizeMap() {
    setFixedSize(int(std::ceil(side * 16 * zoom)), int(std::ceil(side * 16 * zoom)));
    update();
}
QPoint MapCanvas::cellAt(QPointF p) const {
    if (p.x() < 0 || p.y() < 0 || p.x() >= side * 16 * zoom || p.y() >= side * 16 * zoom)
        return {-1, -1};
    return {int(std::floor(p.x() / (16 * zoom))), int(std::floor(p.y() / (16 * zoom)))};
}
void MapCanvas::paintEvent(QPaintEvent *e) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    double cell = 16 * zoom;
    auto r = e->rect();
    p.fillRect(r, QColor("#141b27"));
    for (int y = qMax(0, int(r.top() / cell)); y <= qMin(side - 1, int(r.bottom() / cell)); ++y)
        for (int x = qMax(0, int(r.left() / cell)); x <= qMin(side - 1, int(r.right() / cell));
             ++x) {
            int offset = y * side + x;
            if (offset >= ids.size())
                continue;
            int id = U5::byte(ids, offset);
            QRectF target(x * cell, y * cell, cell, cell);
            if (id < tiles.size())
                p.drawImage(target, tiles[id]);
            else
                p.fillRect(target, Qt::magenta);
            if (grid) {
                p.setPen(QColor(255, 255, 255, 45));
                p.drawRect(target.adjusted(0, 0, -1, -1));
            }
        }
    for (auto a : actors)
        if (a.x >= 0 && a.y >= 0 && a.x < side && a.y < side && a.tile >= 0 &&
            a.tile < tiles.size())
            p.drawImage(QRectF(a.x * cell, a.y * cell, cell, cell), tiles[a.tile]);
    if (hover.x() >= 0) {
        QRectF target(hover.x() * cell, hover.y() * cell, cell, cell);
        if (tool == 0 && brush >= 0 && brush < tiles.size()) {
            p.setOpacity(0.35);
            p.drawImage(target, tiles[brush]);
            p.setOpacity(1);
        }
        p.setPen(QPen(QColor("#f2ce65"), 2));
        p.drawRect(target.adjusted(1, 1, -1, -1));
    }
}
void MapCanvas::line(QPoint a, QPoint b) {
    int x = a.x(), y = a.y(), dx = std::abs(b.x() - x), sx = x < b.x() ? 1 : -1;
    int dy = -std::abs(b.y() - y), sy = y < b.y() ? 1 : -1, error = dx + dy;
    for (;;) {
        ids[y * side + x] = char(brush);
        if (x == b.x() && y == b.y())
            break;
        int e = 2 * error;
        if (e >= dy) {
            error += dy;
            x += sx;
        }
        if (e <= dx) {
            error += dx;
            y += sy;
        }
    }
    update(QRectF(qMin(a.x(), b.x()) * 16 * zoom, qMin(a.y(), b.y()) * 16 * zoom,
                  (std::abs(a.x() - b.x()) + 1) * 16 * zoom,
                  (std::abs(a.y() - b.y()) + 1) * 16 * zoom)
               .toAlignedRect());
}
void MapCanvas::point(QPointF position, bool draw) {
    auto cell = cellAt(position);
    auto previous = hover;
    hover = cell;
    if (previous != hover) {
        for (auto cell : {previous, hover})
            if (cell.x() >= 0)
                update(QRectF(cell.x() * 16 * zoom, cell.y() * 16 * zoom, 16 * zoom, 16 * zoom)
                           .toAlignedRect()
                           .adjusted(-2, -2, 2, 2));
    }
    if (cell.x() < 0) {
        last = {-1, -1};
        return;
    }
    int offset = cell.y() * side + cell.x();
    if (offset >= ids.size())
        return;
    if (inspect)
        inspect(cell.x(), cell.y(), U5::byte(ids, offset));
    if (draw && stroke) {
        line(last.x() < 0 ? cell : last, cell);
        last = cell;
    }
}
void MapCanvas::mousePressEvent(QMouseEvent *e) {
    setFocus(Qt::MouseFocusReason);
    auto cell = cellAt(e->position());
    if (cell.x() < 0)
        return;
    if (e->button() == Qt::RightButton || (e->button() == Qt::LeftButton && tool == 1)) {
        if (pick)
            pick(U5::byte(ids, cell.y() * side + cell.x()));
        point(e->position(), false);
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    if (tool == 3) {
        for (auto a : actors)
            if (a.x == cell.x() && a.y == cell.y()) {
                if (chooseNpc)
                    chooseNpc(a.npc);
                break;
            }
        return;
    }
    if (tool != 0)
        return;
    before = ids;
    stroke = true;
    last = {-1, -1};
    point(e->position(), true);
}
void MapCanvas::mouseMoveEvent(QMouseEvent *e) {
    point(e->position(), stroke && (e->buttons() & Qt::LeftButton));
}
void MapCanvas::mouseReleaseEvent(QMouseEvent *e) {
    if (!stroke || e->button() != Qt::LeftButton)
        return;
    point(e->position(), true);
    stroke = false;
    last = {-1, -1};
    if (ids != before && commit && !commit(ids)) {
        ids = before;
        update();
    }
    before.clear();
}
void MapCanvas::cancelStroke() {
    if (stroke) {
        ids = before;
        before.clear();
        stroke = false;
        last = {-1, -1};
        update();
    }
}
void MapCanvas::leaveEvent(QEvent *) {
    hover = {-1, -1};
    update();
}
void MapCanvas::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) {
        cancelStroke();
        e->accept();
        return;
    }
    if (e->modifiers() == Qt::NoModifier && (e->key() == Qt::Key_B || e->key() == Qt::Key_I)) {
        cancelStroke();
        if (chooseTool)
            chooseTool(e->key() == Qt::Key_B ? 0 : 1);
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}
void MapCanvas::focusOutEvent(QFocusEvent *e) {
    cancelStroke();
    QWidget::focusOutEvent(e);
}
bool MapCanvas::event(QEvent *e) {
    if (e->type() == QEvent::UngrabMouse || e->type() == QEvent::WindowDeactivate)
        cancelStroke();
    return QWidget::event(e);
}

MapView::MapView(MapCanvas *c) : canvas(c) {
    setObjectName("canvasView");
    setWidget(canvas);
    setAlignment(Qt::AlignCenter);
    viewport()->installEventFilter(this);
    canvas->installEventFilter(this);
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        if (changed)
            changed();
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        if (changed)
            changed();
    });
}
QPointF MapView::mapCenter() const {
    auto p = canvas->mapFrom(viewport(), viewport()->rect().center());
    return QPointF(p) / (16 * canvas->zoom);
}
void MapView::centerMap(QPointF cell) {
    horizontalScrollBar()->setValue(
        qRound(cell.x() * 16 * canvas->zoom - viewport()->width() / 2.0));
    verticalScrollBar()->setValue(
        qRound(cell.y() * 16 * canvas->zoom - viewport()->height() / 2.0));
}
void MapView::setZoom(double scale, QPoint anchor, bool fit) {
    canvas->cancelStroke();
    if (anchor.x() < 0)
        anchor = viewport()->rect().center();
    QPointF at = QPointF(canvas->mapFrom(viewport(), anchor)) / (16 * canvas->zoom);
    fitting = fit;
    canvas->zoom = qBound(fit ? 0.001 : 0.02, scale, 8.0);
    canvas->resizeMap();
    horizontalScrollBar()->setValue(qRound(at.x() * 16 * canvas->zoom - anchor.x()));
    verticalScrollBar()->setValue(qRound(at.y() * 16 * canvas->zoom - anchor.y()));
    if (changed)
        changed();
}
void MapView::fitMap() {
    auto s = viewport()->size() - QSize(4, 4);
    setZoom(qMin(s.width(), s.height()) / double(canvas->side * 16), {-1, -1}, true);
}
void MapView::resizeEvent(QResizeEvent *e) {
    QScrollArea::resizeEvent(e);
    if (fitting)
        fitMap();
}
bool MapView::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        auto key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Space) {
            if (!key->isAutoRepeat())
                space = event->type() == QEvent::KeyPress;
            canvas->setCursor(space ? Qt::OpenHandCursor : Qt::CrossCursor);
            return true;
        }
        if (event->type() == QEvent::KeyPress &&
            (key->key() == Qt::Key_Plus || key->key() == Qt::Key_Equal ||
             key->key() == Qt::Key_Minus)) {
            setZoom(canvas->zoom * (key->key() == Qt::Key_Minus ? 0.5 : 2));
            return true;
        }
        if (key->key() == Qt::Key_Escape && panning) {
            panning = false;
            canvas->setCursor(Qt::CrossCursor);
            return true;
        }
    }
    if (event->type() == QEvent::FocusOut || event->type() == QEvent::WindowDeactivate) {
        space = false;
        panning = false;
        canvas->setCursor(Qt::CrossCursor);
    }
    if (event->type() == QEvent::Wheel) {
        auto wheel = static_cast<QWheelEvent *>(event);
        if (wheel->modifiers() & Qt::ControlModifier) {
            QPoint anchor = watched == canvas
                                ? canvas->mapTo(viewport(), wheel->position().toPoint())
                                : wheel->position().toPoint();
            int delta = wheel->angleDelta().y();
            if (delta)
                setZoom(canvas->zoom * (delta > 0 ? 2 : 0.5), anchor);
            return true;
        }
        if (wheel->modifiers() & Qt::ShiftModifier) {
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() -
                                            wheel->angleDelta().y());
            return true;
        }
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto mouse = static_cast<QMouseEvent *>(event);
        // QScrollArea tries to reveal its focused child. For a map-sized child
        // that can recenter the entire map; focusing must preserve the viewport.
        int horizontal = horizontalScrollBar()->value(), vertical = verticalScrollBar()->value();
        canvas->setFocus(Qt::MouseFocusReason);
        horizontalScrollBar()->setValue(horizontal);
        verticalScrollBar()->setValue(vertical);
        if (mouse->button() == Qt::MiddleButton ||
            (mouse->button() == Qt::LeftButton && (space || canvas->tool == 2))) {
            canvas->cancelStroke();
            canvas->setFocus(Qt::MouseFocusReason);
            panning = true;
            panAt = mouse->globalPosition().toPoint();
            canvas->setCursor(Qt::ClosedHandCursor);
            return true;
        }
    }
    if (event->type() == QEvent::MouseMove && panning) {
        auto mouse = static_cast<QMouseEvent *>(event);
        auto now = mouse->globalPosition().toPoint();
        auto delta = now - panAt;
        panAt = now;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease && panning) {
        panning = false;
        canvas->setCursor(space || canvas->tool == 2 ? Qt::OpenHandCursor : Qt::CrossCursor);
        return true;
    }
    return QScrollArea::eventFilter(watched, event);
}

MapWorkspace::MapWorkspace(
    Project *p, const QString &r, QMap<QString, MapViewState> *s, QMap<QString, int> *n,
    std::function<bool(const QMap<QString, QByteArray> &, const QString &)> commit,
    std::function<void(const QString &)> navigate, QWidget *parent)
    : QWidget(parent), project(p), resource(r), document(p, r), states(s), navigation(n) {
    setObjectName("mapWorkspace");
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    auto split = new QSplitter;
    layout->addWidget(split, 1);
    auto left = new QWidget;
    auto leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    auto search = new QLineEdit;
    search->setObjectName("mapSearch");
    search->setPlaceholderText("Find a map…");
    leftLayout->addWidget(search);
    maps = new QTreeWidget;
    maps->setObjectName("mapNavigation");
    maps->setHeaderLabel("Maps");
    leftLayout->addWidget(maps);
    auto world = new QTreeWidgetItem(maps, {"World"});
    auto towns = new QTreeWidgetItem(maps, {"Settlements"});
    auto combat = new QTreeWidgetItem(maps, {"Combat maps"});
    for (auto name : project->resources.keys())
        if (MapDocument::supported(name)) {
            auto pages = U5::mapPages(name, project->data(name));
            QMap<QString, QTreeWidgetItem *> locations;
            QTreeWidgetItem *combatGroup = nullptr;
            if (name.endsWith(".CBT"))
                combatGroup = new QTreeWidgetItem(combat, {name});
            for (int i = 0; i < pages.size(); ++i) {
                auto mp = pages[i];
                QTreeWidgetItem *parentItem = world;
                QString label = mp.name;
                if (mp.settlement >= 0) {
                    auto location = mp.name.section(" — ", 0, 0);
                    if (!locations.contains(location))
                        locations[location] = new QTreeWidgetItem(towns, {location});
                    parentItem = locations[location];
                    label = mp.floor < 0    ? "Basement"
                            : mp.floor == 0 ? "Ground floor"
                                            : QString("Upper floor %1").arg(mp.floor);
                } else if (combatGroup)
                    parentItem = combatGroup;
                auto item = new QTreeWidgetItem(parentItem, {label});
                item->setData(0, Qt::UserRole, name);
                item->setData(0, Qt::UserRole + 1, i);
                item->setToolTip(
                    0, QString("%1 · %2 · engine floor %3").arg(name).arg(mp.name).arg(mp.floor));
            }
        }
    maps->expandToDepth(0);
    left->setMinimumWidth(170);
    split->addWidget(left);
    auto center = new QWidget;
    auto centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    auto row = new QHBoxLayout;
    page = new QComboBox;
    page->setObjectName("mapPage");
    for (auto mp : document.pages)
        page->addItem(mp.name);
    page->setCurrentIndex(
        qBound(0, navigation->value(resource + "/map"), int(document.pages.size() - 1)));
    row->addWidget(page, 1);
    auto exportButton = new QPushButton("Export");
    auto exportMenu = new QMenu(exportButton);
    exportMenu->addAction("Terrain PNG", this, [this] { exportImage(false); });
    exportMenu->addAction("Tile-ID PNG", this, [this] { exportImage(true); });
    exportButton->setMenu(exportMenu);
    row->addWidget(exportButton);
    centerLayout->addLayout(row);
    auto tools = new QHBoxLayout;
    tool = new QComboBox;
    tool->setObjectName("mapTool");
    tool->addItems({"Pencil (B)", "Eyedropper (I)", "Pan"});
    QString companion = resource.left(resource.size() - 4) + ".NPC";
    if (project->resources.contains(companion))
        tool->addItem("Inspect NPCs");
    tools->addWidget(tool);
    zoom = new QComboBox;
    zoom->setObjectName("mapZoom");
    zoom->addItems({"Fit", "100%", "200%", "300%", "400%", "800%"});
    tools->addWidget(zoom);
    grid = new QCheckBox("Grid");
    grid->setObjectName("mapGrid");
    tools->addWidget(grid);
    schedule = new QComboBox;
    schedule->setObjectName("mapSchedule");
    schedule->addItems({"Slot 0 · location 0", "Slot 1 · location 1", "Slot 2 · location 2",
                        "Slot 3 · location 1"});
    if (project->resources.contains(companion))
        tools->addWidget(schedule);
    else
        schedule->hide();
    tools->addStretch();
    centerLayout->addLayout(tools);
    canvas = new MapCanvas;
    canvas->tiles = U5::readGraphics("TILES.16", project->data("TILES.16")).images;
    view = new MapView(canvas);
    centerLayout->addWidget(view, 1);
    auto coordinates = new QHBoxLayout;
    coordinate = new QLabel;
    coordinate->setObjectName("mapCoordinates");
    coordinates->addWidget(coordinate, 1);
    auto x = new QSpinBox;
    auto y = new QSpinBox;
    x->setObjectName("mapGoX");
    y->setObjectName("mapGoY");
    coordinates->addWidget(new QLabel("X"));
    coordinates->addWidget(x);
    coordinates->addWidget(new QLabel("Y"));
    coordinates->addWidget(y);
    auto go = new QPushButton("Go");
    coordinates->addWidget(go);
    connect(go, &QPushButton::clicked, this, [=] {
        view->centerMap({double(x->value()) + 0.5, double(y->value()) + 0.5});
        canvas->setFocus();
    });
    centerLayout->addLayout(coordinates);
    auto hints = new QLabel("Left-drag paints · Right-click picks · Middle-drag / Space pans · "
                            "Ctrl+wheel zooms · Esc cancels");
    hints->setWordWrap(true);
    centerLayout->addWidget(hints);
    split->addWidget(center);
    auto right = new QWidget;
    auto rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    brushImage = new QLabel;
    brushImage->setFixedHeight(70);
    brushImage->setAlignment(Qt::AlignCenter);
    rightLayout->addWidget(brushImage);
    brushLabel = new QLabel;
    brushLabel->setObjectName("mapBrushLabel");
    brushLabel->setAlignment(Qt::AlignCenter);
    rightLayout->addWidget(brushLabel);
    auto tileSearch = new QLineEdit;
    tileSearch->setPlaceholderText("Find tile ID (decimal / 0x…)");
    tileSearch->setObjectName("mapTileSearch");
    rightLayout->addWidget(tileSearch);
    palette = new QListWidget;
    palette->setObjectName("mapPalette");
    palette->setViewMode(QListView::IconMode);
    palette->setResizeMode(QListView::Adjust);
    palette->setIconSize({32, 32});
    palette->setGridSize({58, 58});
    for (int i = 0; i < 256; ++i) {
        auto item = new QListWidgetItem(
            QIcon(QPixmap::fromImage(canvas->tiles[i])
                      .scaled(32, 32, Qt::KeepAspectRatio, Qt::FastTransformation)),
            QString::number(i), palette);
        item->setToolTip(QString("Tile %1 (0x%2)").arg(i).arg(i, 2, 16, QChar('0')));
    }
    rightLayout->addWidget(palette, 1);
    right->setMinimumWidth(180);
    split->addWidget(right);
    split->setStretchFactor(1, 1);
    split->setSizes({190, 780, 220});
    connect(tileSearch, &QLineEdit::textChanged, this, [=](const QString &text) {
        auto query = text.trimmed();
        for (int i = 0; i < 256; ++i)
            palette->item(i)->setHidden(
                !query.isEmpty() && !QString::number(i).contains(query) &&
                !QString("0x%1").arg(i, 2, 16, QChar('0')).contains(query, Qt::CaseInsensitive));
    });
    connect(search, &QLineEdit::textChanged, this, [=](const QString &text) {
        std::function<bool(QTreeWidgetItem *, bool)> filter = [&](QTreeWidgetItem *item,
                                                                  bool parentMatch) {
            bool match = parentMatch || item->text(0).contains(text, Qt::CaseInsensitive) ||
                         item->data(0, Qt::UserRole).toString().contains(text, Qt::CaseInsensitive);
            bool child = false;
            for (int i = 0; i < item->childCount(); ++i)
                child = filter(item->child(i), match) || child;
            item->setHidden(!match && !child);
            if (!text.isEmpty() && child)
                item->setExpanded(true);
            return match || child;
        };
        for (int i = 0; i < maps->topLevelItemCount(); ++i)
            filter(maps->topLevelItem(i), false);
    });
    connect(maps, &QTreeWidget::currentItemChanged, this, [=](QTreeWidgetItem *item) {
        if (restoring || !item)
            return;
        auto name = item->data(0, Qt::UserRole).toString();
        if (name.isEmpty())
            return;
        int index = item->data(0, Qt::UserRole + 1).toInt();
        if (name == resource)
            page->setCurrentIndex(index);
        else {
            cancelGesture();
            saveView();
            (*navigation)[name + "/map"] = index;
            navigate(name);
        }
    });
    connect(page, &QComboBox::currentIndexChanged, this, [=] {
        cancelGesture();
        saveView();
        (*navigation)[resource + "/map"] = page->currentIndex();
        loadPage();
        x->setRange(0, canvas->side - 1);
        y->setRange(0, canvas->side - 1);
    });
    connect(tool, &QComboBox::currentIndexChanged, this, [=](int i) {
        cancelGesture();
        canvas->tool = i;
        canvas->setCursor(i == 2 ? Qt::OpenHandCursor : Qt::CrossCursor);
        saveView();
    });
    connect(grid, &QCheckBox::toggled, this, [=](bool on) {
        canvas->grid = on;
        canvas->update();
        saveView();
    });
    connect(schedule, &QComboBox::currentIndexChanged, this, [=] {
        saveView();
        loadPage();
    });
    connect(zoom, &QComboBox::currentIndexChanged, this, [=](int i) {
        if (restoring)
            return;
        if (i == 0)
            view->fitMap();
        else
            view->setZoom(QVector<double>{1, 2, 3, 4, 8}[i - 1]);
    });
    connect(palette, &QListWidget::currentRowChanged, this, [=](int i) {
        if (i >= 0)
            setBrush(i);
    });
    canvas->pick = [=](int id) {
        tileSearch->clear();
        setBrush(id);
        palette->scrollToItem(palette->item(id));
    };
    canvas->chooseTool = [=](int i) { tool->setCurrentIndex(i); };
    canvas->inspect = [=](int cx, int cy, int id) {
        coordinate->setText(QString("X %1   Y %2   Tile %3   ·   %4")
                                .arg(cx)
                                .arg(cy)
                                .arg(id)
                                .arg(tool->currentText()));
    };
    canvas->chooseNpc = [=](int npc) {
        saveView();
        (*navigation)[companion + "/settlement"] = document.pages[page->currentIndex()].settlement;
        (*navigation)[companion + "/npc"] = npc;
        navigate(companion);
    };
    canvas->commit = [=](const QByteArray &ids) {
        try {
            return commit(document.changes(page->currentIndex(), ids),
                          "Paint " + document.pages[page->currentIndex()].name);
        } catch (const std::exception &e) {
            QMessageBox::warning(this, "Map edit rejected", QString::fromUtf8(e.what()));
            return false;
        }
    };
    view->changed = [this] {
        if (!restoring) {
            updateZoom();
            saveView();
        }
    };
    loadPage();
    x->setRange(0, canvas->side - 1);
    y->setRange(0, canvas->side - 1);
}
void MapWorkspace::cancelGesture() { canvas->cancelStroke(); }
void MapWorkspace::setBrush(int id) {
    canvas->brush = qBound(0, id, 255);
    {
        QSignalBlocker block(palette);
        palette->setCurrentRow(canvas->brush);
    }
    brushImage->setPixmap(QPixmap::fromImage(canvas->tiles[canvas->brush])
                              .scaled(64, 64, Qt::KeepAspectRatio, Qt::FastTransformation));
    brushLabel->setText(
        QString("Brush: Tile %1 · 0x%2").arg(canvas->brush).arg(canvas->brush, 2, 16, QChar('0')));
    canvas->update();
    saveView();
}
void MapWorkspace::updateZoom() {
    QSignalBlocker block(zoom);
    int index = view->fitting ? 0 : QVector<double>{1, 2, 3, 4, 8}.indexOf(canvas->zoom) + 1;
    if (index == 0 && !view->fitting) {
        zoom->setCurrentIndex(-1);
        zoom->setPlaceholderText(QString("%1%").arg(qRound(canvas->zoom * 100)));
    } else
        zoom->setCurrentIndex(index);
}
void MapWorkspace::saveView() {
    if (restoring || key.isEmpty())
        return;
    auto &s = (*states)[key];
    s.zoom = canvas->zoom;
    s.fit = view->fitting;
    s.grid = canvas->grid;
    s.brush = canvas->brush;
    s.tool = canvas->tool;
    s.schedule = schedule->currentIndex();
    if (isVisible())
        s.center = view->mapCenter();
}
void MapWorkspace::loadPage() {
    restoring = true;
    key = resource + "/" + QString::number(page->currentIndex());
    auto state = states->value(key);
    const auto &mp = document.pages[page->currentIndex()];
    canvas->side = mp.side;
    canvas->ids = document.terrain(page->currentIndex());
    canvas->zoom = state.zoom;
    canvas->grid = state.grid;
    canvas->tool = qBound(0, state.tool, tool->count() - 1);
    {
        QSignalBlocker block(grid);
        grid->setChecked(state.grid);
    }
    {
        QSignalBlocker block(tool);
        tool->setCurrentIndex(canvas->tool);
    }
    {
        QSignalBlocker block(schedule);
        schedule->setCurrentIndex(state.schedule);
    }
    canvas->actors.clear();
    QString companion = resource.left(resource.size() - 4) + ".NPC";
    if (mp.settlement >= 0 && project->resources.contains(companion) &&
        project->data(companion).size() == 4608) {
        auto npcs = project->data(companion);
        int base = mp.settlement * 576;
        int loc = state.schedule == 0 ? 0 : state.schedule == 2 ? 2 : 1;
        for (int i = 0; i < 32; ++i) {
            int off = base + i * 16;
            int z = U5::byte(npcs, off + 9 + loc);
            if (z >= 128)
                z -= 256;
            int x = U5::byte(npcs, off + 3 + loc), y = U5::byte(npcs, off + 6 + loc);
            if (z == mp.floor && x < 32 && y < 32)
                canvas->actors.append({x, y, 256 + int(U5::byte(npcs, base + 512 + i)), i});
        }
    }
    setBrush(state.brush);
    canvas->setCursor(canvas->tool == 2 ? Qt::OpenHandCursor : Qt::CrossCursor);
    canvas->resizeMap();
    view->fitting = state.fit;
    updateZoom();
    coordinate->setText(QString("%1 · %2 × %2 tiles").arg(mp.name).arg(mp.side));
    {
        QSignalBlocker block(maps);
        QTreeWidgetItemIterator it(maps);
        while (*it) {
            auto item = *it;
            if (item->data(0, Qt::UserRole).toString() == resource &&
                item->data(0, Qt::UserRole + 1).toInt() == page->currentIndex()) {
                maps->setCurrentItem(item);
                maps->scrollToItem(item);
                break;
            }
            ++it;
        }
    }
    QString loadedKey = key;
    QTimer::singleShot(0, this, [this, state, loadedKey] {
        if (key != loadedKey)
            return;
        if (state.fit)
            view->fitMap();
        else if (state.center.x() >= 0)
            view->centerMap(state.center);
        else
            view->centerMap({canvas->side / 2.0, canvas->side / 2.0});
        restoring = false;
        updateZoom();
        saveView();
    });
}
void MapWorkspace::exportImage(bool ids) {
    try {
        auto path = QFileDialog::getSaveFileName(this, ids ? "Export tile IDs" : "Export terrain",
                                                 {}, "PNG (*.png)");
        if (path.isEmpty())
            return;
        require(QFileInfo(path).absolutePath() != project->sourceDirectory,
                "Export images outside the original game folder");
        QImage image(canvas->side * (ids ? 1 : 16), canvas->side * (ids ? 1 : 16),
                     ids ? QImage::Format_Grayscale8 : QImage::Format_RGB32);
        require(!image.isNull(), "Cannot allocate map export");
        if (ids) {
            for (int y = 0; y < canvas->side; ++y)
                memcpy(image.scanLine(y), canvas->ids.constData() + y * canvas->side, canvas->side);
        } else {
            QPainter painter(&image);
            for (int y = 0; y < canvas->side; ++y)
                for (int x = 0; x < canvas->side; ++x)
                    painter.drawImage(x * 16, y * 16,
                                      canvas->tiles[U5::byte(canvas->ids, y * canvas->side + x)]);
        }
        require(image.save(path, "PNG"), "Cannot export PNG");
    } catch (const std::exception &e) {
        QMessageBox::warning(this, "Map export", QString::fromUtf8(e.what()));
    }
}
