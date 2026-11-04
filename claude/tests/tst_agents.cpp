// The Claude agents list: reading the command's output, finding repositories,
// the status, the fetch through a process, and the view. The test program also
// stands in for Claude Code: run with "agents", it prints a list of agents
// whose account is the CLAUDE_CONFIG_DIR it was given.
#include "../ClaudeAgents.h"
#include "../ClaudeAgentsFetch.h"
#include "../ClaudeAgentsView.h"

#include <mirante/Status.h>
#include <mirante/testing/FakeItemContext.h>
#include <mirante/testing/FakeStatusJob.h>
#include <mirante/widgets/ItemListView.h>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QTimeZone>

#include <cstdio>
#include <cstring>
#include <memory>

using namespace mirante::modules::claude;

namespace {

const QByteArray kSample = R"([
  {"pid": 1, "cwd": "/work/app", "kind": "interactive", "startedAt": 1793811905069,
   "sessionId": "s-1", "name": "Fix the dialogs", "status": "waiting"},
  {"id": "3cd7cf87", "cwd": "/work/other", "kind": "background", "startedAt": 1791323357385,
   "name": "Clear state", "state": "done", "future": {"field": true}},
  "not an object",
  {"sessionId": "s-3", "status": "busy"}
])";

// What the stand-in prints: two agents in the default account, one in another.
int runAsClaude()
{
    const QByteArray account = qgetenv("CLAUDE_CONFIG_DIR");
    QByteArray out;
    if (account.isEmpty())
        out = R"([{"sessionId":"a","name":"One","status":"busy","cwd":""},)"
              R"({"sessionId":"b","name":"Two","status":"waiting","cwd":""}])";
    else if (account.endsWith("broken"))
        out = "not json";
    else
        out = R"([{"sessionId":"c","name":"Three","status":"idle","cwd":""}])";
    std::fwrite(out.constData(), 1, size_t(out.size()), stdout);
    return 0;
}

QString testProgram()
{
    return QCoreApplication::applicationFilePath();
}

// Makes a file with text, and its folders.
void writeFile(const QString &path, const QByteArray &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text);
}

class FakeStatusSource : public mirante::ItemStatusSource
{
public:
    mirante::ItemStatus status;
    std::function<void(const mirante::ItemStatus &)> changed;

    mirante::ItemStatus current() const override { return status; }
    void refresh() override {}
    void subscribe(QObject *receiver, std::function<void(const mirante::ItemStatus &)> callback) override
    {
        Q_UNUSED(receiver)
        changed = std::move(callback);
    }
    void send(const mirante::ItemStatus &next)
    {
        status = next;
        if (changed)
            changed(next);
    }
};

} // namespace

class TestAgents : public QObject
{
    Q_OBJECT

private slots:
    // The fetch reads the bridge's folder: never the user's own.
    void initTestCase()
    {
        QVERIFY(m_bridge.isValid());
        qputenv("MIRANTE_CLAUDE_DIR", m_bridge.path().toUtf8());
    }

    void readsTheBridgeFolder()
    {
        QTemporaryDir temp;
        const QString folder = temp.path();
        const QDateTime now = QDateTime::currentDateTimeUtc();
        writeFile(folder + QStringLiteral("/s-1.hook.json"), R"({"event":"PreToolUse","tool":"Bash"})");
        writeFile(folder + QStringLiteral("/s-1.status.json"), R"({"session_id":"s-1"})");
        writeFile(folder + QStringLiteral("/s-2.status.json"), "not json");
        writeFile(folder + QStringLiteral("/s-3.hook.json"), R"({"event":"Stop"})");
        writeFile(folder + QStringLiteral("/other.json"), "{}");
        writeFile(folder + QStringLiteral("/.tmp.123"), "{}");
        {
            QFile old(folder + QStringLiteral("/s-3.hook.json"));
            QVERIFY(old.open(QIODevice::ReadWrite));
            QVERIFY(old.setFileTime(now.addDays(-8).toLocalTime(), QFileDevice::FileModificationTime));
        }

        const QHash<QString, BridgeSession> sessions = readBridge(folder, now);
        QCOMPARE(sessions.size(), 1);
        const BridgeSession session = sessions.value(QStringLiteral("s-1"));
        QCOMPARE(session.hook.value(QStringLiteral("tool")).toString(), QStringLiteral("Bash"));
        QVERIFY(session.hookAt.isValid());
        QCOMPARE(session.status.value(QStringLiteral("session_id")).toString(), QStringLiteral("s-1"));
        QVERIFY(session.statusAt.isValid());
        // An old file is deleted; a bad one is kept for the next write.
        QVERIFY(!QFile::exists(folder + QStringLiteral("/s-3.hook.json")));
        QVERIFY(QFile::exists(folder + QStringLiteral("/s-2.status.json")));

        QVERIFY(readBridge(temp.filePath(QStringLiteral("missing")), now).isEmpty());
    }

