// The three GitLab summaries, drawn from responses recorded from a GitLab
// server, in tests/data/. "Me" is the user in data/user.json, id 1001. A handler
// stands in for the external browser, so nothing is opened.
#include "../GitLabIssueView.h"
#include "../GitLabMergeRequestView.h"
#include "../GitLabPipelineView.h"
#include "../GitLabStatus.h"

#include <mirante/ItemContext.h>
#include <mirante/testing/FakeApiClient.h>
#include <mirante/widgets/ActionRow.h>
#include <mirante/widgets/SummaryView.h>

#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTest>
#include <QTimeZone>
#include <QTimer>

#include <functional>
#include <memory>
#include <vector>

using mirante::ApiReply;
using mirante::ItemStatus;
using mirante::testing::FakeApiClient;
using mirante::widgets::SummaryView;
using namespace mirante::modules::gitlab;

namespace {

const QString kProject = QStringLiteral("api/v4/projects/acme%2Fbackend");
const QString kMergeRequest = kProject + QStringLiteral("/merge_requests/42");
const QString kIssue = kProject + QStringLiteral("/issues/7");
const QString kPipeline = kProject + QStringLiteral("/pipelines/9001");
const QString kMrAddress = QStringLiteral("https://gitlab.example/acme/backend/-/merge_requests/42");
const QString kIssueAddress = QStringLiteral("https://gitlab.example/acme/backend/-/issues/7");
const QString kPipelineAddress = QStringLiteral("https://gitlab.example/acme/backend/-/pipelines/9001");
const QString kScopeMessage = QStringLiteral("The token cannot do this. It needs the api scope.");

QJsonDocument readJson(const QString &name)
{
    QFile file(QFINDTESTDATA(QStringLiteral("data/") + name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot read %s", qPrintable(name));
    return QJsonDocument::fromJson(file.readAll());
}

QJsonObject readObject(const QString &name)
{
    return readJson(name).object();
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
    QJsonObject itemConfig{{QStringLiteral("project"), QStringLiteral("acme/backend")}, {QStringLiteral("iid"), 42}};
    QJsonObject general{{QStringLiteral("baseUrl"), QStringLiteral("https://built.example")}};
    QList<QUrl> pages;
    FakeApiClient client{QUrl(QStringLiteral("https://built.example"))};
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

bool showsAction(const View &view, const QString &id)
{
    return summary(view)->actions()->isActionVisible(id);
}

QString errorOf(const View &view)
{
    return summary(view)->actions()->error();
}

ItemStatus mergeRequestWith(const QJsonObject &mergeRequest, const QJsonObject &approvals, const QJsonObject &user)
{
    return mergeRequestStatus(mergeRequest, approvals, user);
}

// acme/backend!42 with a failed head pipeline, approved by others.
ItemStatus failedMergeRequest()
{
    ItemStatus status = mergeRequestWith(readObject(QStringLiteral("merge_request.json")),
                                         readObject(QStringLiteral("approvals.json")),
                                         readObject(QStringLiteral("user.json")));
    status.data.insert(QStringLiteral("jobs"), readJson(QStringLiteral("pipeline_jobs.json")).array());
    return status;
}

// A merge request whose head pipeline has this status.
ItemStatus mergeRequestWithPipeline(const QString &pipelineStatus)
{
    QJsonObject mergeRequest = readObject(QStringLiteral("merge_request.json"));
    QJsonObject pipeline = mergeRequest.value(QStringLiteral("head_pipeline")).toObject();
    pipeline.insert(QStringLiteral("status"), pipelineStatus);
    mergeRequest.insert(QStringLiteral("head_pipeline"), pipeline);
    return mergeRequestWith(mergeRequest, readObject(QStringLiteral("approvals.json")),
                            readObject(QStringLiteral("user.json")));
}

// acme/backend#7 with the notes, leaving them out if notes is null.
ItemStatus openIssue(const QJsonValue &notes = QJsonValue(QJsonValue::Undefined))
{
    ItemStatus status = issueStatus(readObject(QStringLiteral("issue.json")));
    if (!notes.isUndefined())
        status.data.insert(QStringLiteral("notes"), notes);
    return status;
}

ItemStatus issueInState(const QString &state)
{
    QJsonObject issue = readObject(QStringLiteral("issue.json"));
    issue.insert(QStringLiteral("state"), state);
    return issueStatus(issue);
}

ItemStatus pipelineWithStatus(const QString &state)
{
    QJsonObject pipeline = readObject(QStringLiteral("pipeline_latest.json"));
    pipeline.insert(QStringLiteral("status"), state);
    return pipelineStatus(QStringLiteral("feature/retry-button"), pipeline,
                          readJson(QStringLiteral("pipeline_jobs.json")).array());
}

QJsonObject note(const QString &body)
{
    return QJsonObject{{QStringLiteral("body"), body},
                       {QStringLiteral("author"), QJsonObject{{QStringLiteral("name"), QStringLiteral("Bruno")}}},
                       {QStringLiteral("created_at"), QStringLiteral("2026-10-26T12:00:00.000Z")}};
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

void writeComment(QPushButton *start, const QString &text)
{
    withDialog(start, [text](QDialog *dialog) {
        dialog->findChild<QPlainTextEdit *>()->setPlainText(text);
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
}

void answerQuestion(QPushButton *start, QMessageBox::StandardButton answer)
{
    withDialog(start, [answer](QDialog *dialog) {
        auto *question = qobject_cast<QMessageBox *>(dialog);
        QVERIFY(question);
        question->button(answer)->click();
    });
}

// Sets up the fake server for a preview of acme/backend!42.
void setUpMergeRequestPreview(FakeContext &context)
{
    QVERIFY(context.client.setJsonFile(QStringLiteral("GET ") + kMergeRequest,
                                       QFINDTESTDATA("data/merge_request.json")));
    QVERIFY(context.client.setJsonFile(QStringLiteral("GET ") + kMergeRequest + QStringLiteral("/approvals"),
                                       QFINDTESTDATA("data/approvals.json")));
    QVERIFY(context.client.setJsonFile(QStringLiteral("GET api/v4/user"), QFINDTESTDATA("data/user.json")));
    QVERIFY(context.client.setJsonFile(QStringLiteral("GET ") + kPipeline + QStringLiteral("/jobs?per_page=100"),
                                       QFINDTESTDATA("data/pipeline_jobs.json")));
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

    // Drawing.

    void mergeRequestDrawsFromData()
    {
        FakeContext context;
        context.source.status = failedMergeRequest();
        View view(createMergeRequestView(context, nullptr));
        view->show();

        const QString text = shownText(view);
        QVERIFY(text.contains(QStringLiteral("Add the retry button to the pipeline panel")));
        QVERIFY(text.contains(QStringLiteral("Open")));
        QVERIFY(text.contains(QStringLiteral("feature/retry-button → main")));
        QVERIFY(!text.contains(QStringLiteral("Draft")));

        QVERIFY(text.contains(QStringLiteral("Failed")));
        QVERIFY(text.contains(QStringLiteral("#9001")));
        // The failed jobs only, with a duration and a log link for each.
        QVERIFY(text.contains(QStringLiteral("test:unit")));
        QVERIFY(text.contains(QStringLiteral("5 min 2 s")));
        QVERIFY(text.contains(QStringLiteral("test:lint")));
        QVERIFY(text.contains(QStringLiteral("Failed, allowed")));
        QVERIFY(!text.contains(QStringLiteral("deploy:review")));
        QVERIFY(button(view, QStringLiteral("log-70002")));
        // A merge request's job has no retry of its own.
        QVERIFY(!button(view, QStringLiteral("retry-job-70002")));

        QVERIFY(text.contains(QStringLiteral("2 of 3 approvals")));
        QVERIFY(text.contains(QStringLiteral("Approved by Carla Reviewer, Dora Maintainer")));
        QVERIFY(text.contains(QStringLiteral("Author: Bruno Author")));
        QVERIFY(text.contains(QStringLiteral("Assignees: Bruno Author")));
        QVERIFY(text.contains(QStringLiteral("Reviewers: Ana Reviewer, Carla Reviewer")));
        QVERIFY(text.contains(QStringLiteral("frontend")));
        QVERIFY(text.contains(QStringLiteral("ci")));
    }

    void mergeRequestShowsADraftAndNoPipeline()
    {
        QJsonObject mergeRequest = readObject(QStringLiteral("merge_request_no_pipeline.json"));
        mergeRequest.insert(QStringLiteral("draft"), true);
        FakeContext context;
        context.source.status =
            mergeRequestWith(mergeRequest, QJsonObject(), readObject(QStringLiteral("user.json")));
        View view(createMergeRequestView(context, nullptr));
        view->show();

        const QString text = shownText(view);
        QVERIFY(text.contains(QStringLiteral("Draft")));
        QVERIFY(text.contains(QStringLiteral("No pipeline")));
        QVERIFY(text.contains(QStringLiteral("No approvals")));
    }

    void pipelineDrawsFromData()
    {
        FakeContext context;
        context.source.status = pipelineWithStatus(QStringLiteral("failed"));
        View view(createPipelineView(context, nullptr));
        view->show();

        const QString text = shownText(view);
        QVERIFY(text.contains(QStringLiteral("Pipeline #9001")));
        QVERIFY(text.contains(QStringLiteral("Failed")));
        QVERIFY(text.contains(QStringLiteral("feature/retry-button")));
        QVERIFY(text.contains(QStringLiteral("11 min 39 s")));

        // One section per stage, in the order of the jobs.
        for (const char *stage : {"build", "test", "deploy"})
            QVERIFY(summary(view)->isSectionVisible(QStringLiteral("stage:") + QLatin1String(stage)));
        QVERIFY(text.indexOf(QStringLiteral("build")) < text.indexOf(QStringLiteral("test:unit")));
        QVERIFY(text.indexOf(QStringLiteral("test:unit")) < text.indexOf(QStringLiteral("deploy:review")));

        QVERIFY(text.contains(QStringLiteral("Passed")));
        QVERIFY(text.contains(QStringLiteral("Skipped")));
        QVERIFY(text.contains(QStringLiteral("3 min 32 s")));

        // Only failed jobs have a log link and a Retry button.
        QVERIFY(button(view, QStringLiteral("log-70002")));
        QVERIFY(button(view, QStringLiteral("retry-job-70002")));
        QVERIFY(!button(view, QStringLiteral("log-70001")));
        QVERIFY(!button(view, QStringLiteral("retry-job-70001")));
    }

    void pipelineStageThatGoesIsHidden()
    {
        FakeContext context;
        context.source.status = pipelineWithStatus(QStringLiteral("failed"));
        View view(createPipelineView(context, nullptr));
        view->show();

        const QJsonArray fewer{readJson(QStringLiteral("pipeline_jobs.json")).array().at(0)};
        context.source.change(pipelineStatus(QStringLiteral("main"), readObject(QStringLiteral("pipeline_latest.json")),
                                             fewer));
        QVERIFY(summary(view)->isSectionVisible(QStringLiteral("stage:build")));
        QVERIFY(!summary(view)->isSectionVisible(QStringLiteral("stage:test")));
    }

    void issueDrawsFromData()
    {
        FakeContext context;
        context.source.status = openIssue(readJson(QStringLiteral("issue_notes.json")).array());
        View view(createIssueView(context, nullptr));
        view->show();

        const QString text = shownText(view);
        QVERIFY(text.contains(QStringLiteral("The pipeline panel does not show manual jobs")));
        QVERIFY(text.contains(QStringLiteral("Open")));
        QVERIFY(text.contains(QStringLiteral("Assignees: Ana Reviewer")));
        QVERIFY(text.contains(QStringLiteral("Milestone: M6")));
        const QString due = QLocale().toString(QDate(2026, 11, 15), QLocale::ShortFormat);
        QVERIFY(text.contains(QStringLiteral("Due: ") + due));
        QVERIFY(text.contains(QStringLiteral("bug")));

        const QDateTime written(QDate(2026, 10, 26), QTime(12, 0), QTimeZone::UTC);
        QVERIFY(text.contains(QStringLiteral("Bruno Author · ")
                              + QLocale().toString(written.toLocalTime(), QLocale::ShortFormat)));
        QVERIFY(text.contains(QStringLiteral("Fixed on the branch. Please check it on your side.")));
    }

    void issueShowsTheLatestFiveCommentsFirst()
    {
        QJsonArray notes;
        for (int i = 1; i <= 7; ++i)
            notes.append(note(QStringLiteral("comment %1").arg(i)));
        FakeContext context;
        context.source.status = openIssue(notes);
        View view(createIssueView(context, nullptr));
        view->show();

        const QString text = shownText(view);
        QVERIFY(text.contains(QStringLiteral("comment 1")));
        QVERIFY(text.contains(QStringLiteral("comment 5")));
        QVERIFY(!text.contains(QStringLiteral("comment 6")));
        QVERIFY(text.indexOf(QStringLiteral("comment 1")) < text.indexOf(QStringLiteral("comment 5")));
    }

    void issueTellsNoCommentsFromAFailedRequest()
    {
        FakeContext context;
        context.source.status = openIssue(QJsonArray());
        View none(createIssueView(context, nullptr));
        none->show();
        QVERIFY(shownText(none).contains(QStringLiteral("No comments")));

        context.source.status = openIssue();
        View failed(createIssueView(context, nullptr));
        failed->show();
        QVERIFY(shownText(failed).contains(QStringLiteral("The comments could not be read.")));
    }

    void nothingFromAServerIsMarkup()
    {
        QJsonObject issue = readObject(QStringLiteral("issue.json"));
        issue.insert(QStringLiteral("title"), QStringLiteral("<b>bold</b>"));
        issue.insert(QStringLiteral("labels"), QJsonArray{QStringLiteral("<i>label</i>")});
        ItemStatus status = issueStatus(issue);
        status.data.insert(QStringLiteral("notes"), QJsonArray{note(QStringLiteral("<img src=x>"))});
        FakeContext context;
        context.source.status = status;
        View view(createIssueView(context, nullptr));
        view->show();

        for (const QLabel *label : view->findChildren<QLabel *>())
            QCOMPARE(label->textFormat(), Qt::PlainText);
        QVERIFY(shownText(view).contains(QStringLiteral("<img src=x>")));
    }

    // Fields.

    void fieldHidesItsSection_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("field");
        QTest::addColumn<QString>("section");
        QTest::addColumn<QStringList>("others");

        const QStringList mr{QStringLiteral("pipeline"), QStringLiteral("approvals"), QStringLiteral("people"),
                             QStringLiteral("labels")};
        const QStringList issue{QStringLiteral("details"), QStringLiteral("labels"), QStringLiteral("comments")};
        QTest::newRow("pipeline") << "mr" << "showPipeline" << "pipeline" << mr;
        QTest::newRow("approvals") << "mr" << "showApprovals" << "approvals" << mr;
        QTest::newRow("people") << "mr" << "showPeople" << "people" << mr;
        QTest::newRow("mr labels") << "mr" << "showLabels" << "labels" << mr;
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
        const bool isMergeRequest = kind == QLatin1String("mr");
        context.source.status = isMergeRequest ? failedMergeRequest() : openIssue(QJsonArray());
        const auto make = [&] {
            return View(isMergeRequest ? createMergeRequestView(context, nullptr) : createIssueView(context, nullptr));
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

    // The buttons' rules.

    void approveFollowsTheRules()
    {
        const QJsonObject mergeRequest = readObject(QStringLiteral("merge_request.json"));
        const QJsonObject user = readObject(QStringLiteral("user.json"));

        FakeContext context;
        context.source.status = mergeRequestWith(mergeRequest, readObject(QStringLiteral("approvals.json")), user);
        View view(createMergeRequestView(context, nullptr));
        QVERIFY(showsAction(view, QStringLiteral("approve")));

        // Approved by me.
        const QJsonObject carla{{QStringLiteral("id"), 3003}, {QStringLiteral("username"), QStringLiteral("carla")}};
        context.source.change(
            mergeRequestWith(mergeRequest, readObject(QStringLiteral("approvals_approved.json")), carla));
        QVERIFY(!showsAction(view, QStringLiteral("approve")));

        // Approved by someone else only.
        context.source.change(
            mergeRequestWith(mergeRequest, readObject(QStringLiteral("approvals_approved.json")), user));
        QVERIFY(showsAction(view, QStringLiteral("approve")));

        // Not open.
        context.source.change(mergeRequestWith(readObject(QStringLiteral("merge_request_merged.json")),
                                               readObject(QStringLiteral("approvals.json")), user));
        QVERIFY(!showsAction(view, QStringLiteral("approve")));
    }

    void retryPipelineFollowsTheRules_data()
    {
        QTest::addColumn<QString>("status");
        QTest::addColumn<bool>("retry");
        QTest::addColumn<bool>("cancel");

        QTest::newRow("failed") << "failed" << true << false;
        QTest::newRow("canceled") << "canceled" << true << false;
        QTest::newRow("running") << "running" << false << true;
        QTest::newRow("pending") << "pending" << false << true;
        QTest::newRow("success") << "success" << false << false;
    }

    void retryPipelineFollowsTheRules()
    {
        QFETCH(QString, status);
        QFETCH(bool, retry);
        QFETCH(bool, cancel);

        FakeContext context;
        context.source.status = mergeRequestWithPipeline(status);
        View mergeRequest(createMergeRequestView(context, nullptr));
        QCOMPARE(showsAction(mergeRequest, QStringLiteral("retry-pipeline")), retry);

        FakeContext pipelineContext;
        pipelineContext.source.status = pipelineWithStatus(status);
        View pipeline(createPipelineView(pipelineContext, nullptr));
        QCOMPARE(showsAction(pipeline, QStringLiteral("retry-pipeline")), retry);
        QCOMPARE(showsAction(pipeline, QStringLiteral("cancel-pipeline")), cancel);
    }

    void mergeRequestWithoutPipelineHasNoRetry()
    {
        FakeContext context;
        context.source.status = mergeRequestWith(readObject(QStringLiteral("merge_request_no_pipeline.json")),
                                                 readObject(QStringLiteral("approvals.json")),
                                                 readObject(QStringLiteral("user.json")));
        View view(createMergeRequestView(context, nullptr));
        QVERIFY(!showsAction(view, QStringLiteral("retry-pipeline")));
    }

    void closeOrReopenFollowsTheState()
    {
        FakeContext context;
        context.source.status = issueInState(QStringLiteral("opened"));
        View view(createIssueView(context, nullptr));
        QVERIFY(showsAction(view, QStringLiteral("close")));
        QVERIFY(!showsAction(view, QStringLiteral("reopen")));

        context.source.change(issueInState(QStringLiteral("closed")));
        QVERIFY(!showsAction(view, QStringLiteral("close")));
        QVERIFY(showsAction(view, QStringLiteral("reopen")));
    }

    // The requests of the actions, and a success refreshing the status.

    void approvePosts()
    {
        FakeContext context;
        context.source.status = failedMergeRequest();
        const QString key = QStringLiteral("POST ") + kMergeRequest + QStringLiteral("/approve");
        succeed(context, key);
        View view(createMergeRequestView(context, nullptr));

        QPushButton *approve = button(view, QStringLiteral("approve"));
        approve->click();
        QVERIFY(!approve->isEnabled());
        QCOMPARE(context.client.requestKeys(), QStringList{key});
        QTRY_COMPARE(context.source.refreshes, 1);
        QVERIFY(approve->isEnabled());
        QVERIFY(errorOf(view).isEmpty());
    }

    void retryPipelineOfAMergeRequestPosts()
    {
        FakeContext context;
        context.source.status = failedMergeRequest();
        const QString key = QStringLiteral("POST ") + kPipeline + QStringLiteral("/retry");
        succeed(context, key);
        View view(createMergeRequestView(context, nullptr));

        button(view, QStringLiteral("retry-pipeline"))->click();
        QCOMPARE(context.client.requestKeys(), QStringList{key});
        QTRY_COMPARE(context.source.refreshes, 1);
    }

    void commentOnAMergeRequestPosts()
    {
        FakeContext context;
        context.source.status = failedMergeRequest();
        const QString key = QStringLiteral("POST ") + kMergeRequest + QStringLiteral("/notes");
        succeed(context, key);
        View view(createMergeRequestView(context, nullptr));

        writeComment(button(view, QStringLiteral("comment")), QStringLiteral("Looks good"));
        QCOMPARE(context.client.requestKeys(), QStringList{key});
        QCOMPARE(context.client.requests().first().body.object().value(QStringLiteral("body")).toString(),
                 QStringLiteral("Looks good"));
        QTRY_COMPARE(context.source.refreshes, 1);
    }

    void aCancelledCommentSendsNothing()
    {
        FakeContext context;
        context.source.status = failedMergeRequest();
        View view(createMergeRequestView(context, nullptr));

        withDialog(button(view, QStringLiteral("comment")), [](QDialog *dialog) { dialog->reject(); });
        QVERIFY(context.client.requests().isEmpty());
        QCOMPARE(context.source.refreshes, 0);
    }

    void aViewThatGoesWhileTheDialogIsOpenSendsNothing()
    {
        FakeContext context;
        context.source.status = failedMergeRequest();
        auto *view = createMergeRequestView(context, nullptr);

        QPushButton *comment = view->findChild<QPushButton *>(QStringLiteral("comment"));
        QTimer::singleShot(0, qApp, [view] { delete view; });
        comment->click();
        QVERIFY(context.client.requests().isEmpty());
    }

    void pipelineActionsPost()
    {
        FakeContext context;
        context.source.status = pipelineWithStatus(QStringLiteral("failed"));
        const QString retryKey = QStringLiteral("POST ") + kPipeline + QStringLiteral("/retry");
        const QString jobKey = QStringLiteral("POST ") + kProject + QStringLiteral("/jobs/70002/retry");
        succeed(context, retryKey);
        succeed(context, jobKey);
        View view(createPipelineView(context, nullptr));

        button(view, QStringLiteral("retry-pipeline"))->click();
        QCOMPARE(context.client.requestKeys(), QStringList{retryKey});
        QTRY_COMPARE(context.source.refreshes, 1);

        button(view, QStringLiteral("retry-job-70002"))->click();
        QCOMPARE(context.client.requestKeys(), (QStringList{retryKey, jobKey}));
        QTRY_COMPARE(context.source.refreshes, 2);
    }

    void onlyCancellingAsksFirst()
    {
        FakeContext context;
        context.source.status = pipelineWithStatus(QStringLiteral("running"));
        const QString key = QStringLiteral("POST ") + kPipeline + QStringLiteral("/cancel");
        succeed(context, key);
        View view(createPipelineView(context, nullptr));
        QPushButton *cancel = button(view, QStringLiteral("cancel-pipeline"));

        answerQuestion(cancel, QMessageBox::No);
        QVERIFY(context.client.requests().isEmpty());

        answerQuestion(cancel, QMessageBox::Yes);
        QCOMPARE(context.client.requestKeys(), QStringList{key});
        QTRY_COMPARE(context.source.refreshes, 1);
    }

    void issueActionsSend()
    {
        FakeContext context;
        context.itemConfig.insert(QStringLiteral("iid"), 7);
        context.source.status = issueInState(QStringLiteral("opened"));
        const QString putKey = QStringLiteral("PUT ") + kIssue;
        const QString noteKey = QStringLiteral("POST ") + kIssue + QStringLiteral("/notes");
        succeed(context, putKey);
        succeed(context, noteKey);
        View view(createIssueView(context, nullptr));

        button(view, QStringLiteral("close"))->click();
        QCOMPARE(context.client.requests().last().body.object().value(QStringLiteral("state_event")).toString(),
                 QStringLiteral("close"));
        QCOMPARE(context.client.requestKeys().last(), putKey);
        QTRY_COMPARE(context.source.refreshes, 1);

        context.source.change(issueInState(QStringLiteral("closed")));
        button(view, QStringLiteral("reopen"))->click();
        QCOMPARE(context.client.requests().last().body.object().value(QStringLiteral("state_event")).toString(),
                 QStringLiteral("reopen"));
        QTRY_COMPARE(context.source.refreshes, 2);

        writeComment(button(view, QStringLiteral("comment")), QStringLiteral("Done"));
        QCOMPARE(context.client.requestKeys().last(), noteKey);
        QTRY_COMPARE(context.source.refreshes, 3);
    }

    // Failures.

    void failureShowsTheMessage()
    {
        FakeContext context;
        context.source.status = failedMergeRequest();
        const QString key = QStringLiteral("POST ") + kMergeRequest + QStringLiteral("/approve");
        context.client.setReply(key, failure(ApiReply::Error::Http, 500, QStringLiteral("Server error (500)")));
        View view(createMergeRequestView(context, nullptr));

        QPushButton *approve = button(view, QStringLiteral("approve"));
        approve->click();
        QTRY_COMPARE(errorOf(view), QStringLiteral("Server error (500)"));
        QVERIFY(approve->isEnabled());
        QCOMPARE(context.source.refreshes, 0);
    }

    void forbiddenNamesTheScope()
    {
        FakeContext context;
        context.source.status = pipelineWithStatus(QStringLiteral("failed"));
        const QString key = QStringLiteral("POST ") + kPipeline + QStringLiteral("/retry");
        context.client.setReply(key, failure(ApiReply::Error::Forbidden, 403, QStringLiteral("Forbidden (403)")));
        View view(createPipelineView(context, nullptr));

        button(view, QStringLiteral("retry-pipeline"))->click();
        QTRY_COMPARE(errorOf(view), kScopeMessage);
        QCOMPARE(context.source.refreshes, 0);
    }

    void jobRetryShowsAndClearsErrors()
    {
        FakeContext context;
        context.source.status = pipelineWithStatus(QStringLiteral("failed"));
        const QString key = QStringLiteral("POST ") + kProject + QStringLiteral("/jobs/70002/retry");
        context.client.setReply(key, failure(ApiReply::Error::Forbidden, 403, QStringLiteral("Forbidden (403)")));
        View view(createPipelineView(context, nullptr));

        QPushButton *retry = button(view, QStringLiteral("retry-job-70002"));
        retry->click();
        QVERIFY(!retry->isEnabled());
        QTRY_COMPARE(errorOf(view), kScopeMessage);
        QVERIFY(retry->isEnabled());

        // It is not in the action row, so it clears the old error itself.
        succeed(context, key);
        retry->click();
        QVERIFY(errorOf(view).isEmpty());
        QTRY_COMPARE(context.source.refreshes, 1);
    }

    // Open page, Log and the Web link.

    void openPageShowsThePage()
    {
        FakeContext context;
        context.source.status = failedMergeRequest();
        View view(createMergeRequestView(context, nullptr));
        view->show();

        QTest::mouseClick(button(view, QStringLiteral("open-page")), Qt::LeftButton);
        QCOMPARE(context.pages, QList<QUrl>{QUrl(kMrAddress)});
        QVERIFY(m_opened.isEmpty());
    }

    void shiftOpensTheBrowserInstead()
    {
        FakeContext context;
        context.source.status = pipelineWithStatus(QStringLiteral("failed"));
        View view(createPipelineView(context, nullptr));
        view->show();

        QTest::mouseClick(button(view, QStringLiteral("open-page")), Qt::LeftButton, Qt::ShiftModifier);
        QVERIFY(context.pages.isEmpty());
        QCOMPARE(m_opened, QList<QUrl>{QUrl(kPipelineAddress)});

        // The next click without Shift shows the page again.
        QTest::mouseClick(button(view, QStringLiteral("open-page")), Qt::LeftButton);
        QCOMPARE(context.pages, QList<QUrl>{QUrl(kPipelineAddress)});
        QCOMPARE(m_opened.size(), 1);
    }

    void logLinkOpensTheJobInTheBrowser()
    {
        FakeContext context;
        context.source.status = failedMergeRequest();
        View view(createMergeRequestView(context, nullptr));

        button(view, QStringLiteral("log-70002"))->click();
        QCOMPARE(m_opened, QList<QUrl>{QUrl(QStringLiteral("https://gitlab.example/acme/backend/-/jobs/70002"))});
        QVERIFY(context.pages.isEmpty());
    }

    void webLinkUsesTheDataThenTheConfiguration()
    {
        FakeContext context;
        context.source.status = failedMergeRequest();
        View fromData(createMergeRequestView(context, nullptr));
        QCOMPARE(summary(fromData)->actions()->webLink(), QUrl(kMrAddress));
        QVERIFY(!button(fromData, QStringLiteral("web"))->isHidden());

        QJsonObject mergeRequest = readObject(QStringLiteral("merge_request.json"));
        mergeRequest.remove(QStringLiteral("web_url"));
        context.source.status = mergeRequestWith(mergeRequest, QJsonObject(), QJsonObject());
        View built(createMergeRequestView(context, nullptr));
        QCOMPARE(summary(built)->actions()->webLink(),
                 QUrl(QStringLiteral("https://built.example/acme/backend/-/merge_requests/42")));

        // An address of another kind is not used.
        mergeRequest.insert(QStringLiteral("web_url"), QStringLiteral("file:///etc/passwd"));
        context.source.status = mergeRequestWith(mergeRequest, QJsonObject(), QJsonObject());
        View other(createMergeRequestView(context, nullptr));
        QCOMPARE(summary(other)->actions()->webLink(),
                 QUrl(QStringLiteral("https://built.example/acme/backend/-/merge_requests/42")));

        // Without any address, Web and Open page hide.
        mergeRequest.remove(QStringLiteral("web_url"));
        context.general.insert(QStringLiteral("baseUrl"), QStringLiteral("not a server"));
        context.source.status = mergeRequestWith(mergeRequest, QJsonObject(), QJsonObject());
        View none(createMergeRequestView(context, nullptr));
        QVERIFY(summary(none)->actions()->webLink().isEmpty());
        QVERIFY(button(none, QStringLiteral("web"))->isHidden());
        QVERIFY(!showsAction(none, QStringLiteral("open-page")));
    }

    void webLinkOfAnIssueUsesTheDataThenTheConfiguration()
    {
        FakeContext context;
        context.itemConfig.insert(QStringLiteral("iid"), 7);
        context.source.status = openIssue(QJsonArray());
        View fromData(createIssueView(context, nullptr));
        QCOMPARE(summary(fromData)->actions()->webLink(), QUrl(kIssueAddress));

        QJsonObject issue = readObject(QStringLiteral("issue.json"));
        issue.remove(QStringLiteral("web_url"));
        context.source.status = issueStatus(issue);
        View built(createIssueView(context, nullptr));
        QCOMPARE(summary(built)->actions()->webLink(),
                 QUrl(QStringLiteral("https://built.example/acme/backend/-/issues/7")));
    }

    void pipelineWebLinkIsItsAddress()
    {
        FakeContext context;
        context.source.status = pipelineWithStatus(QStringLiteral("failed"));
        View view(createPipelineView(context, nullptr));
        QCOMPARE(summary(view)->actions()->webLink(), QUrl(kPipelineAddress));
    }

    void branchWithoutPipelinesShowsAMessage()
    {
        FakeContext context;
        context.source.status = pipelineWithStatus(QStringLiteral("failed"));
        View view(createPipelineView(context, nullptr));
        view->show();

        context.source.change(pipelineStatus(QStringLiteral("main"), {}, {}));
        QVERIFY(shownText(view).contains(QStringLiteral("No pipeline on main")));
        QVERIFY(!shownText(view).contains(QStringLiteral("Pipeline #9001")));
        QVERIFY(summary(view)->actions()->webLink().isEmpty());
        QVERIFY(button(view, QStringLiteral("web"))->isHidden());
        QVERIFY(!showsAction(view, QStringLiteral("open-page")));
    }

    // No data, and old data.

    void noDataShowsLoadingOrTheMessage()
    {
        FakeContext context;
        View loading(createMergeRequestView(context, nullptr));
        loading->show();
        QCOMPARE(shownText(loading), QStringLiteral("Loading…"));

        ItemStatus problem;
        problem.state = ItemStatus::State::NoToken;
        problem.message = QStringLiteral("No token");
        context.source.change(problem);
        QCOMPARE(shownText(loading), QStringLiteral("No token"));

        context.source.status = {};
        context.source.status.message = QStringLiteral("Follows the main folder from M7");
        View pipeline(createPipelineView(context, nullptr));
        pipeline->show();
        QCOMPARE(shownText(pipeline), QStringLiteral("Follows the main folder from M7"));

        context.source.status = {};
        View issue(createIssueView(context, nullptr));
        issue->show();
        QCOMPARE(shownText(issue), QStringLiteral("Loading…"));
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
        context.source.status = failedMergeRequest();
        View view(createMergeRequestView(context, nullptr));
        view->show();

        // The cache keeps the data of the last good status.
        ItemStatus kept = failedMergeRequest();
        kept.state = ItemStatus::State::Error;
        kept.message = QStringLiteral("Network error");
        context.source.change(kept);
        QVERIFY(shownText(view).contains(QStringLiteral("Add the retry button")));
        QVERIFY(!shownText(view).contains(QStringLiteral("Network error")));

        // And a status without data does not wipe what is drawn either.
        ItemStatus empty;
        empty.state = ItemStatus::State::Error;
        empty.message = QStringLiteral("Network error");
        context.source.change(empty);
        QVERIFY(shownText(view).contains(QStringLiteral("Add the retry button")));
        QVERIFY(!shownText(view).contains(QStringLiteral("Network error")));
    }

    // Previews.

    void mergeRequestPreviewReadsItself()
    {
        FakeContext context;
        setUpMergeRequestPreview(context);
        // Its fields are all on, whatever the list item's configuration holds.
        context.itemConfig = QJsonObject{{QStringLiteral("showPipeline"), false}};
        View view(createMergeRequestPreview(context, QStringLiteral("acme/backend"), 42, nullptr));
        view->show();
        QCOMPARE(shownText(view), QStringLiteral("Loading…"));

        QTRY_VERIFY(shownText(view).contains(QStringLiteral("Add the retry button")));
        const QStringList keys = context.client.requestKeys();
        QVERIFY(keys.contains(QStringLiteral("GET ") + kMergeRequest));
        QVERIFY(keys.contains(QStringLiteral("GET ") + kMergeRequest + QStringLiteral("/approvals")));
        QVERIFY(keys.contains(QStringLiteral("GET api/v4/user")));
        QTRY_VERIFY(shownText(view).contains(QStringLiteral("test:unit")));

        for (const char *section : {"pipeline", "approvals", "people", "labels"})
            QVERIFY(summary(view)->isSectionVisible(QLatin1String(section)));
        QCOMPARE(summary(view)->actions()->webLink(), QUrl(kMrAddress));
        QCOMPARE(context.source.refreshes, 0);
    }

    void previewActionReadsAgain()
    {
        FakeContext context;
        setUpMergeRequestPreview(context);
        const QString key = QStringLiteral("POST ") + kMergeRequest + QStringLiteral("/approve");
        succeed(context, key);
        View view(createMergeRequestPreview(context, QStringLiteral("acme/backend"), 42, nullptr));
        view->show();
        QTRY_VERIFY(shownText(view).contains(QStringLiteral("test:unit")));

        // The answer to the second read says that I approved.
        QJsonObject approved = readObject(QStringLiteral("approvals.json"));
        QJsonArray entries = approved.value(QStringLiteral("approved_by")).toArray();
        entries.append(QJsonObject{{QStringLiteral("user"), readObject(QStringLiteral("user.json"))}});
        approved.insert(QStringLiteral("approved_by"), entries);
        context.client.setJson(QStringLiteral("GET ") + kMergeRequest + QStringLiteral("/approvals"),
                               QJsonDocument(approved));

        QVERIFY(showsAction(view, QStringLiteral("approve")));
        button(view, QStringLiteral("approve"))->click();
        QVERIFY(context.client.requestKeys().contains(key));
        QTRY_VERIFY(!showsAction(view, QStringLiteral("approve")));
        QCOMPARE(context.source.refreshes, 0);
    }

    void previewShowsAFailedRead()
    {
        FakeContext context;
        View view(createIssuePreview(context, QStringLiteral("acme/backend"), 7, nullptr));
        view->show();
        QTRY_COMPARE(shownText(view), QStringLiteral("Not found (404)"));
    }

    void issuePreviewReadsItself()
    {
        FakeContext context;
        QVERIFY(context.client.setJsonFile(QStringLiteral("GET ") + kIssue, QFINDTESTDATA("data/issue.json")));
        QVERIFY(context.client.setJsonFile(
            QStringLiteral("GET ") + kIssue + QStringLiteral("/notes?sort=desc&order_by=created_at&per_page=20"),
            QFINDTESTDATA("data/issue_notes.json")));
        context.itemConfig = QJsonObject{{QStringLiteral("showComments"), false}};
        View view(createIssuePreview(context, QStringLiteral("acme/backend"), 7, nullptr));
        view->show();

        QTRY_VERIFY(shownText(view).contains(QStringLiteral("Fixed on the branch.")));
        // System notes are left out.
        QVERIFY(!shownText(view).contains(QStringLiteral("added ~bug label")));
        QVERIFY(summary(view)->isSectionVisible(QStringLiteral("comments")));
        QCOMPARE(summary(view)->actions()->webLink(), QUrl(kIssueAddress));

        succeed(context, QStringLiteral("PUT ") + kIssue);
        const qsizetype before = context.client.requests().size();
        button(view, QStringLiteral("close"))->click();
        QCOMPARE(context.client.requests().at(before).method, QStringLiteral("PUT"));
        QTRY_VERIFY(context.client.requests().size() > before + 1);
        QCOMPARE(context.client.requests().at(before + 1).path, kIssue);
    }

    void openPageOfAPreviewCallsShowPage()
    {
        FakeContext context;
        QVERIFY(context.client.setJsonFile(QStringLiteral("GET ") + kIssue, QFINDTESTDATA("data/issue.json")));
        View view(createIssuePreview(context, QStringLiteral("acme/backend"), 7, nullptr));
        view->show();
        QTRY_VERIFY(shownText(view).contains(QStringLiteral("The pipeline panel")));

        QTest::mouseClick(button(view, QStringLiteral("open-page")), Qt::LeftButton);
        QCOMPARE(context.pages, QList<QUrl>{QUrl(kIssueAddress)});
    }

private:
    QList<QUrl> m_opened;
};

QTEST_MAIN(TestSummaryViews)
#include "tst_summary_views.moc"
