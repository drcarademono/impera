#include "dialogue.h"
#include "dialogue_editor.h"
#include "mod/package.h"
#include "window.h"
#include <QTemporaryDir>
#include <QtWidgets>
#include <cstring>
#include <iostream>
#include <random>
extern "C" int WorkshopEngineMatch(const unsigned char *, char *);
extern "C" int WorkshopEngineDecode(const void *, size_t, void **, unsigned *);
static void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> static void rejects(F fn) {
    bool failed = false;
    try {
        fn();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, "Expected invalid resource to be rejected");
}
static void file(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly), "Cannot make fixture");
    check(f.write(bytes) == bytes.size(), "Cannot write fixture");
}
static Project fixture(const QString &directory) {
    file(directory + "/tiles.16", QByteArray(65536, 0));
    QByteArray init(4192, 0);
    init[0x2b5] = 1;
    file(directory + "/init.gam", init);
    Project p;
    p.openGame(directory);
    return p;
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    int count = 0;
    auto test = [&](const char *name, auto fn) {
        fn();
        ++count;
        std::cout << "PASS " << name << "\n";
    };
    try {
        test("Lossless dialogue structure", [] {
            auto bytes =
                U5::encodeText("Name<Entry>Description<Entry>Greeting<Entry>Job<Entry>Bye<Entry>"
                               "tele<Entry><Or><Entry>star<Entry>Hello<Label 1><Entry><Any><Label "
                               "1>Question?<Entry>No<Entry>y<Entry>Yes<End "
                               "Conversation><Entry><Any><Label 15>@");
            auto d = Dialogue::parse(bytes);
            check(d.bytes() == bytes, "Dialogue no-op changed bytes");
            check(d.structuralError.isEmpty(), "Structured parse failed");
            check(d.topics.size() == 1 && d.topics[0].keywords.size() == 2,
                  "Alias chain not grouped");
            check(d.questions.size() == 1 && d.tail >= 0, "Tail mistaken for question");
            check(d.questions[0].replies.size() == 1, "Reply missing");
            auto changed = Dialogue::replaceAliases(d, d.topics[0], {"moon", "sun"});
            auto next = Dialogue::parse(changed);
            check(next.entries[next.topics[0].response].bytes ==
                      d.entries[d.topics[0].response].bytes,
                  "Alias edit touched response");
            rejects([&] { Dialogue::removeQuestion(d, 1); });
            check(Dialogue::parse(Dialogue::addQuestion(d)).questions.size() == 2,
                  "Add question failed");
            auto malformed = QByteArray::fromHex("85b1");
            check(Dialogue::parse(malformed).bytes() == malformed, "Malformed bytes lost");
            auto operands = Dialogue::parse(QByteArray::fromHex("8c919100"));
            check(operands.bytes() == QByteArray::fromHex("8c919100"),
                  "Operand/control collision lost");
            Dialogue::Simulator sim(d, {});
            sim.start();
            auto lines = sim.respond("star");
            check(!lines.isEmpty(), "Simulation produced no lines");
            sim.respond("yes");
            check(sim.ended, "Question reply did not end conversation");
        });
        test("Dialogue semantics and engine keyword matching", [] {
            auto make = [](const QString &body) {
                return Dialogue::parse(U5::encodeText(
                    "Name<Entry>Description<Entry>Greeting<Entry>Job<Entry>Bye<Entry>" + body +
                    "<Any><Label 15>@"));
            };
            for (auto key : QStringList{"y", "tele", "planet", "password"})
                for (auto input : QStringList{"YES", "TELESCOPE", "PLANETS", "PASSWORD", "NO"}) {
                    auto encoded = Dialogue::encodeKeyword(key);
                    auto upper = input.toLatin1();
                    bool expected = WorkshopEngineMatch(reinterpret_cast<const unsigned char *>(
                                                            encoded.constData()),
                                                        upper.data()) == 0;
                    auto d = make(key + "<Entry>Matched<End Conversation><Entry>");
                    Dialogue::Simulator sim(d, {});
                    sim.start();
                    auto lines = sim.respond(input);
                    bool matched = false;
                    for (auto line : lines)
                        if (line.trace && line.text.startsWith("Matched keyword"))
                            matched = true;
                    check(expected == matched, "Sandbox keyword matching differs from engine");
                }
            auto d = make("help<Entry><Label 1><Entry><Any><Label "
                          "1>Question?<Entry>No<Entry>y<Entry><Gold><Byte 176><Byte "
                          "176><Byte 180>Thanks<End Conversation><Entry>");
            Dialogue::Sandbox state;
            state.gold = 3;
            Dialogue::Simulator poor(d, state);
            poor.start();
            poor.respond("help");
            poor.respond("yes");
            check(!poor.ended && poor.state.gold == 3,
                  "Insufficient payment changed state or ended conversation");
            state.gold = 10;
            Dialogue::Simulator rich(d, state);
            rich.start();
            rich.respond("help");
            rich.respond("yes");
            check(rich.ended && rich.state.gold == 6, "Payment did not use encoded amount");
            auto loop = make("help<Entry><Label 1><Entry><Any><Label 1><Label 1><Entry>No<Entry>");
            Dialogue::Simulator bounded(loop, {});
            bounded.start();
            bounded.respond("help");
            check(bounded.ended, "Recursive question was not bounded");
            check(Dialogue::parse(QByteArray::fromHex("8c919000")).bytes() ==
                      QByteArray::fromHex("8c919000"),
                  "Label-like operand changed");
            auto renamed = Dialogue::addQuestion(d);
            auto next = Dialogue::parse(renamed);
            auto removed = Dialogue::parse(Dialogue::removeQuestion(next, 1, 2));
            check(removed.question(1) < 0 && Dialogue::references(removed, 2).size() > 0,
                  "Retargeted deletion lost callers");
            rejects([] { Dialogue::encodeKeyword(" "); });
            auto missing = make("help<Entry><Label 8><Entry>");
            bool error = false;
            for (auto issue : missing.issues())
                error |= issue.error;
            check(error, "Missing question reference not diagnosed");
        });
        test("LZW width changes and dictionary resets", [] {
            std::mt19937 rng(1948);
            for (int size : {0, 1, 254, 255, 256, 257, 512, 4096, 65536, 150000}) {
                QByteArray bytes;
                for (int i = 0; i < size; i++)
                    bytes.append(char(rng()));
                auto compressed = U5::compress(bytes);
                check(U5::decompress(compressed) == bytes, "LZW roundtrip");
                void *output = nullptr;
                unsigned length = 0;
                check(WorkshopEngineDecode(compressed.constData(), compressed.size(), &output,
                                           &length),
                      "Engine decoder rejected native output");
                check(length == unsigned(bytes.size()) &&
                          QByteArray(static_cast<char *>(output), length) == bytes,
                      "Engine/native codec mismatch");
                free(output);
                check(U5::decompress(U5::compress(QByteArray(size, 'x'))) == QByteArray(size, 'x'),
                      "Repeated dictionary roundtrip");
            }
            rejects([] { U5::decompress(QByteArray::fromHex("0500000000")); });
        });
        test("Graphics palette, mask padding and roundtrip", [] {
            QByteArray raw(6, 0);
            U5::setWord(raw, 0, 1);
            U5::setWord(raw, 2, 6);
            raw += QByteArray::fromHex("09000100");
            raw += QByteArray(8, char(0x11));
            U5::setWord(raw, 4, raw.size());
            raw += QByteArray::fromHex("090001008000");
            auto g = U5::readGraphics("ITEMS.16", U5::compress(raw));
            check(qAlpha(g.images[0].pixel(0, 0)) == 0, "Mask alpha");
            check(qAlpha(g.images[0].pixel(8, 0)) == 255, "Padded alpha");
            check(U5::writeGraphics(g) == g.original, "Graphics no-op identity");
            g.images[0].setPixel(8, 0, qRgba(0, 0, 0, 0));
            auto next = U5::readGraphics("ITEMS.16", U5::writeGraphics(g));
            check(qAlpha(next.images[0].pixel(8, 0)) == 0, "Changed alpha");
            g.images[0].setPixel(0, 0, qRgba(1, 2, 3, 255));
            rejects([&] { U5::writeGraphics(g); });
        });
        test("Raw tiles and PNG alpha rejection", [] {
            auto g = U5::readGraphics("TILES.16", QByteArray(65536, 0));
            check(g.images.size() == 512, "Tile count");
            g.images[0].setPixel(0, 0, qRgba(0, 0, 0, 0));
            rejects([&] { U5::writeGraphics(g); });
        });
        test("Dialogue control operands and token identity", [] {
            QByteArray bytes = QByteArray::fromHex("0181c18285");
            rejects([&] { U5::decodeText(bytes); });
            bytes = QByteArray::fromHex("0181c18285b0b1b286ff8cfffe01ffc000909fc00000");
            check(U5::encodeText(U5::decodeText(bytes)) == bytes, "Dialogue exact byte identity");
            check(U5::encodeText("<thee><thee,><great><Great>") == QByteArray::fromHex("0d393861"),
                  "Canonical word indices");
            rejects([] { U5::encodeText("<Set Flag>"); });
            rejects([] { U5::encodeText("<Byte 133><Byte 0><Byte 1><Byte 2>"); });
        });
        test("TLK rebuild, tails, IDs and limits", [] {
            QVector<U5::Conversation> entries = {{1, QByteArray::fromHex("c100909fc000")},
                                                 {2, QByteArray::fromHex("c200")}};
            auto b = U5::writeDialogue(entries);
            auto decoded = U5::readDialogue(b);
            check(decoded.size() == 2 && decoded[0].bytes == entries[0].bytes, "TLK segments");
            entries[1].id = 1;
            rejects([&] { U5::writeDialogue(entries); });
            entries[1].id = 2;
            entries[1].bytes = QByteArray(1025, 'x');
            rejects([&] { U5::writeDialogue(entries); });
        });
        test("Britannia implicit water and shared chunks", [] {
            QByteArray ovl(0x3986, 0);
            ovl.replace(0x3886, 256, QByteArray(256, char(255)));
            ovl[0x3886] = 0;
            ovl[0x3887] = 0;
            QByteArray chunks(256, char(2));
            auto map = U5::worldMap("BRIT.DAT", chunks, ovl);
            map[0] = 3;
            map[32] = 4;
            U5::writeWorld("BRIT.DAT", map, chunks, ovl);
            check(U5::worldMap("BRIT.DAT", chunks, ovl) == map,
                  "Shared chunks and water preserved");
            check((unsigned char)map[16] == 2, "Original alias unaffected");
        });
        test("Underworld extensions and settlement basement mapping", [] {
            QByteArray b(65536, 2);
            b.append(QByteArray(256, 9));
            auto map = U5::worldMap("UNDER.DAT", b, {});
            map[0] = 4;
            QByteArray unused;
            U5::writeWorld("UNDER.DAT", map, b, unused);
            check(b.right(256) == QByteArray(256, 9), "Underworld tail");
            auto pages = U5::mapPages("TOWNE.DAT", QByteArray(16384, 0));
            check(pages[6].floor == -1 && pages[6].settlement == 3, "Yew basement");
            auto keep = U5::mapPages("KEEP.DAT", QByteArray(16384, 0));
            check(keep[13].floor == -1 && keep[13].settlement == 7, "Serpent's Hold basement");
        });
        test("Fixed story windows and rejection", [] {
            auto offsets = U5::storyOffsets();
            QByteArray b(offsets.last() + 20, 0);
            for (int offset : offsets)
                b.replace(offset, 5, "HELLO");
            auto pages = U5::storyPages(b);
            check(pages.size() == 20, "Story pages");
            auto changed = U5::changeStory(b, 3, "HI");
            check(changed.size() == b.size() && U5::storyPages(changed)[4] == "HELLO",
                  "Fixed offsets");
            rejects([&] { U5::changeStory(b, 0, "TOO LONG"); });
        });
        test("Sparse packages, projects, checksum and original preservation", [] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            p.title = "Test mod";
            p.resources["INIT.GAM"].edited[0x202] = 9;
            auto bytes = p.package();
            check(bytes.size() < 150, "Sparse package");
            auto next = fixture(dir.path());
            next.importPackage(bytes);
            check(next.data("INIT.GAM") == p.data("INIT.GAM"), "Shared C decoder");
            p.save(dir.path() + "/mod.imperaproject");
            Project restored;
            restored.load(dir.path() + "/mod.imperaproject");
            check(restored.package() == bytes, "Project roundtrip");
            auto corrupt = bytes;
            corrupt[corrupt.size() - 1] ^= 1;
            rejects([&] { next.importPackage(corrupt); });
            check(next.data("INIT.GAM") == p.data("INIT.GAM"), "Rejected package changed project");
            next.resources["INIT.GAM"].original[0] = 1;
            rejects([&] { next.importPackage(bytes); });
            QFile f(dir.path() + "/init.gam");
            f.open(QIODevice::ReadOnly);
            check(f.readAll() == p.resources["INIT.GAM"].original, "Original file mutated");
        });
        test("Native widgets are read-only until an edit", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            WorkshopWindow w;
            check(w.openGame(dir.path()), "Window game load");
            for (auto name : p.resources.keys()) {
                w.selectResource(name);
                app.processEvents();
            }
            check(w.projectForTests().changed().isEmpty(), "GUI load mutated resources");
            w.selectResource("INIT.GAM");
            auto table = w.findChild<QStackedWidget *>("workspaces")
                             ->currentWidget()
                             ->findChild<QTableWidget *>("byteInspector");
            check(table, "Native byte inspector missing");
            table->item(0, 0)->setText("01");
            check(w.projectForTests().data("INIT.GAM")[0] == 1, "Byte edit did not persist");
            app.processEvents();
        });
        test("Native painting, undo and unapplied dialogue safety", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            file(dir.path() + "/TOWNE.DAT", QByteArray(16384, 0));
            file(
                dir.path() + "/TOWNE.TLK",
                U5::writeDialogue({{1, U5::encodeText("Name<Entry>Description<Entry>Greeting<Entry>"
                                                      "Job<Entry>Bye<Entry><Any><Label 15>@")}}));
            WorkshopWindow w;
            check(w.openGame(dir.path()), "Fixture load");
            w.show();
            app.processEvents();
            auto active = [&] {
                return w.findChild<QStackedWidget *>("workspaces")->currentWidget();
            };
            auto click = [&](QWidget *widget, QPointF position) {
                QMouseEvent down(QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton,
                                 Qt::NoModifier);
                QMouseEvent up(QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton,
                               Qt::NoModifier);
                QApplication::sendEvent(widget, &down);
                QApplication::sendEvent(widget, &up);
                app.processEvents();
            };
            w.selectResource("TOWNE.DAT");
            auto canvas = active()->findChild<QWidget *>("mapCanvas");
            check(canvas, "Map canvas absent");
            click(canvas, {40, 40});
            check(U5::byte(w.projectForTests().data("TOWNE.DAT"), 33) == 1, "Paint stroke lost");
            auto undo = [&] {
                for (auto action : w.findChildren<QAction *>())
                    if (action->text().startsWith("Undo ")) {
                        action->trigger();
                        app.processEvents();
                        return;
                    }
                throw std::runtime_error("Undo action absent");
            };
            undo();
            check(w.projectForTests().data("TOWNE.DAT") == QByteArray(16384, 0),
                  "Map undo lost original bytes");
            w.selectResource("TILES.16");
            canvas = active()->findChild<QWidget *>("pixelCanvas");
            check(canvas, "Pixel canvas absent");
            click(canvas, {3, 3});
            auto graphics = U5::readGraphics("TILES.16", w.projectForTests().data("TILES.16"));
            check(graphics.images[0].pixel(0, 0) == qRgb(255, 255, 255),
                  "Native pixel brush failed");
            undo();
            check(w.projectForTests().data("TILES.16") == QByteArray(65536, 0),
                  "Pixel undo changed original encoding");
            w.selectResource("TOWNE.TLK");
            auto scope = active()->findChild<QComboBox *>("conversationSourceScope");
            scope->setCurrentIndex(1);
            auto text = active()->findChild<QPlainTextEdit *>("dialogueText");
            check(text, "Dialogue editor absent");
            text->setPlainText("<Gold><Byte 177>");
            auto answer = [&](QMessageBox::StandardButton which) {
                QTimer::singleShot(0, [which] {
                    for (auto widget : QApplication::topLevelWidgets())
                        if (auto box = qobject_cast<QMessageBox *>(widget))
                            box->button(which)->click();
                });
            };
            answer(QMessageBox::Cancel);
            w.selectResource("TILES.16");
            check(active()->findChild<QPlainTextEdit *>("dialogueText"), "Cancel discarded draft");
            check(!w.projectForTests().changed().contains("TOWNE.TLK"),
                  "Draft applied without consent");
            text->setPlainText("Hello<Entry>Description<Entry>Greeting<Entry>Job<"
                               "Entry>Bye<Entry><Any><Label 15>@");
            w.selectResource("TILES.16");
            check(w.projectForTests().changed().contains("TOWNE.TLK"),
                  "Validated automatic apply on navigation failed");
            auto package = w.projectForTests().package();
            Project restored;
            restored.openGame(dir.path());
            restored.importPackage(package);
            check(restored.data("TOWNE.TLK") == w.projectForTests().data("TOWNE.TLK"),
                  "Native edited package roundtrip");
        });
        test("Structured conversation widgets, aliases, undo and annotations", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            auto bytes =
                U5::encodeText("Name<Entry>Description<Entry>Greeting<Entry>Job<Entry>Bye<Entry>"
                               "help<Entry><Or><Entry>hint<Entry>Hello<Label 1><Entry><Any><Label "
                               "1>Question?<Entry>No<Entry>y<Entry>Yes<End "
                               "Conversation><Entry><Any><Label 15>@");
            auto original = U5::writeDialogue({{1, bytes}});
            file(dir.path() + "/TOWNE.TLK", original);
            WorkshopWindow w;
            check(w.openGame(dir.path()), "Cannot open structured fixture");
            w.show();
            w.selectResource("TOWNE.TLK");
            app.processEvents();
            auto active = [&] {
                return w.findChild<QStackedWidget *>("workspaces")->currentWidget();
            };
            auto editor = dynamic_cast<ConversationEditor *>(
                active()->findChild<QWidget *>("conversationEditor"));
            check(editor, "Structured conversation editor absent");
            auto block = editor->findChild<QPlainTextEdit *>("conversationBlockText");
            check(block && block->toPlainText() == "Name", "Basic text not readable");
            block->setPlainText("New Name");
            check(editor->flush(), "Valid basic text did not apply");
            auto next =
                Dialogue::parse(U5::readDialogue(w.projectForTests().data("TOWNE.TLK"))[0].bytes);
            check(next.entries[1].bytes == Dialogue::parse(bytes).entries[1].bytes,
                  "Basic edit changed unrelated entry");
            for (auto action : w.findChildren<QAction *>())
                if (action->text().startsWith("Undo ")) {
                    action->trigger();
                    break;
                }
            app.processEvents();
            check(w.projectForTests().data("TOWNE.TLK") == original,
                  "Structured undo did not restore exact bytes");
            editor = dynamic_cast<ConversationEditor *>(
                active()->findChild<QWidget *>("conversationEditor"));
            auto tree = editor->findChild<QTreeWidget *>("conversationOutline");
            QTreeWidgetItemIterator it(tree);
            while (*it) {
                if ((*it)->text(0) == "help / hint") {
                    tree->setCurrentItem(*it);
                    break;
                }
                ++it;
            }
            auto aliases = editor->findChild<QLineEdit *>("conversationAliases");
            check(aliases && aliases->text() == "help, hint", "Shared aliases absent");
            aliases->setText("help, clue");
            QMetaObject::invokeMethod(aliases, "editingFinished", Qt::DirectConnection);
            next =
                Dialogue::parse(U5::readDialogue(w.projectForTests().data("TOWNE.TLK"))[0].bytes);
            check(Dialogue::keyword(next.entries[next.topics[0].keywords[1]].bytes) == "clue",
                  "Alias edit failed");
            check(
                next.entries[next.topics[0].response].bytes ==
                    Dialogue::parse(bytes).entries[Dialogue::parse(bytes).topics[0].response].bytes,
                "Alias edit changed shared response");
            auto before = w.projectForTests().data("TOWNE.TLK");
            aliases->setText("help, ");
            QMetaObject::invokeMethod(aliases, "editingFinished", Qt::DirectConnection);
            check(w.projectForTests().data("TOWNE.TLK") == before,
                  "Invalid alias draft changed the project");
            aliases->setText("help, clue");
            QMetaObject::invokeMethod(aliases, "editingFinished", Qt::DirectConnection);
            check(editor->flush(), "Corrected alias draft did not apply");
            for (auto b : editor->findChildren<QPushButton *>())
                if (b->text() == "Reset") {
                    b->click();
                    break;
                }
            auto input = editor->findChild<QLineEdit *>("conversationInput");
            input->setText("help");
            QMetaObject::invokeMethod(input, "returnPressed", Qt::DirectConnection);
            auto transcript = editor->findChild<QTreeWidget *>("conversationTranscript");
            check(transcript->topLevelItemCount() > 1, "Test conversation transcript absent");
            check(w.projectForTests().data("TOWNE.TLK") == before, "Simulation changed project");
            w.projectForTests().dialogueNames["TOWNE.TLK/1/1"] = "Password question";
            QString path = dir.path() + "/test.imperaproject";
            w.projectForTests().save(path);
            Project loaded;
            loaded.load(path);
            check(loaded.dialogueNames.value("TOWNE.TLK/1/1") == "Password question",
                  "Question names not saved");
            check(loaded.data("TOWNE.TLK") == before, "Annotation changed game data");
            check(!Dialogue::inventoryOptions().value(65).isEmpty() &&
                      Dialogue::inventoryOptions().value(16) == "Dagger",
                  "Engine inventory labels incorrect");
        });
        if (qEnvironmentVariableIsSet("U5_GAME_DIR"))
            test("Original resource compatibility and native workspaces", [&] {
                Project p;
                p.openGame(qEnvironmentVariable("U5_GAME_DIR"));
                for (auto name : p.resources.keys()) {
                    auto b = p.data(name);
                    if (name.endsWith(".TLK"))
                        for (auto e : U5::readDialogue(b)) {
                            auto d = Dialogue::parse(e.bytes);
                            check(d.bytes() == e.bytes, "Original dialogue structure no-op");
                            check(d.structuralError.isEmpty(),
                                  "Original dialogue has unsupported structure");
                            for (auto issue : d.issues())
                                check(!issue.error,
                                      "Original dialogue rejected by semantic validation");
                        }
                    if (name.endsWith(".16")) {
                        auto g = U5::readGraphics(name, b);
                        check(U5::writeGraphics(g) == b, "Original graphics no-op");
                    } else if (name.endsWith(".TLK")) {
                        for (auto e : U5::readDialogue(b))
                            check(U5::encodeText(U5::decodeText(e.bytes)) == e.bytes,
                                  "Original dialogue text roundtrip");
                    }
                }
                U5::storyPages(p.data("STORY.DAT"));
                WorkshopWindow w;
                check(w.openGame(p.sourceDirectory), "Original folder load");
                for (auto name : p.resources.keys()) {
                    w.selectResource(name);
                    app.processEvents();
                }
                check(w.projectForTests().changed().isEmpty(), "Original GUI mutation");
            });
        std::cout << count << " test groups passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