    void mergesTheBridge()
    {
        const QDateTime now = QDateTime::fromSecsSinceEpoch(1800000000, QTimeZone::UTC);
        const QDateTime started = now.addSecs(-600);
        AccountResult result{QString(),
                             {Agent{QStringLiteral("work"), QStringLiteral("Works"), QString(), QStringLiteral("interactive"),
                                    QStringLiteral("busy"), started},
                              Agent{QStringLiteral("ask"), QStringLiteral("Asks"), QString(), QStringLiteral("interactive"),
                                    QStringLiteral("waiting"), started},
                              Agent{QStringLiteral("idle"), QStringLiteral("Rests"), QString(), QStringLiteral("interactive"),
                                    QStringLiteral("idle"), started},
                              Agent{QStringLiteral("none"), QStringLiteral("Plain"), QString(), QStringLiteral("interactive"),
                                    QStringLiteral("busy"), started}},
                             QString()};
        mirante::ItemStatus status = agentsStatus({result}, nullptr);

        QHash<QString, BridgeSession> sessions;
        sessions.insert(QStringLiteral("work"),
                        BridgeSession{QJsonObject{{QStringLiteral("event"), QStringLiteral("PreToolUse")},
                                                  {QStringLiteral("tool"), QStringLiteral("Bash")}},
                                      now.addSecs(-5),
                                      QJsonDocument::fromJson(R"({
                                          "model": {"id": "claude-opus-5-5", "display_name": "Opus 5.5"},
                                          "effort": {"level": "high"},
                                          "context_window": {"used_percentage": 42.5},
                                          "cost": {"total_cost_usd": 1.25},
                                          "rate_limits": {
                                              "five_hour": {"used_percentage": 30, "resets_at": 1800003600},
                                              "seven_day": {"used_percentage": 12, "resets_at": 1799999000}}})")
                                          .object(),
                                      now.addSecs(-60)});
        sessions.insert(QStringLiteral("ask"),
                        BridgeSession{QJsonObject{{QStringLiteral("event"), QStringLiteral("Notification")},
                                                  {QStringLiteral("notification"), QStringLiteral("permission_prompt")}},
                                      now.addSecs(-5),
                                      QJsonDocument::fromJson(R"({
                                          "model": {"id": "claude-haiku-5-5"},
                                          "rate_limits": {"five_hour": {"used_percentage": 31, "resets_at": 1800003600}}})")
                                          .object(),
                                      now.addSecs(-10)});
        // A tool event of a session that rests now: a late file.
        sessions.insert(QStringLiteral("idle"),
                        BridgeSession{QJsonObject{{QStringLiteral("event"), QStringLiteral("PreToolUse")},
                                                  {QStringLiteral("tool"), QStringLiteral("Read")}},
                                      now.addSecs(-5), QJsonObject(), QDateTime()});
        mergeBridge(status, sessions, now);

        const QJsonArray rows = status.data.value(QStringLiteral("rows")).toArray();
        QCOMPARE(rows.size(), 4);
        const QJsonObject work = rows.at(0).toObject();
        QCOMPARE(work.value(QStringLiteral("tool")).toString(), QStringLiteral("Bash"));
        QCOMPARE(work.value(QStringLiteral("model")).toString(), QStringLiteral("Opus 5.5"));
        QCOMPARE(work.value(QStringLiteral("effort")).toString(), QStringLiteral("high"));
        QCOMPARE(work.value(QStringLiteral("contextPercent")).toDouble(), 42.5);
        QCOMPARE(work.value(QStringLiteral("costUsd")).toDouble(), 1.25);
        QVERIFY(!work.contains(QStringLiteral("waitingFor")));

        const QJsonObject ask = rows.at(1).toObject();
        QCOMPARE(ask.value(QStringLiteral("waitingFor")).toString(), QStringLiteral("permission"));
        QCOMPARE(ask.value(QStringLiteral("model")).toString(), QStringLiteral("claude-haiku-5-5"));
        QVERIFY(!ask.contains(QStringLiteral("tool")));
        QVERIFY(!ask.contains(QStringLiteral("costUsd")));

        QVERIFY(!rows.at(2).toObject().contains(QStringLiteral("tool")));
        QCOMPARE(rows.at(3).toObject(), agentsStatus({result}, nullptr).data.value(QStringLiteral("rows")).toArray().at(3).toObject());

        // The account's newest status file wins, and the weekly window has
        // reset there, so it is left out.
        const QJsonArray limits = status.data.value(QStringLiteral("limits")).toArray();
        QCOMPARE(limits.size(), 1);
        const QJsonObject limit = limits.at(0).toObject();
        QCOMPARE(limit.value(QStringLiteral("account")).toString(), QStringLiteral("Default"));
        QCOMPARE(limit.value(QStringLiteral("fiveHourPercent")).toDouble(), 31.0);
        QCOMPARE(limit.value(QStringLiteral("fiveHourResetsAt")).toDouble(), 1800003600000.0);
        QVERIFY(!limit.contains(QStringLiteral("weeklyPercent")));
    }

    void ignoresHookEventsFromBeforeTheSession()
    {
        const QDateTime now = QDateTime::fromSecsSinceEpoch(1800000000, QTimeZone::UTC);
        AccountResult result{QString(),
                             {Agent{QStringLiteral("s"), QStringLiteral("Works"), QString(), QStringLiteral("interactive"),
                                    QStringLiteral("busy"), now.addSecs(-10)}},
                             QString()};
        mirante::ItemStatus status = agentsStatus({result}, nullptr);
        QHash<QString, BridgeSession> sessions;
        sessions.insert(QStringLiteral("s"),
                        BridgeSession{QJsonObject{{QStringLiteral("event"), QStringLiteral("PreToolUse")},
                                                  {QStringLiteral("tool"), QStringLiteral("Bash")}},
                                      now.addSecs(-60), QJsonObject(), QDateTime()});
        mergeBridge(status, sessions, now);
        QVERIFY(!status.data.value(QStringLiteral("rows")).toArray().at(0).toObject().contains(QStringLiteral("tool")));
        QVERIFY(status.data.value(QStringLiteral("limits")).toArray().isEmpty());

        // An error status is left alone.
        mirante::ItemStatus error;
        error.state = mirante::ItemStatus::State::Error;
        mergeBridge(error, sessions, now);
        QVERIFY(error.data.isEmpty());
    }

    void parsesTheCommandsOutput()
    {
        QString error;
        const auto agents = parseAgents(kSample, &error);
        QVERIFY(agents);
        QCOMPARE(agents->size(), 3);

        QCOMPARE(agents->at(0).sessionId, QStringLiteral("s-1"));
        QCOMPARE(agents->at(0).name, QStringLiteral("Fix the dialogs"));
        QCOMPARE(agents->at(0).cwd, QStringLiteral("/work/app"));
        QCOMPARE(agents->at(0).kind, QStringLiteral("interactive"));
        QCOMPARE(agents->at(0).activity, QStringLiteral("waiting"));
        QCOMPARE(agents->at(0).startedAt.toMSecsSinceEpoch(), qint64(1793811905069));

        // A background session has a short id and a state.
        QCOMPARE(agents->at(1).sessionId, QStringLiteral("3cd7cf87"));
        QCOMPARE(agents->at(1).activity, QStringLiteral("done"));

        QVERIFY(agents->at(2).name.isEmpty());
        QVERIFY(!agents->at(2).startedAt.isValid());
    }

    void readsAnEmptyList()
    {
        const auto agents = parseAgents("[]\n", nullptr);
        QVERIFY(agents);
        QVERIFY(agents->isEmpty());
    }

    void refusesOtherOutput_data()
    {
        QTest::addColumn<QByteArray>("output");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("text") << QByteArray("Not logged in");
        QTest::newRow("object") << QByteArray(R"({"sessions": []})");
    }

    void refusesOtherOutput()
    {
        QFETCH(QByteArray, output);
        QString error;
        QVERIFY(!parseAgents(output, &error));
        QVERIFY(!error.isEmpty());
    }

    void mapsActivities()
    {
        QCOMPARE(activityOf(QStringLiteral("busy")), Activity::Working);
        QCOMPARE(activityOf(QStringLiteral("working")), Activity::Working);
        QCOMPARE(activityOf(QStringLiteral("waiting")), Activity::NeedsInput);
        QCOMPARE(activityOf(QStringLiteral("blocked")), Activity::NeedsInput);
        QCOMPARE(activityOf(QStringLiteral("idle")), Activity::Idle);
        QCOMPARE(activityOf(QStringLiteral("done")), Activity::Done);
        QCOMPARE(activityOf(QStringLiteral("failed")), Activity::Failed);
        QCOMPARE(activityOf(QStringLiteral("stopped")), Activity::Stopped);
        QCOMPARE(activityOf(QStringLiteral("thinking")), Activity::Unknown);
        QCOMPARE(activityText(Activity::Unknown, QStringLiteral("thinking")), QStringLiteral("thinking"));
        QCOMPARE(activityText(Activity::NeedsInput, QString()), QStringLiteral("Needs input"));
    }

    void findsTheRepository()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString main = temp.filePath(QStringLiteral("mirante-core"));
        QDir().mkpath(main + QStringLiteral("/.git/worktrees/fix"));
        QDir().mkpath(main + QStringLiteral("/src/api"));

        auto repository = repositoryOf(main + QStringLiteral("/src/api"));
        QVERIFY(repository);
        QCOMPARE(repository->name, QStringLiteral("mirante-core"));
        QCOMPARE(QDir(repository->top), QDir(main));

        // A worktree names its main repository, and its top is its own folder.
        const QString worktree = main + QStringLiteral("/.claude/worktrees/fix");
        writeFile(worktree + QStringLiteral("/.git"), "gitdir: " + main.toUtf8() + "/.git/worktrees/fix\n");
        repository = repositoryOf(worktree);
        QVERIFY(repository);
        QCOMPARE(repository->name, QStringLiteral("mirante-core"));
        QCOMPARE(QDir(repository->top), QDir(worktree));

        // A relative gitdir.
        const QString other = temp.filePath(QStringLiteral("elsewhere"));
        writeFile(other + QStringLiteral("/.git"), "gitdir: ../mirante-core/.git/worktrees/fix\n");
        repository = repositoryOf(other);
        QVERIFY(repository);
        QCOMPARE(repository->name, QStringLiteral("mirante-core"));

        const QString plain = temp.filePath(QStringLiteral("plain"));
        QDir().mkpath(plain);
        QVERIFY(!repositoryOf(plain) || repositoryOf(plain)->top != plain);
        QVERIFY(!repositoryOf(QString()));
    }

    void readsTheConfiguration()
    {
        const QJsonObject config{{QStringLiteral("accounts"),
                                  QJsonArray{QStringLiteral(" ~/.claude-work "), QString(), QStringLiteral("~/.claude-work"),
                                             QStringLiteral("D:/claude/home")}},
                                 {QStringLiteral("includeFinished"), true}};
        QCOMPARE(accountsOf(config), (QStringList{QStringLiteral("~/.claude-work"), QStringLiteral("D:/claude/home")}));
        QCOMPARE(accountLabel(QStringLiteral("~/.claude-work")), QStringLiteral(".claude-work"));
        QCOMPARE(accountLabel(QStringLiteral("D:\\claude\\home\\")), QStringLiteral("home"));
        QCOMPARE(accountLabel(QString()), QStringLiteral("Default"));

        QCOMPARE(agentArguments(QJsonObject(), QString()), (QStringList{QStringLiteral("agents"), QStringLiteral("--json")}));
        const QStringList arguments = agentArguments(config, QStringLiteral("/work/app"));
        QCOMPARE(arguments.mid(0, 4), (QStringList{QStringLiteral("agents"), QStringLiteral("--json"),
                                                   QStringLiteral("--all"), QStringLiteral("--cwd")}));
        QCOMPARE(QDir::fromNativeSeparators(arguments.at(4)), QStringLiteral("/work/app"));
    }

    void buildsTheStatus()
    {
        QTemporaryDir temp;
        const QString repo = temp.filePath(QStringLiteral("app"));
        QDir().mkpath(repo + QStringLiteral("/.git"));

        Agent waiting{QStringLiteral("s-1"), QStringLiteral("Fix"), repo, QStringLiteral("interactive"),
                      QStringLiteral("waiting"), QDateTime::fromMSecsSinceEpoch(1000, QTimeZone::UTC)};
        Agent busy{QStringLiteral("s-2"), QStringLiteral("Build"), QString(), QStringLiteral("interactive"),
                   QStringLiteral("busy"), QDateTime()};
        QStringList headsAsked;
        const auto headOf = [&headsAsked](const QString &top) {
            headsAsked << top;
            return QStringLiteral("main");
        };

        AccountResult first{QString(), {waiting, busy}, QString()};
        AccountResult second{QStringLiteral("~/.claude-work"), {}, QStringLiteral("Not logged in")};
        mirante::ItemStatus status = agentsStatus({first, second}, headOf);
        QCOMPARE(status.state, mirante::ItemStatus::State::Ok);
        QCOMPARE(status.summary, QStringLiteral("2 agents · 1 waiting for input · 1 account failed"));
        QVERIFY(!status.closed.has_value());
        QVERIFY(status.attention.isEmpty());

        const QJsonArray rows = status.data.value(QStringLiteral("rows")).toArray();
        QCOMPARE(rows.size(), 2);
        const QJsonObject row = rows.at(0).toObject();
        QCOMPARE(row.value(QStringLiteral("title")).toString(), QStringLiteral("Fix"));
        QCOMPARE(row.value(QStringLiteral("repository")).toString(), QStringLiteral("app"));
        QCOMPARE(row.value(QStringLiteral("branch")).toString(), QStringLiteral("main"));
        QCOMPARE(row.value(QStringLiteral("account")).toString(), QStringLiteral("Default"));
        QCOMPARE(row.value(QStringLiteral("startedAt")).toDouble(), 1000.0);
        QVERIFY(!rows.at(1).toObject().contains(QStringLiteral("repository")));
        QCOMPARE(headsAsked.size(), 1);

        const QJsonArray errors = status.data.value(QStringLiteral("errors")).toArray();
        QCOMPARE(errors.size(), 1);
        QCOMPARE(errors.at(0).toObject().value(QStringLiteral("account")).toString(), QStringLiteral(".claude-work"));

        // Only when every account fails is the list an error.
        status = agentsStatus({second}, headOf);
        QCOMPARE(status.state, mirante::ItemStatus::State::Error);
        QCOMPARE(status.message, QStringLiteral("Not logged in"));
        QVERIFY(!status.data.contains(QStringLiteral("rows")));

        status = agentsStatus({AccountResult{}}, headOf);
        QCOMPARE(status.state, mirante::ItemStatus::State::Ok);
        QCOMPARE(status.summary, QStringLiteral("0 agents"));
    }

    void fetchesEachAccount()
    {
        mirante::testing::FakeStatusJob job;
        job.configValues = QJsonObject{
            {QStringLiteral("accounts"), QJsonArray{QStringLiteral("/home/me/.claude-work"),
                                                    QStringLiteral("/home/me/broken")}}};
        fetchAgents(job, testProgram());
        QTRY_VERIFY_WITH_TIMEOUT(job.finished.has_value(), 20000);
        QCOMPARE(job.finishCount, 1);
        QCOMPARE(job.finished->state, mirante::ItemStatus::State::Ok);
        const QJsonArray rows = job.finished->data.value(QStringLiteral("rows")).toArray();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.at(0).toObject().value(QStringLiteral("account")).toString(), QStringLiteral(".claude-work"));
        QCOMPARE(job.finished->data.value(QStringLiteral("errors")).toArray().size(), 1);
    }

    void fetchesTheDefaultAccount()
    {
        mirante::testing::FakeStatusJob job;
        fetchAgents(job, testProgram());
        QTRY_VERIFY_WITH_TIMEOUT(job.finished.has_value(), 20000);
        QCOMPARE(job.finished->state, mirante::ItemStatus::State::Ok);
        QCOMPARE(job.finished->summary, QStringLiteral("2 agents · 1 waiting for input"));
    }

    void reportsAProgramThatDoesNotStart()
    {
        mirante::testing::FakeStatusJob job;
        QTemporaryDir temp;
        fetchAgents(job, temp.filePath(QStringLiteral("no-such-claude")));
        QTRY_VERIFY_WITH_TIMEOUT(job.finished.has_value(), 20000);
        QCOMPARE(job.finished->state, mirante::ItemStatus::State::Error);
        QVERIFY(!job.finished->message.isEmpty());
    }

    void needsTheFolderMapped()
    {
        mirante::testing::FakeStatusJob job;
        job.configValues = QJsonObject{{QStringLiteral("folder"), QStringLiteral("f1")}};
        fetchAgents(job, testProgram());
        QVERIFY(job.finished.has_value());
        QCOMPARE(job.finished->state, mirante::ItemStatus::State::Error);
        QCOMPARE(job.finished->message, QStringLiteral("The project folder is not mapped on this device."));
    }

    void viewShowsTheRows()
    {
        FakeStatusSource source;
        mirante::testing::FakeItemContext context;
        context.statusSource = &source;
        std::unique_ptr<QWidget> view(createAgentsView(context, nullptr));
        auto *list = qobject_cast<mirante::widgets::ItemListView *>(view.get());
        QVERIFY(list);
        QVERIFY(list->visibleKeys().isEmpty());

        AccountResult result{QString(),
                             {Agent{QStringLiteral("s-1"), QStringLiteral("Fix"), QString(), QStringLiteral("interactive"),
                                    QStringLiteral("waiting"), QDateTime()},
                              Agent{QStringLiteral("s-2"), QString(), QString(), QStringLiteral("interactive"),
                                    QStringLiteral("busy"), QDateTime()}},
                             QString()};
        source.send(agentsStatus({result}, nullptr));
        QStringList keys = list->visibleKeys();
        keys.sort();
        QCOMPARE(keys, (QStringList{QStringLiteral("|s-1"), QStringLiteral("|s-2")}));

        // An error keeps the rows.
        mirante::ItemStatus error;
        error.state = mirante::ItemStatus::State::Error;
        error.message = QStringLiteral("Not logged in");
        source.send(error);
        QCOMPARE(list->visibleKeys().size(), 2);
    }

private:
    QTemporaryDir m_bridge;
};

int main(int argc, char *argv[])
{
    if (argc > 1 && std::strcmp(argv[1], "agents") == 0)
        return runAsClaude();
    QApplication app(argc, argv);
    TestAgents test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_agents.moc"
