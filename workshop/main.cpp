#include "window.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QStyleFactory>
#include <QTimer>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("Impera Workshop");
    app.setOrganizationName("Impera");
    app.setApplicationVersion("0.1");
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    p.setColor(QPalette::Window, QColor("#18202e"));
    p.setColor(QPalette::WindowText, QColor("#e5ebf5"));
    p.setColor(QPalette::Base, QColor("#101723"));
    p.setColor(QPalette::AlternateBase, QColor("#202b3a"));
    p.setColor(QPalette::Text, QColor("#e5ebf5"));
    p.setColor(QPalette::PlaceholderText, QColor("#a7b5cc"));
    p.setColor(QPalette::Button, QColor("#293549"));
    p.setColor(QPalette::ButtonText, QColor("#e5ebf5"));
    p.setColor(QPalette::Highlight, QColor("#527fc4"));
    p.setColor(QPalette::HighlightedText, Qt::white);
    app.setPalette(p);
    app.setStyleSheet("QToolBar{spacing:8px;padding:8px;}QPushButton{padding:7px "
                      "12px;}QLineEdit,QComboBox,QSpinBox{padding:5px;}QTabBar::tab{padding:9px "
                      "14px;}QTreeWidget{border:0;}QSplitter::handle{background:#2d3a50;}QLineEdit:"
                      "focus,QComboBox:focus,QSpinBox:focus,QPushButton:focus,QCheckBox:focus{"
                      "border:1px solid #85b9ff;}");
    QCommandLineParser parser;
    parser.setApplicationDescription("Native Ultima 5 mod-package authoring for Impera");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"game", "Read original DOS resources from this folder", "directory"});
    parser.addOption({"project", "Open a saved Workshop project", "file"});
    parser.addOption({"resource", "Initially select this resource", "name"});
    parser.addOption(
        {"smoke-test", "Open every resource editor and exit (uses --game or --project)"});
    parser.addOption(
        {"screenshot", "Save a UI screenshot before exiting (for development)", "file"});
    parser.process(app);
    WorkshopWindow window;
    if (parser.isSet("game") && !window.openGame(parser.value("game")))
        return 1;
    if (parser.isSet("project") && !window.openProject(parser.value("project")))
        return 1;
    if (!parser.isSet("game") && !parser.isSet("project"))
        window.restoreGameDirectory();
    if (parser.isSet("resource")) {
        if (!window.projectForTests().resources.contains(parser.value("resource")))
            return 1;
        window.selectResource(parser.value("resource"));
    }
    window.show();
    if (parser.isSet("smoke-test")) {
        if (window.projectForTests().resources.isEmpty())
            return 1;
        QTimer::singleShot(0, &window, [&] {
            for (auto name : window.projectForTests().resources.keys()) {
                window.selectResource(name);
                app.processEvents();
            }
            app.quit();
        });
    }
    if (parser.isSet("screenshot"))
        QTimer::singleShot(250, &window, [&] {
            bool ok = window.grab().save(parser.value("screenshot"), "PNG");
            app.exit(ok ? 0 : 1);
        });
    return app.exec();
}
