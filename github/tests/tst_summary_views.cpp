// The three GitHub summaries, drawn from the status tests' answers in
// tests/data/: acme/widgets pull request 42 and issue 7, and the runs of
// workflow_runs.json and workflow_runs_failed.json. Counts show Qt's source
// forms, such as "11 check(s) passed", because no translation is loaded. A
// handler stands in for the external browser, so nothing is opened.
#include "../GitHubIssueView.h"
#include "../GitHubPullRequestView.h"
#include "../GitHubRunView.h"
#include "../GitHubStatus.h"
#include "../GitHubViewSupport.h"

#include <mirante/ItemContext.h>
#include <mirante/testing/FakeApiClient.h>
#include <mirante/widgets/ActionRow.h>
#include <mirante/widgets/StatusPill.h>
#include <mirante/widgets/SummaryView.h>

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QTest>
#include <QTimeZone>
#include <QTimer>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

using mirante::ApiReply;
using mirante::ItemStatus;
using mirante::testing::FakeApiClient;
using mirante::widgets::StatusPill;
using mirante::widgets::StatusRole;
using mirante::widgets::SummaryView;
using namespace mirante::modules::github;

namespace {

const QString kRuns = QStringLiteral("repos/acme/widgets/actions/runs/");
const QString kPrAddress = QStringLiteral("https://github.com/acme/widgets/pull/42");
const QString kBuiltPrAddress = QStringLiteral("https://built.example/acme/widgets/pull/42");
const QString kIssueAddress = QStringLiteral("https://github.com/acme/widgets/issues/7");
const QString kRunAddress = QStringLiteral("https://github.com/acme/widgets/actions/runs/9103");
const QString kTokenMessage = QStringLiteral("The token cannot do this. It needs write access to Actions.");

QString jobsKey(qint64 runId)
{
    return QStringLiteral("GET ") + kRuns + QStringLiteral("%1/jobs?filter=latest&per_page=100").arg(runId);
}

QString postKey(qint64 runId, const QString &action)
{
    return QStringLiteral("POST ") + kRuns + QStringLiteral("%1/").arg(runId) + action;
}

QString dataFile(const QString &name)
{
    return QFINDTESTDATA(QStringLiteral("tests/data/") + name);
}

QJsonDocument readJson(const QString &name)
{
    QFile file(dataFile(name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot read %s", qPrintable(name));
    return QJsonDocument::fromJson(file.readAll());
}

QJsonObject readObject(const QString &name)
{
    return readJson(name).object();
}

QJsonArray readArray(const QString &name)
{
    return readJson(name).array();
}

// The rows under key of an answer that wraps them.
QJsonArray rowsOf(const QString &name, const QString &key)
{
    return readObject(name).value(key).toArray();
}

QJsonArray checkRuns()
{
    return rowsOf(QStringLiteral("check_runs.json"), QStringLiteral("check_runs"));
}

QJsonArray jobsOf(const QString &name)
{
    return rowsOf(name, QStringLiteral("jobs"));
}

// A status source that a test drives by hand.
class FakeStatusSource : public mirante::ItemStatusSource
{
public:
    ItemStatus status;
    int refreshes = 0;

    ItemStatus current() const override { return status; }
    void refresh() override { ++refreshes; }
    void subscribe(QObject *receiver, std::function<void(const ItemStatus &)> changed) override
    {
        m_subscribers.push_back({receiver, std::move(changed)});
    }

    void change(const ItemStatus &next)
    {
        status = next;
        for (const Subscriber &subscriber : m_subscribers) {
            if (subscriber.receiver)
                subscriber.changed(next);
        }
    }

private:
    struct Subscriber
    {
        QPointer<QObject> receiver;
        std::function<void(const ItemStatus &)> changed;
    };
    std::vector<Subscriber> m_subscribers;
};

class FakeContext : public mirante::ItemContext
{
public:
    QJsonObject itemConfig{{QStringLiteral("repository"), QStringLiteral("acme/widgets")},
                           {QStringLiteral("number"), 42}};
    QJsonObject general{{QStringLiteral("baseUrl"), QStringLiteral("https://built.example")}};
    QList<QUrl> pages;
    FakeApiClient client{QUrl(QStringLiteral("https://api.built.example/"))};
    FakeStatusSource source;
    bool hasStatus = true;

    QString itemName() const override { return QStringLiteral("Item"); }
    QJsonObject config() const override { return itemConfig; }
    QJsonValue projectSetting(const QString &) const override { return {}; }
    QJsonObject moduleSettings(const QString &screenId) const override
    {
        return screenId == QLatin1String("general") ? general : QJsonObject();
    }
    QString folderPath(const QString &) const override { return {}; }
    QWidget *createWebView(const QUrl &, QWidget *) override { return nullptr; }
    QWebEngineProfile *profile() override { return nullptr; }
    QWidget *createMessageView(const QString &, QWidget *) override { return nullptr; }
    void updateConfig(const QJsonObject &config) override { itemConfig = config; }
    void showPage(const QUrl &url) override { pages << url; }
    mirante::ApiClient *server() override { return &client; }
    mirante::ItemStatusSource *status() override { return hasStatus ? &source : nullptr; }
};

using View = std::unique_ptr<QWidget>;

SummaryView *summary(const View &view)
{
    return dynamic_cast<SummaryView *>(view.get());
}

// The texts of the labels that show, one per line.
QString shownText(const View &view)
{
    QStringList lines;
    for (const QLabel *label : view->findChildren<QLabel *>()) {
        if (label->isVisibleTo(view.get()) && !label->text().isEmpty())
            lines << label->text();
    }
    return lines.join(QLatin1Char('\n'));
}

QPushButton *button(const View &view, const QString &id)
{
    return view->findChild<QPushButton *>(id);
}

StatusPill *runPillOf(const View &view, qint64 id)
{
    return dynamic_cast<StatusPill *>(view->findChild<QLabel *>(QStringLiteral("run-pill-%1").arg(id)));
}

// The role of the first pill that shows text.
std::optional<StatusRole> roleOf(const View &view, const QString &text)
{
    for (const QLabel *label : view->findChildren<QLabel *>()) {
        const auto *pill = dynamic_cast<const StatusPill *>(label);
        if (pill && pill->text() == text && pill->isVisibleTo(view.get()))
            return pill->role();
    }
    return std::nullopt;
}

QTimer *refreshTimer(const View &view)
{
    return view->findChild<QTimer *>(QStringLiteral("run-refresh"));
}

bool showsAction(const View &view, const QString &id)
{
    return summary(view)->actions()->isActionVisible(id);
}

QString errorOf(const View &view)
{
    return summary(view)->actions()->error();
}

// Pull request 42, open, with the reviews of reviewsFile and the checks of
// check_runs.json and combinedFile.
ItemStatus pullRequest(const QJsonObject &object = readObject(QStringLiteral("pull_request.json")),
                       const QString &reviewsFile = QStringLiteral("pull_request_reviews.json"),
                       const QJsonArray &runs = checkRuns(),
                       const QString &combinedFile = QStringLiteral("combined_status.json"))
{
    return pullRequestStatus(object, readArray(reviewsFile), readObject(QStringLiteral("user.json")), runs,
                             readObject(combinedFile));
}

// Issue 7. Its comments, newest first, if comments is not empty.
ItemStatus issue(const QJsonArray &comments = {}, const QJsonObject &object = readObject(QStringLiteral("issue.json")))
{
    return issueStatus(object, comments);
}

QJsonObject comment(const QString &body, const QString &login = QStringLiteral("bruno-author"))
{
    return QJsonObject{{QStringLiteral("body"), body},
                       {QStringLiteral("user"), QJsonObject{{QStringLiteral("login"), login}}},
                       {QStringLiteral("created_at"), QStringLiteral("2026-10-26T12:00:00Z")}};
}

QJsonArray runsOf(const QString &name)
{
    return rowsOf(name, QStringLiteral("workflow_runs"));
}

// The group of workflow_runs_failed.json: CI 9103 failed and leads, with Lint
// 9102 timed out and Docs 9101 passed beside it, and CI 9090 before it.
ItemStatus failedGroup(bool siblingJobs = true)
{
    const RunGroup group = runGroup(runsOf(QStringLiteral("workflow_runs_failed.json")));
    const QHash<qint64, QString> files{{9102, QStringLiteral("run_jobs_lint.json")},
                                       {9101, QStringLiteral("run_jobs_codeql.json")}};
    QJsonArray siblings;
    for (const QJsonValue &value : group.siblings) {
        const QJsonObject run = value.toObject();
        QJsonObject entry{{QStringLiteral("run"), run}};
        if (siblingJobs)
            entry.insert(QStringLiteral("jobs"), jobsOf(files.value(run.value(QStringLiteral("id")).toInteger())));
        siblings.append(entry);
    }
    return runStatus(QStringLiteral("main"), group.lead, jobsOf(QStringLiteral("run_jobs_failed.json")), siblings,
                     group.older);
}

// The group of workflow_runs.json: Unit and Integration Tests leads, with Lint
// and Code Scanning, all passed and of pull request 42, and five runs before.
ItemStatus passedGroup()
{
    const RunGroup group = runGroup(runsOf(QStringLiteral("workflow_runs.json")));
    const QStringList files{QStringLiteral("run_jobs_lint.json"), QStringLiteral("run_jobs_codeql.json")};
    QJsonArray siblings;
    for (qsizetype i = 0; i < group.siblings.size(); ++i)
        siblings.append(QJsonObject{{QStringLiteral("run"), group.siblings.at(i)},
                                    {QStringLiteral("jobs"), jobsOf(files.at(i))}});
    return runStatus(QStringLiteral("feature/skip-refresh"), group.lead, jobsOf(QStringLiteral("run_jobs.json")),
                     siblings, group.older);
}

// CI 9103 alone, with this status and conclusion.
ItemStatus singleRun(const QString &state, const QString &conclusion,
                     const QJsonArray &jobs = jobsOf(QStringLiteral("run_jobs_failed.json")))
{
    QJsonObject run = runsOf(QStringLiteral("workflow_runs_failed.json")).at(1).toObject();
    run.insert(QStringLiteral("status"), state);
    run.insert(QStringLiteral("conclusion"), conclusion.isEmpty() ? QJsonValue(QJsonValue::Null) : conclusion);
    return runStatus(QStringLiteral("main"), run, jobs);
}

ApiReply failure(ApiReply::Error error, int httpStatus, const QString &message)
{
    ApiReply reply;
    reply.error = error;
    reply.httpStatus = httpStatus;
    reply.message = message;
    return reply;
}

void succeed(FakeContext &context, const QString &key)
{
    context.client.setJson(key, QJsonDocument(QJsonObject()));
}

// Runs while a dialog started by click is open, with that dialog.
void withDialog(QPushButton *start, const std::function<void(QDialog *)> &answer)
{
    QTimer::singleShot(0, start, [answer] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        answer(dialog);
    });
    start->click();
}

void answerQuestion(QPushButton *start, QMessageBox::StandardButton answer)
{
    withDialog(start, [answer](QDialog *dialog) {
        auto *question = qobject_cast<QMessageBox *>(dialog);
        QVERIFY(question);
        question->button(answer)->click();
    });
}

} // namespace

class TestSummaryViews : public QObject
{
    Q_OBJECT

public slots:
    // Takes the place of the browser.
    void openUrl(const QUrl &url) { m_opened << url; }

private slots:
    void init()
    {
        m_opened.clear();
        QDesktopServices::setUrlHandler(QStringLiteral("https"), this, "openUrl");
        QDesktopServices::setUrlHandler(QStringLiteral("http"), this, "openUrl");
    }

