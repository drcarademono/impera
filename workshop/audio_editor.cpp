#include "audio_editor.h"
#include "mod/audio_resource.h"
#include "mod/maps.h"
#include "save_dialog.h"
extern "C" {
#include "audio/audio_catalog.h"
#include "audio/pcspeaker.h"
}
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <algorithm>
#include <cmath>
#include <memory>
using U5::require;
namespace Workshop {
static void number(QByteArray &b, unsigned n) {
    char out[4];
    MA_Put(out, n);
    b.append(out, 4);
}
QString audioName(int kind, int id) {
    static const char *tracks[] = {"",
                                   "Title and victory",
                                   "Britannia",
                                   "Sailing",
                                   "Combat",
                                   "Shrines and camping",
                                   "Villages and huts",
                                   "Keeps",
                                   "Lord British's Castle",
                                   "Towns",
                                   "Dungeons",
                                   "Underworld",
                                   "Blackthorn's Castle",
                                   "Lighthouses and keeps",
                                   "Ending — rescue",
                                   "Ending — conclusion",
                                   "Additional recording 16",
                                   "Additional recording 17"};
    if (!kind && id > 0 && id < 18)
        return QString::fromUtf8(tracks[id]);
    if (kind && id > 0 && id <= 115)
        return QString::fromUtf8(audioCatalog[id - 1].name);
    return QString("New %1 %2").arg(kind ? "sound effect" : "track").arg(id);
}
AudioDocument AudioDocument::read(const Project &p) {
    AudioDocument d;
    auto b = p.resources.value("AUDIO.OVR").edited;
    if (b.isEmpty())
        return d;
    require(MA_Validate(b.constData(), b.size()), "Invalid audio resource");
    auto data = (const unsigned char *)b.constData();
    size_t at = 24;
    for (unsigned i = 0; i < MA_U32(data + 8); i++) {
        ModAudioClip c;
        at = MA_Clip(data, at, &c);
        AudioClip clip;
        clip.kind = c.kind;
        clip.id = c.id;
        clip.gain = c.gain;
        clip.loop = c.loop;
        clip.start = c.start;
        clip.end = c.end;
        clip.name = QString::fromUtf8((const char *)c.name, c.nameSize);
        clip.bytes = QByteArray((const char *)c.data, c.size);
        d.clips.append(clip);
    }
    for (unsigned i = 0; i < MA_U32(data + 12); i++, at += 16)
        d.routes.append({MA_U32(data + at), MA_U32(data + at + 4), MA_U32(data + at + 8),
                         MA_U32(data + at + 12)});
    return d;
}
QByteArray AudioDocument::encode() const {
    QByteArray b("IAUDIO01", 8);
    number(b, clips.size());
    number(b, routes.size());
    number(b, 0);
    number(b, 0);
    for (const auto &c : clips) {
        auto name = c.name.toUtf8();
        number(b, c.kind);
        number(b, c.id);
        number(b, c.kind ? 0 : c.loop);
        number(b, c.gain);
        number(b, c.start);
        number(b, c.end);
        number(b, name.size());
        number(b, c.bytes.size());
        b += name;
        b += c.bytes;
    }
    for (const auto &r : routes) {
        number(b, r.scope);
        number(b, r.key);
        number(b, r.level);
        number(b, r.track);
    }
    MA_Put(b.data() + 16, MOD_Crc32(b.constData() + 24, b.size() - 24));
    require(MA_Validate(b.constData(), b.size()),
            "Audio settings exceed supported limits or contain invalid assignments");
    return b;
}
AudioClip AudioDocument::clip(int kind, int id) const {
    for (const auto &c : clips)
        if (c.kind == kind && c.id == id)
            return c;
    AudioClip c;
    c.kind = kind;
    c.id = id;
    c.name = audioName(kind, id);
    c.loop = !kind;
    return c;
}
void AudioDocument::put(const AudioClip &c) {
    for (auto &a : clips)
        if (a.kind == c.kind && a.id == c.id) {
            a = c;
            return;
        }
    clips.append(c);
}
static QByteArray readFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    require(f.size() <= MOD_MAX_AUDIO, "Audio file exceeds 64 MiB");
    return f.readAll();
}
static QByteArray wave(const QByteArray &pcm) {
    QByteArray b("RIFF", 4);
    number(b, 36 + pcm.size());
    b += "WAVEfmt ";
    number(b, 16);
    b.append("\1\0\2\0", 4);
    number(b, 48000);
    number(b, 192000);
    b.append("\4\0\20\0", 4);
    b += "data";
    number(b, pcm.size());
    b += pcm;
    return b;
}
static QByteArray decode(const QByteArray &bytes) {
    require(!bytes.isEmpty(), "No source audio is available. Import a recording first.");
    require(MIX_Init(), SDL_GetError());
    auto decoder =
        MIX_CreateAudioDecoder_IO(SDL_IOFromConstMem(bytes.constData(), bytes.size()), true, 0);
    if (!decoder) {
        QString error = SDL_GetError();
        MIX_Quit();
        require(false, "Cannot decode audio: " + error);
    }
    QByteArray pcm;
    SDL_AudioSpec spec = {SDL_AUDIO_S16LE, 2, 48000};
    char block[32768];
    for (;;) {
        int n = MIX_DecodeAudio(decoder, block, sizeof(block), &spec);
        if (n < 0) {
            MIX_DestroyAudioDecoder(decoder);
            MIX_Quit();
            require(false, "Audio decoding failed");
        }
        if (!n)
            break;
        if (pcm.size() + n > 192000 * 600) {
            MIX_DestroyAudioDecoder(decoder);
            MIX_Quit();
            require(false, "Audio exceeds ten minutes");
        }
        pcm.append(block, n);
    }
    MIX_DestroyAudioDecoder(decoder);
    MIX_Quit();
    require(!pcm.isEmpty(), "Audio contains no samples");
    return pcm;
}
QByteArray audioWav(const QByteArray &b) { return wave(decode(b)); }
QByteArray audioSource(const Project &p, const AudioClip &c, QString *origin) {
    if (origin)
        *origin =
            "Source unavailable — import a recording, or locate the original recordings folder.";
    if (!c.bytes.isEmpty()) {
        if (origin)
            *origin = "Source: imported recording (included in mod)";
        return c.bytes;
    }
    QStringList roots = {
        QSettings().value(c.kind ? "audio/soundFolder" : "audio/musicFolder").toString(),
        p.sourceDirectory, QCoreApplication::applicationDirPath()};
    for (const auto &root : roots)
        for (const auto &folder :
             c.kind ? QStringList{"Sound", "SFX", ""} : QStringList{"Music", "BGM", ""}) {
            if (root.isEmpty())
                continue;
            QDir dir(folder.isEmpty() ? root : QDir(root).filePath(folder));
            QStringList candidates;
            for (const auto &file : dir.entryList(QDir::Files, QDir::Name)) {
                bool matches =
                    c.kind ? c.id <= 115 &&
                                 file.compare(QString::fromUtf8(audioCatalog[c.id - 1].file),
                                              Qt::CaseInsensitive) == 0
                           : file.left(2) == QString("%1").arg(c.id, 2, 10, QChar('0')) &&
                                 (file.size() < 3 || !file[2].isDigit());
                if (matches && QStringList{"wav", "ogg", "mp3", "flac"}.contains(
                                   QFileInfo(file).suffix().toLower()))
                    candidates.append(file);
            }
            if (!candidates.isEmpty()) {
                if (origin)
                    *origin = "Source: " + dir.filePath(candidates.first());
                return readFile(dir.filePath(candidates.first()));
            }
        }
    if (!c.kind || c.id < 1 || c.id > 115)
        return {};
    const auto &recipe = audioCatalog[c.id - 1];
    QByteArray pcm;
    uint16_t seed = 0;
    for (unsigned i = 0; i < recipe.count; i++) {
        const auto &r = audioRecipes[recipe.first + i];
        size_t count = 0;
        float *data = PCSPK_Render(r.kind, r.p[0], r.p[1], r.p[2], r.p[3], r.p[4], &seed, &count);
        require(data, "Cannot render PC speaker sound");
        int previous = pcm.size();
        pcm.resize(previous + count * 4);
        for (size_t n = 0; n < count; n++) {
            int v = qBound(-32768, int(data[n] * 32767), 32767);
            for (int ch = 0; ch < 2; ch++) {
                pcm[previous + n * 4 + ch * 2] = char(v);
                pcm[previous + n * 4 + ch * 2 + 1] = char(v >> 8);
            }
        }
        SDL_free(data);
    }
    if (origin && !pcm.isEmpty())
        *origin = "Source: vanilla PC speaker synthesis";
    return pcm.isEmpty() ? QByteArray() : wave(pcm);
}
static void save(const QString &path, const QByteArray &b) {
    QSaveFile f(path);
    require(f.open(QIODevice::WriteOnly) && f.write(b) == b.size() && f.commit(),
            "Cannot save " + path);
}
QStringList exportSoundEffects(const Project &p, const QString &directory) {
    require(QDir(directory).exists(), "Export folder does not exist");
    auto doc = AudioDocument::read(p);
    QStringList skipped;
    QVector<int> ids;
    for (int i = 1; i <= 115; i++)
        ids.append(i);
    for (const auto &c : doc.clips)
        if (c.kind && c.id > 115)
            ids.append(c.id);
    for (int id : ids) {
        auto c = doc.clip(1, id);
        auto bytes = audioSource(p, c);
        if (bytes.isEmpty()) {
            skipped.append(c.name);
            continue;
        }
        QString stem = c.name;
        stem.replace(QRegularExpression("[^A-Za-z0-9_-]+"), "-");
        save(QDir(directory).filePath(QString("%1-%2.wav").arg(id, 3, 10, QChar('0')).arg(stem)),
             audioWav(bytes));
    }
    return skipped;
}
class Player {
    MIX_Mixer *mixer = nullptr;
    MIX_Track *track = nullptr;
    MIX_Audio *audio = nullptr;

