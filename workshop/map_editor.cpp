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
QByteArray MapDocument::terrain(int index, bool original) const {
    const auto &p = pages.at(index);
    auto data = original ? project->resources[resource].original : project->data(resource);
    if (p.side == 256)
        return U5::worldMap(
            resource, data,
            resource == "BRIT.DAT"
                ? (original ? project->resources["DATA.OVL"].original : project->data("DATA.OVL"))
                : QByteArray());
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
            if (comparison && original.size() == ids.size() && ids[offset] != original[offset]) {
                p.fillRect(target, QColor(255, 150, 30, 65));
                p.setPen(QColor("#ffae42"));
                p.drawRect(target.adjusted(1, 1, -1, -1));
            }
            if (grid) {
                p.setPen(QColor(255, 255, 255, 45));
                p.drawRect(target.adjusted(0, 0, -1, -1));
            }
        }
    for (auto a : actors)
        if (a.x >= 0 && a.y >= 0 && a.x < side && a.y < side && a.tile >= 0 &&
            a.tile < tiles.size())
            p.drawImage(QRectF(a.x * cell, a.y * cell, cell, cell), tiles[a.tile]);
    if (!selection.isEmpty()) {
        QRectF area(selection.x() * cell, selection.y() * cell, selection.width() * cell,
                    selection.height() * cell);
        p.fillRect(area, QColor(80, 170, 255, 35));
        p.setPen(QPen(QColor("#72caff"), 2, Qt::DashLine));
        p.drawRect(area.adjusted(1, 1, -1, -1));
    }
    if (pasting() && hover.x() >= 0) {
        p.setOpacity(0.6);
        for (int y = qMax(0, int(r.top() / cell) - hover.y());
             y <= qMin(stampSize.height() - 1, int(r.bottom() / cell) - hover.y()); ++y)
            for (int x = qMax(0, int(r.left() / cell) - hover.x());
                 x <= qMin(stampSize.width() - 1, int(r.right() / cell) - hover.x()); ++x) {
                int id = U5::byte(stamp, y * stampSize.width() + x);
                if (id < tiles.size())
                    p.drawImage(QRectF((hover.x() + x) * cell, (hover.y() + y) * cell, cell, cell),
                                tiles[id]);
            }
        p.setOpacity(1);
        p.setPen(QPen(pasteFits() ? QColor("#83e28b") : QColor("#ff6868"), 2));
        p.drawRect(QRectF(hover.x() * cell, hover.y() * cell, stampSize.width() * cell,
                          stampSize.height() * cell)
                       .adjusted(1, 1, -1, -1));
    }
    if (hover.x() >= 0) {
        QRectF target(hover.x() * cell, hover.y() * cell, cell, cell);
        if (!pasting() && tool == 0 && brush >= 0 && brush < tiles.size()) {
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
        if (editable({x, y}))
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
    if (pasting())
        update();
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
    if (pasting())
        say(pasteFits() ? QString("Paste preview: %1 changed cells · click to place · Esc cancels")
                              .arg(pasteChangedCells())
                        : "Paste is outside map bounds; placement rejected");
    if (draw && stroke) {
        if (tool == Rectangle)
            previewRectangle(cell);
        else if (tool == Select) {
            selection = QRect(
                QPoint(qMin(start.x(), cell.x()), qMin(start.y(), cell.y())),
                QSize(std::abs(start.x() - cell.x()) + 1, std::abs(start.y() - cell.y()) + 1));
            update();
            if (selectionChanged)
                selectionChanged();
        } else {
            line(last.x() < 0 ? cell : last, cell);
            last = cell;
        }
    }
}
void MapCanvas::mousePressEvent(QMouseEvent *e) {
    setFocus(Qt::MouseFocusReason);
    auto cell = cellAt(e->position());
    if (cell.x() < 0)
        return;
    if (e->button() == Qt::RightButton ||
        (e->button() == Qt::LeftButton && tool == 1 && !pasting())) {
        if (pick)
            pick(U5::byte(ids, cell.y() * side + cell.x()));
        point(e->position(), false);
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    if (pasting()) {
        pasteAt(cell);
        return;
    }
    if (tool == 3) {
        for (auto a : actors)
            if (a.x == cell.x() && a.y == cell.y()) {
                if (chooseNpc)
                    chooseNpc(a.npc);
                break;
            }
        return;
    }

    if (tool != Pencil && tool != Select && tool != Rectangle && tool != Fill)
        return;
    if (tool != Select && !editable(cell))
        return;
    before = ids;
    previousSelection = selection;
    start = cell;
    operation = tool == Rectangle ? "Rectangle" : tool == Fill ? "Fill" : "Paint";
    if (tool == Fill) {
        flood(cell);
        finishEdit();
        return;
    }
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
    if (tool == Select) {
        before.clear();
        if (selectionChanged)
            selectionChanged();
    } else
        finishEdit();
}
void MapCanvas::cancelStroke() {
    if (stroke) {
        ids = before;
        if (tool == Select)
            selection = previousSelection;
        before.clear();
        stroke = false;
        last = {-1, -1};
        update();
        if (selectionChanged)
            selectionChanged();
    }
    if (pasting()) {
        stamp.clear();
        update();
        say("Paste cancelled");
    }
}
void MapCanvas::say(const QString &message) {
    if (feedback)
        feedback(message);
}
bool MapCanvas::editable(QPoint cell) const {
    return cell.x() >= 0 && cell.y() >= 0 && cell.x() < side && cell.y() < side &&
           (selection.isEmpty() || selection.contains(cell));
}
void MapCanvas::finishEdit() {
    auto rollback = before;
    before.clear();
    stroke = false;
    if (ids == rollback)
        return;
    if (!commit || !commit(ids))
        ids = rollback;
    update();
    if (changed)
        changed();
}
void MapCanvas::previewRectangle(QPoint cell) {
    ids = before;
    auto area =
        QRect(QPoint(qMin(start.x(), cell.x()), qMin(start.y(), cell.y())),
              QSize(std::abs(start.x() - cell.x()) + 1, std::abs(start.y() - cell.y()) + 1));
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x)
            if (editable({x, y}) && (!outline || x == area.left() || x == area.right() ||
                                     y == area.top() || y == area.bottom()))
                ids[y * side + x] = char(brush);
    update();
}
void MapCanvas::flood(QPoint cell) {
    char source = ids[cell.y() * side + cell.x()];
    if (source == char(brush))
        return;
    QVector<QPoint> queue{cell};
    ids[cell.y() * side + cell.x()] = char(brush);
    for (int i = 0; i < queue.size(); ++i) {
        auto current = queue[i];
        for (auto delta : {QPoint(-1, 0), QPoint(1, 0), QPoint(0, -1), QPoint(0, 1)}) {
            auto next = current + delta;
            if (editable(next) && ids[next.y() * side + next.x()] == source) {
                ids[next.y() * side + next.x()] = char(brush);
                queue.append(next);
            }
        }
    }
}
void MapCanvas::clearSelection() {
    selection = {};
    update();
    if (selectionChanged)
        selectionChanged();
}
bool MapCanvas::copySelection() {
    if (selection.isEmpty()) {
        say("Select terrain before copying");
        return false;
    }
    auto area = selection.intersected(QRect(0, 0, side, side));
    if (area != selection || ids.size() != side * side)
        return false;
    QByteArray payload("IMPTILE1", 8);
    for (int value : {area.width(), area.height()}) {
        payload.append(char(value & 255));
        payload.append(char(value >> 8));
    }
    for (int y = area.top(); y <= area.bottom(); ++y)
        payload += ids.mid(y * side + area.left(), area.width());
    auto mime = new QMimeData;
    mime->setData("application/x-impera-terrain", payload);
    QApplication::clipboard()->setMimeData(mime);
    say(QString("Copied %1 × %2 terrain tiles; NPCs and combat records excluded")
            .arg(area.width())
            .arg(area.height()));
    return true;
}
bool MapCanvas::beginPaste() {
    cancelStroke();
    auto mime = QApplication::clipboard()->mimeData();
    auto payload = mime ? mime->data("application/x-impera-terrain") : QByteArray();
    if (payload.size() < 12 || payload.left(8) != "IMPTILE1") {
        say("Clipboard does not contain Impera terrain tiles");
        return false;
    }
    int width = U5::byte(payload, 8) + 256 * U5::byte(payload, 9);
    int height = U5::byte(payload, 10) + 256 * U5::byte(payload, 11);
    if (width < 1 || height < 1 || width > 256 || height > 256 ||
        payload.size() != 12 + width * height) {
        say("Clipboard terrain dimensions are invalid");
        return false;
    }
    stampSize = {width, height};
    stamp = payload.mid(12);
    update();
    say("Move pointer to preview terrain paste · click to place · Esc cancels");
    return true;
}
bool MapCanvas::pasteFits() const {
    return hover.x() >= 0 && hover.y() >= 0 && hover.x() + stampSize.width() <= side &&
           hover.y() + stampSize.height() <= side;
}
int MapCanvas::pasteChangedCells() const {
    if (!pasting() || !pasteFits())
        return 0;
    int count = 0;
    for (int y = 0; y < stampSize.height(); ++y)
        for (int x = 0; x < stampSize.width(); ++x)
            count +=
                ids[(hover.y() + y) * side + hover.x() + x] != stamp[y * stampSize.width() + x];
    return count;
}
void MapCanvas::pasteAt(QPoint cell) {
    hover = cell;
    if (!pasteFits()) {
        say("Paste rejected: outside map bounds");
        return;
    }
    before = ids;
    operation = "Paste terrain";
    for (int y = 0; y < stampSize.height(); ++y)
        ids.replace((cell.y() + y) * side + cell.x(), stampSize.width(),
                    stamp.mid(y * stampSize.width(), stampSize.width()));
    stamp.clear();
    finishEdit();
}

void MapCanvas::leaveEvent(QEvent *) {
    hover = {-1, -1};
    update();
}
void MapCanvas::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) {
        bool pending = stroke || pasting();
        cancelStroke();
        if (!pending)
            clearSelection();
        e->accept();
        return;
    }
    if (e->matches(QKeySequence::Copy)) {
        cancelStroke();
        copySelection();
        e->accept();
        return;
    }
    if (e->matches(QKeySequence::Paste)) {
        beginPaste();
        e->accept();
        return;
    }
    const QMap<int, int> shortcuts{{Qt::Key_B, Pencil},
                                   {Qt::Key_I, Eyedropper},
                                   {Qt::Key_V, Select},
                                   {Qt::Key_R, Rectangle},
                                   {Qt::Key_F, Fill}};
    if (e->modifiers() == Qt::NoModifier && shortcuts.contains(e->key())) {
        cancelStroke();
        if (chooseTool)
            chooseTool(shortcuts[e->key()]);
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

MapMinimap::MapMinimap(MapCanvas *c, MapView *v) : canvas(c), view(v) {
    setObjectName("mapMinimap");
    setMinimumSize(100, 100);
    setMaximumHeight(220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setToolTip("Click or drag to navigate; outline shows the visible map area");
}
QRectF MapMinimap::imageRect() const {
    double edge = qMax(1, qMin(width(), height()) - 8);
    return {(width() - edge) / 2, (height() - edge) / 2, edge, edge};
}
void MapMinimap::refresh() {
    QVector<QImage> thumbnails;
    for (const auto &image : canvas->tiles)
        thumbnails.append(image.scaled(4, 4, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    cache = QImage(canvas->side * 4, canvas->side * 4, QImage::Format_RGB32);
    cache.fill(Qt::black);
    QPainter painter(&cache);
    for (int y = 0; y < canvas->side; ++y)
        for (int x = 0; x < canvas->side; ++x) {
            int offset = y * canvas->side + x;
            if (offset >= canvas->ids.size())
                continue;
            int id = U5::byte(canvas->ids, offset);
            if (id < canvas->tiles.size())
                painter.drawImage(QRect(x * 4, y * 4, 4, 4), thumbnails[id]);
            if (canvas->comparison && canvas->original.size() == canvas->ids.size() &&
                canvas->original[offset] != canvas->ids[offset])
                painter.fillRect(QRect(x * 4, y * 4, 4, 4), QColor("#ffae42"));
        }
    update();
}
void MapMinimap::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    auto area = imageRect();
    painter.fillRect(rect(), QColor("#141b27"));
    painter.drawImage(area, cache);
    double scale = area.width() / canvas->side;
    QPointF top = QPointF(canvas->mapFrom(view->viewport(), QPoint(0, 0))) / (16 * canvas->zoom);
    QSizeF visible = QSizeF(view->viewport()->size()) / (16 * canvas->zoom);
    auto region = QRectF(top, visible).intersected(QRectF(0, 0, canvas->side, canvas->side));
    painter.setPen(QPen(QColor("#ffffff"), 1));
    painter.drawRect(QRectF(area.topLeft() + region.topLeft() * scale, region.size() * scale));
    if (!canvas->selection.isEmpty()) {
        painter.setPen(QPen(QColor("#72caff"), 1));
        painter.drawRect(QRectF(area.topLeft() + QPointF(canvas->selection.topLeft()) * scale,
                                QSizeF(canvas->selection.size()) * scale));
    }
}
void MapMinimap::navigate(QPointF position) {
    auto area = imageRect();
    if (area.contains(position))
        view->centerMap((position - area.topLeft()) * (canvas->side / area.width()));
}
void MapMinimap::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton)
        navigate(event->position());
}
void MapMinimap::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() & Qt::LeftButton)
        navigate(event->position());
}

