#include "extended_maps.h"
#include "mod/npcs.h"
#include "color_palette.h"
#include "window.h"
#include "contents_editor.h"
#include "custom_tiles.h"
#include "cutscene_editor.h"
#include "dialogue_editor.h"
#include "dungeon_editor.h"
#include "engine_tables.h"
#include "font_editor.h"
#include "inventory_names.h"
#include "location_text.h"
#include "map_entities.h"
#include "map_positions.h"
#include "mod/package.h"
#include "mod/tileset_resources.h"
#include "narrative_editor.h"
#include "resource_document.h"
#include "scene_editor.h"
#include "text_preview.h"
#include "world_objects.h"
#include <QCollator>
#include <QMouseEvent>
#include <QPainter>
#include <QtWidgets>
#include <memory>
using U5::require;
namespace {
class Change : public QUndoCommand {
    Project *project;
    QMap<QString, QByteArray> before, after;
    QMap<QString, bool> beforeInclusion, afterInclusion;
    std::function<void()> notify;

  public:
    Change(Project *p, const QMap<QString, QByteArray> &changes, const QString &title,
           std::function<void()> callback, const QMap<QString, bool> &inclusion)
        : project(p), after(changes), afterInclusion(inclusion), notify(callback) {
        setText(title);
        for (auto it = changes.begin(); it != changes.end(); ++it)
            before[it.key()] = p->data(it.key());
        for(auto it=inclusion.begin();it!=inclusion.end();++it)
            beforeInclusion[it.key()]=p->resources[it.key()].included;
    }
    void apply(const QMap<QString, QByteArray> &changes, const QMap<QString, bool> &inclusion) {
        for (auto it = changes.begin(); it != changes.end(); ++it)
            project->resources[it.key()].edited = it.value();
        for(auto it=inclusion.begin();it!=inclusion.end();++it)
            project->resources[it.key()].included=it.value();
        notify();
    }
    void undo() override { apply(before, beforeInclusion); }
    void redo() override { apply(after, afterInclusion); }
};
class QuestionNameChange : public QUndoCommand {
    Project *project;
    QString key, before, after;
    bool existed;
    std::function<void()> notify;

  public:
    QuestionNameChange(Project *p, const QString &k, const QString &value, std::function<void()> fn)
        : project(p), key(k), before(p->dialogueNames.value(k)), after(value),
          existed(p->dialogueNames.contains(k)), notify(fn) {
        setText("Name question");
    }
    void redo() override {
        project->dialogueNames[key] = after;
        notify();
    }
    void undo() override {
        if (existed)
            project->dialogueNames[key] = before;
        else
            project->dialogueNames.remove(key);
        notify();
    }
};
QWidget *panel(QVBoxLayout **layout) {
    auto w = new QWidget;
    *layout = new QVBoxLayout(w);
    (*layout)->setContentsMargins(18, 16, 18, 16);
    (*layout)->setSpacing(12);
    return w;
}
QLabel *hint(const QString &text) {
    auto l = new QLabel(text);
    l->setWordWrap(true);
    l->setStyleSheet("color:#abb6c7;");
    return l;
}
QPushButton *button(QHBoxLayout *row, const QString &text, std::function<void()> action) {
    auto b = new QPushButton(text);
    row->addWidget(b);
    QObject::connect(b, &QPushButton::clicked, b, action);
    return b;
}
QScrollArea *makeScroll(QWidget *widget, bool resize = false) {
    auto area = new QScrollArea;
    area->setWidget(widget);
    area->setWidgetResizable(resize);
    return area;
}
QIcon artworkPreview(const QImage &image) {
    if (image.isNull()) return {};
    QImage preview(48, 48, QImage::Format_ARGB32);
    QPainter painter(&preview);
    for (int y = 0; y < 48; y += 6)
        for (int x = 0; x < 48; x += 6)
            painter.fillRect(x, y, 6, 6, (x / 6 + y / 6) % 2 ? QColor("#384254") : QColor("#222b39"));
    auto scaled = image.scaled(48, 48, Qt::KeepAspectRatio, Qt::FastTransformation);
    painter.drawImage((48 - scaled.width()) / 2, (48 - scaled.height()) / 2, scaled);
    painter.end();
    return QIcon(QPixmap::fromImage(preview));
}
class PixelCanvas : public QWidget {
  public:
    QImage image;
    QRgb brush = qRgb(255, 255, 255);
    QVector<QRgb> allowedColors;
    int zoom = 6;
    QImage before;
    std::function<void(const QImage &)> commit;
    void size() {
        setFixedSize(image.width() * zoom, image.height() * zoom);
        update();
    }