    void cleanup()
    {
        QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
        QDesktopServices::unsetUrlHandler(QStringLiteral("http"));
    }

    // The helpers.

    void pillsFollowTheTable_data()
    {
        QTest::addColumn<QString>("status");
        QTest::addColumn<QString>("conclusion");
        QTest::addColumn<QString>("text");
        QTest::addColumn<StatusRole>("role");

        QTest::newRow("queued") << "queued" << "" << "Queued" << StatusRole::Waiting;
        QTest::newRow("waiting") << "waiting" << "" << "Waiting" << StatusRole::Waiting;
        QTest::newRow("requested") << "requested" << "" << "Waiting" << StatusRole::Waiting;
        QTest::newRow("pending") << "pending" << "" << "Waiting" << StatusRole::Waiting;
        QTest::newRow("in_progress") << "in_progress" << "" << "Running" << StatusRole::Waiting;
        QTest::newRow("success") << "completed" << "success" << "Passed" << StatusRole::Good;
        QTest::newRow("failure") << "completed" << "failure" << "Failed" << StatusRole::Bad;
        QTest::newRow("timed_out") << "completed" << "timed_out" << "Timed out" << StatusRole::Bad;
        QTest::newRow("startup_failure") << "completed" << "startup_failure" << "Failed" << StatusRole::Bad;
        QTest::newRow("cancelled") << "completed" << "cancelled" << "Cancelled" << StatusRole::Neutral;
        QTest::newRow("skipped") << "completed" << "skipped" << "Skipped" << StatusRole::Neutral;
        QTest::newRow("neutral") << "completed" << "neutral" << "Neutral" << StatusRole::Neutral;
        QTest::newRow("action_required") << "completed" << "action_required" << "Action required"
                                         << StatusRole::Waiting;
        QTest::newRow("stale") << "completed" << "stale" << "Stale" << StatusRole::Neutral;
    }

    void pillsFollowTheTable()
    {
        QFETCH(QString, status);
        QFETCH(QString, conclusion);
        QFETCH(QString, text);
        QFETCH(StatusRole, role);

        QCOMPARE(statusText(status, conclusion), text);
        QCOMPARE(statusRole(status, conclusion), role);
        std::unique_ptr<StatusPill> pill(runPill(
            QJsonObject{{QStringLiteral("status"), status}, {QStringLiteral("conclusion"), conclusion}}));
        QCOMPARE(pill->text(), text);
        QCOMPARE(pill->role(), role);
    }

    void smallPillsAndTexts()
    {
        const auto commit = [](const char *state) {
            return std::unique_ptr<StatusPill>(commitStatusPill(QJsonObject{{QStringLiteral("state"), QLatin1String(state)}}));
        };
        QCOMPARE(commit("success")->text(), QStringLiteral("Passed"));
        QCOMPARE(commit("failure")->role(), StatusRole::Bad);
        QCOMPARE(commit("error")->text(), QStringLiteral("Error"));
        QCOMPARE(commit("pending")->role(), StatusRole::Waiting);

        QCOMPARE(checksText(ChecksState::Passed), QStringLiteral("Checks passed"));
        QCOMPARE(checksText(ChecksState::Failed), QStringLiteral("Checks failed"));
        QCOMPARE(checksText(ChecksState::Running), QStringLiteral("Checks running"));
        QCOMPARE(checksText(ChecksState::None), QStringLiteral("No checks"));
        QCOMPARE(checksRole(ChecksState::Failed), StatusRole::Bad);

        QCOMPARE(std::unique_ptr<StatusPill>(reviewPill(QStringLiteral("APPROVED")))->role(), StatusRole::Good);
        QCOMPARE(std::unique_ptr<StatusPill>(reviewPill({}))->text(), QStringLiteral("Waiting"));

        for (const char *state : {"queued", "in_progress", "waiting", "requested", "pending"})
            QVERIFY(canCancelRun(QLatin1String(state)));
        QVERIFY(!canCancelRun(QStringLiteral("completed")));

        QCOMPARE(durationText(42), QStringLiteral("42 s"));
        QCOMPARE(durationText(302), QStringLiteral("5 min 2 s"));
        QCOMPARE(durationText(3780), QStringLiteral("1 h 3 min"));
        QVERIFY(durationText(QJsonValue()).isEmpty());
        QCOMPARE(durationBetween(QStringLiteral("2026-10-29T09:30:05Z"), QStringLiteral("2026-10-29T09:35:00Z")),
                 QStringLiteral("4 min 55 s"));
        QVERIFY(durationBetween(QStringLiteral("2026-10-29T09:30:05Z"), {}).isEmpty());

        const QJsonArray users{QJsonObject{{QStringLiteral("login"), QStringLiteral("ana")}},
                               QJsonObject{{QStringLiteral("login"), QStringLiteral("bruno")}}};
        QCOMPARE(userNames(users), QStringLiteral("ana, bruno"));
        QCOMPARE(localDateTime(QStringLiteral("not a date")), QStringLiteral("not a date"));
    }

