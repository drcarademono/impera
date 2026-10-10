#pragma once
#include "dialogue.h"
#include "project.h"
#include <QtWidgets>
#include <functional>
class ConversationEditor : public QWidget {
  public:
    ConversationEditor(Project *project, const QString &resource,
                       std::function<void(const QByteArray &, const QString &)> commit,
                       std::function<void(const QString &, const QString &)> rename, int npc,
                       int entry, std::function<void(int, int)> selection,
                       QWidget *parent = nullptr);
    bool flush();

  private:
    Project *project;
    QString resource;
    QVector<U5::Conversation> conversations;
    Dialogue::Document doc;
    std::function<void(const QByteArray &, const QString &)> commit;
    std::function<void(int, int)> selection;
    std::function<void(const QString &, const QString &)> rename;
    QString questionName(int label) const;
    void renameQuestion();
    void moveTopic(int delta, bool duplicate);
    QTreeWidget *npcs, *outline;
    QLineEdit *search, *aliases;
    QLabel *budget, *context, *status;
    QTabWidget *tabs, *previewTabs;
    QPlainTextEdit *source, *readable;
    QComboBox *sourceScope;
    bool wholeSource = false;
    void refreshSource();
    QLabel *dosImage;
    QWidget *blocksWidget;
    QVBoxLayout *blocksLayout;
    QTimer timer;
    QVector<std::function<QByteArray()>> readers;
    QVector<Dialogue::Block> currentBlocks;
    QVector<Dialogue::Issue> issues;
    QListWidget *diagnostics;
    QTreeWidget *transcript;
    QLineEdit *input, *avatar;
    QCheckBox *introduced, *announce;
    QSpinBox *gold, *karma, *partySize;
    std::unique_ptr<Dialogue::Simulator> simulation;
    int npcIndex = -1, selected = -1, prefix = 0;
    bool loading = false, pending = false, sourceDraft = false, aliasesDraft = false;
    QByteArray sourceBaseline;
    void loadNpc(int index, int entry = 0);
    void buildOutline(int selectEntry);
    void loadEntry(int entry);
    void buildBlocks();
    void changed();
    bool applyDraft();
    void install(const QByteArray &bytes, const QString &description, int selectEntry = -1);
    void updateDiagnostics();
    void mutateBlocks(int block, int delta, bool remove);
    void addBlock(bool isText);
    void aliasesChanged();
    void addTopic(bool reply);
    void deleteSelected();
    void resetSimulation();
    void appendTranscript(const QVector<Dialogue::Line> &lines);
    bool attempt(const std::function<void()> &operation);
};