MapWorkspace::MapWorkspace(
    Project *p, const QString &r, QMap<QString, MapViewState> *s, QMap<QString, int> *n,
    std::function<bool(const QMap<QString, QByteArray> &, const QString &)> commit,
    std::function<void(const QString &)> navigate, MapBrushState *brushState, QWidget *parent)
    : QWidget(parent), project(p), resource(r), document(p, r), states(s), navigation(n),
      brushes(brushState ? brushState : &localBrushes) {
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
    tool->addItems({"Inspect NPCs", "Select (V)", "Rectangle (R)", "Fill (F)"});
    if (!project->resources.contains(companion))
        qobject_cast<QStandardItemModel *>(tool->model())
            ->item(MapCanvas::InspectNpc)
            ->setEnabled(false);
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
    auto terrainTools = new QHBoxLayout;
    auto outline = new QCheckBox("Rectangle outline");
    outline->setObjectName("mapRectangleOutline");
    terrainTools->addWidget(outline);
    auto copy = new QPushButton("Copy");
    copy->setObjectName("mapCopy");
    auto paste = new QPushButton("Paste");
    paste->setObjectName("mapPaste");
    auto clear = new QPushButton("Clear selection");
    clear->setObjectName("mapClearSelection");
    terrainTools->addWidget(copy);
    terrainTools->addWidget(paste);
    terrainTools->addWidget(clear);
    comparison = new QCheckBox("Highlight changes");
    comparison->setObjectName("mapComparison");
    comparison->setToolTip("Orange marks terrain that differs from original game files");
    terrainTools->addWidget(comparison);
    terrainTools->addStretch();
    centerLayout->addLayout(terrainTools);
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
    selectionLabel = new QLabel;
    selectionLabel->setObjectName("mapSelectionInfo");
    selectionLabel->setWordWrap(true);
    rightLayout->addWidget(selectionLabel);
    minimap = new MapMinimap(canvas, view);
    rightLayout->addWidget(minimap);
    auto brushControls = new QHBoxLayout;
    paletteFilter = new QComboBox;
    paletteFilter->setObjectName("mapPaletteFilter");
    paletteFilter->addItems({"All tiles", "Favorites", "Recent"});
    brushControls->addWidget(paletteFilter, 1);
    favorite = new QPushButton("Favorite");
    favorite->setObjectName("mapFavorite");
    favorite->setCheckable(true);
    brushControls->addWidget(favorite);
    rightLayout->addLayout(brushControls);
    tileSearch = new QLineEdit;
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
    connect(tileSearch, &QLineEdit::textChanged, this, [this] { filterPalette(); });
    connect(paletteFilter, &QComboBox::currentIndexChanged, this, [this] { filterPalette(); });
    connect(favorite, &QPushButton::toggled, this, [this](bool enabled) {
        int id = canvas->brush;
        brushes->favorites.removeAll(id);
        if (enabled)
            brushes->favorites.append(id);
        filterPalette();
    });
    connect(outline, &QCheckBox::toggled, this, [this](bool enabled) {
        cancelGesture();
        canvas->outline = enabled;
        saveView();
    });
    connect(copy, &QPushButton::clicked, this, [this] { canvas->copySelection(); });
    connect(paste, &QPushButton::clicked, this, [this] {
        canvas->setFocus();
        canvas->beginPaste();
    });
    connect(clear, &QPushButton::clicked, this, [this] { canvas->clearSelection(); });
    connect(comparison, &QCheckBox::toggled, this, [this](bool enabled) {
        canvas->comparison = enabled;
        canvas->update();
        minimap->refresh();
        saveView();
    });
    canvas->selectionChanged = [this] {
        updateSelection();
        minimap->update();
        saveView();
    };
    canvas->feedback = [this](const QString &message) { coordinate->setText(message); };
    canvas->changed = [this] {
        minimap->refresh();
        updateSelection();
    };
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
        paletteFilter->setCurrentIndex(0);
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
                          canvas->operation + " " + document.pages[page->currentIndex()].name);
        } catch (const std::exception &e) {
            QMessageBox::warning(this, "Map edit rejected", QString::fromUtf8(e.what()));
            return false;
        }
    };
    view->changed = [this] {
        if (!restoring) {
            updateZoom();
            saveView();
            minimap->update();
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
    {
        QSignalBlocker block(favorite);
        favorite->setChecked(brushes->favorites.contains(canvas->brush));
    }
    if (!restoring) {
        brushes->recent.removeAll(canvas->brush);
        brushes->recent.prepend(canvas->brush);
        while (brushes->recent.size() > 16)
            brushes->recent.removeLast();
    }
    filterPalette();
    canvas->update();
    saveView();
}
void MapWorkspace::filterPalette() {
    QString query = tileSearch->text().trimmed();
    for (int i = 0; i < 256; ++i) {
        bool matches =
            query.isEmpty() || QString::number(i).contains(query) ||
            QString("0x%1").arg(i, 2, 16, QChar('0')).contains(query, Qt::CaseInsensitive);
        bool group =
            paletteFilter->currentIndex() == 0 ||
            (paletteFilter->currentIndex() == 1 ? brushes->favorites : brushes->recent).contains(i);
        palette->item(i)->setHidden(!matches || !group);
    }
}
void MapWorkspace::updateSelection() {
    int changed = 0;
    if (canvas->ids.size() == canvas->original.size())
        for (int i = 0; i < canvas->ids.size(); ++i)
            changed += canvas->ids[i] != canvas->original[i];
    auto area = canvas->selection;
    selectionLabel->setText((area.isEmpty() ? QString("No selection")
                                            : QString("Selection: %1 × %2 at %3, %4")
                                                  .arg(area.width())
                                                  .arg(area.height())
                                                  .arg(area.x())
                                                  .arg(area.y())) +
                            QString("\n%1 changed terrain cells").arg(changed));
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
    s.selection = canvas->selection;
    s.comparison = canvas->comparison;
    s.outline = canvas->outline;
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
    canvas->original = document.terrain(page->currentIndex(), true);
    canvas->selection = state.selection.intersected(QRect(0, 0, mp.side, mp.side));
    canvas->comparison = state.comparison;
    canvas->outline = state.outline;
    {
        auto checkbox = findChild<QCheckBox *>("mapRectangleOutline");
        QSignalBlocker block(checkbox);
        checkbox->setChecked(state.outline);
    }
    {
        QSignalBlocker block(comparison);
        comparison->setChecked(state.comparison);
    }
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
    minimap->refresh();
    updateSelection();
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