    // The pull request.

    void pullRequestDrawsFromData()
    {
        FakeContext context;
        context.source.status = pullRequest();
        View view(createPullRequestView(context, nullptr));
        view->show();

        const QString text = shownText(view);
        QVERIFY(text.contains(QStringLiteral("Skip token refresh when gh's state directory is not writable")));
        QCOMPARE(roleOf(view, QStringLiteral("Open")), StatusRole::Good);
        QVERIFY(text.contains(QStringLiteral("feature/skip-refresh → trunk")));
        QVERIFY(!text.contains(QStringLiteral("Draft")));

        // The commit statuses fail the checks. Only those that did not pass
        // show, each with a link to its details.
        QCOMPARE(roleOf(view, QStringLiteral("Checks failed")), StatusRole::Bad);
        QVERIFY(text.contains(QStringLiteral("tide")));
        QCOMPARE(roleOf(view, QStringLiteral("Pending")), StatusRole::Waiting);
        QVERIFY(text.contains(QStringLiteral("pull-kubernetes-verify")));
        QCOMPARE(roleOf(view, QStringLiteral("Error")), StatusRole::Bad);
        QVERIFY(!text.contains(QStringLiteral("EasyCLA")));
        QVERIFY(!text.contains(QStringLiteral("CodeQL")));
        QVERIFY(!text.contains(QStringLiteral("check(s) passed")));
        button(view, QStringLiteral("status-details-57720069121"))->click();
        QCOMPARE(m_opened, QList<QUrl>{QUrl(QStringLiteral("https://ci.example.com/pull-kubernetes-verify"))});
        QVERIFY(context.pages.isEmpty());

        // Each reviewer's latest review, and the requested reviewer who has
        // not reviewed yet.
        QVERIFY(text.contains(QStringLiteral("review-bot[bot]")));
        QVERIFY(text.contains(QStringLiteral("dario-dev")));
        QCOMPARE(roleOf(view, QStringLiteral("Approved")), StatusRole::Good);
        QCOMPARE(roleOf(view, QStringLiteral("Commented")), StatusRole::Neutral);
        QCOMPARE(text.count(QStringLiteral("carla-dev")), 1);
        QVERIFY(text.contains(QStringLiteral("ana-reviewer")));
        QCOMPARE(roleOf(view, QStringLiteral("Waiting")), StatusRole::Waiting);

        QVERIFY(text.contains(QStringLiteral("Author: bruno-author")));
        QVERIFY(text.contains(QStringLiteral("Assignees: none")));
        QVERIFY(text.contains(QStringLiteral("No labels")));
    }

    void pullRequestChecksThatAllPassed()
    {
        FakeContext context;
        context.source.status = pullRequest(readObject(QStringLiteral("pull_request.json")),
                                            QStringLiteral("pull_request_reviews.json"), checkRuns(),
                                            QStringLiteral("combined_status_empty.json"));
        View view(createPullRequestView(context, nullptr));
        view->show();

        const QString text = shownText(view);
        QCOMPARE(roleOf(view, QStringLiteral("Checks passed")), StatusRole::Good);
        QVERIFY(text.contains(QStringLiteral("11 check(s) passed")));
        QVERIFY(!text.contains(QStringLiteral("CodeQL")));
        QVERIFY(view->findChildren<QPushButton *>(QRegularExpression(QStringLiteral("details"))).isEmpty());
    }

    void pullRequestWithoutChecks()
    {
        FakeContext context;
        context.source.status = pullRequest(readObject(QStringLiteral("pull_request.json")),
                                            QStringLiteral("pull_request_reviews.json"), {},
                                            QStringLiteral("combined_status_empty.json"));
        View view(createPullRequestView(context, nullptr));
        view->show();
        QCOMPARE(roleOf(view, QStringLiteral("No checks")), StatusRole::Neutral);
        QVERIFY(!shownText(view).contains(QStringLiteral("check(s) passed")));
    }

    void failedCheckRunLinksToItsDetails()
    {
        QJsonArray runs = checkRuns();
        QJsonObject failed = runs.at(0).toObject();
        failed.insert(QStringLiteral("conclusion"), QStringLiteral("failure"));
        runs.replace(0, failed);
        QJsonObject running = runs.at(1).toObject();
        running.insert(QStringLiteral("status"), QStringLiteral("in_progress"));
        running.insert(QStringLiteral("conclusion"), QJsonValue::Null);
        running.remove(QStringLiteral("html_url"));
        runs.replace(1, running);
        QJsonObject cancelled = runs.at(2).toObject();
        cancelled.insert(QStringLiteral("conclusion"), QStringLiteral("cancelled"));
        runs.replace(2, cancelled);

        FakeContext context;
        context.source.status = pullRequest(readObject(QStringLiteral("pull_request.json")),
                                            QStringLiteral("pull_request_reviews.json"), runs,
                                            QStringLiteral("combined_status_empty.json"));
        View view(createPullRequestView(context, nullptr));
        view->show();

        const QString text = shownText(view);
        QCOMPARE(roleOf(view, QStringLiteral("Checks failed")), StatusRole::Bad);
        QVERIFY(text.contains(QStringLiteral("CodeQL")));
        QCOMPARE(roleOf(view, QStringLiteral("Failed")), StatusRole::Bad);
        QVERIFY(text.contains(QStringLiteral("build (windows-latest)")));
        QCOMPARE(roleOf(view, QStringLiteral("Running")), StatusRole::Waiting);
        // A cancelled check shows a pill of its own.
        QVERIFY(text.contains(QStringLiteral("integration-tests (windows-latest)")));
        QCOMPARE(roleOf(view, QStringLiteral("Cancelled")), StatusRole::Neutral);
        QVERIFY(!text.contains(QStringLiteral("build (macos-latest)")));

        button(view, QStringLiteral("check-details-122364269635"))->click();
        // Without html_url, details_url.
        button(view, QStringLiteral("check-details-122363778241"))->click();
        QCOMPARE(m_opened, (QList<QUrl>{QUrl(QStringLiteral("https://github.com/acme/widgets/runs/122364269635")),
                                        QUrl(QStringLiteral("https://github.com/acme/widgets/actions/runs/"
                                                            "40553652299/job/122363778241"))}));
    }

    void pullRequestShowsDraftAndMerged()
    {
        QJsonObject object = readObject(QStringLiteral("pull_request.json"));
        object.insert(QStringLiteral("draft"), true);
        FakeContext context;
        context.source.status = pullRequest(object);
        View draft(createPullRequestView(context, nullptr));
        draft->show();
        QCOMPARE(roleOf(draft, QStringLiteral("Draft")), StatusRole::Waiting);

        object.insert(QStringLiteral("draft"), false);
        object.insert(QStringLiteral("state"), QStringLiteral("closed"));
        object.insert(QStringLiteral("merged_at"), QStringLiteral("2026-10-29T10:00:00Z"));
        context.source.status = pullRequest(object);
        View merged(createPullRequestView(context, nullptr));
        merged->show();
        QVERIFY(roleOf(merged, QStringLiteral("Merged")));
        QVERIFY(!roleOf(merged, QStringLiteral("Closed")));

        object.remove(QStringLiteral("merged_at"));
        context.source.status = pullRequest(object);
        View closed(createPullRequestView(context, nullptr));
        closed->show();
        QCOMPARE(roleOf(closed, QStringLiteral("Closed")), StatusRole::Neutral);
    }

    void reviewsShowChangesRequested()
    {
        FakeContext context;
        context.source.status = pullRequest(readObject(QStringLiteral("pull_request.json")),
                                            QStringLiteral("pull_request_reviews_changes.json"));
        View view(createPullRequestView(context, nullptr));
        view->show();

        const QString text = shownText(view);
        QCOMPARE(roleOf(view, QStringLiteral("Changes requested")), StatusRole::Bad);
        QCOMPARE(roleOf(view, QStringLiteral("Approved")), StatusRole::Good);
        QVERIFY(!text.contains(QStringLiteral("Commented")));
        // Dismissed and pending reviews count for nothing.
        QVERIFY(!text.contains(QStringLiteral("erin-user")));
        QVERIFY(!text.contains(QStringLiteral("frank-user")));
        QVERIFY(text.contains(QStringLiteral("ana-reviewer")));
    }

