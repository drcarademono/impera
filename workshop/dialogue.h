#pragma once
#include "formats.h"
#include <QSet>
namespace Dialogue {
struct Entry {
    QByteArray bytes;
    bool terminated = true;
    int offset = 0;
};
struct Rule {
    QVector<int> keywords;
    int response = -1;
};
struct Question {
    int label = 0, prompt = -1, fallback = -1;
    QVector<Rule> replies;
};
struct Issue {
    bool error;
    int entry;
    QString message;
};
struct Block {
    QByteArray bytes;
    int opcode = -1;
}; // -1: text, otherwise instruction
struct Document {
    QVector<Entry> entries;
    QVector<Rule> topics;
    QVector<Question> questions;
    QSet<int> rawEntries;
    int tail = -1;
    QString structuralError;
    QByteArray bytes() const;
    QVector<Issue> issues() const;
    int question(int label) const;
    int freeLabel() const;
};
Document parse(const QByteArray &bytes);
QVector<Block> blocks(const QByteArray &bytes);
int operandCount(int opcode);
QString plain(const QByteArray &bytes);
QByteArray text(const QString &plain);
QString actionName(int opcode);
QMap<int, QString> inventoryOptions();
QByteArray action(int opcode, int value = 0, int target = 0);
QString keyword(const QByteArray &bytes);
QByteArray encodeKeyword(const QString &value);
QByteArray replaceAliases(const Document &doc, const Rule &rule, const QStringList &aliases);
QByteArray insertRule(const Document &doc, int before, const QString &keyword);
QByteArray addQuestion(const Document &doc);
QByteArray removeQuestion(const Document &doc, int label, int retarget = 0);
QVector<int> references(const Document &doc, int label);
struct Sandbox {
    QString avatar = "Avatar";
    QStringList partyNames = {"Avatar"};
    bool introduced = false, announceName = true;
    int karma = 50, gold = 100, partySize = 3;
};
struct Line {
    QString text;
    int entry = -1;
    bool trace = false;
};
class Simulator {
  public:
    Simulator(const Document &document, const Sandbox &initial);
    QVector<Line> start();
    QVector<Line> respond(const QString &input);
    Sandbox state;
    bool ended = false;

  private:
    Document doc;
    bool redirected = false;
    int currentQuestion = -1, askEntry = -1, askOffset = 0, steps = 0;
    QVector<Line> output;
    void execute(int entry, int offset = 0);
    void enter(int label);
    bool match(const QByteArray &key, const QString &input);
};
} // namespace Dialogue