  protected:
    void paintEvent(QPaintEvent *e) override {
        QPainter p(this);
        for (int y = e->rect().top() / 12; y <= e->rect().bottom() / 12; y++)
            for (int x = e->rect().left() / 12; x <= e->rect().right() / 12; x++)
                p.fillRect(x * 12, y * 12, 12, 12,
                           (x + y) % 2 ? QColor("#384254") : QColor("#222b39"));
        p.drawImage(rect(), image);
    }
    void point(QMouseEvent *e) {
        int x = e->position().x() / zoom, y = e->position().y() / zoom;
        if (x < 0 || y < 0 || x >= image.width() || y >= image.height())
            return;
        if (e->buttons() & Qt::RightButton)
            brush = allowedColors.isEmpty() ? image.pixel(x, y)
                                           : Workshop::paletteColor(image.pixel(x, y), allowedColors);
        else if (e->buttons() & Qt::LeftButton) {
            image.setPixel(x, y, brush);
            update();
        }
    }
    void mousePressEvent(QMouseEvent *e) override {
        before = image;
        point(e);
    }
    void mouseMoveEvent(QMouseEvent *e) override { point(e); }
    void mouseReleaseEvent(QMouseEvent *) override {
        if (image != before && commit)
            commit(image);
    }
};
QByteArray readFile(const QString &path) {
    QFile f(path);
    require(f.open(QIODevice::ReadOnly), f.errorString());
    require(f.size() <= MOD_MAX_PACKAGE, "File is too large");
    return f.readAll();
}
void writeFile(const QString &path, const QByteArray &bytes) {
    QSaveFile f(path);
    require(f.open(QIODevice::WriteOnly), f.errorString());
    require(f.write(bytes) == bytes.size() && f.commit(),
            "Cannot write " + path + ": " + f.errorString());
}
} // namespace
WorkshopWindow::WorkshopWindow() {
    setWindowTitle("Impera Workshop");
    auto messages = statusBar();
    messages->hide();
    connect(messages, &QStatusBar::messageChanged, messages,
            [messages](const QString &message) { messages->setVisible(!message.isEmpty()); });
    resize(1320, 860);
    setWindowIcon(QIcon(":/impera.png"));
    const QMap<QString, QString> help{
        {"New Mod",
         "Start a mod using the remembered Ultima 5 game folder, or locate it if unavailable."},
        {"Open Project", "Open an editable Impera Workshop project."},
        {"Save Project", "Save your editable project; original game files are left untouched."},
        {"Name Mod", "Set the name included in the exported mod package."},
        {"Export Package", "Validate and export a mod package for Impera’s Mods folder."},
        {"Import Package", "Import a mod package into an editable Workshop project."},
        {"Change Game Folder", "Locate a different set of original Ultima 5 game files."},
        {"Validate / Package Preview",
         "Check resource formats, warnings, package contents, and installed mod conflicts."},
        {"Test Mod", "Launch Impera with this mod in an isolated copy of the game data."},
        {"Open Last Test Folder", "Open the last isolated runtime to inspect its files and logs."},
        {"Stop Mod Tests", "Terminate Impera test processes launched by Workshop."},
        {"Set Impera Executable",
         "Choose the Impera executable or AppImage used for mod testing."}};
    auto projectMenu = menuBar()->addMenu("Project");
    projectMenu->setToolTipsVisible(true);
    auto action = [&](const QString &label, const QKeySequence &key, std::function<void()> fn) {
        auto a = new QAction(label, this);
        addAction(a); // Preserve shortcuts for commands moved out of the toolbar.
        projectMenu->addAction(a);
        a->setShortcut(key);
        a->setToolTip(help.value(label));
        connect(a, &QAction::triggered, this, fn);
        return a;
    };
    action("New Mod", QKeySequence::New, [this] {
        if (!canLeave())
            return;
        if (restoreGameDirectory())
            return;
        QString p = QFileDialog::getExistingDirectory(this, "Locate Ultima 5 game files");
        if (!p.isEmpty())
            openGame(p);
    });
    auto editMenu = menuBar()->addMenu("Edit");
    editMenu->setToolTipsVisible(true);
    auto toolsMenu = menuBar()->addMenu("Tools");
    toolsMenu->setToolTipsVisible(true);
    auto extra = [this, toolsMenu, help](const QString &label, std::function<void()> fn) {
        auto item = toolsMenu->addAction(label);
        item->setToolTip(help.value(label));
        connect(item, &QAction::triggered, this, fn);
    };
    extra("Change Game Folder", [this] {
        if (!canLeave())
            return;
        QString path = QFileDialog::getExistingDirectory(this, "Locate Ultima 5 game files",
                                                         project.sourceDirectory);
        if (!path.isEmpty())
            openGame(path);
    });
    extra("Validate / Package Preview", [this] { reviewPackage(false); });
    extra("Test Mod", [this] { reviewPackage(true); });
    extra("Open Last Test Folder", [this] {
        const QString path = QSettings().value("paths/testSession").toString();
        if (QDir(path).exists() && !path.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        else
            statusBar()->showMessage("No test session folder is available yet", 6000);
    });
    extra("Stop Mod Tests", [this] {
        for (auto process : findChildren<QProcess *>())
            if (process->state() != QProcess::NotRunning)
                process->terminate();
    });
    extra("Set Impera Executable", [this] {
        QString path = QFileDialog::getOpenFileName(this, "Choose Impera executable or AppImage",
                                                    QSettings().value("paths/engine").toString());
        if (!path.isEmpty())
            QSettings().setValue("paths/engine", path);
    });
    action("Open Project", QKeySequence::Open, [this] {
        if (!canLeave())
            return;
        QString p = QFileDialog::getOpenFileName(this, "Open mod project", {},
                                                 "Impera project (*.imperaproject)");
        if (!p.isEmpty())
            openProject(p);
    });
    action("Save Project", QKeySequence::Save, [this] { saveProject(); });
    auto undo = history.createUndoAction(this, "Undo");
    undo->setShortcut(QKeySequence::Undo);
    undo->setToolTip("Undo the last committed edit.");
    editMenu->addAction(undo);
    addAction(undo);
    QObject::disconnect(undo, nullptr, &history, nullptr);
    connect(undo, &QAction::triggered, this, [this] {
        if (flushDraft && !flushDraft())
            return;
        history.undo();
    });
    auto redo = history.createRedoAction(this, "Redo");
    redo->setShortcut(QKeySequence::Redo);
    redo->setToolTip("Reapply the last undone edit.");
    editMenu->addAction(redo);
    addAction(redo);
    QObject::disconnect(redo, nullptr, &history, nullptr);
    connect(redo, &QAction::triggered, this, [this] {
        if (flushDraft && !flushDraft())
            return;
        history.redo();
    });
    action("Name Mod", {}, [this] {
        if (project.resources.isEmpty())
            return;
        bool ok;
        QString title = QInputDialog::getText(
            this, "Mod name", "Name shown in Impera's log:", QLineEdit::Normal, project.title, &ok);
        if (ok) {
            project.title = title;
            updateSummary();
        }
    });
    action("Export Package", QKeySequence(Qt::CTRL | Qt::Key_E), [this] {
        if (flushDraft && !flushDraft())
            return;
        guard([&] {
            if (!reviewPackage(false))
                return;
            auto bytes = project.package();
            QString path = QFileDialog::getSaveFileName(this, "Export mod package", {},
                                                        "Impera mod (*.imperamod)");
            if (path.isEmpty())
                return;
            if (!path.endsWith(".imperamod", Qt::CaseInsensitive))
                path += ".imperamod";
            require(QFileInfo(path).absolutePath() != project.sourceDirectory,
                    "Place mod packages in the Mods subfolder, or another "
                    "distribution folder");
            writeFile(path, bytes);
            statusBar()->showMessage(
                "Exported " + path + " — place it in Mods beside Impera, then restart Impera", 12000);
        });
    });
    action("Import Package", {}, [this] {
        if (flushDraft && !flushDraft())
            return;
        if (project.resources.isEmpty())
            return;
        QString path = QFileDialog::getOpenFileName(
            this, "Import a package against this game's original files", {},
            "Impera mod (*.imperamod)");
        if (path.isEmpty())
            return;
        guard([&] {
            Project next = project;
            next.importPackage(readFile(path));
            QMap<QString, QByteArray> changes;
            for (auto name : next.changed())
                changes[name] = next.data(name);
            edit(changes, "Import package");
            project.title = next.title;
            updateSummary();
        });
    });
    auto central = new QWidget;
    auto layout = new QVBoxLayout(central);
    auto split = new QSplitter;
    auto library = new QWidget;
    auto lv = new QVBoxLayout(library);
    auto search = new QLineEdit;
    contentSearch = search;
    search->setPlaceholderText("Find content…");
    workspaceSelector = new QComboBox;
    workspaceSelector->setObjectName("contentWorkspace");
    workspaceSelector->addItems(Workshop::contentWorkspaces());
    workspaceSelector->setToolTip(
        "Choose the kind of game content to edit. Navigation stays available in every editor.");
    lv->addWidget(workspaceSelector);
    characterLocation = new QComboBox;
    characterLocation->setObjectName("characterLocationFilter");
    characterLocation->setToolTip(
        "Filter dialogue by location, or choose Shops & Services for shared service dialogue.");
    lv->addWidget(characterLocation);
    connect(characterLocation, &QComboBox::currentIndexChanged, this, [this] { rebuildTree(); });
    connect(workspaceSelector, &QComboBox::currentTextChanged, this, [this](const QString &workspace) {
        rebuildTree();
        if (workspace == "Mod Overview" && current != "@overview") {
            navigateContent("overview");
            if (current != "@overview") {
                // Invalid drafts retain both editor and navigation context.
                for (const auto &d : destinations)
                    if (d.resource == current && Workshop::matchesSelection(d, editorState)) {
                        QSignalBlocker block(workspaceSelector);
                        workspaceSelector->setCurrentText(d.workspace);
                        break;
                    }
                rebuildTree();
            }
        }
    });
    lv->addWidget(search);
    assets = new QTreeWidget;
    assets->setHeaderHidden(true);
    assets->setMinimumWidth(230);
    lv->addWidget(assets);
    split->addWidget(library);
    auto showResources = new QAction("Content", this);
    showResources->setObjectName("showResourceLibrary");
    showResources->setShortcut(QKeySequence("Ctrl+L"));
    showResources->setToolTip("Focus content navigation (Ctrl+L). Your current edit remains open.");
    addAction(showResources);
    toolsMenu->addAction(showResources);
    showResources->setText("Find Content");
    connect(showResources, &QAction::triggered, this, [library, search] {
        library->show();
        search->setFocus();
        search->selectAll();
    });
    library->setObjectName("resourceLibrary");
    content = new QStackedWidget;
    content->setObjectName("workspaces");
    split->addWidget(content);
    split->setStretchFactor(1, 1);
    layout->addWidget(split, 1);
    setCentralWidget(central);
    connect(search, &QLineEdit::textChanged, this, [this] { rebuildTree(); });
    connect(assets, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        if (item)
            navigateContent(item->data(0, Qt::UserRole + 2).toString());
    });
    editMenu->addSeparator();
    backAction = editMenu->addAction("Back");
    forwardAction = editMenu->addAction("Forward");
    addAction(backAction);
    addAction(forwardAction);
    backAction->setShortcut(QKeySequence("Alt+Left"));
    forwardAction->setShortcut(QKeySequence("Alt+Right"));
    backAction->setToolTip(
        "Return to the previous content destination (Alt+Left). Does not undo edits.");
    forwardAction->setToolTip("Revisit the next content destination (Alt+Right).");
    auto travel = [this](int offset) {
        const int target = navigationIndex + offset;
        if (target < 0 || target >= navigationHistory.size())
            return;
        if (flushDraft && !flushDraft())
            return;
        prepareNavigation();
        if (navigationIndex >= 0)
            navigationStates[navigationIndex] = editorState;
        editorState = navigationStates[target];
        navigationIndex = target;
        replayNavigation = true;
        navigateContent(navigationHistory[target]);
        replayNavigation = false;
        syncNavigation();
    };
    connect(backAction, &QAction::triggered, this, [travel] { travel(-1); });
    connect(forwardAction, &QAction::triggered, this, [travel] { travel(1); });
    syncNavigation();
    connect(&history, &QUndoStack::cleanChanged, this, [this] { updateSummary(); });
    QVBoxLayout *welcome;
    auto w = panel(&welcome);
    auto title = new QLabel("Build your Britannia");
    title->setStyleSheet("font-size:30px;font-weight:600;");
    welcome->addWidget(title);
    welcome->addWidget(hint("Create a mod from your DOS Ultima 5 folder. Edit maps, artwork, "
                            "conversations, "
                            "story and starting state in one workspace. Original game files are "
                            "read-only; "
                            "Save stores a project, Export creates a mod package."));
    welcome->addWidget(hint("Impera loads .imperamod files from the application "
                            "folder's Mods subfolder at startup. Existing saved "
                            "games keep their saved state; starting-state edits "
                            "apply to new games."));
    welcome->addStretch();
    content->addWidget(w);
    updateSummary();
}
bool WorkshopWindow::guard(const std::function<void()> &operation) {
    try {
        operation();
        return true;
    } catch (const std::exception &e) {
        QMessageBox::warning(this, "Cannot complete edit", QString::fromUtf8(e.what()));
        return false;
    }
}
void WorkshopWindow::updateSummary() {
    setWindowModified(!history.isClean() || project.title != savedTitle);
    setWindowTitle("Impera Workshop — " + project.title + "[*]");
    if (current == "@overview" && !rebuildPending) {
        rebuildPending = true;
        QTimer::singleShot(0, this, [this] {
            rebuildPending = false;
            if (current == "@overview") rebuildEditor();
        });
    }
}
bool WorkshopWindow::canLeave() {
    if (flushDraft && !flushDraft())
        return false;
    if (project.resources.isEmpty() || !isWindowModified())
        return true;
    auto answer =
        QMessageBox::question(this, "Unsaved project", "Save your project before continuing?",
                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel)
        return false;
    return answer != QMessageBox::Save || saveProject();
}
void WorkshopWindow::closeEvent(QCloseEvent *event) {
    if (canLeave())
        event->accept();
    else
        event->ignore();
}
bool WorkshopWindow::saveProject() {
    if (flushDraft && !flushDraft())
        return false;
    if (project.resources.isEmpty())
        return false;
    QString path = project.projectPath;
    if (path.isEmpty())
        path = QFileDialog::getSaveFileName(this, "Save mod project", {},
                                            "Impera project (*.imperaproject)");
    if (path.isEmpty())
        return false;
    if (!path.endsWith(".imperaproject"))
        path += ".imperaproject";
    return guard([&] {
        project.save(path);
        history.setClean();
        savedTitle = project.title;
        updateSummary();
        statusBar()->showMessage("Project saved; original game files were not changed", 6000);
    });
}
bool WorkshopWindow::restoreGameDirectory() {
    const QString remembered = QSettings().value("paths/game").toString();
    if (remembered.isEmpty())
        return false;
    try {
        Project probe;
        probe.openGame(remembered);
        return openGame(remembered);
    } catch (const std::exception &) {
        // Stale preferences must not prevent opening the welcome screen.
        return false;
    }
}
bool WorkshopWindow::openGame(const QString &path) {
    return guard([&] {
        project.openGame(path);
        QSettings().setValue("paths/game", project.sourceDirectory);
        history.clear();
        navigationHistory.clear();
        navigationStates.clear();
        navigationIndex = -1;
        syncNavigation();
        savedTitle = project.title;
        current.clear();
        editorState.clear();
        mapViewStates.clear();
        lastMap.clear();
        flushDraft = {};
        rebuildTree();
        openInitialMap();
        updateSummary();
    });
}
bool WorkshopWindow::openProject(const QString &path) {
    return guard([&] {
        project.load(path);
        QSettings().setValue("paths/game", project.sourceDirectory);
        history.clear();
        navigationHistory.clear();
        navigationStates.clear();
        navigationIndex = -1;
        syncNavigation();
        savedTitle = project.title;
        current.clear();
        editorState.clear();
        mapViewStates.clear();
        lastMap.clear();
        flushDraft = {};
        rebuildTree();
        openInitialMap();
        updateSummary();
    });
}
void WorkshopWindow::openInitialMap() {
    if (project.resources.contains("BRIT.DAT") && project.resources.contains("DATA.OVL")) {
        // Location 17 is Lord British's Castle in the engine's entrance table.
        // Use the same defaults and mod overrides as the location editor.
        const QByteArray rules = project.resources.contains("RULES.TBL")
                                     ? project.data("RULES.TBL") : QByteArray();
        QPointF center;
        try {
            center = {Workshop::TableDocument::value(rules, 7, 16) + 0.5,
                      Workshop::TableDocument::value(rules, 8, 16) + 0.5};
        } catch (...) {
            center = {Workshop::TableDocument::value({}, 7, 16) + 0.5,
                      Workshop::TableDocument::value({}, 8, 16) + 0.5};
        }
        mapViewStates["BRIT.DAT/0"].center = center;
        selectResource("BRIT.DAT");
    } else {
        // Partial projects used for recovery may not contain an overworld.
        selectResource("TILES.16");
    }
}
void WorkshopWindow::prepareNavigation() {
    if (current.isEmpty()) return; // A new project must not inherit the previous map camera.
    auto widget = content->currentWidget();
    if (widget) if (auto map = dynamic_cast<MapWorkspace *>(widget->findChild<QWidget *>("mapWorkspace")))
        map->prepareNavigation();
}
void WorkshopWindow::syncNavigation() {
    if (!backAction)
        return;
    backAction->setEnabled(navigationIndex > 0);
    forwardAction->setEnabled(navigationIndex + 1 < navigationHistory.size());
}
void WorkshopWindow::navigateContent(const QString &id) {
    if (id.isEmpty())
        return;
    auto navigationTargets = destinations;
    if (id.startsWith("character/") &&
        std::none_of(navigationTargets.cbegin(), navigationTargets.cend(),
                     [&](const auto &d) { return d.id == id; }))
        navigationTargets += Workshop::characterDestinations(project);
    for (const auto &destination : navigationTargets) {
        if (destination.id != id)
            continue;
        const auto d = destination;
        if (flushDraft && !flushDraft()) {
            rebuildTree();
            return;
        }
        prepareNavigation();
        if (!replayNavigation && navigationIndex >= 0)
            navigationStates[navigationIndex] = editorState;
        if (d.workspace == "Dialogue" && d.resource != "SHOPPE.DAT") {
            if (!replayNavigation && d.character >= 0 && d.resource != current &&
                current.left(current.size() - 4) == d.resource.left(d.resource.size() - 4) &&
                editorState.value(current + "/character", -1) == d.character)
                editorState[d.resource + "/characterTab"] =
                    editorState.value(current + "/characterTab");
            editorState[d.resource + "/indexedCharacter"] = 1;
            editorState[d.resource + "/character"] = d.character;
            if (d.conversation >= 0) {
                editorState[d.resource + "/conversation"] = d.conversation;
                if (!replayNavigation)
                    editorState[d.resource + "/entry"] = 0;
            }
        }
        if (!replayNavigation && d.page >= 0)
            editorState[d.resource + "/map"] = d.page;
        for (auto it = d.selection.begin(); it != d.selection.end(); ++it)
            editorState[it.key()] = it.value();
        selectResource(d.resource);
        if (!replayNavigation &&
            (navigationIndex < 0 || navigationHistory.value(navigationIndex) != id)) {
            navigationHistory = navigationHistory.mid(0, navigationIndex + 1);
            navigationHistory.append(id);
            navigationStates.resize(navigationIndex + 1);
            navigationStates.append(editorState);
            navigationIndex = navigationHistory.size() - 1;
        }
        syncNavigation();
        return;
    }
}
void WorkshopWindow::rebuildTree() {
    Workshop::hydrateExtendedMaps(project);
    QSignalBlocker block(assets);
    const bool sameWorkspace = assets->property("workspace").toString() == workspaceSelector->currentText();
    const int oldScroll = assets->verticalScrollBar()->value();
    QMap<QString, bool> expanded;
    for (int i = 0; i < assets->topLevelItemCount(); ++i) {
        auto group = assets->topLevelItem(i);
        expanded[group->text(0).section(" *", 0, 0)] = group->isExpanded();
    }
    assets->clear();
    assets->setProperty("workspace", workspaceSelector->currentText());
    destinations = Workshop::contentDestinations(project);
    QMap<QString, QTreeWidgetItem *> groups;
    const bool characters = workspaceSelector->currentText() == "Dialogue";
    characterLocation->setVisible(characters);
    const QString selectedLocation = characterLocation->currentData().toString();
    {
        QSignalBlocker filterBlock(characterLocation);
        characterLocation->clear();
        characterLocation->addItem("All NPCs & services", QString());
        QSet<QString> locations;
        for (const auto &d : destinations)
            if (d.workspace == "Dialogue")
                locations.insert(d.group);
        QStringList sorted = locations.values();
        sorted.sort();
        for (const auto &location : sorted)
            characterLocation->addItem(location, location);
        characterLocation->setCurrentIndex(qMax(0, characterLocation->findData(selectedLocation)));
    }
    const auto query = contentSearch->text();
    if (workspaceSelector->currentText() == "World") {
        for (const auto &label :
             {"World maps", "Locations", "Dungeons", "Combat maps"}) {
            groups[label] = new QTreeWidgetItem(assets, {label});
            groups[label]->setExpanded(QString(label) != "Combat maps");
        }
    }
    if (workspaceSelector->currentText() == "Graphics") {
        for (const QString &label :
             {QString("Tilesets"), QString("Custom tiles"), QString("Illustrations"), QString("Fonts")}) {
            const bool present = std::any_of(destinations.cbegin(), destinations.cend(),
                                            [&](const auto &d) {
                                                return d.workspace == "Graphics" && d.group == label;
                                            });
            if (present) {
                groups[label] = new QTreeWidgetItem(assets, {label});
                groups[label]->setExpanded(true);
            }
        }
    }
    const auto changes = Workshop::contentChanges(project, destinations);
    auto displayed = destinations;
    if (workspaceSelector->currentText() == "World" || characters) {
        QCollator alphabetical{QLocale{QLocale::English}};
        alphabetical.setCaseSensitivity(Qt::CaseInsensitive);
        alphabetical.setNumericMode(true);
        std::stable_sort(displayed.begin(), displayed.end(), [&](const auto &a, const auto &b) {
            int group = alphabetical.compare(a.group, b.group);
            return group ? group < 0 : alphabetical.compare(a.title, b.title) < 0;
        });
    }
    for (const auto &d : displayed) {
        if (d.workspace != workspaceSelector->currentText())
            continue;
        if (d.workspace == "Mod Overview" && d.id != "overview" &&
            !property("overviewAdvanced").toBool())
            continue;
        if (characters &&
            !characterLocation->currentData().toString().isEmpty() &&
            d.group != characterLocation->currentData().toString())
            continue;
        if (!query.isEmpty() && !d.title.contains(query, Qt::CaseInsensitive) &&
            !d.group.contains(query, Qt::CaseInsensitive))
            continue;
        if (!groups.contains(d.group)) {
            groups[d.group] = new QTreeWidgetItem(assets, {d.group});
            groups[d.group]->setExpanded(true);
        }
        const auto details = changes.value(d.id);
        auto item = new QTreeWidgetItem(groups[d.group],
            {d.title + (details.isEmpty() ? "" : " *")});
        item->setData(0, Qt::UserRole, d.resource);
        item->setData(0, Qt::UserRole + 2, d.id);
        item->setToolTip(
            0, d.provenance + (details.isEmpty() ? QString() : "\nChanged: " + details.join(", ")) + "\n" + Workshop::capability(d.resource).status +
                   "\nSource: " + (d.sources.isEmpty() ? d.resource : d.sources.join(" / ")) +
                   (d.scheduleResource.isEmpty() ? QString() : " / " + d.scheduleResource));
        if (d.resource == current) {
            const int selected = editorState.value(current + "/map");
            bool matches = (d.page < 0 || selected == d.page) && Workshop::matchesSelection(d, editorState);
            if (d.workspace == "Dialogue" && (d.character >= 0 || d.conversation >= 0))
                matches = editorState.value(current + "/character", -1) == d.character &&
                          (d.conversation < 0 ||
                           editorState.value(current + "/conversation") == d.conversation);
            if (!matches && d.page >= 0) {
                const auto pages = project.mapPages(current);
                matches = selected >= 0 && selected < pages.size() &&
                          pages[d.page].settlement >= 0 &&
                          pages[d.page].settlement == pages[selected].settlement;
            }
            if (matches) {
                assets->setCurrentItem(item);
                if (d.workspace == "Dialogue" && content->currentWidget())
                    if (auto title = content->currentWidget()->findChild<QLabel *>("characterTitle"))
                        title->setText(d.title + " — " + d.group);
            }
        }
    }
    for (auto group : groups) {
        int changedCount = 0;
        for (int i = 0; i < group->childCount(); ++i)
            changedCount += !changes.value(group->child(i)->data(0, Qt::UserRole + 2).toString()).isEmpty();
        if (changedCount) group->setText(0, group->text(0) + QString(" * (%1)").arg(changedCount));
        group->setHidden(group->childCount() == 0);
    }
    if (sameWorkspace) {
        for (auto group : groups) {
            const auto key = group->text(0).section(" *", 0, 0);
            if (expanded.contains(key)) group->setExpanded(expanded[key]);
        }
        assets->doItemsLayout();
        assets->verticalScrollBar()->setValue(oldScroll);
    }
}
void WorkshopWindow::edit(const QMap<QString, QByteArray> &changes, const QString &description,
                          const QMap<QString, bool> &inclusion) {
    QMap<QString, QByteArray> actual;
    for (auto it = changes.begin(); it != changes.end(); ++it)
        if (it.value() != project.resources[it.key()].edited)
            actual.insert(it.key(), it.value());
    bool inclusionChanged=false;
    for(auto it=inclusion.begin();it!=inclusion.end();++it)
        inclusionChanged |= project.resources[it.key()].included != it.value();
    if (actual.isEmpty() && !inclusionChanged)
        return;
    history.push(new Change(&project, actual, description, [this, first = true]() mutable {
        updateSummary();
        rebuildTree();
        if (first) {
            first = false;
            return;
        }
        if (!rebuildPending) {
            rebuildPending = true;
            QTimer::singleShot(0, this, [this] {
                rebuildPending = false;
                rebuildEditor();
            });
        }
    }, inclusion));
}
void WorkshopWindow::selectResource(const QString &name) {
    if (name == "DATA.OVL") {
        if (project.resources.contains("BRIT.DAT")) selectResource("BRIT.DAT");
        else navigateContent("overview");
        return;
    }
    if (flushDraft && !flushDraft()) {
        rebuildTree();
        return;
    }
    flushDraft = {};
    prepareNavigation();
    if (!replayNavigation && navigationIndex >= 0 && current != name)
        navigationStates[navigationIndex] = editorState;
    current = name;
    destinations = Workshop::contentDestinations(project);
    for (const auto &d : destinations)
        if ((d.resource == name || d.sources.contains(name)) &&
            Workshop::matchesSelection(d, editorState)) {
            QSignalBlocker block(workspaceSelector);
            workspaceSelector->setCurrentText(d.workspace);
            break;
        }
    // Diagnostic links can still open companion resources hidden from normal navigation.
    if (name == "QUESTION.DAT") {
        QSignalBlocker block(workspaceSelector);
        workspaceSelector->setCurrentText("Game Rules");
    } else if (name == "MISCMSG.DAT" || name == "ENDMSG.DAT") {
        QSignalBlocker block(workspaceSelector);
        workspaceSelector->setCurrentText("Cutscenes");
    }
    rebuildTree();
    if (name != "DUNGEON.CBT")
        dungeonRoomNavigation = false;
    if (MapDocument::supported(name))
        lastMap = name;
    rebuildEditor();
    if (!replayNavigation)
        for (const auto &d : destinations) {
            if (d.resource != current || !Workshop::matchesSelection(d, editorState))
                continue;
            if (d.workspace == "Dialogue" && (d.character >= 0 || d.conversation >= 0) &&
                (editorState.value(current + "/character", -1) != d.character ||
                 (d.conversation >= 0 &&
                  editorState.value(current + "/conversation") != d.conversation)))
                continue;
            if (d.page >= 0 && d.page != editorState.value(current + "/map")) {
                const auto pages = project.mapPages(current);
                const int selected = editorState.value(current + "/map");
                if (selected >= pages.size() || pages[d.page].settlement < 0 ||
                    pages[d.page].settlement != pages[selected].settlement)
                    continue;
            }
            if (navigationHistory.value(navigationIndex) != d.id) {
                navigationHistory = navigationHistory.mid(0, navigationIndex + 1);
                navigationHistory.append(d.id);
                navigationStates.resize(navigationIndex + 1);
                navigationStates.append(editorState);
                navigationIndex = navigationHistory.size() - 1;
            }
            break;
        }
    syncNavigation();
}
void WorkshopWindow::rebuildEditor() {
    if (auto id = MAPS_LocationResource(current.toLatin1().constData())) {
        bool found = false;
        for (const auto &m : Workshop::extendedMaps(project))
            found |= m.id == id && m.kind == 1;
        if (!found) {
            current = editorState.value(current + "/entranceWorld", 0) == -1 &&
                              project.resources.contains("UNDER.DAT")
                          ? "UNDER.DAT"
                          : "BRIT.DAT";
            lastMap = current;
            rebuildTree();
        }
    }
    if (current.isEmpty())
        return;
    // A conversation-link edit (or undo) can move a character between a
    // dialogue resource and the schedule-only view. Resolve identity, not the
    // previous file.
    const int character = editorState.value(current + "/character", -1);
    if (character >= 0 && (current.endsWith(".TLK") || current.endsWith(".NPC"))) {
        const QString stem = current.left(current.size() - 4);
        for (const auto &d : Workshop::characterDestinations(project))
            if (d.character == character && d.scheduleResource == stem + ".NPC" &&
                d.resource != current) {
                const int tab = editorState.value(current + "/characterTab");
                current = d.resource;
                editorState[current + "/character"] = character;
                editorState[current + "/indexedCharacter"] = 1;
                editorState[current + "/characterTab"] = tab;
                if (d.conversation >= 0)
                    editorState[current + "/conversation"] = d.conversation;
                rebuildTree();
                break;
            }
    }
    flushDraft = {};
    QPoint scrollPosition;
    if (auto previous = content->currentWidget())
        if (auto split = previous->findChild<QSplitter *>("mapPanelSplitter")) {
            mapPanelState = split->saveState();
            mapPaletteWidth = split->sizes().last();
        }
    auto oldView = content->currentWidget()
                       ? content->currentWidget()->findChild<QScrollArea *>("canvasView")
                       : nullptr;
    if (oldView)
        scrollPosition =
            QPoint(oldView->horizontalScrollBar()->value(), oldView->verticalScrollBar()->value());
    QWidget *w = nullptr;
    try {
        w = resourceEditor(current);
    } catch (const std::exception &e) {
        QVBoxLayout *l;
        w = panel(&l);
        l->addWidget(hint(QString::fromUtf8(e.what())));
        l->addWidget(hint("This resource cannot be opened by its supported editor. Use Resources "
                          "to choose another file; original game data has not been changed."));
        l->addStretch();
    }
    while (content->count()) {
        auto old = content->widget(0);
        content->removeWidget(old);
        old->deleteLater();
    }
    content->addWidget(w);
    content->setCurrentWidget(w);
    if (auto map = dynamic_cast<MapWorkspace *>(w->findChild<QWidget *>("mapWorkspace")))
        map->useSharedNavigation();
    if (auto split = w->findChild<QSplitter *>("mapPanelSplitter")) {
        if (!mapPanelState.isEmpty()) {
            split->restoreState(mapPanelState);
            // Native layouts settle after showing the workspace. Preserve a pixel width,
            // rather than letting the new layout rescale the serialized splitter ratios.
            const int width = mapPaletteWidth;
            QTimer::singleShot(0, w, [w, split, width] {
                if (w->layout())
                    w->layout()->activate();
                if (auto map = split->parentWidget(); map && map->layout())
                    map->layout()->activate();
                const auto sizes = split->sizes();
                int available = 0;
                for (int size : sizes)
                    available += size;
                if (sizes.size() == 3 && width > 0)
                    split->setSizes({sizes[0], qMax(0, available - sizes[0] - width), width});
            });
        }
    }
    if (auto scenes = w->findChild<QComboBox *>("sceneSelector"))
        connect(scenes, &QComboBox::currentIndexChanged, w, [this] {
            for (const auto &d : destinations)
                if (d.resource == current && Workshop::matchesSelection(d, editorState)) {
                    QSignalBlocker block(workspaceSelector);
                    workspaceSelector->setCurrentText(d.workspace);
                    break;
                }
            rebuildTree();
        });
    if (oldView && !w->findChild<QWidget *>("mapWorkspace"))
        QTimer::singleShot(0, w, [w, scrollPosition] {
            auto view = w->findChild<QScrollArea *>("canvasView");
            if (view) {
                view->horizontalScrollBar()->setValue(scrollPosition.x());
                view->verticalScrollBar()->setValue(scrollPosition.y());
            }
        });
}
QWidget *WorkshopWindow::overviewEditor() {
    auto root = new QWidget;
    root->setObjectName("modOverview");
    auto layout = new QVBoxLayout(root);
    auto title = new QLabel("Mod Overview — " + project.title);
    title->setObjectName("overviewTitle");
    title->setStyleSheet("font-size:22px;font-weight:600;");
    auto heading = new QHBoxLayout;
    heading->addWidget(title, 1);
    auto advanced = new QToolButton;
    advanced->setObjectName("overviewAdvanced");
    advanced->setText("Advanced");
    advanced->setCheckable(true);
    advanced->setToolTip("Show package files and technical compatibility diagnostics.");
    heading->addWidget(advanced);
    layout->addLayout(heading);
    const auto routes = Workshop::contentDestinations(project);
    const auto changes = Workshop::contentChanges(project, routes);
    int changedCount = 0;
    for (auto it = changes.begin(); it != changes.end(); ++it)
        changedCount += !it.value().isEmpty();
    auto summary = new QLabel(
        QString("%1 changed items · %2 files in the mod\n%3")
            .arg(changedCount)
            .arg(project.changed().size())
            .arg(
                isWindowModified()
                    ? "Project has unsaved changes."
                    : project.projectPath.isEmpty()
                        ? "Project has not been saved yet."
                        : "All changes saved."));
    summary->setObjectName("overviewSummary");
    summary->setWordWrap(true);
    summary->setToolTip("Destinations can share data. The package replaces whole changed "
                        "resources, not just the listed locations or characters.");
    layout->addWidget(summary);
    auto tabs = new QTabWidget;
    tabs->setObjectName("overviewTabs");
    auto diagnostics = new QWidget(root);
    diagnostics->setObjectName("overviewAdvancedPanel");
    auto diagnosticsLayout = new QVBoxLayout(diagnostics);
    auto diagnosticTabs = new QTabWidget;
    diagnosticsLayout->addWidget(diagnosticTabs);
    auto changed = new QTreeWidget;
    changed->setObjectName("overviewChanges");
    changed->setHeaderLabels({"Content", "Workspace", "Changes"});
    changed->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    changed->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    for (const auto &d : routes) {
        if (changes.value(d.id).isEmpty())
            continue;
        auto item = new QTreeWidgetItem(changed, {d.title, d.workspace, changes[d.id].join(", ")});
        item->setData(0, Qt::UserRole, d.id);
        item->setToolTip(
            0,
            "Open this content editor. Shared conversations may affect more than one character.");
    }
    changed->setToolTip("Double-click changed content to open its editor. Empty means no "
                        "identifiable content changes.");
    connect(changed, &QTreeWidget::itemDoubleClicked, root, [this](QTreeWidgetItem *item) {
        navigateContent(item->data(0, Qt::UserRole).toString());
    });
    tabs->addTab(changed, "Changed content");
    auto resources = new QTreeWidget;
    resources->setObjectName("overviewPackageResources");
    resources->setHeaderLabels({"Package resource", "Size", "Reason"});
    resources->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    for (const auto &name : project.changed()) {
        const auto &r = project.resources[name];
        auto item =
            new QTreeWidgetItem(resources, {name, QString::number(r.edited.size()) + " bytes",
                                            r.included && r.edited == r.original
                                                ? "Explicit override of inherited defaults"
                                                : "Changed content; entire resource is replaced"});
        item->setData(0, Qt::UserRole, name);
        item->setToolTip(0,
                         Workshop::capability(name).title +
                             " — double-click to open the source editor or compatibility details.");
    }
    connect(resources, &QTreeWidget::itemDoubleClicked, root, [this](QTreeWidgetItem *item) {
        selectResource(item->data(0, Qt::UserRole).toString());
    });
    diagnosticTabs->addTab(resources, "Package contents");
    auto compatibility = new QTreeWidget;
    compatibility->setObjectName("overviewCompatibilityDetails");
    compatibility->setHeaderLabels({"Content", "Status"});
    compatibility->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (const auto &d : routes) {
        if (d.workspace != "Mod Overview" || d.id == "overview")
            continue;
        auto item = new QTreeWidgetItem(
            compatibility, {d.title, Workshop::capability(d.resource).status});
        item->setData(0, Qt::UserRole, d.id);
    }
    connect(compatibility, &QTreeWidget::itemDoubleClicked, root,
            [this](QTreeWidgetItem *item) {
                navigateContent(item->data(0, Qt::UserRole).toString());
            });
    diagnosticTabs->addTab(compatibility, "Compatibility details");
    auto validation = new QWidget;
    auto vl = new QVBoxLayout(validation);
    auto status = new QLabel("Not checked");
    status->setObjectName("overviewValidationStatus");
    vl->addWidget(status);
    auto report = new QTreeWidget;
    report->setObjectName("overviewDiagnostics");
    report->setHeaderLabels({"Result", "Source", "Details"});
    report->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    report->setWordWrap(true);
    auto validate = new QPushButton("Run validation");
    validate->setObjectName("overviewValidate");
    validate->setToolTip("Check live project formats, engine compatibility, package round trip and "
                         "installed mod conflicts. Does not export or launch anything.");
    vl->addWidget(validate);
    vl->addWidget(report, 1);
    connect(validate, &QPushButton::clicked, root, [this, report, status] {
        if (flushDraft && !flushDraft())
            return;
        guard([&] {
            report->clear();
            int errors = 0, warnings = 0;
            for (const auto &d : validateMod(project)) {
                errors += d.severity == ModDiagnostic::Error;
                warnings += d.severity == ModDiagnostic::Warning;
                auto item =
                    new QTreeWidgetItem(report, {d.severity == ModDiagnostic::Error     ? "Error"
                                                 : d.severity == ModDiagnostic::Warning ? "Warning"
                                                                                        : "Info",
                                                 d.resource, d.message});
                item->setData(0, Qt::UserRole, d.resource);
                item->setToolTip(2, d.message);
            }
            status->setText(QString("%1 errors · %2 warnings. Errors block export and testing.")
                                .arg(errors)
                                .arg(warnings));
        });
    });
    connect(report, &QTreeWidget::itemDoubleClicked, root, [this](QTreeWidgetItem *item) {
        const auto name = item->data(0, Qt::UserRole).toString();
        if (project.resources.contains(name))
            selectResource(name);
    });
    tabs->addTab(validation, "Validation");
    auto showAdvanced = [this, tabs, diagnostics](bool visible) {
        setProperty("overviewAdvanced", visible);
        const int index = tabs->indexOf(diagnostics);
        if (visible && index < 0)
            tabs->addTab(diagnostics, "Advanced");
        else if (!visible && index >= 0)
            tabs->removeTab(index);
        if (!visible)
            diagnostics->hide();
        rebuildTree();
    };
    connect(advanced, &QToolButton::toggled, root, [tabs, diagnostics, showAdvanced](bool visible) {
        showAdvanced(visible);
        if (visible)
            tabs->setCurrentWidget(diagnostics);
    });
    {
        QSignalBlocker block(advanced);
        advanced->setChecked(property("overviewAdvanced").toBool());
    }
    showAdvanced(advanced->isChecked());
    layout->addWidget(tabs, 1);
    return root;
}
QWidget *WorkshopWindow::resourceEditor(const QString &name) {
    if (name == "@overview") return overviewEditor();
    auto root = new QWidget;
    root->setObjectName("resourceWorkspace");
    auto layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    auto navigation = new QHBoxLayout;
    if (!MapDocument::supported(name) && !lastMap.isEmpty()) {
        auto back = new QPushButton("Back to map");
        back->setObjectName("backToMap");
        back->setToolTip("Return to the map you were editing.");
        navigation->addWidget(back);
        connect(back, &QPushButton::clicked, this, [this] { selectResource(lastMap); });
    }
    if (name == "DUNGEON.CBT" && dungeonRoomNavigation) {
        auto back = new QPushButton("Back to dungeon");
        back->setObjectName("backToDungeon");
        back->setToolTip("Return to the dungeon level and cell that opened this room.");
        navigation->addWidget(back);
        connect(back, &QPushButton::clicked, this, [this] { selectResource("DUNGEON.DAT"); });
    }
    if (navigation->count()) {
        navigation->addStretch();
        layout->addLayout(navigation);
    } else
        delete navigation;
    QStringList related;
    if (name == "STORY.DAT") related = {"STORY1.16", "STORY2.16", "STORY3.16", "STORY4.16", "STORY5.16", "STORY6.16", "TEXT.16"};
    else if (name == "END.DAT") related = {"END1.16", "END2.16", "TEXT.16"};
    else if (name == "QUESTION.DAT") related = {"CREATE.16"};
    else if (name == "MISCMAPS.DAT") related = {"CREATE.16", "ENDSC.16"};
    if (!related.isEmpty()) {
        auto artwork = new QHBoxLayout;
        artwork->addWidget(new QLabel("Related artwork:"));
        auto collections = new QComboBox;
        collections->setObjectName("relatedArtwork");
        collections->setToolTip("Collections used by this story or scene. Select a collection, then Open artwork.");
        for (const auto &source : related) {
            if (!project.resources.contains(source)) continue;
            QIcon icon;
            QString tooltip = "Source: " + source + ". Original image ordering is preserved.";
            try {
                auto images = U5::readGraphics(source, project.data(source)).images;
                for (const auto &image : images)
                    if (!image.isNull()) { icon = artworkPreview(image); break; }
            } catch (...) {
                tooltip += " Preview unavailable: damaged artwork.";
            }
            collections->addItem(icon, Workshop::artworkTitle(source), source);
            collections->setItemData(collections->count() - 1, tooltip, Qt::ToolTipRole);
        }
        auto open = new QPushButton("Open artwork");
        open->setEnabled(collections->count() > 0);
        open->setToolTip("Open the selected collection. Valid pending text is applied before navigating.");
        artwork->addWidget(collections, 1);
        artwork->addWidget(open);
        connect(open, &QPushButton::clicked, this, [this, collections] {
            navigateContent("content/" + collections->currentData().toString());
        });
        layout->addLayout(artwork);
    }
    QWidget *editor = nullptr;

    const auto kind = Workshop::capability(name).editor;
    auto objectCommit = [this](const QString &name, const QByteArray &bytes, bool included) {
        return guard([&] {
            Project candidate = project;
            candidate.resources[name].edited = bytes;
            candidate.resources[name].included = included;
            candidate.validate();
            edit({{name, bytes}}, "Edit starting world objects", {{name, included}});
        });
    };
    if (kind == Workshop::EditorKind::WorldObjects) {
        editor = Workshop::worldObjectEditor(&project, name == "BRIT.OOL" ? 0 : 1,
                                             objectCommit, &editorState, &flushDraft);
    } else if (kind == Workshop::EditorKind::LocationText) {
        editor = Workshop::locationTextEditor(
            &project, name,
            [this](const QMap<QString, QByteArray> &changes, const QString &title) {
                return guard([&] {
                    Project candidate = project;
                    for (auto it = changes.begin(); it != changes.end(); ++it)
                        candidate.resources[it.key()].edited = it.value();
                    candidate.validate();
                    edit(changes, title);
                });
            },
            &editorState, &flushDraft);
    } else if (name == "STORY.DAT" || name == "END.DAT" || kind == Workshop::EditorKind::Narrative || kind == Workshop::EditorKind::Scenes ||
               kind == Workshop::EditorKind::Fonts || kind == Workshop::EditorKind::Tables) {
        editor = (name == "STORY.DAT" || name == "END.DAT" ? Workshop::cutsceneEditor
                  : kind == Workshop::EditorKind::Tables   ? Workshop::tableEditor
                  : kind == Workshop::EditorKind::Fonts  ? Workshop::fontEditor
                  : kind == Workshop::EditorKind::Scenes ? Workshop::sceneEditor
                                                         : Workshop::narrativeEditor)(
            &project, name,
            [this](const QMap<QString, QByteArray> &changes, const QString &title) {
                return guard([&] {
                    Project candidate = project;
                    for (auto it = changes.begin(); it != changes.end(); ++it)
                        candidate.resources[it.key()].edited = it.value();
                    candidate.validate();
                    edit(changes, title);
                });
            },
            &editorState, &flushDraft);
    } else if (kind == Workshop::EditorKind::Dungeon) {
        auto workspace = new Workshop::DungeonWorkspace(
            &project, &editorState,
            [this](const QMap<QString, QByteArray> &changes, const QString &description) {
                return guard([&] {
                    Project candidate = project;
                    for (auto it = changes.begin(); it != changes.end(); ++it)
                        candidate.resources[it.key()].edited = it.value();
                    candidate.validate();
                    edit(changes, description);
                });
            },
            [this](int room) {
                editorState["DUNGEON.CBT/map"] = room;
                dungeonRoomNavigation = true;
                selectResource("DUNGEON.CBT");
            });
        flushDraft = [workspace] {
            workspace->canvas->cancelStroke();
            return true;
        };
        editor = workspace;
    } else if (name == "TYPES.OVR") {
        editor = CustomTiles::editor(
            &project,
            [this](const QMap<QString, QByteArray> &changes, const QString &title) {
                return guard([&] {
                    Project candidate = project;
                    for (auto it = changes.begin(); it != changes.end(); ++it)
                        candidate.resources[it.key()].edited = it.value();
                    candidate.validate();
                    edit(changes, title);
                });
            },
            &editorState);
    } else if (kind == Workshop::EditorKind::Artwork)
        editor = graphicsEditor(name);
    else if (kind == Workshop::EditorKind::Conversation)
        editor = characterEditor(name);
    else if (kind == Workshop::EditorKind::State)
        editor = stateEditor();
    else if (kind == Workshop::EditorKind::Schedule)
        editor = editorState.value(name + "/character", -1) >= 0 ? characterEditor(name)
                                                                 : npcEditor(name);
    else if (kind == Workshop::EditorKind::Map)
        editor = mapEditor(name);
    if (!editor) {
        QVBoxLayout *layout;
        editor = panel(&layout);
        const auto info = Workshop::capability(name);
        layout->addWidget(hint(info.title + "\n" + info.status));
        auto entries = new QListWidget;
        entries->setObjectName("resourceEntries");
        entries->setToolTip(
            "Read-only overview. This resource has no supported authoring controls yet.");
        Workshop::ResourceDocument document(project, name);
        for (const auto &entry : document.entries()) {
            auto item = new QListWidgetItem(entry.title, entries);
            item->setData(Qt::UserRole, document.id() + "/" + entry.id);
            item->setData(Qt::UserRole + 1, entry.offset);
            item->setToolTip(QString("Offset %1; %2 bytes").arg(entry.offset).arg(entry.length));
        }
        layout->addWidget(entries);
    }
    layout->addWidget(editor);
    return root;
}