    void pullRequestWithoutReviews()
    {
        QJsonObject object = readObject(QStringLiteral("pull_request.json"));
        object.insert(QStringLiteral("requested_reviewers"), QJsonArray());
        object.insert(QStringLiteral("assignees"),
                      QJsonArray{QJsonObject{{QStringLiteral("login"), QStringLiteral("bruno-author")}}});
        object.insert(QStringLiteral("labels"),
                      QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("enhancement")}}});
        FakeContext context;
        context.source.status = pullRequestStatus(object, {}, readObject(QStringLiteral("user.json")), {}, {});
        View view(createPullRequestView(context, nullptr));
        view->show();

        const QString text = shownText(view);
        QVERIFY(text.contains(QStringLiteral("No reviews")));
        QVERIFY(text.contains(QStringLiteral("Assignees: bruno-author")));
        QVERIFY(roleOf(view, QStringLiteral("enhancement")));
    }

    // Decision 0102: a check run is re-run by the app that made it.
    void pullRequestHasNoActionsOfItsOwn()
    {
        FakeContext context;
        context.source.status = pullRequest();
        View view(createPullRequestView(context, nullptr));
        for (const char *id : {"rerun-run", "rerun-failed", "cancel-run", "approve", "comment", "close", "merge"})
            QVERIFY(!button(view, QLatin1String(id)));
        QVERIFY(showsAction(view, QStringLiteral("open-page")));
    }

    // The issue.

    void issueDrawsFromData()
    {
        QJsonObject object = readObject(QStringLiteral("issue.json"));
        object.insert(QStringLiteral("milestone"), QJsonObject{{QStringLiteral("title"), QStringLiteral("v2.60")}});
        FakeContext context;
        context.itemConfig.insert(QStringLiteral("number"), 7);
        context.source.status = issue(QJsonArray{comment(QStringLiteral("Fixed on the branch."))}, object);
        View view(createIssueView(context, nullptr));
        view->show();

        const QString text = shownText(view);
        QVERIFY(text.contains(QStringLiteral("Unable to assign issues to myself")));
        QCOMPARE(roleOf(view, QStringLiteral("Open")), StatusRole::Good);
        QVERIFY(text.contains(QStringLiteral("Assignees: carla-dev")));
        QVERIFY(text.contains(QStringLiteral("Milestone: v2.60")));
        QVERIFY(roleOf(view, QStringLiteral("bug")));
        QVERIFY(roleOf(view, QStringLiteral("gh-issue")));

        const QDateTime written(QDate(2026, 10, 26), QTime(12, 0), QTimeZone::UTC);
        QVERIFY(text.contains(QStringLiteral("bruno-author · ")
                              + QLocale().toString(written.toLocalTime(), QLocale::ShortFormat)));
        QVERIFY(text.contains(QStringLiteral("Fixed on the branch.")));
        QCOMPARE(summary(view)->actions()->webLink(), QUrl(kIssueAddress));
    }

    void issueClosedEitherWayReadsClosed()
    {
        for (const char *reason : {"completed", "not_planned"}) {
            QJsonObject object = readObject(QStringLiteral("issue.json"));
            object.insert(QStringLiteral("state"), QStringLiteral("closed"));
            object.insert(QStringLiteral("state_reason"), QLatin1String(reason));
            FakeContext context;
            context.source.status = issue({}, object);
            View view(createIssueView(context, nullptr));
            view->show();
            QCOMPARE(roleOf(view, QStringLiteral("Closed")), StatusRole::Neutral);
        }
    }

    // The status keeps them newest first.
    void issueShowsTheLatestFiveCommentsFirst()
    {
        QJsonArray comments;
        for (int i = 21; i >= 1; --i)
            comments.append(comment(QStringLiteral("comment %1.").arg(i)));
        FakeContext context;
        context.source.status = issue(comments);
        View view(createIssueView(context, nullptr));
        view->show();

        const QString text = shownText(view);
        QVERIFY(text.contains(QStringLiteral("comment 21.")));
        QVERIFY(text.contains(QStringLiteral("comment 17.")));
        QVERIFY(!text.contains(QStringLiteral("comment 16.")));
        QVERIFY(text.indexOf(QStringLiteral("comment 21.")) < text.indexOf(QStringLiteral("comment 17.")));
    }

    // Without comments in the data, the issue's count tells none from a
    // failed request.
    void issueTellsNoCommentsFromAFailedRequest()
    {
        QJsonObject object = readObject(QStringLiteral("issue.json"));
        object.insert(QStringLiteral("comments"), 0);
        FakeContext context;
        context.source.status = issue({}, object);
        View none(createIssueView(context, nullptr));
        none->show();
        QVERIFY(shownText(none).contains(QStringLiteral("No comments")));
        QVERIFY(!shownText(none).contains(QStringLiteral("could not be read")));

        context.source.status = issue();
        View failed(createIssueView(context, nullptr));
        failed->show();
        QVERIFY(shownText(failed).contains(QStringLiteral("The comments could not be read.")));
        QVERIFY(!shownText(failed).contains(QStringLiteral("No comments")));
    }

    void nothingFromAServerIsMarkup()
    {
        QJsonObject object = readObject(QStringLiteral("issue.json"));
        object.insert(QStringLiteral("title"), QStringLiteral("<b>bold</b>"));
        object.insert(QStringLiteral("labels"),
                      QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("<i>label</i>")}}});
        FakeContext context;
        context.source.status = issue(QJsonArray{comment(QStringLiteral("<img src=x>"), QStringLiteral("<u>x</u>"))},
                                      object);
        View view(createIssueView(context, nullptr));
        view->show();

        for (const QLabel *label : view->findChildren<QLabel *>())
            QCOMPARE(label->textFormat(), Qt::PlainText);
        QVERIFY(shownText(view).contains(QStringLiteral("<img src=x>")));

        QJsonObject pull = readObject(QStringLiteral("pull_request.json"));
        pull.insert(QStringLiteral("title"), QStringLiteral("<b>pull</b>"));
        context.source.status = pullRequest(pull);
        View pullView(createPullRequestView(context, nullptr));
        pullView->show();
        for (const QLabel *label : pullView->findChildren<QLabel *>())
            QCOMPARE(label->textFormat(), Qt::PlainText);

        QJsonArray jobs = jobsOf(QStringLiteral("run_jobs_failed.json"));
        QJsonObject job = jobs.at(0).toObject();
        job.insert(QStringLiteral("name"), QStringLiteral("<b>name</b>"));
        jobs.replace(0, job);
        context.source.status = singleRun(QStringLiteral("completed"), QStringLiteral("failure"), jobs);
        View runView(createRunView(context, nullptr));
        runView->show();
        for (const QLabel *label : runView->findChildren<QLabel *>())
            QCOMPARE(label->textFormat(), Qt::PlainText);
        QVERIFY(shownText(runView).contains(QStringLiteral("<b>name</b>")));
    }

    // Fields.

    void fieldHidesItsSection_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("field");
        QTest::addColumn<QString>("section");
        QTest::addColumn<QStringList>("others");

        const QStringList pr{QStringLiteral("checks"), QStringLiteral("reviews"), QStringLiteral("people"),
                             QStringLiteral("labels")};
        const QStringList issue{QStringLiteral("details"), QStringLiteral("labels"), QStringLiteral("comments")};
        QTest::newRow("checks") << "pr" << "showChecks" << "checks" << pr;
        QTest::newRow("reviews") << "pr" << "showReviews" << "reviews" << pr;
        QTest::newRow("people") << "pr" << "showPeople" << "people" << pr;
        QTest::newRow("pr labels") << "pr" << "showLabels" << "labels" << pr;
        QTest::newRow("details") << "issue" << "showDetails" << "details" << issue;
        QTest::newRow("issue labels") << "issue" << "showLabels" << "labels" << issue;
        QTest::newRow("comments") << "issue" << "showComments" << "comments" << issue;
    }

    void fieldHidesItsSection()
    {
        QFETCH(QString, kind);
        QFETCH(QString, field);
        QFETCH(QString, section);
        QFETCH(QStringList, others);

        FakeContext context;
        const bool isPullRequest = kind == QLatin1String("pr");
        context.source.status = isPullRequest ? pullRequest() : issue();
        const auto make = [&] {
            return View(isPullRequest ? createPullRequestView(context, nullptr) : createIssueView(context, nullptr));
        };

        // Absent means on.
        View absent = make();
        for (const QString &key : others)
            QVERIFY(summary(absent)->isSectionVisible(key));

        context.itemConfig.insert(field, true);
        View on = make();
        QVERIFY(summary(on)->isSectionVisible(section));

        context.itemConfig.insert(field, false);
        View off = make();
        QVERIFY(!summary(off)->isSectionVisible(section));
        for (const QString &key : others) {
            if (key != section)
                QVERIFY(summary(off)->isSectionVisible(key));
        }
    }

    // The run: drawing.

    void runDrawsFromData()
    {
        FakeContext context;
        context.source.status = failedGroup();
        View view(createRunView(context, nullptr));
        view->show();

        const QString text = shownText(view);
        QVERIFY(text.contains(QStringLiteral("CI #812")));
        QCOMPARE(roleOf(view, QStringLiteral("Failed")), StatusRole::Bad);
        QVERIFY(text.contains(QStringLiteral("main")));
        // From run_started_at, the second attempt's, to updated_at.
        QVERIFY(text.contains(QStringLiteral("10 min 0 s")));
        const QDateTime started(QDate(2026, 10, 29), QTime(9, 30), QTimeZone::UTC);
        QVERIFY(text.contains(QLocale().toString(started.toLocalTime(), QLocale::ShortFormat)));

        // The jobs, in their order, with their durations.
        QVERIFY(summary(view)->isSectionVisible(QStringLiteral("jobs")));
        QVERIFY(text.indexOf(QStringLiteral("build")) < text.indexOf(QStringLiteral("test")));
        QCOMPARE(text.count(QStringLiteral("4 min 55 s")), 2);
        QCOMPARE(roleOf(view, QStringLiteral("Passed")), StatusRole::Good);

        QCOMPARE(summary(view)->actions()->webLink(), QUrl(kRunAddress));
    }

    void runPillsSwitchWithinTheGroup()
    {
        FakeContext context;
        context.source.status = failedGroup();
        succeed(context, postKey(9102, QStringLiteral("rerun")));
        View view(createRunView(context, nullptr));
        view->show();

        // A pill for each run of the group only, named by its workflow.
        QVERIFY(runPillOf(view, 9103));
        QVERIFY(runPillOf(view, 9102));
        QVERIFY(runPillOf(view, 9101));
        QVERIFY(!runPillOf(view, 9090));
        QCOMPARE(runPillOf(view, 9102)->text(), QStringLiteral("Lint"));
        QVERIFY(runPillOf(view, 9103)->isVisibleTo(view.get()));
        QVERIFY(runPillOf(view, 9103)->font().bold());
        QCOMPARE(runPillOf(view, 9103)->role(), StatusRole::Bad);
        QCOMPARE(runPillOf(view, 9102)->role(), StatusRole::Bad);
        QCOMPARE(runPillOf(view, 9101)->role(), StatusRole::Good);

        QTest::mouseClick(runPillOf(view, 9102), Qt::LeftButton);
        QVERIFY(shownText(view).contains(QStringLiteral("Lint #520")));
        QVERIFY(shownText(view).contains(QStringLiteral("Timed out")));
        QVERIFY(shownText(view).contains(QStringLiteral("govulncheck")));
        QVERIFY(!shownText(view).contains(QStringLiteral("test\n")));
        QVERIFY(runPillOf(view, 9102)->font().bold());
        QVERIFY(!runPillOf(view, 9103)->font().bold());
        // The actions and links follow what is shown. No request was needed.
        QCOMPARE(summary(view)->actions()->webLink(),
                 QUrl(QStringLiteral("https://github.com/acme/widgets/actions/runs/9102")));
        QVERIFY(context.client.requests().isEmpty());
        button(view, QStringLiteral("rerun-run"))->click();
        QCOMPARE(context.client.requestKeys(), QStringList{postKey(9102, QStringLiteral("rerun"))});
    }

    void aRunAloneHasNoPillsOrSteps()
    {
        FakeContext context;
        context.source.status = singleRun(QStringLiteral("completed"), QStringLiteral("failure"));
        View view(createRunView(context, nullptr));
        view->show();
        QVERIFY(!runPillOf(view, 9103)->isVisibleTo(view.get()));
        QVERIFY(!button(view, QStringLiteral("previous-run"))->isVisibleTo(view.get()));
        QVERIFY(!button(view, QStringLiteral("next-run"))->isVisibleTo(view.get()));
    }

    // Previous goes back in time, past the group, and an older run's jobs are
    // read when it shows.
    void previousAndNextStepThroughAll()
    {
        FakeContext context;
        context.source.status = failedGroup();
        QVERIFY(context.client.setJsonFile(jobsKey(9090), dataFile(QStringLiteral("run_jobs.json"))));
        View view(createRunView(context, nullptr));
        view->show();

        QPushButton *previous = button(view, QStringLiteral("previous-run"));
        QPushButton *next = button(view, QStringLiteral("next-run"));
        QVERIFY(previous->isVisibleTo(view.get()));
        QVERIFY(previous->isEnabled());
        QVERIFY(!next->isEnabled());

        previous->click();
        QVERIFY(shownText(view).contains(QStringLiteral("Lint #520")));
        previous->click();
        QVERIFY(shownText(view).contains(QStringLiteral("Docs #310")));
        QVERIFY(context.client.requests().isEmpty());

        previous->click();
        QVERIFY(shownText(view).contains(QStringLiteral("CI #811")));
        QVERIFY(shownText(view).contains(QStringLiteral("Loading…")));
        QVERIFY(!previous->isEnabled());
        QCOMPARE(context.client.requestKeys(), QStringList{jobsKey(9090)});
        QTRY_VERIFY(shownText(view).contains(QStringLiteral("build (macos-latest)")));

        next->click();
        QVERIFY(shownText(view).contains(QStringLiteral("Docs #310")));
        next->click();
        next->click();
        QVERIFY(shownText(view).contains(QStringLiteral("CI #812")));
        QVERIFY(!next->isEnabled());

        // What was read shows at once, while it is read again.
        for (int i = 0; i < 3; ++i)
            previous->click();
        QVERIFY(shownText(view).contains(QStringLiteral("build (macos-latest)")));
        QCOMPARE(context.client.requestKeys().size(), 2);
    }

    void anOlderRunWhoseJobsFailSaysSo()
    {
        FakeContext context;
        context.source.status = passedGroup();
        View view(createRunView(context, nullptr));
        view->show();

        QPushButton *previous = button(view, QStringLiteral("previous-run"));
        for (int i = 0; i < 3; ++i)
            previous->click();
        QVERIFY(shownText(view).contains(QStringLiteral("Running Copilot Code Review #300")));
        QCOMPARE(context.client.requestKeys(), QStringList{jobsKey(40552055136)});
        QTRY_VERIFY(shownText(view).contains(QStringLiteral("The jobs could not be read.")));
    }

    // A sibling's jobs that the status could not read are asked for, too.
    void aSiblingWithoutJobsIsReadWhenShown()
    {
        FakeContext context;
        context.source.status = failedGroup(false);
        QVERIFY(context.client.setJsonFile(jobsKey(9102), dataFile(QStringLiteral("run_jobs_lint.json"))));
        View view(createRunView(context, nullptr));
        view->show();

        QTest::mouseClick(runPillOf(view, 9102), Qt::LeftButton);
        QCOMPARE(context.client.requestKeys(), QStringList{jobsKey(9102)});
        QTRY_VERIFY(shownText(view).contains(QStringLiteral("govulncheck")));
    }

    // An answer for what is no longer shown is dropped.
    void aLateAnswerDoesNotDraw()
    {
        FakeContext context;
        context.source.status = failedGroup();
        QVERIFY(context.client.setJsonFile(jobsKey(9090), dataFile(QStringLiteral("run_jobs.json"))));
        View view(createRunView(context, nullptr));
        view->show();

        QPushButton *previous = button(view, QStringLiteral("previous-run"));
        for (int i = 0; i < 3; ++i)
            previous->click();
        button(view, QStringLiteral("next-run"))->click();
        QCoreApplication::processEvents();
        QVERIFY(shownText(view).contains(QStringLiteral("Docs #310")));
        QVERIFY(!shownText(view).contains(QStringLiteral("build (macos-latest)")));
    }

    // A choice stays through a new status while it is still there. Otherwise,
    // and when the lead is shown, the view follows the lead.
    void aNewStatusKeepsTheChoice()
    {
        FakeContext context;
        context.source.status = failedGroup();
        View view(createRunView(context, nullptr));
        view->show();

        QTest::mouseClick(runPillOf(view, 9102), Qt::LeftButton);
        context.source.change(failedGroup());
        QVERIFY(shownText(view).contains(QStringLiteral("Lint #520")));

        context.source.change(singleRun(QStringLiteral("completed"), QStringLiteral("success")));
        QVERIFY(shownText(view).contains(QStringLiteral("CI #812")));
        QVERIFY(!runPillOf(view, 9102));

        QJsonObject newer = runsOf(QStringLiteral("workflow_runs_failed.json")).at(1).toObject();
        newer.insert(QStringLiteral("id"), 9200);
        newer.insert(QStringLiteral("run_number"), 813);
        context.source.change(runStatus(QStringLiteral("main"), newer, {}));
        QVERIFY(shownText(view).contains(QStringLiteral("CI #813")));
        QVERIFY(shownText(view).contains(QStringLiteral("No jobs")));
    }

    // The jobs' links.

    void jobLinksFollowTheRules()
    {
        QJsonArray jobs = jobsOf(QStringLiteral("run_jobs_failed.json"));
        jobs.append(QJsonObject{{QStringLiteral("id"), 91033}, {QStringLiteral("name"), QStringLiteral("deploy")},
                                {QStringLiteral("status"), QStringLiteral("in_progress")},
                                {QStringLiteral("conclusion"), QJsonValue::Null},
                                {QStringLiteral("started_at"), QStringLiteral("2026-10-29T09:40:05Z")},
                                {QStringLiteral("completed_at"), QJsonValue::Null},
                                {QStringLiteral("html_url"),
                                 QStringLiteral("https://github.com/acme/widgets/actions/runs/9103/job/91033")}});
        jobs.append(QJsonObject{{QStringLiteral("id"), 91034}, {QStringLiteral("name"), QStringLiteral("publish")},
                                {QStringLiteral("status"), QStringLiteral("queued")},
                                {QStringLiteral("conclusion"), QJsonValue::Null},
                                {QStringLiteral("started_at"), QJsonValue::Null},
                                {QStringLiteral("html_url"),
                                 QStringLiteral("https://github.com/acme/widgets/actions/runs/9103/job/91034")}});
        FakeContext context;
        context.source.status = singleRun(QStringLiteral("in_progress"), {}, jobs);
        View view(createRunView(context, nullptr));
        view->show();

        // Completed: a log and a re-run.
        QVERIFY(button(view, QStringLiteral("log-91031")));
        QVERIFY(button(view, QStringLiteral("rerun-job-91031")));
        QVERIFY(button(view, QStringLiteral("log-91032")));
        QVERIFY(button(view, QStringLiteral("rerun-job-91032")));
        // Started, not completed: a log only.
        QVERIFY(button(view, QStringLiteral("log-91033")));
        QVERIFY(!button(view, QStringLiteral("rerun-job-91033")));
        // Not started: neither.
        QVERIFY(!button(view, QStringLiteral("log-91034")));
        QVERIFY(!button(view, QStringLiteral("rerun-job-91034")));
        QCOMPARE(roleOf(view, QStringLiteral("Queued")), StatusRole::Waiting);

        button(view, QStringLiteral("log-91032"))->click();
        QCOMPARE(m_opened,
                 QList<QUrl>{QUrl(QStringLiteral("https://github.com/acme/widgets/actions/runs/9103/job/91032"))});
        QVERIFY(context.pages.isEmpty());
    }

    // The run's actions.

    void runActionsFollowTheRules_data()
    {
        QTest::addColumn<QString>("status");
        QTest::addColumn<QString>("conclusion");
        QTest::addColumn<bool>("rerunFailed");
        QTest::addColumn<bool>("rerun");
        QTest::addColumn<bool>("cancel");

        QTest::newRow("failure") << "completed" << "failure" << true << true << false;
        QTest::newRow("timed_out") << "completed" << "timed_out" << true << true << false;
        QTest::newRow("startup_failure") << "completed" << "startup_failure" << true << true << false;
        QTest::newRow("success") << "completed" << "success" << false << true << false;
        QTest::newRow("cancelled") << "completed" << "cancelled" << false << true << false;
        QTest::newRow("in_progress") << "in_progress" << "" << false << false << true;
        QTest::newRow("queued") << "queued" << "" << false << false << true;
        QTest::newRow("waiting") << "waiting" << "" << false << false << true;
        QTest::newRow("requested") << "requested" << "" << false << false << true;
        QTest::newRow("pending") << "pending" << "" << false << false << true;
    }

    void runActionsFollowTheRules()
    {
        QFETCH(QString, status);
        QFETCH(QString, conclusion);
        QFETCH(bool, rerunFailed);
        QFETCH(bool, rerun);
        QFETCH(bool, cancel);

        FakeContext context;
        context.source.status = singleRun(status, conclusion);
        View view(createRunView(context, nullptr));
        QCOMPARE(showsAction(view, QStringLiteral("rerun-failed")), rerunFailed);
        QCOMPARE(showsAction(view, QStringLiteral("rerun-run")), rerun);
        QCOMPARE(showsAction(view, QStringLiteral("cancel-run")), cancel);
    }

    void runActionsPost()
    {
        FakeContext context;
        context.source.status = failedGroup();
        const QString failedKey = postKey(9103, QStringLiteral("rerun-failed-jobs"));
        const QString rerunKey = postKey(9103, QStringLiteral("rerun"));
        const QString jobKey = QStringLiteral("POST repos/acme/widgets/actions/jobs/91032/rerun");
        succeed(context, failedKey);
        succeed(context, rerunKey);
        succeed(context, jobKey);
        View view(createRunView(context, nullptr));

        button(view, QStringLiteral("rerun-failed"))->click();
        QCOMPARE(context.client.requestKeys(), QStringList{failedKey});
        QCOMPARE(context.client.requests().first().body, QJsonDocument(QJsonObject()));
        QTRY_COMPARE(context.source.refreshes, 1);

        button(view, QStringLiteral("rerun-run"))->click();
        QCOMPARE(context.client.requestKeys(), (QStringList{failedKey, rerunKey}));
        QTRY_COMPARE(context.source.refreshes, 2);

        QPushButton *job = button(view, QStringLiteral("rerun-job-91032"));
        job->click();
        QVERIFY(!job->isEnabled());
        QCOMPARE(context.client.requestKeys(), (QStringList{failedKey, rerunKey, jobKey}));
        QTRY_COMPARE(context.source.refreshes, 3);
        QVERIFY(job->isEnabled());
    }

    void cancellingAsksFirst()
    {
        FakeContext context;
        context.source.status = singleRun(QStringLiteral("in_progress"), {});
        const QString key = postKey(9103, QStringLiteral("cancel"));
        succeed(context, key);
        View view(createRunView(context, nullptr));
        QPushButton *cancel = button(view, QStringLiteral("cancel-run"));

        withDialog(cancel, [](QDialog *dialog) {
            auto *question = qobject_cast<QMessageBox *>(dialog);
            QVERIFY(question);
            QCOMPARE(question->text(), QStringLiteral("Cancel CI #812?"));
            QCOMPARE(question->textFormat(), Qt::PlainText);
            question->button(QMessageBox::No)->click();
        });
        QVERIFY(context.client.requests().isEmpty());

        answerQuestion(cancel, QMessageBox::Yes);
        QCOMPARE(context.client.requestKeys(), QStringList{key});
        QTRY_COMPARE(context.source.refreshes, 1);
    }

    void aViewThatGoesWhileTheQuestionIsOpenSendsNothing()
    {
        FakeContext context;
        context.source.status = singleRun(QStringLiteral("in_progress"), {});
        auto *view = createRunView(context, nullptr);

        QPushButton *start = view->findChild<QPushButton *>(QStringLiteral("cancel-run"));
        QTimer::singleShot(0, qApp, [view] { delete view; });
        start->click();
        QVERIFY(context.client.requests().isEmpty());
    }

    // Failures.

    void failureShowsTheMessage()
    {
        FakeContext context;
        context.source.status = singleRun(QStringLiteral("completed"), QStringLiteral("failure"));
        const QString key = postKey(9103, QStringLiteral("rerun"));
        context.client.setReply(key, failure(ApiReply::Error::Http, 500, QStringLiteral("Server error (500)")));
        View view(createRunView(context, nullptr));

        QPushButton *rerun = button(view, QStringLiteral("rerun-run"));
        rerun->click();
        QTRY_COMPARE(errorOf(view), QStringLiteral("Server error (500)"));
        QVERIFY(rerun->isEnabled());
        QCOMPARE(context.source.refreshes, 0);
    }

    void forbiddenNeedsWriteAccess()
    {
        FakeContext context;
        context.source.status = singleRun(QStringLiteral("completed"), QStringLiteral("failure"));
        const QString key = postKey(9103, QStringLiteral("rerun-failed-jobs"));
        context.client.setReply(key, failure(ApiReply::Error::Forbidden, 403, QStringLiteral("Forbidden (403)")));
        View view(createRunView(context, nullptr));

        button(view, QStringLiteral("rerun-failed"))->click();
        QTRY_COMPARE(errorOf(view), kTokenMessage);
        QCOMPARE(context.source.refreshes, 0);

        // Another run shown clears the error.
        context.source.change(failedGroup());
        QTest::mouseClick(runPillOf(view, 9102), Qt::LeftButton);
        QVERIFY(errorOf(view).isEmpty());
    }

    void jobRerunShowsAndClearsErrors()
    {
        FakeContext context;
        context.source.status = singleRun(QStringLiteral("completed"), QStringLiteral("failure"));
        const QString key = QStringLiteral("POST repos/acme/widgets/actions/jobs/91032/rerun");
        context.client.setReply(key, failure(ApiReply::Error::Forbidden, 403, QStringLiteral("Forbidden (403)")));
        View view(createRunView(context, nullptr));

        QPushButton *rerun = button(view, QStringLiteral("rerun-job-91032"));
        rerun->click();
        QVERIFY(!rerun->isEnabled());
        QTRY_COMPARE(errorOf(view), kTokenMessage);
        QVERIFY(rerun->isEnabled());

        // It is not in the action row, so it clears the old error itself.
        succeed(context, key);
        rerun->click();
        QVERIFY(errorOf(view).isEmpty());
        QTRY_COMPARE(context.source.refreshes, 1);
    }

    // The refresh.

    void refreshRunsWhileTheShownRunHasNotCompleted_data()
    {
        QTest::addColumn<QString>("status");
        QTest::addColumn<QString>("conclusion");
        QTest::addColumn<bool>("refreshes");

        QTest::newRow("in_progress") << "in_progress" << "" << true;
        QTest::newRow("queued") << "queued" << "" << true;
        QTest::newRow("waiting") << "waiting" << "" << true;
        QTest::newRow("pending") << "pending" << "" << true;
        QTest::newRow("success") << "completed" << "success" << false;
        QTest::newRow("failure") << "completed" << "failure" << false;
        QTest::newRow("action_required") << "completed" << "action_required" << false;
    }

    void refreshRunsWhileTheShownRunHasNotCompleted()
    {
        QFETCH(QString, status);
        QFETCH(QString, conclusion);
        QFETCH(bool, refreshes);

        FakeContext context;
        context.source.status = singleRun(status, conclusion);
        View view(createRunView(context, nullptr));
        view->show();
        QCOMPARE(refreshTimer(view)->isActive(), refreshes);
        QCOMPARE(refreshTimer(view)->interval(), 15000);
    }

    void refreshStartsAndStopsWithTheStatus()
    {
        FakeContext context;
        context.source.status = singleRun(QStringLiteral("in_progress"), {});
        View view(createRunView(context, nullptr));
        view->show();
        QVERIFY(refreshTimer(view)->isActive());

        // It asks at each interval.
        refreshTimer(view)->setInterval(1);
        QTRY_VERIFY(context.source.refreshes >= 2);

        context.source.change(singleRun(QStringLiteral("completed"), QStringLiteral("success")));
        QVERIFY(!refreshTimer(view)->isActive());
        const int asked = context.source.refreshes;
        QTest::qWait(20);
        QCOMPARE(context.source.refreshes, asked);

        context.source.change(singleRun(QStringLiteral("queued"), {}));
        QVERIFY(refreshTimer(view)->isActive());

        context.source.change(runStatus(QStringLiteral("main"), {}, {}));
        QVERIFY(!refreshTimer(view)->isActive());
    }

    void refreshStopsWhileHidden()
    {
        FakeContext context;
        context.source.status = singleRun(QStringLiteral("in_progress"), {});
        View view(createRunView(context, nullptr));

        // Not shown yet.
        QVERIFY(!refreshTimer(view)->isActive());
        view->show();
        QVERIFY(refreshTimer(view)->isActive());

        view->hide();
        QVERIFY(!refreshTimer(view)->isActive());
        // A status that comes while hidden does not start it.
        context.source.change(singleRun(QStringLiteral("in_progress"), {}));
        QVERIFY(!refreshTimer(view)->isActive());

        view->show();
        QVERIFY(refreshTimer(view)->isActive());

        // The same when a parent hides, as a tab does.
        QWidget parent;
        view->setParent(&parent);
        parent.show();
        view->show();
        QVERIFY(refreshTimer(view)->isActive());
        parent.hide();
        QVERIFY(!refreshTimer(view)->isActive());
        view->setParent(nullptr);
    }

    // It follows the run shown, not the lead.
    void refreshFollowsTheShownRun()
    {
        ItemStatus status = failedGroup();
        QJsonArray siblings = status.data.value(QStringLiteral("siblings")).toArray();
        QJsonObject entry = siblings.at(0).toObject();
        QJsonObject run = entry.value(QStringLiteral("run")).toObject();
        run.insert(QStringLiteral("status"), QStringLiteral("in_progress"));
        run.insert(QStringLiteral("conclusion"), QJsonValue::Null);
        entry.insert(QStringLiteral("run"), run);
        siblings.replace(0, entry);
        status.data.insert(QStringLiteral("siblings"), siblings);
        const qint64 runningId = run.value(QStringLiteral("id")).toInteger();

        FakeContext context;
        context.source.status = status;
        View view(createRunView(context, nullptr));
        view->show();
        QVERIFY(!refreshTimer(view)->isActive());

        QTest::mouseClick(runPillOf(view, runningId), Qt::LeftButton);
        QVERIFY(refreshTimer(view)->isActive());
        QVERIFY(showsAction(view, QStringLiteral("cancel-run")));

        QTest::mouseClick(runPillOf(view, 9103), Qt::LeftButton);
        QVERIFY(!refreshTimer(view)->isActive());
    }

    // The pull request's link.

    void pullRequestLinkOfALinkedPullRequest()
    {
        FakeContext context;
        context.itemConfig.insert(QStringLiteral("branchSource"), QStringLiteral("pullRequest"));
        ItemStatus status = passedGroup();
        status.data.insert(QStringLiteral("pullRequest"), readObject(QStringLiteral("pull_request.json")));
        context.source.status = status;
        View view(createRunView(context, nullptr));
        QVERIFY(showsAction(view, QStringLiteral("pull-request")));
        button(view, QStringLiteral("pull-request"))->click();
        QCOMPARE(m_opened, QList<QUrl>{QUrl(kPrAddress)});
        QVERIFY(context.pages.isEmpty());

        // Without the pull request in the data, from the configuration.
        m_opened.clear();
        context.source.status = passedGroup();
        View built(createRunView(context, nullptr));
        button(built, QStringLiteral("pull-request"))->click();
        QCOMPARE(m_opened, QList<QUrl>{QUrl(kBuiltPrAddress)});
    }

    void pullRequestLinkOfTheShownRun()
    {
        FakeContext context;
        context.itemConfig.insert(QStringLiteral("branchSource"), QStringLiteral("fixed"));
        context.source.status = passedGroup();
        View view(createRunView(context, nullptr));
        QVERIFY(showsAction(view, QStringLiteral("pull-request")));
        button(view, QStringLiteral("pull-request"))->click();
        QCOMPARE(m_opened, QList<QUrl>{QUrl(kBuiltPrAddress)});

        // Runs of a push have none.
        context.source.change(failedGroup());
        QVERIFY(!showsAction(view, QStringLiteral("pull-request")));
    }

    // The links of the summaries.

    void openPageShowsThePage()
    {
        FakeContext context;
        context.source.status = pullRequest();
        View view(createPullRequestView(context, nullptr));
        view->show();

        QTest::mouseClick(button(view, QStringLiteral("open-page")), Qt::LeftButton);
        QCOMPARE(context.pages, QList<QUrl>{QUrl(kPrAddress)});
        QVERIFY(m_opened.isEmpty());
    }

    void shiftOpensTheBrowserInstead()
    {
        FakeContext context;
        context.source.status = failedGroup();
        View view(createRunView(context, nullptr));
        view->show();

        QTest::mouseClick(button(view, QStringLiteral("open-page")), Qt::LeftButton, Qt::ShiftModifier);
        QVERIFY(context.pages.isEmpty());
        QCOMPARE(m_opened, QList<QUrl>{QUrl(kRunAddress)});

        QTest::mouseClick(button(view, QStringLiteral("open-page")), Qt::LeftButton);
        QCOMPARE(context.pages, QList<QUrl>{QUrl(kRunAddress)});
        QCOMPARE(m_opened.size(), 1);
    }

    void webLinkUsesTheDataThenTheConfiguration()
    {
        FakeContext context;
        context.source.status = pullRequest();
        View fromData(createPullRequestView(context, nullptr));
        QCOMPARE(summary(fromData)->actions()->webLink(), QUrl(kPrAddress));
        QVERIFY(!button(fromData, QStringLiteral("web"))->isHidden());

        QJsonObject object = readObject(QStringLiteral("pull_request.json"));
        object.remove(QStringLiteral("html_url"));
        context.source.status = pullRequest(object);
        View built(createPullRequestView(context, nullptr));
        QCOMPARE(summary(built)->actions()->webLink(), QUrl(kBuiltPrAddress));

        // An address of another kind is not used.
        object.insert(QStringLiteral("html_url"), QStringLiteral("file:///etc/passwd"));
        context.source.status = pullRequest(object);
        View other(createPullRequestView(context, nullptr));
        QCOMPARE(summary(other)->actions()->webLink(), QUrl(kBuiltPrAddress));

        // Without any address, Web and Open page hide.
        object.remove(QStringLiteral("html_url"));
        context.general.insert(QStringLiteral("baseUrl"), QStringLiteral("not a server"));
        context.source.status = pullRequest(object);
        View none(createPullRequestView(context, nullptr));
        QVERIFY(summary(none)->actions()->webLink().isEmpty());
        QVERIFY(button(none, QStringLiteral("web"))->isHidden());
        QVERIFY(!showsAction(none, QStringLiteral("open-page")));
    }

    void webLinkOfAnIssueUsesTheDataThenTheConfiguration()
    {
        FakeContext context;
        context.itemConfig.insert(QStringLiteral("number"), 7);
        context.source.status = issue();
        View fromData(createIssueView(context, nullptr));
        QCOMPARE(summary(fromData)->actions()->webLink(), QUrl(kIssueAddress));

        QJsonObject object = readObject(QStringLiteral("issue.json"));
        object.remove(QStringLiteral("html_url"));
        context.source.status = issue({}, object);
        View built(createIssueView(context, nullptr));
        QCOMPARE(summary(built)->actions()->webLink(), QUrl(QStringLiteral("https://built.example/acme/widgets/issues/7")));
    }

    void branchWithoutRunsShowsAMessage()
    {
        FakeContext context;
        context.source.status = failedGroup();
        View view(createRunView(context, nullptr));
        view->show();

        context.source.change(runStatus(QStringLiteral("main"), {}, {}));
        QVERIFY(shownText(view).contains(QStringLiteral("No runs on main")));
        QVERIFY(!shownText(view).contains(QStringLiteral("CI #812")));
        QVERIFY(!summary(view)->isSectionVisible(QStringLiteral("jobs")));
        for (const char *id : {"rerun-failed", "rerun-run", "cancel-run", "pull-request", "open-page"})
            QVERIFY(!showsAction(view, QLatin1String(id)));
        QVERIFY(summary(view)->actions()->webLink().isEmpty());
        QVERIFY(button(view, QStringLiteral("web"))->isHidden());
    }

    // No data, and old data.

    void noDataShowsLoadingOrTheMessage()
    {
        FakeContext context;
        View loading(createPullRequestView(context, nullptr));
        loading->show();
        QCOMPARE(shownText(loading), QStringLiteral("Loading…"));

        ItemStatus problem;
        problem.state = ItemStatus::State::NoToken;
        problem.message = QStringLiteral("No token");
        context.source.change(problem);
        QCOMPARE(shownText(loading), QStringLiteral("No token"));

        context.source.status = {};
        context.source.status.message = QStringLiteral("The main folder is not on a branch.");
        View run(createRunView(context, nullptr));
        run->show();
        QCOMPARE(shownText(run), QStringLiteral("The main folder is not on a branch."));

        context.source.status = {};
        View issueView(createIssueView(context, nullptr));
        issueView->show();
        QCOMPARE(shownText(issueView), QStringLiteral("Loading…"));
    }

    void withoutAStatusSourceItStaysAtLoading()
    {
        FakeContext context;
        context.hasStatus = false;
        View view(createIssueView(context, nullptr));
        view->show();
        QCOMPARE(shownText(view), QStringLiteral("Loading…"));
    }

    void oldDataStaysThroughAnError()
    {
        FakeContext context;
        context.source.status = pullRequest();
        View view(createPullRequestView(context, nullptr));
        view->show();

        ItemStatus kept = pullRequest();
        kept.state = ItemStatus::State::Error;
        kept.message = QStringLiteral("Network error");
        context.source.change(kept);
        QVERIFY(shownText(view).contains(QStringLiteral("Skip token refresh")));
        QVERIFY(!shownText(view).contains(QStringLiteral("Network error")));

        ItemStatus empty;
        empty.state = ItemStatus::State::Error;
        empty.message = QStringLiteral("Network error");
        context.source.change(empty);
        QVERIFY(shownText(view).contains(QStringLiteral("Skip token refresh")));
        QVERIFY(!shownText(view).contains(QStringLiteral("Network error")));
    }

    // A message without an error, such as a main folder that left its branch,
    // replaces what is drawn, and new data replaces the message.
    void messageWithoutAnErrorReplacesOldData()
    {
        FakeContext context;
        context.source.status = failedGroup();
        View view(createRunView(context, nullptr));
        view->show();
        QVERIFY(shownText(view).contains(QStringLiteral("CI #812")));

        ItemStatus detached;
        detached.message = QStringLiteral("The main folder is not on a branch.");
        context.source.change(detached);
        QCOMPARE(shownText(view), QStringLiteral("The main folder is not on a branch."));

        context.source.change(failedGroup());
        QVERIFY(!shownText(view).contains(QStringLiteral("The main folder is not on a branch.")));
        QVERIFY(shownText(view).contains(QStringLiteral("CI #812")));
    }

private:
    QList<QUrl> m_opened;
};

QTEST_MAIN(TestSummaryViews)
#include "tst_summary_views.moc"
