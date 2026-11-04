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