QWidget *WorkshopWindow::mapEditor(const QString &name) {
    auto workspace = new MapWorkspace(
        &project, name, &mapViewStates, &editorState,
        [this](const QMap<QString, QByteArray> &changes, const QString &description) {
            return guard([&] {
                Project candidate = project;
                QMap<QString, bool> inclusion;
                for (auto it = changes.begin(); it != changes.end(); ++it) {
                    candidate.resources[it.key()].edited = it.value();
                    if (it.key() == "BRIT.OOL" || it.key() == "UNDER.OOL") {
                        inclusion[it.key()] = true;
                        candidate.resources[it.key()].included = true;
                    }
                }
                Workshop::hydrateExtendedMaps(candidate);
                candidate.validate();
                edit(changes, description, inclusion);
            });
        },
        [this](const QString &resource) { selectResource(resource); });
    workspace->editTileType = [this](unsigned id) {
        auto type = CustomTiles::definition(project, id);
        int kind = type.kind;
        if (id < 512 && kind && type.base >= 128)
            kind = 2;
        editorState["customTiles/kind"] = kind;
        editorState[kind ? QString("customTiles/%1/id").arg(kind) : "customTiles/id"] = id;
        selectResource("TYPES.OVR");
    };
    workspace->locationChanged = [this] { rebuildTree(); };
    QStringList modes{"Terrain"};
    const auto npc = name.left(name.size() - 4) + ".NPC";
    if (name.endsWith(".CBT"))
        modes.append("Encounter");
    else if (project.resources.contains(npc))
        modes.append("NPCs / Schedules");
    if (project.resources.contains("SIGNS.DAT") &&
        (MAPS_LocationResource(name.toLatin1().constData()) ||
         QStringList{"BRIT.DAT", "UNDER.DAT", "TOWNE.DAT", "DWELLING.DAT", "CASTLE.DAT", "KEEP.DAT"}
             .contains(name)))
        modes.append("Signs");
    if ((name == "BRIT.DAT" || name == "UNDER.DAT") && project.resources.contains("INIT.OOL") &&
        project.resources.contains("BRIT.OOL") && project.resources.contains("UNDER.OOL"))
        modes.append("Starting Objects");
    if (name.endsWith(".CBT"))
        modes.append("Encounter Details");
    if (!name.endsWith(".CBT"))
        modes.append("Positions");
    modes.append("Contents");
    workspace->enableLocationModes(
        modes,
        [this, name, workspace](const QString &mode, int page, MapCanvas *canvas,
                                std::function<bool()> *flush) -> QWidget * {
            if (mode == "Positions") {
                *flush = [] { return true; };
                MapDocument document(&project, name);
                return Workshop::mapPositionEditor(
                    &project, name, document.pages[page], canvas,
                    [this, workspace, name](const auto &changes, const QString &title) {
                        return guard([&] {
                            edit(changes, title);
                            if (changes.contains(name) || changes.contains("MAPS.OVR"))
                                workspace->refreshTerrain();
                        });
                    },
                    &editorState, [workspace](QPoint cell) { workspace->centerOnCell(cell); },
                    [this](const QString &resource) { selectResource(resource); });
            }
            if (mode == "Contents") {
                *flush = [] { return true; };
                MapDocument document(&project, name);
                return new Workshop::ContentsEditor(
                    &project, Workshop::contentsTarget(name, document.pages[page], page), canvas,
                    [this](const QMap<QString, QByteArray> &changes, const QString &title) {
                        return guard([&] {
                            Project candidate = project;
                            QMap<QString, bool> inclusion;
                            for (auto it = changes.begin(); it != changes.end(); ++it) {
                                candidate.resources[it.key()].edited = it.value();
                                if (it.key() == "BRIT.OOL" || it.key() == "UNDER.OOL") {
                                    inclusion[it.key()] = true;
                                    candidate.resources[it.key()].included = true;
                                }
                            }
                            candidate.validate();
                            edit(changes, title, inclusion);
                        });
                    });
            }
            if (mode == "Signs") {
                return Workshop::locationTextEditor(
                    &project, "SIGNS.DAT",
                    [this](const QMap<QString, QByteArray> &changes, const QString &title) {
                        return guard([&] {
                            Project candidate = project;
                            for (auto it = changes.begin(); it != changes.end(); ++it)
                                candidate.resources[it.key()].edited = it.value();
                            candidate.validate();
                            edit(changes, title);
                        });
                    },
                    &editorState, flush, {name, page, canvas, [workspace](QPoint cell) {
                                              workspace->centerOnCell(cell);
                                          }});
            }
            if (mode == "Starting Objects") {
                auto saveObjects =
                    [this](const QString &resource, const QByteArray &bytes, bool included,
                           int slot = -1, unsigned type = 0) {
                        return guard([&] {
                            Project candidate = project;
                            candidate.resources[resource].edited = bytes;
                            candidate.resources[resource].included = included;
                            QMap<QString, QByteArray> changes{{resource, bytes}};
                            auto definitions = Workshop::readContents(
                                project.resources.value("CONTENTS.OVR").edited);
                            const int floor = resource == "UNDER.OOL" ? -1 : 0;
                            const auto objects =
                                Workshop::WorldObjects::effective(candidate, floor < 0 ? 1 : 0);
                            bool removed = false;
                            for (int i = definitions.size() - 1; i >= 0; i--) {
                                const auto &d = definitions[i];
                                if (!d.kind && !d.map && d.floor == floor && d.slot != 255 &&
                                    U5::byte(objects, d.slot * 8) != 1) {
                                    definitions.removeAt(i);
                                    removed = true;
                                }
                            }
                            if (removed) {
                                auto contents = Workshop::writeContents(
                                    definitions, project.data("CONTENTS.OVR"));
                                changes["CONTENTS.OVR"] = contents;
                                candidate.resources["CONTENTS.OVR"].edited = contents;
                            }
                            if (slot < 0 && !included) {
                                auto placements = CustomTiles::placements(project);
                                for (auto it = placements.begin(); it != placements.end();) {
                                    if (it->kind == 4 && it->floor == floor)
                                        it = placements.erase(it);
                                    else
                                        ++it;
                                }
                                if (!project.data("TYPEMAP.OVR").isEmpty()) {
                                    auto mapTypes = CustomTiles::mapBytes(project, placements);
                                    changes["TYPEMAP.OVR"] = mapTypes;
                                    candidate.resources["TYPEMAP.OVR"].edited = mapTypes;
                                }
                            }
                            if (slot > 0 && (type >= 512 || !project.resources.value("TYPEMAP.OVR").edited.isEmpty())) {
                                QString map = floor < 0 ? "UNDER.DAT" : "BRIT.DAT";
                                auto pages = project.mapPages(map);
                                auto typeChanges =
                                    CustomTiles::assignActor(project, map, pages[0], 0, slot, type);
                                for (auto it = typeChanges.begin(); it != typeChanges.end(); ++it) {
                                    changes[it.key()] = it.value();
                                    candidate.resources[it.key()].edited = it.value();
                                }
                            }
                            candidate.validate();
                            edit(changes, "Edit starting world objects", {{resource, included}});
                        });
                    };
                return Workshop::worldObjectEditor(
                    &project, name == "BRIT.DAT" ? 0 : 1, saveObjects, &editorState, flush, canvas,
                    [workspace](QPoint cell) { workspace->centerOnCell(cell); }, saveObjects);
            }
            *flush = [] { return true; };
            return combatEditor(name, page);
        });
    flushDraft = [workspace] { return workspace->flushLocationMode(); };
    return workspace;
}
QWidget *WorkshopWindow::graphicsEditor(const QString &name) {
    auto graphics = std::make_shared<U5::Graphics>(U5::readGraphics(name, project.data(name)));
    require(!graphics->images.isEmpty(), "Artwork collection has no image slots");
    QVBoxLayout *layout;
    auto w = panel(&layout);
    auto heading = new QLabel(Workshop::artworkTitle(name));
    heading->setObjectName("artworkTitle");
    heading->setToolTip("Source: " + name + ". Numbered images retain their original slot ordering.");
    layout->addWidget(heading);
    auto row = new QHBoxLayout;
    auto slot = new QComboBox;
    auto tileLabel = [graphics](int i) {
        const int tile = i + graphics->tileOffset;
        if (tile < 256)
            return MapDocument::tileName(tile);
        const int actor = tile - 256;
        return actor < 64
                   ? QString("Item / actor frame %1").arg(actor)
                   : Workshop::actorName(actor & ~3) + QString(" · Frame %1").arg(actor % 4 + 1);
    };
    for (int i = 0; i < graphics->images.size(); i++)
        slot->addItem(QString("%1 — %2")
                          .arg(graphics->tiles ? QString("Tile %1 · %2").arg(i).arg(tileLabel(i))
                                               : QString("Image %1").arg(i + 1))
                          .arg(graphics->images[i].isNull()
                                   ? "Empty"
                                   : QString("%1 × %2")
                                         .arg(graphics->images[i].width())
                                         .arg(graphics->images[i].height())));
    slot->setObjectName("imageSlot");
    slot->setCurrentIndex(
        qBound(0, editorState.value(name + "/image"), int(graphics->images.size()) - 1));
    if (graphics->tiles) {
        auto create = new QPushButton("New tile…");
        create->setObjectName("newTileDefinition");
        row->addWidget(create);
        connect(create, &QPushButton::clicked, w, [this] {
            editorState["customTiles/kind"] = 0;
            selectResource("TYPES.OVR");
            QTimer::singleShot(0, this, [this] {
                if (auto add = findChild<QPushButton *>("newCustomTile"))
                    add->click();
            });
        });
    }
    row->addWidget(slot, 1);
    auto zoom = new QComboBox;
    zoom->addItems({"1×", "2×", "4×", "6×", "8×", "12×"});
    zoom->setObjectName("artworkZoom");
    zoom->setCurrentIndex(qBound(0, editorState.value(name + "/zoom", 3), 5));
    zoom->setToolTip("Magnify source pixels without smoothing; does not resize exported artwork.");
    slot->setToolTip("Select an original image slot. Empty slots are preserved for compatibility.");
    row->addWidget(zoom);
    layout->addLayout(row);
    auto canvas = new PixelCanvas;
    canvas->setObjectName("pixelCanvas");
    canvas->image = graphics->images[slot->currentIndex()];
    canvas->zoom = QVector<int>{1, 2, 4, 6, 8, 12}[zoom->currentIndex()];
    canvas->size();
    auto view = makeScroll(canvas);
    view->setObjectName("canvasView");
    auto imageSplit = new QSplitter;
    auto gallery = new QListWidget;
    gallery->setObjectName("artworkGallery");
    gallery->setToolTip("Original image slots. Choose a thumbnail to edit it; transparent pixels reveal the canvas background.");
    gallery->setViewMode(QListView::IconMode);
    gallery->setResizeMode(QListView::Adjust);
    gallery->setIconSize(QSize(48, 48));
    gallery->setGridSize(QSize(76, 76));
    gallery->setMaximumWidth(280);
    for (int i = 0; i < graphics->images.size(); i++) {
        auto item = new QListWidgetItem(artworkPreview(graphics->images[i]),
                                        graphics->tiles ? QString::number(i) : QString("Image %1").arg(i + 1), gallery);
        item->setToolTip(slot->itemText(i));
    }
    gallery->setCurrentRow(slot->currentIndex());
    imageSplit->addWidget(gallery);
    imageSplit->addWidget(view);
    imageSplit->setStretchFactor(1, 1);
    layout->addWidget(imageSplit, 1);
    connect(gallery, &QListWidget::currentRowChanged, w, [=](int i) {
        if (i >= 0)
            slot->setCurrentIndex(i);
    });
    connect(slot, &QComboBox::currentIndexChanged, w, [=](int i) {
        QSignalBlocker block(gallery);
        gallery->setCurrentRow(i);
    });
    auto palette = new Workshop::ColorPalette(16);
    palette->setObjectName("artworkPalette");
    auto colors = Workshop::artworkColors(U5::readGraphics(name, project.resources.value(name).original));
    canvas->allowedColors = colors;
    canvas->brush = Workshop::paletteColor(canvas->brush, colors);
    palette->setColors(colors, !graphics->tiles);
    palette->select(canvas->brush);
    palette->changed = [=](QRgb color) { canvas->brush = color; };
    layout->addWidget(palette);
    connect(slot, &QComboBox::currentIndexChanged, w, [=](int i) {
        editorState[name + "/image"] = i;
        canvas->image = graphics->images[i];
        canvas->size();
    });
    connect(zoom, &QComboBox::currentIndexChanged, w, [=](int i) {
        editorState[name + "/zoom"] = i;
        canvas->zoom = QVector<int>{1, 2, 4, 6, 8, 12}[i];
        canvas->size();
    });
    auto change = [=](const QImage &img) {
        int i = slot->currentIndex();
        QImage old = graphics->images[i];
        auto constrained = Workshop::paletteImage(img, colors);
        graphics->images[i] = constrained;
        if (!guard([&] {
                edit({{name, U5::writeGraphics(*graphics)}}, "Edit image " + QString::number(i));
            })) {
            graphics->images[i] = old;
            canvas->image = old;
            canvas->update();
        } else {
            canvas->image = constrained;
            canvas->size();
            gallery->item(i)->setIcon(artworkPreview(constrained));
        }
    };
    canvas->commit = change;
    auto actions = new QHBoxLayout;
    button(actions, "Import PNG into slot", [=] {
        QString path = QFileDialog::getOpenFileName(
            this, graphics->pngResource.isEmpty() ? "Import EGA PNG" : "Import PNG", {},
            "PNG (*.png)");
        if (path.isEmpty())
            return;
        guard([&] {
            QImage img(path);
            require(!img.isNull() && img.size() == canvas->image.size(),
                    "PNG dimensions must match the selected image");
            change(img.convertToFormat(QImage::Format_ARGB32));
            palette->setColors(colors, !graphics->tiles);
        });
    });
    button(actions, "Export slot PNG", [=] {
        guard([&] {
            QString path = QFileDialog::getSaveFileName(this, "Export PNG", {}, "PNG (*.png)");
            if (path.isEmpty())
                return;
            require(QFileInfo(path).absolutePath() != project.sourceDirectory,
                    "Export images outside the original game folder");
            require(canvas->image.save(path, "PNG"), "Cannot export PNG");
        });
    });
    if (graphics->tiles) {
        button(actions, "Export tilesheet", [=] {
            guard([&] {
                QString path =
                    QFileDialog::getSaveFileName(this, "Export tilesheet", {}, "PNG (*.png)");
                if (path.isEmpty())
                    return;
                require(QFileInfo(path).absolutePath() != project.sourceDirectory,
                        "Export outside original game folder");
                QImage sheet(graphics->columns * graphics->images[0].width(),
                             16 * graphics->images[0].height(), QImage::Format_ARGB32);
                sheet.fill(Qt::black);
                QPainter p(&sheet);
                for (int i = 0; i < graphics->images.size(); i++)
                    p.drawImage((i % graphics->columns) * graphics->images[0].width(),
                                (i / graphics->columns) * graphics->images[0].height(),
                                graphics->images[i]);
                p.end();
                require(sheet.save(path, "PNG"), "Cannot export PNG");
            });
        });
        button(actions, "Import tilesheet", [=] {
            QString path =
                QFileDialog::getOpenFileName(this, "Import tilesheet", {}, "PNG (*.png)");
            if (path.isEmpty())
                return;
            guard([&] {
                QImage sheet(path);
                const QSize cell = graphics->images[0].size();
                require(sheet.size() == QSize(graphics->columns * cell.width(), 16 * cell.height()),
                        "Tilesheet dimensions must match the current source sheet");
                auto next = *graphics;
                for (int i = 0; i < graphics->images.size(); i++)
                    next.images[i] = sheet
                                         .copy((i % graphics->columns) * cell.width(),
                                               (i / graphics->columns) * cell.height(),
                                               cell.width(), cell.height())
                                         .convertToFormat(QImage::Format_ARGB32);
                for (auto &image : next.images)
                    image = Workshop::paletteImage(image, colors);
                auto bytes = U5::writeGraphics(next);
                edit({{name, bytes}}, "Import tilesheet");
                *graphics = next;
                palette->setColors(colors, !graphics->tiles);
                canvas->image = graphics->images[slot->currentIndex()];
                canvas->size();
                for (int i = 0; i < graphics->images.size(); i++)
                    gallery->item(i)->setIcon(artworkPreview(graphics->images[i]));
            });
        });
    }
    actions->addStretch();
    layout->addLayout(actions);
    return w;
}
QWidget *WorkshopWindow::characterEditor(const QString &name) {
    const int character = editorState.value(name + "/character", -1);
    const int conversation = editorState.value(name + "/conversation");
    Workshop::ContentDestination selected;
    bool found = false;
    for (const auto &d : Workshop::characterDestinations(project))
        if (d.resource == name && d.character == character &&
            (d.conversation < 0 || d.conversation == conversation)) {
            selected = d;
            found = true;
            break;
        }
    if (!found || (character < 0 && !editorState.value(name + "/indexedCharacter")))
        return name.endsWith(".TLK") ? dialogueEditor(name) : npcEditor(name);
    QVBoxLayout *layout;
    auto root = panel(&layout);
    root->setObjectName("characterWorkspace");
    auto heading = new QHBoxLayout;
    auto title = new QLabel(selected.title + " — " + selected.group);
    title->setObjectName("characterTitle");
    title->setTextFormat(Qt::PlainText);
    title->setToolTip(selected.provenance);
    heading->addWidget(title, 1);
    auto provenance = new QLabel(selected.provenance);
    provenance->setObjectName("characterProvenance");
    provenance->setTextFormat(Qt::PlainText);
    provenance->setToolTip(selected.provenance);
    provenance->hide();
    layout->addWidget(provenance);
    auto tabs = new QTabWidget;
    tabs->setObjectName("characterModes");
    layout->addWidget(tabs, 1);
    if (selected.conversation >= 0) {
        auto dialogue = dialogueEditor(name);
        static_cast<ConversationEditor *>(dialogue)->focusCharacter();
        tabs->addTab(dialogue, "Conversation");
    } else {
        tabs->addTab(hint("No editable conversation matches this NPC's "
                          "conversation number. Merchants and special NPCs "
                          "may use engine-controlled dialogue."),
                     "Conversation");
    }
    if (character >= 0) {
        const QString schedule = selected.scheduleResource;
        const int town = Workshop::npcTown(character), slot = Workshop::npcSlot(character);
        auto show = new QPushButton("Show NPC on map");
        show->setObjectName("showCharacterMap");
        show->setToolTip("Open this NPC's first scheduled floor in "
                         "NPCs / Schedules mode.");
        const QString map = schedule.left(schedule.size() - 4) + ".DAT";
        show->setEnabled(project.resources.contains(map));
        connect(show, &QPushButton::clicked, root, [this, map, schedule, town, slot] {
            if (flushDraft && !flushDraft())
                return;
            const auto pages = project.mapPages(map);
            MapNpcDocument doc(&project, schedule);
            const auto destination = doc.location(town, slot, 0);
            for (int i = 0; i < pages.size(); ++i)
                if (pages[i].settlement == town && pages[i].floor == destination.floor) {
                    editorState[map + "/map"] = i;
                    editorState[map + "/mode"] = 1;
                    editorState[map + "/actor/" + QString::number(town)] = slot;
                    auto &state = mapViewStates[map + "/" + QString::number(i)];
                    state.npc = slot;
                    selectResource(map);
                    return;
                }
            statusBar()->showMessage("The scheduled floor has no map page", 5000);
        });
        heading->addWidget(show);
    }
    layout->insertLayout(0, heading);
    tabs->tabBar()->hide();
    return root;
}
QWidget *WorkshopWindow::dialogueEditor(const QString &name) {
    auto editor = new ConversationEditor(
        &project, name,
        [this, name](const QByteArray &bytes, const QString &description) {
            edit({{name, bytes}}, description);
        },
        [this](const QString &key, const QString &value) {
            history.push(
                new QuestionNameChange(&project, key, value, [this, first = true]() mutable {
                    updateSummary();
                    if (first) {
                        first = false;
                        return;
                    }
                    QTimer::singleShot(0, this, [this] { rebuildEditor(); });
                }));
        },
        editorState.value(name + "/conversation"), editorState.value(name + "/entry"),
        [this, name](int npc, int entry) {
            editorState[name + "/conversation"] = npc;
            editorState[name + "/entry"] = entry;
        });
    auto location = editor->findChild<QComboBox *>("conversationLocationFilter");
    editor->setCharacterIdentity(editorState.value(name + "/character", -1));
    location->setCurrentIndex(
        qMax(0, location->findData(editorState.value(name + "/locationFilter", -1))));
    connect(location, &QComboBox::currentIndexChanged, this, [this, name, location] {
        editorState[name + "/locationFilter"] = location->currentData().toInt();
    });
    flushDraft = [editor] { return editor->flush(); };
    return editor;
}
#include "state_fields.h"
QWidget *WorkshopWindow::stateEditor() {
    require(project.data("INIT.GAM").size() == 4192, "Starting state must be 4192 bytes");
    auto tabs = new QTabWidget;
    auto numeric = [this](QFormLayout *form, QWidget *owner, const QString &label, int off,
                          int width, const QString &help) {
        auto sp = new QSpinBox;
        sp->setObjectName(QString("startingField_%1").arg(off, 0, 16));
        sp->setRange(0, width == 1 ? 255 : 65535);
        sp->setValue(width == 1 ? U5::byte(project.data("INIT.GAM"), off)
                                : U5::word(project.data("INIT.GAM"), off));
        int maximum = off == 0x2d7                   ? 13
                      : off == 0x2d8                 ? 28
                      : off == 0x2d9                 ? 23
                      : off == 0x2db                 ? 59
                      : off == 0x2e2                 ? 99
                      : off == 0x202 || off == 0x204 ? 9999
                      : (off >= 0x206 && off <= 0x208) || off == 0x20a || off == 0x20b ||
                              (off >= 0x21a && off <= 0x289) || (off >= 0x2aa && off <= 0x2b1)
                          ? 99
                          : sp->maximum();
        sp->setMaximum(qMax(maximum, sp->value())); // Preserve unusual inherited values.
        sp->setToolTip(help);
        form->addRow(label, sp);
        if (off == 0x2b5) {
            sp->setReadOnly(true);
            sp->setButtonSymbols(QAbstractSpinBox::NoButtons);
        } else
            connect(sp, &QSpinBox::valueChanged, owner, [this, off, width, label](int value) {
                auto bytes = project.data("INIT.GAM");
                if (width == 1)
                    bytes[off] = char(value);
                else
                    U5::setWord(bytes, off, value);
                if (off == 0x2d9)
                    bytes[0x2da] = char(value); // The engine keeps a second copy of the hour.
                edit({{"INIT.GAM", bytes}}, "Set " + label);
            });
    };
    auto values = new QWidget;
    auto form = new QFormLayout(values);
    for (const auto &field : stateFields)
        numeric(form, values, QString::fromLatin1(field.name), field.offset, field.width,
                QString::fromLatin1(field.help));
    auto activeMember = new QComboBox;
    activeMember->setObjectName("startingActiveMember");
    activeMember->addItem("None", 255);
    for (int i = 0; i < qMin(6, int(U5::byte(project.data("INIT.GAM"), 0x2b5))); ++i)
        activeMember->addItem(
            QString::fromLatin1(project.data("INIT.GAM").mid(2 + i * 32, 9).split(char(0)).first()),
            i);
    int active = U5::byte(project.data("INIT.GAM"), 0x2d5);
    if (activeMember->findData(active) < 0)
        activeMember->addItem("Unassigned (preserved)", active);
    activeMember->setCurrentIndex(activeMember->findData(active));
    activeMember->setToolTip("Party member selected when the new game starts.");
    form->addRow("Active party member", activeMember);
    connect(activeMember, &QComboBox::currentIndexChanged, values, [this, activeMember] {
        auto b = project.data("INIT.GAM");
        b[0x2d5] = char(activeMember->currentData().toInt());
        edit({{"INIT.GAM", b}}, "Set active party member");
    });
    const QStringList phaseNames{"New moon",       "Waxing crescent", "First quarter",
                                 "Waxing gibbous", "Full moon",       "Waning gibbous",
                                 "Last quarter",   "Waning crescent"};
    for (auto moon :
         {qMakePair(QString("Trammel phase"), 0x2df), qMakePair(QString("Felucca phase"), 0x2e0)}) {
        auto phase = new QComboBox;
        phase->setObjectName(QString("startingMoonPhase_%1").arg(moon.second, 0, 16));
        for (int i = 0; i < 8; ++i)
            phase->addItem(phaseNames[i], 0x30 + i);
        int inherited = U5::byte(project.data("INIT.GAM"), moon.second);
        if (phase->findData(inherited) < 0)
            phase->addItem("Unassigned (preserved)", inherited);
        phase->setCurrentIndex(phase->findData(inherited));
        phase->setToolTip("Initial moon phase; the game advances phases as time passes.");
        form->addRow(moon.first, phase);
        connect(phase, &QComboBox::currentIndexChanged, values, [this, phase, moon] {
            auto b = project.data("INIT.GAM");
            b[moon.second] = char(phase->currentData().toInt());
            edit({{"INIT.GAM", b}}, "Set " + moon.first);
        });
    }
    auto startMap = new QPushButton("Show party start on map");
    startMap->setObjectName("showPartyStartMap");
    startMap->setToolTip("Open the starting map and place or drag the party start marker.");
    startMap->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    form->addRow("Starting location", startMap);
    auto openPosition = [this](int id) {
        guard([&] {
            auto positions = Workshop::mapPositions(project);
            auto pos = std::find_if(positions.begin(), positions.end(),
                                    [id](auto p) { return p.id == id; });
            require(pos != positions.end(), "Starting position is unavailable");
            int map = pos->map, floor = pos->floor;
            QString resource = Workshop::isDungeon(project, map) ? "DUNGEON.DAT"
                               : map == 0  ? (floor == -1 ? "UNDER.DAT" : "BRIT.DAT")
                               : map <= 32 ? QStringList{"TOWNE.DAT", "DWELLING.DAT", "CASTLE.DAT",
                                                         "KEEP.DAT"}[(map - 1) / 8]
                                           : QString();
            require(!resource.isEmpty() && project.resources.contains(resource),
                    "The starting map is not available in this project");
            if (resource == "DUNGEON.DAT") {
                editorState["DUNGEON.DAT/dungeon"] = map - 33;
                editorState["DUNGEON.DAT/level"] = floor;
                editorState["positions/selected"] = id;
                selectResource(resource);
                if (auto button =
                        content->currentWidget()->findChild<QToolButton *>("mapPositions"))
                    button->click();
                return;
            }
            MapDocument doc(&project, resource);
            int page = -1;
            for (int i = 0; i < doc.pages.size(); ++i)
                if ((map == 0 || doc.pages[i].settlement == (map - 1) % 8) &&
                    (map == 0 || doc.pages[i].floor == floor)) {
                    page = i;
                    break;
                }
            require(page >= 0, "The starting floor is not available in this project");
            editorState["positions/selected"] = id;
            editorState[resource + "/map"] = page;
            selectResource(resource);
            if (auto mapView = dynamic_cast<MapWorkspace *>(
                    content->currentWidget()->findChild<QWidget *>("mapWorkspace")))
                mapView->centerOnCell(pos->cell);
            auto workspace = content->currentWidget()->findChild<QTabBar *>("locationModes");
            if (workspace)
                for (int i = 0; i < workspace->count(); ++i)
                    if (workspace->tabText(i) == "Positions")
                        workspace->setCurrentIndex(i);
        });
    };
    connect(startMap, &QPushButton::clicked, values, [openPosition] { openPosition(0); });
    tabs->addTab(makeScroll(values, true), "Party / time / supplies");
    auto quests = new QWidget;
    auto qform = new QFormLayout(quests);
    auto moonstonesMap = new QPushButton("Show moonstones on map");
    moonstonesMap->setObjectName("showMoonstonesMap");
    moonstonesMap->setToolTip(
        "Open the map editor to place buried moonstones or change which are carried.");
    moonstonesMap->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    qform->addRow("Moonstones", moonstonesMap);
    connect(moonstonesMap, &QPushButton::clicked, quests, [openPosition] { openPosition(1); });
    QStringList virtues = {"Honesty",   "Compassion", "Valor",        "Justice",
                           "Sacrifice", "Honor",      "Spirituality", "Humility"};
    QStringList dungeons = {"Deceit",   "Despise", "Destard",  "Wrong",
                            "Covetous", "Shame",   "Hythloth", "Doom"};
    for (int i = 0; i < 3; i++)
        numeric(qform, quests, QString("Prevent Shadowlord visits %1").arg(i + 1), 0x322 + i, 1,
                "255 prevents visits");
    auto flags = [this, quests, qform, virtues](const QString &label, int offset) {
        auto box = new QWidget;
        auto row = new QHBoxLayout(box);
        for (int bit = 0; bit < 8; bit++) {
            auto check = new QCheckBox(virtues[bit]);
            check->setChecked(U5::byte(project.data("INIT.GAM"), offset) & (1 << bit));
            row->addWidget(check);
            connect(check, &QCheckBox::toggled, quests, [this, bit, offset, label](bool on) {
                auto bytes = project.data("INIT.GAM");
                int value = U5::byte(bytes, offset);
                bytes[offset] = char(on ? value | (1 << bit) : value & ~(1 << bit));
                edit({{"INIT.GAM", bytes}}, label);
            });
        }
        qform->addRow(label, box);
    };
    flags("Ordained", 0x326);
    flags("Completed Codex pilgrimage", 0x328);
    for (int i = 0; i < 8; i++) {
        numeric(qform, quests, dungeons[i] + " open flags", 0x32a + i, 1, "Bit 7 = open");
        numeric(qform, quests, virtues[i] + " shrine status", 0x332 + i, 1, "Bit 7 = destroyed");
    }
    for (auto item : QList<QPair<QString, int>>{{"Grapple", 0x209},
                                                {"Lord British's amulet", 0x20d},
                                                {"Lord British's crown", 0x20e},
                                                {"Lord British's sceptre", 0x20f},
                                                {"Shard of Falsehood", 0x210},
                                                {"Shard of Hatred", 0x211},
                                                {"Shard of Cowardice", 0x212},
                                                {"HMS Cape plans", 0x215},
                                                {"Pocket watch", 0x217},
                                                {"Black badge", 0x218},
                                                {"Sandalwood box", 0x219}}) {
        auto owned = new QCheckBox;
        owned->setObjectName(QString("startingOwned_%1").arg(item.second, 0, 16));
        owned->setChecked(U5::byte(project.data("INIT.GAM"), item.second) != 0);
        owned->setToolTip("Start with this item in the party's inventory.");
        qform->addRow(item.first, owned);
        connect(owned, &QCheckBox::toggled, quests, [this, item](bool on) {
            auto b = project.data("INIT.GAM");
            b[item.second] = on ? char(255) : 0;
            edit({{"INIT.GAM", b}}, "Set " + item.first);
        });
    }
    tabs->addTab(makeScroll(quests, true), "Quest items / progress");
    auto inventory = new QWidget;
    auto inventoryTabs = new QTabWidget;
    auto inventoryLayout = new QVBoxLayout(inventory);
    inventoryLayout->addWidget(inventoryTabs);
    auto inventorySection = [inventoryTabs](const QString &title) {
        auto body = new QWidget;
        auto form = new QFormLayout(body);
        inventoryTabs->addTab(makeScroll(body, true), title);
        return form;
    };
    auto inventoryForm = inventorySection("Equipment");
    for (int i = 0; i < 48; ++i) {
        QString name = QString::fromUtf8(engineEquipmentNames[i]);
        if (!name.isEmpty() && name != "x")
            numeric(inventoryForm, inventory, name, 0x21a + i, 1, "Starting equipment quantity.");
    }
    inventoryForm = inventorySection("Spell mixtures");
    for (int i = 0; i < 48; ++i) {
        QStringList words;
        for (char c : QByteArray(engineSpellCodes[i]))
            if (c >= 'A' && c <= 'Z' && engineSpellSyllables[c - 'A']) {
                QString word = QString::fromLatin1(engineSpellSyllables[c - 'A']).toLower();
                word[0] = word[0].toUpper();
                words.append(word);
            }
        numeric(inventoryForm, inventory, words.join(' ') + " mixtures", 0x24a + i, 1,
                "Starting prepared spell mixtures.");
    }
    inventoryForm = inventorySection("Scrolls / potions");
    const QStringList scrolls{"Vas Lor",     "Rel Hur", "In Sanct",   "In An",
                              "In Quas Wis", "Kal Xen", "In Bet Xen", "An Tym"};
    const QStringList potions{"Blue",   "Yellow", "Red",   "Green",
                              "Orange", "Purple", "Black", "White"};
    for (int i = 0; i < 8; ++i) {
        numeric(inventoryForm, inventory, scrolls[i] + " scrolls", 0x27a + i, 1,
                "Starting scroll quantity.");
        numeric(inventoryForm, inventory, potions[i] + " potions", 0x282 + i, 1,
                "Starting potion quantity.");
    }
    inventoryForm = inventorySection("Reagents / tools");
    QStringList reagents = {"Sulphurous ash", "Ginseng",     "Garlic",     "Spider silk",
                            "Blood moss",     "Black pearl", "Nightshade", "Mandrake root"};
    for (int i = 0; i < 8; i++)
        numeric(inventoryForm, inventory, reagents[i], 0x2aa + i, 1, {});
    numeric(inventoryForm, inventory, "Magic carpets", 0x20a, 1, "Starting magic carpets.");
    numeric(inventoryForm, inventory, "Spyglasses", 0x214, 1, "Starting spyglasses.");
    numeric(inventoryForm, inventory, "Sextants", 0x216, 1, "Starting sextants.");
    tabs->addTab(inventory, "Inventory");
    QVBoxLayout *characterLayout;
    auto characters = panel(&characterLayout);
    auto selector = new QComboBox;
    for (int i = 0; i < 16; i++)
        selector->addItem(
            QString("%1 — %2%3")
                .arg(i)
                .arg(QString::fromLatin1(
                    project.data("INIT.GAM").mid(2 + i * 32, 9).split(char(0)).first()))
                .arg(i < int(U5::byte(project.data("INIT.GAM"), 0x2b5)) ? " (party)" : ""));
    characterLayout->addWidget(selector);
    auto detail = new QStackedWidget;
    characterLayout->addWidget(detail, 1);
    for (int i = 0; i < 16; i++) {
        auto card = new QWidget;
        auto cf = new QFormLayout(card);
        int base = 2 + i * 32;
        auto name = new QLineEdit(
            QString::fromLatin1(project.data("INIT.GAM").mid(base, 9).split(char(0)).first()));
        name->setMaxLength(8);
        cf->addRow("Name (8 ASCII characters)", name);
        connect(name, &QLineEdit::editingFinished, card, [this, name, base] {
            guard([&] {
                QByteArray encoded;
                for (QChar c : name->text()) {
                    require(c.unicode() >= 32 && c.unicode() <= 126, "Names require ASCII text");
                    encoded.append(char(c.unicode()));
                }
                encoded.append(QByteArray(9 - encoded.size(), 0));
                auto bytes = project.data("INIT.GAM");
                bytes.replace(base, 9, encoded);
                edit({{"INIT.GAM", bytes}}, "Rename character");
            });
        });
        const QStringList names = {"Gender (11 male / 12 female)",
                                   "Class (ASCII code)",
                                   "Status (ASCII code)",
                                   "Strength",
                                   "Dexterity",
                                   "Intelligence",
                                   "Magic"};
        for (int j = 0; j < names.size(); j++)
            numeric(cf, card, names[j], base + 9 + j, 1, {});
        numeric(cf, card, "HP", base + 16, 2, {});
        numeric(cf, card, "Maximum HP", base + 18, 2, {});
        numeric(cf, card, "Experience", base + 20, 2, {});
        numeric(cf, card, "Level", base + 22, 1, {});
        for (int j = 0; j < 6; j++)
            numeric(cf, card, QString("Equipment slot %1").arg(j + 1), base + 25 + j, 1, {});
        numeric(cf, card, "Home map ID", base + 31, 1,
                "Party membership is a contiguous prefix, independent of this field");
        auto membership = new QPushButton(i < int(U5::byte(project.data("INIT.GAM"), 0x2b5))
                                              ? "Remove from party…"
                                              : "Add to party");
        membership->setEnabled(i != 0);
        cf->addRow(membership);
        connect(membership, &QPushButton::clicked, card, [this, i] {
            guard([&] {
                auto b = project.data("INIT.GAM");
                int count = U5::byte(b, 0x2b5);
                require(count >= 1 && count <= 6, "Invalid party size");
                QVector<QByteArray> records;
                QVector<int> order;
                for (int r = 0; r < 16; r++) {
                    records << b.mid(2 + r * 32, 32);
                    order << r;
                }
                if (i < count) {
                    bool ok;
                    int home = QInputDialog::getInt(this, "Companion home",
                                                    "Settlement map ID (1–32)", 1, 1, 32, 1, &ok);
                    if (!ok)
                        return;
                    require(i != 0, "The Avatar cannot leave");
                    records[i][31] = char(home);
                    records[i][23] = 0;
                    records.append(records.takeAt(i));
                    order.append(order.takeAt(i));
                    --count;
                } else {
                    require(count < 6 && records[i][0] != 0,
                            "Party is full or selected record is empty");
                    records[i][31] = 0;
                    records.swapItemsAt(i, count);
                    order.swapItemsAt(i, count);
                    ++count;
                }
                int active = order.indexOf(U5::byte(b, 0x2d5));
                b[0x2d5] = char(active >= 0 && active < count ? active : 255);
                b[0x2b5] = char(count);
                for (int r = 0; r < 16; r++)
                    b.replace(2 + r * 32, 32, records[r]);
                edit({{"INIT.GAM", b}}, "Change party membership");
                rebuildEditor();
            });
        });
        detail->addWidget(makeScroll(card, true));
    }
    connect(selector, &QComboBox::currentIndexChanged, characters,
            [=](int i) { detail->setCurrentIndex(i); });
    tabs->addTab(characters, "Party members");
    return tabs;
}
QWidget *WorkshopWindow::npcEditor(const QString &name) {
    require(NPCS_Validate(project.data(name).constData(), project.data(name).size()),
            "Invalid NPC schedules");
    QVBoxLayout *layout;
    auto w = panel(&layout);
    auto row = new QHBoxLayout;
    auto settlement = new QComboBox;
    QString mapName = name.left(name.size() - 4) + ".DAT";
    QStringList labels;
    try {
        for (auto p : project.mapPages(mapName)) {
            QString label = p.name.section(" — ", 0, 0);
            if (!labels.contains(label))
                labels << label;
        }
    } catch (...) {
        for (int i = 0; i < 8; i++)
            labels << QString("Settlement %1").arg(i + 1);
    }
    settlement->addItems(labels);
    settlement->setCurrentIndex(qBound(0, editorState.value(name + "/settlement"), 7));
    row->addWidget(settlement, 1);
    auto npc = new QSpinBox;
    npc->setRange(0, MapNpcDocument(&project, name).count(settlement->currentIndex()) - 1);
    npc->setValue(editorState.value(name + "/npc"));
    row->addWidget(new QLabel("NPC"));
    row->addWidget(npc);
    layout->addLayout(row);
    auto table = new QTableWidget(18, 3);
    table->setHorizontalHeaderLabels({"Field", "Offset", "Value"});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(table, 1);
    auto load = [=] {
        QSignalBlocker blocked(table);
        MapNpcDocument npcs(&project, name);
        QSignalBlocker blockNpc(npc);
        npc->setMaximum(npcs.count(settlement->currentIndex()) - 1);
        int base = npcs.offset(settlement->currentIndex(), npc->value());
        QStringList fields = {"Behavior: change 1",
                              "Behavior: changes 2 and 4",
                              "Behavior: change 3",
                              "Destination X: change 1",
                              "Destination X: changes 2 and 4",
                              "Destination X: change 3",
                              "Destination Y: change 1",
                              "Destination Y: changes 2 and 4",
                              "Destination Y: change 3",
                              "Floor: change 1",
                              "Floor: changes 2 and 4",
                              "Floor: change 3",
                              "Start hour: change 1",
                              "Start hour: change 2",
                              "Start hour: change 3",
                              "Start hour: change 4",
                              "Appearance code",
                              "Conversation number"};
        for (int r = 0; r < 18; r++) {
            int off =
                r < 16 ? base + r
                       : (r == 16 ? npcs.shapeOffset(settlement->currentIndex(), npc->value())
                                  : npcs.dialogueOffset(settlement->currentIndex(), npc->value()));
            auto label = new QTableWidgetItem(fields[r]);
            label->setFlags(label->flags() & ~Qt::ItemIsEditable);
            table->setItem(r, 0, label);
            auto pos = new QTableWidgetItem(QString::number(off, 16));
            pos->setFlags(pos->flags() & ~Qt::ItemIsEditable);
            table->setItem(r, 1, pos);
            int v = U5::byte(project.data(name), off);
            if (r >= 9 && r < 12 && v >= 128)
                v -= 256;
            auto value = new QTableWidgetItem(QString::number(v));
            value->setData(Qt::UserRole, off);
            table->setItem(r, 2, value);
        }
    };
    load();
    connect(settlement, &QComboBox::currentIndexChanged, w, [=](int i) {
        editorState[name + "/settlement"] = i;
        load();
    });
    connect(npc, &QSpinBox::valueChanged, w, [=](int i) {
        editorState[name + "/npc"] = i;
        load();
    });
    connect(table, &QTableWidget::itemChanged, w, [=](QTableWidgetItem *item) {
        if (item->column() != 2)
            return;
        if (!guard([&] {
                bool ok;
                int v = item->text().toInt(&ok);
                bool z = item->row() >= 9 && item->row() < 12;
                require(ok && v >= (z ? -128 : 0) && v <= (z ? 127 : 255),
                        "NPC value is outside its byte range");
                auto b = project.data(name);
                b[item->data(Qt::UserRole).toInt()] = char(v);
                edit({{name, b}}, "Edit NPC schedule");
            }))
            load();
    });
    return w;
}
QWidget *WorkshopWindow::combatEditor(const QString &name, int pageIndex) {
    auto pages = project.mapPages(name);
    QVBoxLayout *layout;
    auto w = panel(&layout);
    auto choice = new QComboBox;
    for (auto page : pages)
        choice->addItem(page.name);
    layout->addWidget(choice);
    if (pageIndex >= 0) {
        choice->setCurrentIndex(pageIndex);
        choice->hide();
    }
    auto table = new QTableWidget(11, 21);
    QStringList headers;
    for (int i = 0; i < 21; i++)
        headers << QString::number(i);
    table->setHorizontalHeaderLabels(headers);
    table->setVerticalHeaderLabels({"New tiles (8)", "Party E X/Y (6+6)", "Party W X/Y",
                                    "Party S X/Y", "Party N X/Y", "Monster tiles (16)",
                                    "Monster X (16)", "Monster Y (16)", "Triggers X/Y (8+8)",
                                    "Change 1 X/Y", "Change 2 X/Y"});
    layout->addWidget(table, 1);
    auto load = [=] {
        QSignalBlocker blocked(table);
        int base = pages[choice->currentIndex()].offset;
        for (int y = 0; y < 11; y++)
            for (int x = 0; x < 21; x++) {
                int off = base + y * 32 + 11 + x;
                auto item =
                    new QTableWidgetItem(QString::number(U5::byte(project.data(name), off)));
                item->setData(Qt::UserRole, off);
                table->setItem(y, x, item);
            }
    };
    load();
    connect(choice, &QComboBox::currentIndexChanged, w, [=] { load(); });
    connect(table, &QTableWidget::itemChanged, w, [=](QTableWidgetItem *item) {
        if (!guard([&] {
                bool ok;
                int v = item->text().toInt(&ok);
                require(ok && v >= 0 && v <= 255, "Combat setup values must be 0–255");
                auto b = project.data(name);
                b[item->data(Qt::UserRole).toInt()] = char(v);
                edit({{name, b}}, "Edit combat setup");
            }))
            load();
    });
    return w;
}
bool WorkshopWindow::reviewPackage(bool launch) {
    if (flushDraft && !flushDraft())
        return false;
    if (project.resources.isEmpty())
        return false;
    const auto diagnostics = validateMod(project);
    bool errors = false;
    QDialog dialog(this);
    dialog.setWindowTitle(launch ? "Validate and test mod" : "Validation / package preview");
    dialog.resize(850, 500);
    auto layout = new QVBoxLayout(&dialog);
    auto report = new QTreeWidget;
    report->setObjectName("modValidationReport");
    report->setHeaderLabels({"Result", "Resource", "Details"});
    report->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    for (const auto &diagnostic : diagnostics) {
        errors |= diagnostic.severity == ModDiagnostic::Error;
        auto item =
            new QTreeWidgetItem(report, {diagnostic.severity == ModDiagnostic::Error     ? "Error"
                                         : diagnostic.severity == ModDiagnostic::Warning ? "Warning"
                                                                                         : "Info",
                                         diagnostic.resource, diagnostic.message});
        item->setToolTip(2, diagnostic.message);
        item->setData(0, Qt::UserRole, diagnostic.resource);
        item->setData(0, Qt::UserRole + 1, diagnostic.entryId);
    }
    report->setWordWrap(true);
    layout->addWidget(report, 1);
    connect(report, &QTreeWidget::itemDoubleClicked, &dialog,
            [this, &dialog](QTreeWidgetItem *item) {
                if (project.resources.contains(item->data(0, Qt::UserRole).toString())) {
                    if (item->data(0, Qt::UserRole).toString() == "DUNGEON.DAT") {
                        auto parts = item->data(0, Qt::UserRole + 1).toString().split('/');
                        if (parts.size() == 6 && parts[0] == "dungeon" && parts[2] == "level" &&
                            parts[4] == "cell") {
                            editorState["DUNGEON.DAT/dungeon"] = parts[1].toInt();
                            editorState["DUNGEON.DAT/level"] = parts[3].toInt();
                            const auto size =
                                Workshop::mapSize(project, 33 + parts[1].toInt(), parts[3].toInt());
                            editorState["DUNGEON.DAT/x"] = parts[5].toInt() % size.width();
                            editorState["DUNGEON.DAT/y"] = parts[5].toInt() / size.width();
                        }
                    }
                    selectResource(item->data(0, Qt::UserRole).toString());
                    dialog.reject();
                }
            });
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    auto proceed = buttons->addButton(launch ? "Start a fresh game" : "Continue",
                                      QDialogButtonBox::AcceptRole);
    proceed->setEnabled(!errors && !project.changed().isEmpty());
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    if (!launch)
        return true;
    return guard([&] {
        QString executable = QSettings().value("paths/engine").toString();
        if (!QFileInfo(executable).exists()) {
            QDir applications(QFileInfo(imperaModsDirectory()).absolutePath());
            for (const QString &name : {QString("Impera.exe"), QString("Impera"), QString("Impera.AppImage"), QString("Impera.app")})
                if (applications.exists(name)) {
                    executable = applications.filePath(name);
                    break;
                }
        }
        if (!QFileInfo(executable).isFile() &&
            !(QFileInfo(executable).isDir() && executable.endsWith(".app", Qt::CaseInsensitive))) {
            executable = QFileDialog::getOpenFileName(this, "Choose Impera executable or AppImage",
                                                      executable);
            if (executable.isEmpty())
                return;
        }
        if (executable.endsWith(".app", Qt::CaseInsensitive))
            executable = QDir(executable).filePath("Contents/MacOS/Impera-engine");
        require(QFileInfo(executable).isFile() && QFileInfo(executable).isExecutable(),
                "Choose an executable Impera file");
        QSettings().setValue("paths/engine", executable);
        QString root = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                           .filePath("test-sessions");
        QString session = prepareModTest(project, root);
        QSettings().setValue("paths/testSession", session);
        auto process = new QProcess(this);
        process->setProgram(QFileInfo(executable).absoluteFilePath());
        process->setWorkingDirectory(session);
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("U5D_DATA_DIR", session);
        environment.insert("U5D_RUNTIME_DIR", session);
        environment.insert("U5D_MODS_DIR", QDir(session).filePath("Mods"));
        // Existing Linux release launchers use XDG_DATA_HOME; isolate those too.
        environment.insert("XDG_DATA_HOME", session);
        environment.insert("XDG_CONFIG_HOME", QDir(session).filePath("config"));
        environment.insert("XDG_CACHE_HOME", QDir(session).filePath("cache"));
        process->setProcessEnvironment(environment);
        process->setStandardOutputFile(QDir(session).filePath("engine-output.log"));
        process->setStandardErrorFile(QDir(session).filePath("engine-errors.log"));
        connect(process, &QProcess::errorOccurred, this, [this, process, session] {
            if (process->error() == QProcess::FailedToStart)
                process->deleteLater();
            QMessageBox::warning(this, "Mod test launch failed",
                                 process->errorString() + "\nTest files/logs: " + session);
        });
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, process, session](int code, QProcess::ExitStatus status) {
                    statusBar()->showMessage(
                        QString("Test finished (%1). Saves and logs: %2")
                            .arg(status == QProcess::CrashExit ? "crashed" : QString::number(code))
                            .arg(session),
                        30000);
                    process->deleteLater();
                });
        process->start();
        statusBar()->showMessage("Launching an isolated test in " + session +
                                     ". Start a NEW character to exercise this mod's starting "
                                     "state. Saves/logs stay in this test folder.",
                                 30000);
    });
}
