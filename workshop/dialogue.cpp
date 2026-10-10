#include "dialogue.h"
#include "inventory_names.h"
#include <QRegularExpression>
#include <algorithm>
#include <functional>
namespace Dialogue {
int operandCount(int c) { return c == 133 ? 3 : c == 134 || c == 140 ? 1 : c == 254 ? 2 : 0; }
QVector<Block> blocks(const QByteArray &b) {
    QVector<Block> out;
    for (int i = 0; i < b.size();) {
        int c = U5::byte(b, i), n = operandCount(c);
        U5::require(i + n < b.size(), "Truncated dialogue instruction");
        bool instruction = (c >= 129 && c <= 140) || c == 142 || c == 143 ||
                           (c >= 145 && c <= 159) || c >= 254 || c == 144;
        if (!instruction && !out.isEmpty() && out.last().opcode == -1)
            out.last().bytes += b.mid(i, 1);
        else
            out.append({b.mid(i, n + 1), instruction ? c : -1});
        i += n + 1;
    }
    return out;
}
QByteArray Document::bytes() const {
    QByteArray b;
    for (const auto &e : entries) {
        b += e.bytes;
        if (e.terminated)
            b += char(0);
    }
    return b;
}
int Document::question(int label) const {
    for (int i = 0; i < questions.size(); ++i)
        if (questions[i].label == label)
            return i;
    return -1;
}
int Document::freeLabel() const {
    for (int i = 1; i <= 15; ++i)
        if (question(i) < 0 && !(i == 15 && tail >= 0))
            return i;
    return 0;
}
Document parse(const QByteArray &bytes) {
    Document d;
    int begin = 0;
    try {
        for (int i = 0; i < bytes.size();) {
            int c = U5::byte(bytes, i), n = operandCount(c);
            U5::require(i + n < bytes.size(), "Truncated dialogue instruction");
            if (c == 0) {
                d.entries.append({bytes.mid(begin, i - begin), true, begin});
                begin = ++i;
            } else {
                for (int j = 1; j <= n; ++j)
                    U5::require(bytes[i + j] != 0, "NUL in instruction operands");
                i += n + 1;
            }
        }
        if (begin < bytes.size())
            d.entries.append({bytes.mid(begin), false, begin});
        for (int i = 5; i < d.entries.size(); ++i) {
            if (d.entries[i].bytes == QByteArray::fromHex("909fc0")) {
                bool last = true;
                for (int j = i + 1; j < d.entries.size(); ++j)
                    if (!d.entries[j].bytes.isEmpty())
                        last = false;
                if (last)
                    d.tail = i;
            }
        }
        if (d.entries.size() < 5) {
            d.structuralError = "Missing the five fixed conversation entries";
            return d;
        }
        auto rule = [&](int &i, int end) -> Rule {
            Rule r;
            do {
                U5::require(i + 1 < end, "Keyword lacks its response");
                r.keywords.append(i);
                ++i;
                if (d.entries[i].bytes != QByteArray(1, char(135)))
                    break;
                ++i;
            } while (i < end);
            U5::require(i < end, "Alias chain lacks a response");
            r.response = i++;
            return r;
        };
        int i = 5, end = d.tail >= 0 ? d.tail : d.entries.size();
        while (end > i && d.entries[end - 1].bytes.isEmpty())
            --end;
        auto isQuestion = [&](int e) {
            auto b = d.entries[e].bytes;
            return b.size() >= 2 && U5::byte(b, 0) == 144 && U5::byte(b, 1) >= 145 &&
                   U5::byte(b, 1) <= 159;
        };
        int topicEnd = i;
        while (topicEnd < end && !isQuestion(topicEnd))
            ++topicEnd;
        while (i < topicEnd) {
            int beginRule = i;
            try {
                d.topics.append(rule(i, topicEnd));
            } catch (const std::exception &) {
                for (int e = beginRule; e < topicEnd; ++e)
                    d.rawEntries.insert(e);
                i = topicEnd;
            }
        }
        while (i < end) {
            U5::require(isQuestion(i), "Unrecognized question section; use Advanced source");
            Question q;
            q.label = U5::byte(d.entries[i].bytes, 1) - 144;
            q.prompt = i++;
            if (i >= end || isQuestion(i)) {
                bool exits = false;
                for (auto b : blocks(d.entries[q.prompt].bytes.mid(2)))
                    if (b.opcode == 130 || b.opcode == 132 || b.opcode == 255 ||
                        (b.opcode >= 145 && b.opcode <= 159))
                        exits = true;
                U5::require(exits, "Question lacks its fallback response");
                d.questions.append(q);
                continue;
            }
            q.fallback = i++;
            int limit = i;
            while (limit < end && !isQuestion(limit))
                ++limit;
            while (i < limit) {
                if (i + 1 >= limit) {
                    d.rawEntries.insert(i++);
                    break;
                }
                q.replies.append(rule(i, limit));
            }
            d.questions.append(q);
        }
    } catch (const std::exception &e) {
        d.structuralError = QString::fromUtf8(e.what());
        // Always retain all source bytes, even if a malformed instruction prevents
        // tokenization.
        if (d.bytes() != bytes) {
            d.entries.clear();
            d.entries.append({bytes, false, 0});
        }
    }
    return d;
}
QString plain(const QByteArray &b) {
    QString out = U5::decodeText(b);
    // Dictionary expansion mirrors the engine's implicit surrounding word
    // spacing.
    static QRegularExpression tag("<([^>]+)>");
    QString result;
    int last = 0;
    auto matches = tag.globalMatch(out);
    while (matches.hasNext()) {
        auto m = matches.next();
        result += out.mid(last, m.capturedStart() - last);
        QString t = m.captured(1);
        if (t == "New Line")
            result += '\n';
        else if (t == "Byte 188")
            result += '<';
        else if (t == "Byte 190")
            result += '>';
        else if (!t.startsWith("Byte ") && !t.startsWith("Label ") &&
                 !U5::textTags().contains(m.captured())) {
            if (!result.isEmpty() && !result.endsWith(' ') && !result.endsWith('\n'))
                result += ' ';
            result += t;
            result += ' ';
        } else
            result += m.captured();
        last = m.capturedEnd();
    }
    result += out.mid(last);
    return result;
}
QByteArray text(const QString &s) {
    QByteArray out;
    for (auto c : s) {
        if (c == '\n')
            out += char(141);
        else {
            U5::require(c.unicode() >= 32 && c.unicode() <= 126, "Conversation text must be ASCII");
            out += char(c.unicode() + 128);
        }
    }
    // Compress only complete canonical words, retaining literal spaces around
    // tokens. The original token spelling/case is determined by the native codec.
    for (int code = 1; code <= 128; ++code) {
        QString decoded = U5::decodeText(QByteArray(1, char(code)));
        if (!decoded.startsWith('<') || decoded.startsWith("<Byte"))
            continue;
        QString word = decoded.mid(1, decoded.size() - 2);
        QByteArray literal;
        for (auto c : word)
            literal += char(c.unicode() + 128);
        QByteArray bounded = QByteArray(1, char(160)) + literal + char(160);
        out.replace(bounded, QByteArray(1, char(code)));
    }
    return out;
}
QString keyword(const QByteArray &b) {
    QString s;
    for (auto c : b)
        s += QChar(static_cast<unsigned char>(c) & 127);
    return s;
}
QByteArray encodeKeyword(const QString &s) {
    U5::require(!s.trimmed().isEmpty() && s == s.trimmed(),
                "Keyword must be nonempty without surrounding spaces");
    QByteArray out;
    for (auto c : s) {
        U5::require(c.unicode() >= 32 && c.unicode() <= 126, "Keyword must be ASCII text");
        out += char(c.unicode() + 128);
    }
    return out;
}
QString actionName(int c) {
    switch (c) {
    case 129:
        return "Avatar name";
    case 130:
        return "End conversation";
    case 131:
        return "Pause";
    case 132:
        return "Attempt party joining";
    case 133:
        return "Attempt gold payment";
    case 134:
        return "Give inventory item";
    case 135:
        return "Alias continuation (advanced)";
    case 136:
        return "Ask player's name";
    case 137:
        return "Karma +1";
    case 138:
        return "Karma −1";
    case 139:
        return "Call guards";
    case 140:
        return "If introduced, branch";
    case 142:
        return "Toggle rune text";
    case 143:
        return "Wait for key";
    case 254:
        return "If karma at least, ask";
    case 255:
        return "Return to ordinary topics";
    default:
        return c >= 145 && c <= 159 ? QString("Ask question %1").arg(c - 144) : "Raw instruction";
    }
}
QMap<int, QString> inventoryOptions() {
    QMap<int, QString> names;
    for (unsigned i = 0; i < sizeof(engineEquipmentNames) / sizeof(*engineEquipmentNames); ++i)
        names[int(i)] = QString::fromLatin1(engineEquipmentNames[i]);
    QStringList supplies = {"Food",     "Gold",        "Keys",         "Gems",
                            "Torches",  "Grapple",     "Magic Carpet", "Sextant",
                            "Spyglass", "Black Badge", "Skull Key"};
    for (int i = 0; i < supplies.size(); ++i)
        names[65 + i] = supplies[i];
    return names;
}
QByteArray action(int c, int v, int target) {
    QByteArray b(1, char(c));
    if (c == 133) {
        U5::require(v >= 0 && v <= 999, "Payment must be 0..999");
        for (auto x : QString::number(v).rightJustified(3, '0'))
            b += char(x.unicode() + 128);
    }
    if (c == 134) {
        U5::require(v >= 0 && v <= 127, "Inventory operand must be 0..127");
        b += char(v + 128);
    }
    if (c == 140) {
        U5::require(target == 0 || (target >= 1 && target <= 15), "Invalid question");
        b += char(target == 0 ? 255 : 144 + target);
    }
    if (c == 254) {
        U5::require(v >= 1 && v <= 255 && target >= 1 && target <= 15, "Invalid karma condition");
        b += char(v);
        b += char(144 + target);
    }
    return b;
}
QVector<int> references(const Document &d, int label) {
    QVector<int> r;
    for (int e = 0; e < d.entries.size(); ++e) {
        if (e == d.tail)
            continue;
        auto b = d.entries[e].bytes;
        if (b.startsWith(QByteArray::fromHex("90")) && b.size() >= 2)
            b = b.mid(2);
        for (auto x : blocks(b)) {
            int dest = x.opcode >= 145 && x.opcode <= 159 ? x.opcode - 144
                       : x.opcode == 140                  ? U5::byte(x.bytes, 1) - 144
                       : x.opcode == 254                  ? U5::byte(x.bytes, 2) - 144
                                                          : 0;
            if (dest == label) {
                r.append(e);
                break;
            }
        }
    }
    return r;
}
QVector<Issue> Document::issues() const {
    QVector<Issue> out;
    if (!structuralError.isEmpty()) {
        out.append({true, -1, structuralError});
        return out;
    }
    if (bytes().size() > 1024)
        out.append({true, -1, "Conversation exceeds the 1024-byte engine buffer"});
    for (int e : rawEntries)
        out.append({false, e,
                    "Unpaired/unknown section retained as raw bytes; edit in "
                    "Advanced source"});
    for (int e = 0; e < entries.size(); ++e)
        if (e != tail && !entries[e].terminated)
            out.append({true, e, "Executable text entry lacks a NUL terminator"});
    if (tail < 0)
        out.append({false, -1,
                    "No conventional structural tail; Advanced source can retain "
                    "custom endings"});
    QSet<int> labels;
    for (auto q : questions) {
        if (labels.contains(q.label))
            out.append({true, q.prompt, "Duplicate question label"});
        labels.insert(q.label);
    }
    for (int e = 0; e < entries.size(); ++e) {
        if (e == tail)
            continue;
        auto b = entries[e].bytes;
        if (b.size() >= 2 && U5::byte(b, 0) == 144)
            b = b.mid(2);
        bool terminal = false;
        for (auto x : blocks(b)) {
            if (terminal)
                out.append({false, e, "Text/action follows a transfer or terminal action"});
            int dest = x.opcode >= 145 && x.opcode <= 159 ? x.opcode - 144
                       : x.opcode == 140                  ? U5::byte(x.bytes, 1) - 144
                       : x.opcode == 254                  ? U5::byte(x.bytes, 2) - 144
                                                          : 0;
            if (dest > 0 && dest <= 15 && !labels.contains(dest))
                out.append({true, e, QString("Missing question %1").arg(dest)});
            if (x.opcode == 134 && (U5::byte(x.bytes, 1) & 127) >= 48 &&
                (U5::byte(x.bytes, 1) & 127) < 64)
                out.append({false, e,
                            "Inventory operand extends beyond the equipment array; "
                            "engine writes adjacent state"});
            if (x.opcode == 133) {
                for (int j = 1; j <= 3; ++j) {
                    int c = U5::byte(x.bytes, j) & 127;
                    if (c < '0' || c > '9') {
                        out.append({true, e, "Payment operands must be three digits"});
                        break;
                    }
                }
            }
            terminal = x.opcode == 130 || x.opcode == 255 || (x.opcode >= 145 && x.opcode <= 159);
        }
    }
    auto rules = [&](const QVector<Rule> &rs) {
        QStringList keys;
        for (auto r : rs)
            for (int e : r.keywords) {
                QString k = keyword(entries[e].bytes).toUpper();
                if (k.isEmpty() || k.size() >= 9)
                    out.append(
                        {false, e, "Empty/long keyword: engine comparison boundary is unsafe"});
                if (QStringList{"NAME", "JOB", "WORK", "BYE", "THANK"}.contains(k))
                    out.append({false, e, "Built-in topic is dispatched before custom rules"});
                for (auto prev : keys)
                    if (prev.startsWith(k) || k.startsWith(prev))
                        out.append({false, e, "Overlapping keywords: first matching rule wins"});
                keys << k;
            }
    };
    rules(topics);
    for (auto q : questions)
        rules(q.replies);
    for (auto q : questions) {
        QSet<int> visited;
        std::function<bool(int)> cycle = [&](int label) {
            if (visited.contains(label))
                return false;
            visited.insert(label);
            int index = question(label);
            if (index < 0)
                return false;
            auto node = questions[index];
            int end = index + 1 < questions.size() ? questions[index + 1].prompt
                      : tail >= 0                  ? tail
                                                   : entries.size();
            for (auto target : questions)
                for (int ref : references(*this, target.label))
                    if (ref >= node.prompt && ref < end) {
                        if (target.label == q.label)
                            return true;
                        if (cycle(target.label))
                            return true;
                    }
            return false;
        };
        if (cycle(q.label))
            out.append({false, q.prompt,
                        "Question participates in a cycle; sandbox execution is bounded"});
    }
    for (auto q : questions)
        if (references(*this, q.label).isEmpty())
            out.append({false, q.prompt, "Question has no incoming references"});
    return out;
}
QByteArray replaceAliases(const Document &d, const Rule &r, const QStringList &keys) {
    U5::require(!keys.isEmpty(), "Topic needs at least one keyword");
    auto n = d;
    int begin = r.keywords.first(), count = r.response - begin;
    for (int i = 0; i < count; ++i)
        n.entries.removeAt(begin);
    int at = begin;
    for (int i = 0; i < keys.size(); ++i) {
        n.entries.insert(at++, {encodeKeyword(keys[i]), true, 0});
        if (i + 1 < keys.size())
            n.entries.insert(at++, {QByteArray(1, char(135)), true, 0});
    }
    return n.bytes();
}
QByteArray insertRule(const Document &d, int before, const QString &key) {
    auto n = d;
    n.entries.insert(before, {encodeKeyword(key), true, 0});
    n.entries.insert(before + 1, {text("New response."), true, 0});
    return n.bytes();
}
QByteArray addQuestion(const Document &d) {
    int l = d.freeLabel();
    U5::require(l > 0, "No available question labels");
    auto n = d;
    int at = d.tail >= 0 ? d.tail : d.entries.size();
    n.entries.insert(at,
                     {QByteArray(1, char(144)) + char(144 + l) + text("New question?"), true, 0});
    n.entries.insert(at + 1, {text("I cannot help thee with that."), true, 0});
    return n.bytes();
}
QByteArray removeQuestion(const Document &d, int label, int retarget) {
    int q = d.question(label);
    U5::require(q >= 0, "Question missing");
    U5::require(retarget == 0 || d.question(retarget) >= 0, "Retarget question missing");
    U5::require(retarget != label, "Cannot retarget to the deleted question");
    U5::require(retarget != 0 || references(d, label).isEmpty(),
                "Question is referenced; select a destination for its callers");
    auto n = d;
    if (retarget)
        for (int e = 0; e < n.entries.size(); ++e) {
            if (e == n.tail)
                continue;
            auto bs = blocks(n.entries[e].bytes);
            QByteArray b;
            for (int i = 0; i < bs.size(); ++i) {
                auto x = bs[i];
                if (!(i == 1 && !bs.isEmpty() && bs[0].opcode == 144)) {
                    if (x.opcode == 144 + label)
                        x.bytes[0] = char(144 + retarget);
                    if (x.opcode == 140 && U5::byte(x.bytes, 1) == unsigned(144 + label))
                        x.bytes[1] = char(144 + retarget);
                    if (x.opcode == 254 && U5::byte(x.bytes, 2) == unsigned(144 + label))
                        x.bytes[2] = char(144 + retarget);
                }
                b += x.bytes;
            }
            n.entries[e].bytes = b;
        }
    int begin = d.questions[q].prompt, end = q + 1 < d.questions.size() ? d.questions[q + 1].prompt
                                             : d.tail >= 0              ? d.tail
                                                                        : d.entries.size();
    for (int i = begin; i < end; ++i)
        n.entries.removeAt(begin);
    return n.bytes();
}
Simulator::Simulator(const Document &d, const Sandbox &s) : state(s), doc(d) {}
bool Simulator::match(const QByteArray &b, const QString &input) {
    QString k = keyword(b).toUpper();
    if (k.isEmpty() || k.size() >= 9) {
        output.append({"Unsafe keyword boundary; simulator skips this rule", -1, true});
        return false;
    }
    return input.toUpper().startsWith(k);
}
void Simulator::enter(int label) {
    int q = doc.question(label);
    if (q < 0) {
        output.append({QString("Missing question %1").arg(label), -1, true});
        ended = true;
        return;
    }
    currentQuestion = q;
    output.append({QString("Enter question %1").arg(label), doc.questions[q].prompt, true});
    execute(doc.questions[q].prompt, 2);
}
void Simulator::execute(int e, int offset) {
    if (e < 0 || e >= doc.entries.size()) {
        ended = true;
        return;
    }
    auto bs = blocks(doc.entries[e].bytes.mid(offset));
    int pos = offset;
    for (auto b : bs) {
        if (++steps > 512) {
            output.append({"Stopped after 512 instructions (possible loop)", e, true});
            ended = true;
            return;
        }
        int c = b.opcode;
        pos += b.bytes.size();
        if (c < 0) {
            QString s = plain(b.bytes);
            if (!s.isEmpty())
                output.append({s, e, false});
            continue;
        }
        if (c == 129)
            output.append({state.avatar, e, false});
        else if (c == 130) {
            ended = true;
            output.append({"Conversation ended", e, true});
            return;
        } else if (c >= 145 && c <= 159) {
            enter(c - 144);
            return;
        } else if (c == 255) {
            redirected = true;
            currentQuestion = -1;
            output.append({"Return to ordinary topics", e, true});
            return;
        } else if (c == 140 && state.introduced) {
            int dest = U5::byte(b.bytes, 1);
            if (dest == 255) {
                redirected = true;
                currentQuestion = -1;
                return;
            }
            enter(dest - 144);
            return;
        } else if (c == 254 && state.karma >= int(U5::byte(b.bytes, 1))) {
            enter(U5::byte(b.bytes, 2) - 144);
            return;
        } else if (c == 136) {
            askEntry = e;
            askOffset = pos;
            output.append({"What is thy name?", e, false});
            return;
        } else if (c == 133) {
            int value = 0;
            for (int j = 1; j <= 3; ++j)
                value = value * 10 + (U5::byte(b.bytes, j) & 127) - '0';
            if (value < 0 || value > 999) {
                ended = true;
                return;
            }
            if (state.gold < value) {
                output.append({"Thou hast not enough gold!", e, false});
                currentQuestion = -1;
                return;
            }
            state.gold -= value;
            output.append({QString("Paid %1 gold (sandbox only)").arg(value), e, true});
        } else if (c == 137)
            state.karma = qMin(99, state.karma + 1);
        else if (c == 138)
            state.karma = qMax(0, state.karma - 1);
        else if (c == 132) {
            output.append(
                {"Party joining depends on actual recruit records; not simulated", e, true});
            ended = true;
            return;
        } else if (c == 139)
            output.append({"Call guards: world effect not simulated", e, true});
        else if (c == 134)
            output.append({QString("Give inventory operand %1: not persisted")
                               .arg(U5::byte(b.bytes, 1) & 127),
                           e, true});
        else if (c == 131 || c == 143 || c == 142)
            output.append({actionName(c), e, true});
        else if (c != 140 && c != 254) {
            output.append({"Unsupported instruction; simulation stopped", e, true});
            ended = true;
            return;
        }
    }
}
QVector<Line> Simulator::start() {
    output.clear();
    steps = 0;
    execute(1);
    if (!ended && !redirected && askEntry < 0 && currentQuestion < 0) {
        if (state.introduced)
            execute(2);
        else if (state.announceName)
            execute(0);
    }
    return output;
}
QVector<Line> Simulator::respond(const QString &raw) {
    output.clear();
    steps = 0;
    if (ended) {
        output.append({"Reset to start another conversation", -1, true});
        return output;
    }
    QString s = raw.toUpper().left(15);
    if (askEntry >= 0) {
        for (auto n : state.partyNames)
            if (s.startsWith(n.left(4).toUpper()))
                state.introduced = true;
        output.append(
            {state.introduced ? "Name recognized" : "Name not recognized", askEntry, true});
        int e = askEntry, o = askOffset;
        askEntry = -1;
        execute(e, o);
        return output;
    }
    if (s.isEmpty() && currentQuestion >= 0) {
        output.append({"What didst thou say?", -1, false});
        return output;
    }
    if (s.isEmpty() || s.startsWith("BYE") || s.startsWith("THANK")) {
        execute(4);
        if (!ended)
            ended = true;
        return output;
    }
    if (currentQuestion < 0 &&
        (s.startsWith("NAME") || s.startsWith("JOB") || s.startsWith("WORK"))) {
        execute(s.startsWith("NAME") ? 0 : 3);
        return output;
    }
    auto rs = currentQuestion < 0 ? doc.topics : doc.questions[currentQuestion].replies;
    for (auto r : rs)
        for (int e : r.keywords)
            if (match(doc.entries[e].bytes, s)) {
                output.append(
                    {QString("Matched keyword %1").arg(keyword(doc.entries[e].bytes)), e, true});
                currentQuestion = -1;
                execute(r.response);
                return output;
            }
    if (currentQuestion >= 0) {
        int e = doc.questions[currentQuestion].fallback;
        currentQuestion = -1;
        execute(e);
    } else
        output.append({"I cannot help thee with that.", -1, false});
    return output;
}
} // namespace Dialogue
