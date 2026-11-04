#undef NDEBUG
#include "audio_editor.h"
#include "content_navigation.h"
#include "mod/audio_resource.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <cassert>
#include <memory>
static void write(const QString &path, const QByteArray &b) {
    QFile f(path);
    assert(f.open(QIODevice::WriteOnly));
    assert(f.write(b) == b.size());
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    Project p;
    p.openGame(TEST_GAME_DIR);
    QTemporaryDir temp;
    assert(temp.isValid());
    auto original = Workshop::audioSource(p, Workshop::AudioDocument{}.clip(1, 1));
    assert(original.startsWith("RIFF") && original.mid(8, 4) == "WAVE");
    auto wav = Workshop::audioWav(original);
    assert(wav.size() > 44 && MA_U32(wav.constData() + 40) == unsigned(wav.size() - 44));
    Workshop::AudioDocument d;
    auto s = d.clip(1, 116);
    s.name = "Custom door";
    s.bytes = wav;
    d.put(s);
    auto music = d.clip(0, 18);
    music.name = "Custom town music";
    QFile ogg(QString(TEST_GAME_DIR) + "/../tests/fixtures/audio-tone.ogg");
    assert(ogg.open(QIODevice::ReadOnly));
    auto compressed = ogg.readAll();
    assert(compressed.startsWith("OggS") && Workshop::audioWav(compressed).size() > 44);
    music.bytes = compressed;
    music.start = 1;
    music.end = 50;
    music.gain = 75;
    d.put(music);
    d.routes = {{1, 1, 0, 18}, {2, 4, 0, 18}, {3, 1, 0, 18}, {4, 94, 0, 116}};
    p.resources["AUDIO.OVR"].edited = d.encode();
    p.validate();
    auto encoded = p.data("AUDIO.OVR");
    assert(MA_Validate(encoded.constData(), encoded.size()));
    auto broken = encoded;
    broken[20] = 1;
    assert(!MA_Validate(broken.constData(), broken.size()));
    auto missing = d;
    missing.routes[0].track = 4000;
    bool rejected = false;
    try {
        missing.encode();
    } catch (...) {
        rejected = true;
    }
    assert(rejected);
    auto package = p.package();
    assert(package.startsWith("IMOD0030"));
    Project imported;
    imported.openGame(TEST_GAME_DIR);
    imported.importPackage(package);
    assert(imported.data("AUDIO.OVR") == encoded);
    p.save(temp.filePath("audio.imperaproject"));
    Project loaded;
    loaded.load(temp.filePath("audio.imperaproject"));
    assert(loaded.data("AUDIO.OVR") == encoded);
    auto decoded = Workshop::AudioDocument::read(loaded);
    assert(decoded.clip(0, 18).start == 1 && decoded.clip(0, 18).gain == 75 &&
           decoded.routes.size() == 4);
    assert(Workshop::audioWav(Workshop::audioSource(loaded, decoded.clip(1, 116))) == wav);
    QDir out(temp.filePath("exports"));
    assert(QDir().mkpath(out.path()));
    auto skipped = Workshop::exportSoundEffects(loaded, out.path());
    assert(skipped.size() == 2);
    assert(out.entryList({"*.wav"}, QDir::Files).size() == 114);
    for (const auto &name : out.entryList({"*.wav"}, QDir::Files)) {
        QFile file(out.filePath(name));
        assert(file.open(QIODevice::ReadOnly));
        auto b = file.readAll();
        assert(b.startsWith("RIFF") && b.size() > 44 && b.mid(8, 4) == "WAVE");
    }
    std::function<bool()> flush;
    std::unique_ptr<QWidget> editor(Workshop::audioEditor(
        p, 1, [&](const QByteArray &b, const QString &) { p.resources["AUDIO.OVR"].edited = b; },
        nullptr, &flush));
    assert(editor->findChild<QListWidget *>("audioList")->count() == 116);
    assert(editor->findChild<QPushButton *>("exportAllSoundEffects"));
    assert(editor->findChild<QPushButton *>("exportAudio"));
    if (argc > 1 && QString(argv[1]) == "--screenshot") {
        editor->resize(1100, 760);
        editor->show();
        app.processEvents();
        editor->grab().save("/tmp/workshop-audio.png");
    }
    std::unique_ptr<QWidget> musicEditor(Workshop::audioEditor(
        p, 0, [&](const QByteArray &b, const QString &) { p.resources["AUDIO.OVR"].edited = b; }));
    assert(musicEditor->findChild<QListWidget *>("audioList")->count() == 18);
    assert(musicEditor->findChild<QComboBox *>("audioContext")->count() > 50);
    auto volumeField = editor->findChild<QSpinBox *>("audioGain");
    auto beforeVolume = p.data("AUDIO.OVR");
    volumeField->setValue(75);
    assert(p.data("AUDIO.OVR") == beforeVolume);
    assert(flush());
    QMetaObject::invokeMethod(volumeField, "editingFinished");
    assert(Workshop::AudioDocument::read(p).clip(1, 1).gain == 75);
    auto beforePreview = p.data("AUDIO.OVR");
    editor->findChild<QPushButton *>("audioPlay")->click();
    app.processEvents();
    editor->findChild<QPushButton *>("audioPause")->click();
    editor->findChild<QPushButton *>("audioPause")->click();
    editor->findChild<QPushButton *>("audioStop")->click();
    assert(p.data("AUDIO.OVR") == beforePreview);
    auto destinations = Workshop::contentDestinations(p);
    int entries = 0;
    for (const auto &c : destinations)
        if (c.workspace == "Sound and Music")
            entries++;
    assert(entries == 2);
    return 0;
}