  public:
    bool initialized = false, paused = false;
    double volume = 0.5;
    int gain = 100;
    ~Player() {
        stop();
        if (track)
            MIX_DestroyTrack(track);
        if (mixer)
            MIX_DestroyMixer(mixer);
        if (initialized) {
            MIX_Quit();
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
    }
    void stop() {
        if (track) {
            MIX_StopTrack(track, 0);
            MIX_SetTrackAudio(track, nullptr);
        }
        if (audio)
            MIX_DestroyAudio(audio);
        audio = nullptr;
        paused = false;
    }
    void play(const QByteArray &bytes, const AudioClip &c) {
        stop();
        if (!mixer) {
            if (!initialized) {
                require(SDL_InitSubSystem(SDL_INIT_AUDIO), SDL_GetError());
                if (!MIX_Init()) {
                    SDL_QuitSubSystem(SDL_INIT_AUDIO);
                    require(false, SDL_GetError());
                }
                initialized = true;
            }
            mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
            require(mixer, SDL_GetError());
            track = MIX_CreateTrack(mixer);
            if (!track) {
                MIX_DestroyMixer(mixer);
                mixer = nullptr;
                require(false, SDL_GetError());
            }
        }
        audio = MIX_LoadAudio_IO(mixer, SDL_IOFromConstMem(bytes.constData(), bytes.size()), true,
                                 true);
        require(audio, SDL_GetError());
        require(MIX_SetTrackAudio(track, audio), SDL_GetError());
        auto props = SDL_CreateProperties();
        SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, c.loop ? -1 : 0);
        SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOP_START_MILLISECOND_NUMBER, c.start);
        if (c.end)
            SDL_SetNumberProperty(props, MIX_PROP_PLAY_MAX_MILLISECONDS_NUMBER, c.end);
        gain = c.gain;
        setVolume(volume);
        bool ok = MIX_PlayTrack(track, props);
        SDL_DestroyProperties(props);
        require(ok, SDL_GetError());
    }
    void toggle() {
        if (!track)
            return;
        if (paused)
            MIX_ResumeTrack(track);
        else
            MIX_PauseTrack(track);
        paused = !paused;
    }
    void setVolume(double v) {
        volume = v;
        if (track)
            MIX_SetTrackGain(track, float(v * gain / 100.0));
    }
};
class Waveform : public QWidget {
  public:
    QByteArray pcm;
    int start = 0, end = 0, duration = 0;
    bool dragging = false, dragEnd = false;
    std::function<void(int, int)> changed;
    std::function<void()> finished;
    Waveform() {
        setMinimumHeight(100);
        setToolTip("Drag the left and right loop markers to set the music loop. The beginning "
                   "plays once before looping.");
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#101822"));
        p.setPen(QColor("#829abe"));
        int frames = pcm.size() / 4;
        for (int x = 0; x < width() && frames; x++) {
            int peak = 0;
            int a = (qint64)x * frames / width(),
                b = qMax(a + 1, int((qint64)(x + 1) * frames / width()));
            for (int n = a; n < b && n < frames; n++) {
                const auto *v = (const unsigned char *)pcm.constData() + n * 4;
                peak = qMax(peak, std::abs(int(int16_t(v[0] | v[1] << 8))));
            }
            int h = peak * (height() / 2 - 8) / 32768;
            p.drawLine(x, height() / 2 - h, x, height() / 2 + h);
        }
        if (duration && changed) {
            p.setPen(QColor("#74daff"));
            int a = qint64(start) * width() / duration,
                b = qint64(end ? end : duration) * width() / duration;
            p.drawLine(a, 0, a, height());
            p.drawLine(qMin(b, width() - 1), 0, qMin(b, width() - 1), height());
            p.drawText(6, 16, "Loop start / end");
        }
    }
    void mousePressEvent(QMouseEvent *e) override {
        if (!changed || !duration)
            return;
        int pos = e->position().x();
        dragEnd = std::abs(pos - qint64(end ? end : duration) * width() / duration) <
                  std::abs(pos - qint64(start) * width() / duration);
        dragging = true;
        mouseMoveEvent(e);
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (!dragging)
            return;
        int ms = qBound(0, int(e->position().x() * duration / qMax(1, width())), duration);
        if (dragEnd)
            end = qMax(start + 1, ms);
        else
            start = qMin(ms, (end ? end : duration) - 1);
        update();
        changed(start, end);
    }
    void mouseReleaseEvent(QMouseEvent *) override {
        bool wasDragging = dragging;
        dragging = false;
        if (wasDragging && finished)
            finished();
    }
};
QWidget *audioEditor(Project &project, int kind,
                     std::function<void(const QByteArray &, const QString &)> edit, QWidget *parent,
                     std::function<bool()> *flushDraft) {
    auto root = new QWidget(parent);
    root->setObjectName("audioEditor");
    auto layout = new QHBoxLayout(root);
    auto split = new QSplitter;
    layout->addWidget(split);
    auto browser = new QWidget;
    auto bl = new QVBoxLayout(browser);
    auto search = new QLineEdit;
    search->setPlaceholderText(kind ? "Find a sound effect…" : "Find music…");
    search->setToolTip("Filter audio by its name or ID.");
    bl->addWidget(search);
    auto list = new QListWidget;
    list->setObjectName("audioList");
    bl->addWidget(list, 1);
    auto add = new QPushButton(kind ? "New sound effect…" : "New track…");
    add->setToolTip("Import a new audio asset, then assign it to a music context or sound effect.");
    bl->addWidget(add);
    QPushButton *bulk = nullptr;
    if (kind) {
        bulk = new QPushButton("Export all sound effects…");
        bulk->setObjectName("exportAllSoundEffects");
        bulk->setToolTip(
            "Export every available original, replaced and custom sound as a separate WAV file.");
        bl->addWidget(bulk);
    }
    split->addWidget(browser);
    auto pane = new QWidget;
    auto pl = new QVBoxLayout(pane);
    split->addWidget(pane);
    split->setStretchFactor(1, 1);
    auto title = new QLabel;
    pl->addWidget(title);
    auto source = new QLabel;
    source->setWordWrap(true);
    pl->addWidget(source);
    auto name = new QLineEdit;
    name->setObjectName("audioName");
    name->setMaxLength(100);
    name->setToolTip("Display name in Workshop. Renaming does not change where the sound plays.");
    auto form = new QFormLayout;
    form->addRow("Name", name);
    auto gain = new QSpinBox;
    gain->setRange(0, 200);
    gain->setSuffix("%");
    gain->setObjectName("audioGain");
    gain->setToolTip("Mod playback volume relative to this asset's normal level. Preview volume "
                     "below is independent.");
    form->addRow("Volume", gain);
    pl->addLayout(form);
    auto waveform = new Waveform;
    pl->addWidget(waveform);
    auto loops = new QCheckBox("Loop music");
    loops->setToolTip("Repeat this track, preserving any introduction before the loop start.");
    loops->setVisible(!kind);
    auto start = new QDoubleSpinBox;
    auto end = new QDoubleSpinBox;
    for (auto box : {start, end}) {
        box->setRange(0, 600);
        box->setDecimals(3);
        box->setSuffix(" s");
        box->setSingleStep(0.1);
        box->setVisible(!kind);
    }
    start->setToolTip("Return to this position when the music loops.");
    end->setToolTip("Stop or loop at this position. Zero uses the end of the recording.");
    end->setSpecialValueText("End of recording");
    if (!kind) {
        auto lf = new QFormLayout;
        lf->addRow(loops);
        lf->addRow("Loop start", start);
        lf->addRow("Loop end", end);
        pl->addLayout(lf);
    }
    auto transport = new QHBoxLayout;
    auto play = new QPushButton("Play");
    auto pause = new QPushButton("Pause / resume");
    auto stop = new QPushButton("Stop");
    play->setObjectName("audioPlay");
    pause->setObjectName("audioPause");
    stop->setObjectName("audioStop");
    play->setToolTip("Preview the selected sound, including its volume and loop settings.");
    pause->setToolTip("Pause or resume the current preview.");
    stop->setToolTip("Stop preview playback.");
    for (auto b : {play, pause, stop})
        transport->addWidget(b);
    pl->addLayout(transport);
    auto volume = new QSlider(Qt::Horizontal);
    volume->setRange(0, 100);
    volume->setValue(QSettings().value("audio/previewVolume", 50).toInt());
    volume->setToolTip(
        "Editor preview volume only. Does not change the mod or the player's settings.");
    form = new QFormLayout;
    form->addRow("Preview volume", volume);
    pl->addLayout(form);
    auto controls = new QHBoxLayout;
    auto import = new QPushButton("Import audio…");
    auto exportButton = new QPushButton(kind ? "Export WAV…" : "Export audio…");
    exportButton->setObjectName("exportAudio");
    auto restore = new QPushButton("Restore original");
    import->setToolTip(
        "Import a WAV or Ogg Vorbis recording. The recording is included inside the mod.");
    exportButton->setToolTip(kind ? "Export the complete sound as a standard stereo WAV, including "
                                    "synthesized vanilla effects."
                                  : "Export the original recording in its existing format.");
    restore->setToolTip("Remove this replacement and its volume/loop changes, or delete a new "
                        "asset. Undo is available.");
    for (auto b : {import, exportButton, restore})
        controls->addWidget(b);
    pl->addLayout(controls);
    auto folderButton =
        new QPushButton(kind ? "Original recordings folder…" : "Music recordings folder…");
    folderButton->setToolTip("Locate external recordings for preview and export. This is an editor "
                             "preference; use Import to include a recording in your mod.");
    pl->addWidget(folderButton);
    auto usage = new QLabel;
    usage->setWordWrap(true);
    usage->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pl->addWidget(usage);
    auto assignment = new QFormLayout;
    auto context = new QComboBox;
    auto target = new QComboBox;
    context->setObjectName("audioContext");
    target->setObjectName("audioAssignment");
    context->setToolTip(kind ? "Choose the original sound event whose effect you want to replace."
                             : "Choose a map, scene/event, or shared music slot to override.");
    target->setToolTip(
        "Inherit the original behavior, choose silence, or assign an available asset.");
    assignment->addRow(kind ? "Sound event" : "Music context", context);
    assignment->addRow("Play", target);
    pl->addLayout(assignment);
    pl->addStretch();
    struct State {
        int id = 1;
        bool loading = false;
        QByteArray source;
        Player player;
    };
    auto state = std::make_shared<State>();
    state->player.setVolume(volume->value() / 100.0);
    auto protect = [root](const std::function<void()> &f) {
        try {
            f();
        } catch (const std::exception &e) {
            QMessageBox::warning(root, "Cannot complete audio operation",
                                 QString::fromUtf8(e.what()));
        }
    };
    auto commit = [&project, edit](const AudioClip &c) {
        auto d = AudioDocument::read(project);
        d.put(c);
        edit(d.encode(), "Edit audio");
    };
    auto fillList = [&project, kind, list, state, search, target] {
        state->loading = true;
        QSignalBlocker block(list), targets(target);
        list->clear();
        target->clear();
        target->addItem("Inherit original", -1);
        target->addItem("Silence", 0);
        auto d = AudioDocument::read(project);
        int base = kind ? 115 : 17;
        QVector<int> ids;
        for (int id = 1; id <= base; id++)
            ids.append(id);
        for (const auto &c : d.clips)
            if (c.kind == kind && c.id > base)
                ids.append(c.id);
        for (int id : ids) {
            auto c = d.clip(kind, id);
            auto item = new QListWidgetItem(QString("%1 — %2").arg(id).arg(c.name), list);
            item->setData(Qt::UserRole, id);
            item->setHidden(!item->text().contains(search->text(), Qt::CaseInsensitive));
            if (id == state->id)
                list->setCurrentItem(item);
            target->addItem(c.name, id);
        }
        state->loading = false;
    };
    auto refresh = [&project, kind, state, title, source, name, gain, waveform, loops, start, end,
                    usage] {
        QScopedValueRollback<bool> loading(state->loading, true);
        auto c = AudioDocument::read(project).clip(kind, state->id);
        title->setText(QString("%1 — %2").arg(c.id).arg(c.name));
        name->setText(c.name);
        gain->setValue(c.gain);
        loops->setChecked(c.loop);
        start->setValue(c.start / 1000.0);
        end->setValue(c.end / 1000.0);
        QString origin;
        auto bytes = audioSource(project, c, &origin);
        if (bytes != state->source) {
            state->source = bytes;
            waveform->pcm = bytes.isEmpty() ? QByteArray() : decode(bytes);
        }
        waveform->duration = waveform->pcm.size() / 192;
        waveform->start = c.start;
        waveform->end = c.end;
        waveform->update();
        source->setText(origin);
        QStringList uses;
        for (const auto &r : AudioDocument::read(project).routes)
            if (int(r.track) == c.id && ((kind && r.scope == 4) || (!kind && r.scope != 4)))
                uses.append(r.scope == 1
                                ? (r.key == 0 && r.level == 255 ? QString("Underworld")
                                                                : project.locationName(r.key))
                                : QString("%1 %2")
                                      .arg(r.scope == 2   ? "Event"
                                           : r.scope == 5 ? "Combat map"
                                                          : "Original slot")
                                      .arg(r.key));
        usage->setText("Used by: " +
                       (uses.isEmpty()
                            ? (c.id <= (kind ? 115 : 17)
                                   ? (kind ? QString::fromUtf8(audioCatalog[c.id - 1].uses)
                                           : audioName(0, c.id))
                                   : QString("Not assigned yet."))
                            : uses.join(", ")));
        state->loading = false;
    };
    auto updateRoute = [&project, context, target, state] {
        auto key = context->currentData().toList();
        if (key.size() != 3)
            return;
        int value = -1;
        for (const auto &r : AudioDocument::read(project).routes)
            if (r.scope == key[0].toUInt() && r.key == key[1].toUInt() &&
                r.level == key[2].toUInt())
                value = r.track;
        QSignalBlocker b(target);
        target->setCurrentIndex(qMax(0, target->findData(value)));
    };
    if (kind) {
        for (int id = 1; id <= 115; id++)
            context->addItem(audioName(1, id), QVariantList{4, id, 0});
    } else {
        const char *events[] = {"Main menu",
                                "Character creation",
                                "Shrine ritual",
                                "Combat",
                                "Combat victory",
                                "Camping",
                                "Ending — rescue",
                                "Ending — conclusion",
                                "Introduction",
                                "Codex interaction",
                                "Blackthorn imprisonment"};
        for (int i = 0; i < 11; i++)
            context->addItem(QString("Event: ") + events[i], QVariantList{2, i + 1, 0});
        context->addItem("World: Britannia", QVariantList{1, 0, 0});
        context->addItem("World: Underworld", QVariantList{1, 0, 255});
        for (int id = 1; id < 128; id++) {
            if (id == 41 || (id >= 42 && !MAPS_NewLocation(id)))
                continue;
            QString n = project.locationName(id);
            if (!n.isEmpty() && !n.startsWith("Location "))
                context->addItem("Map: " + n, QVariantList{1, id, 0});
        }
        for (const auto &resource : QStringList{"BRIT.CBT", "DUNGEON.CBT"})
            if (project.resources.contains(resource)) {
                const auto pages = project.mapPages(resource);
                for (int id = 0; id < pages.size(); id++)
                    context->addItem("Combat: " + pages[id].name,
                                     QVariantList{5, resource == "BRIT.CBT" ? 1 : 2, id});
            }
        for (int id = 1; id <= 17; id++)
            context->addItem("Shared slot: " + audioName(0, id), QVariantList{3, id, 0});
    }
    QObject::connect(context, qOverload<int>(&QComboBox::currentIndexChanged), root,
                     [updateRoute] { updateRoute(); });
    QObject::connect(target, qOverload<int>(&QComboBox::activated), root,
                     [&project, state, context, target, edit, protect, refresh](int) {
                         protect([&] {
                             if (state->loading)
                                 return;
                             auto key = context->currentData().toList();
                             if (key.size() != 3)
                                 return;
                             auto d = AudioDocument::read(project);
                             for (int i = d.routes.size() - 1; i >= 0; i--)
                                 if (d.routes[i].scope == key[0].toUInt() &&
                                     d.routes[i].key == key[1].toUInt() &&
                                     d.routes[i].level == key[2].toUInt())
                                     d.routes.removeAt(i);
                             int v = target->currentData().toInt();
                             if (v >= 0)
                                 d.routes.append({key[0].toUInt(), key[1].toUInt(), key[2].toUInt(),
                                                  unsigned(v)});
                             edit(d.encode(), "Assign audio");
                             refresh();
                         });
                     });
    QObject::connect(search, &QLineEdit::textChanged, root,
                     [fillList](const QString &) { fillList(); });
    QObject::connect(list, &QListWidget::currentItemChanged, root,
                     [state, refresh, updateRoute, protect, context, kind](QListWidgetItem *i) {
                         if (!i || state->loading)
                             return;
                         state->player.stop();
                         state->id = i->data(Qt::UserRole).toInt();
                         if (kind && state->id <= 115)
                             context->setCurrentIndex(
                                 context->findData(QVariantList{4, state->id, 0}));
                         protect(refresh);
                         updateRoute();
                     });
    QObject::connect(name, &QLineEdit::editingFinished, root,
                     [&project, state, name, kind, commit, fillList, protect] {
                         protect([&] {
                             if (state->loading)
                                 return;
                             auto c = AudioDocument::read(project).clip(kind, state->id);
                             if (name->text().trimmed().isEmpty())
                                 return;
                             c.name = name->text().trimmed();
                             commit(c);
                             fillList();
                         });
                     });
    QObject::connect(gain, qOverload<int>(&QSpinBox::valueChanged), root, [state](int v) {
        state->player.gain = v;
        state->player.setVolume(state->player.volume);
    });
    QObject::connect(gain, &QSpinBox::editingFinished, root,
                     [&project, state, kind, commit, protect, gain] {
                         if (state->loading)
                             return;
                         protect([&] {
                             auto c = AudioDocument::read(project).clip(kind, state->id);
                             c.gain = gain->value();
                             commit(c);
                         });
                     });
    auto setLoop = [&project, state, kind, commit, loops, start, end, waveform, protect] {
        if (state->loading)
            return;
        protect([&] {
            auto c = AudioDocument::read(project).clip(kind, state->id);
            c.loop = loops->isChecked();
            c.start = qRound(start->value() * 1000);
            c.end = qRound(end->value() * 1000);
            require(!c.end || c.start < c.end, "Loop end must be after loop start");
            require(!waveform->duration || c.start < waveform->duration,
                    "Loop start exceeds the recording");
            require(!waveform->duration || (!c.end || c.end <= waveform->duration),
                    "Loop end exceeds the recording");
            commit(c);
            waveform->start = c.start;
            waveform->end = c.end;
            waveform->update();
        });
    };
    QObject::connect(loops, &QCheckBox::toggled, root, [setLoop] { setLoop(); });
    for (auto box : {start, end})
        QObject::connect(box, &QDoubleSpinBox::editingFinished, root, setLoop);
    if (!kind)
        waveform->changed = [state, start, end](int a, int b) {
            state->loading = true;
            start->setValue(a / 1000.0);
            end->setValue(b / 1000.0);
            state->loading = false;
        };
    waveform->finished = setLoop;
    QObject::connect(volume, &QSlider::valueChanged, root, [state](int v) {
        state->player.setVolume(v / 100.0);
        QSettings().setValue("audio/previewVolume", v);
    });
    QObject::connect(play, &QPushButton::clicked, root, [&project, kind, state, protect] {
        protect([&] {
            state->player.play(state->source, AudioDocument::read(project).clip(kind, state->id));
        });
    });
    QObject::connect(pause, &QPushButton::clicked, root, [state] { state->player.toggle(); });
    QObject::connect(stop, &QPushButton::clicked, root, [state] { state->player.stop(); });
    auto importAudio = [&project, root, state, kind, commit, refresh, fillList, updateRoute,
                        protect](bool newAsset) {
        protect([&] {
            auto path =
                QFileDialog::getOpenFileName(root, "Import audio", {}, "Audio (*.wav *.ogg)");
            if (path.isEmpty())
                return;
            auto bytes = readFile(path);
            auto pcm = decode(bytes);
            AudioClip c = AudioDocument::read(project).clip(kind, state->id);
            if (newAsset) {
                auto d = AudioDocument::read(project);
                c = {};
                c.kind = kind;
                c.loop = !kind;
                c.id = kind ? 116 : 18;
                for (const auto &a : d.clips)
                    if (a.kind == kind)
                        c.id = qMax(c.id, a.id + 1);
                c.name = QFileInfo(path).completeBaseName();
            }
            c.bytes = bytes;
            c.start = c.end = 0;
            commit(c);
            state->id = c.id;
            state->player.stop();
            fillList();
            refresh();
            updateRoute();
        });
    };
    QObject::connect(import, &QPushButton::clicked, root, [importAudio] { importAudio(false); });
    QObject::connect(add, &QPushButton::clicked, root, [importAudio] { importAudio(true); });
    QObject::connect(exportButton, &QPushButton::clicked, root,
                     [&project, root, state, kind, protect] {
                         protect([&] {
                             auto c = AudioDocument::read(project).clip(kind, state->id);
                             auto bytes = audioSource(project, c);
                             require(!bytes.isEmpty(), "No source audio available");
                             QString suffix = kind                       ? "wav"
                                              : bytes.startsWith("OggS") ? "ogg"
                                              : bytes.startsWith("RIFF") ? "wav"
                                              : bytes.startsWith("fLaC") ? "flac"
                                                                         : "mp3";
                             auto path = saveFileName(root, "Export audio", c.name + "." + suffix,
                                                      "Audio (*." + suffix + ")", suffix);
                             if (!path.isEmpty())
                                 save(path, kind ? audioWav(bytes) : bytes);
                         });
                     });
    QObject::connect(
        restore, &QPushButton::clicked, root,
        [&project, root, state, kind, edit, fillList, refresh, updateRoute, protect] {
            protect([&] {
                if (QMessageBox::question(root, "Restore audio",
                                          "Remove this asset's changes? Assignments to a deleted "
                                          "custom asset will inherit their original behavior.") !=
                    QMessageBox::Yes)
                    return;
                auto d = AudioDocument::read(project);
                for (int i = d.clips.size() - 1; i >= 0; i--)
                    if (d.clips[i].kind == kind && d.clips[i].id == state->id)
                        d.clips.removeAt(i);
                if (state->id > (kind ? 115 : 17)) {
                    for (int i = d.routes.size() - 1; i >= 0; i--)
                        if (int(d.routes[i].track) == state->id &&
                            ((kind && d.routes[i].scope == 4) || (!kind && d.routes[i].scope != 4)))
                            d.routes.removeAt(i);
                    state->id = 1;
                }
                edit(d.encode(), "Restore audio");
                state->player.stop();
                fillList();
                refresh();
                updateRoute();
            });
        });
    if (bulk)
        QObject::connect(bulk, &QPushButton::clicked, root, [&project, root, protect] {
            protect([&] {
                auto folder =
                    QFileDialog::getExistingDirectory(root, "Export all sound effects as WAV");
                if (folder.isEmpty())
                    return;
                if (!QDir(folder).entryList({"*.wav"}, QDir::Files).isEmpty() &&
                    QMessageBox::question(
                        root, "Replace WAV files?",
                        "Existing exports with matching filenames will be replaced. Continue?") !=
                        QMessageBox::Yes)
                    return;
                QApplication::setOverrideCursor(Qt::WaitCursor);
                QStringList skipped;
                try {
                    skipped = exportSoundEffects(project, folder);
                } catch (...) {
                    QApplication::restoreOverrideCursor();
                    throw;
                }
                QApplication::restoreOverrideCursor();
                QMessageBox::information(
                    root, "Sound effects exported",
                    skipped.isEmpty()
                        ? "All sound effects exported as WAV files."
                        : "Available sound effects exported. No source recording exists for:\n" +
                              skipped.join("\n"));
            });
        });
    QObject::connect(
        folderButton, &QPushButton::clicked, root, [root, kind, refresh, protect, state] {
            auto path = QFileDialog::getExistingDirectory(root, "Locate original audio recordings");
            if (path.isEmpty())
                return;
            QSettings().setValue(kind ? "audio/soundFolder" : "audio/musicFolder", path);
            state->player.stop();
            protect(refresh);
        });
    if (flushDraft) {
        QPointer<QWidget> alive(root);
        *flushDraft = [alive, &project, state, kind, name, gain, loops, start, end, waveform,
                       commit] {
            if (!alive)
                return true;
            try {
                auto c = AudioDocument::read(project).clip(kind, state->id);
                auto text = name->text().trimmed();
                int a = kind ? 0 : qRound(start->value() * 1000),
                    b = kind ? 0 : qRound(end->value() * 1000);
                bool repeat = !kind && loops->isChecked();
                require(!text.isEmpty(), "Audio needs a name");
                require(!b || a < b, "Loop end must be after loop start");
                require(!waveform->duration || a < waveform->duration,
                        "Loop start exceeds the recording");
                require(!waveform->duration || !b || b <= waveform->duration,
                        "Loop end exceeds the recording");
                if (c.name != text || c.gain != gain->value() || c.start != a || c.end != b ||
                    c.loop != repeat) {
                    c.name = text;
                    c.gain = gain->value();
                    c.start = a;
                    c.end = b;
                    c.loop = repeat;
                    commit(c);
                }
                return true;
            } catch (const std::exception &e) {
                QMessageBox::warning(alive, "Cannot save audio settings",
                                     QString::fromUtf8(e.what()));
                return false;
            }
        };
    }
    fillList();
    protect(refresh);
    updateRoute();
    split->setSizes({280, 750});
    return root;
}
} // namespace Workshop
