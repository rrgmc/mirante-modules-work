// The models of the three GitLab lists, which every list layout draws. They run
// whichever layouts the build compiled in, so an app's layout keeps them. The
// default layouts' own tests are in tst_list_views.cpp.
#include "../GitLabIssueListLayout.h"
#include "../GitLabIssueListView.h"
#include "../GitLabMergeRequestListLayout.h"
#include "../GitLabMergeRequestListView.h"
#include "../GitLabPipelineListLayout.h"
#include "../GitLabPipelineListView.h"
#include "ListTestSupport.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTest>
#include <QWidget>

#include <algorithm>
#include <memory>

using mirante::widgets::StatusRole;
using namespace listtest;

class TestListModels : public QObject
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

    void withoutRowsItShowsLoadingOrTheMessage()
    {
        FakeContext context;
        IssueListModel model(context);
        model.follow(&context.source);
        QVERIFY(!model.hasData());
        QCOMPARE(model.message(), QStringLiteral("Loading…"));

        ItemStatus failed;
        failed.state = ItemStatus::State::NoToken;
        failed.message = QStringLiteral("Token not set");
        context.source.change(failed);
        QCOMPARE(model.message(), QStringLiteral("Token not set"));

        context.source.change(issues());
        QVERIFY(model.hasData());
        context.source.change(failed);
        QVERIFY(model.hasData());
        QCOMPARE(model.keys().size(), 2);
    }

    void issueRowsReadTheData()
    {
        FakeContext context;
        context.source.status = issues();
        IssueListModel model(context);
        model.follow(&context.source);
        QCOMPARE(model.emptyText(), QStringLiteral("No issues"));
        const QList<IssueListModel::Issue> rows = model.issues();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0).ref, QStringLiteral("backend#7"));
        QCOMPARE(rows.at(0).title, QStringLiteral("The pipeline panel does not show manual jobs"));
        QCOMPARE(rows.at(0).assignees, QStringList{QStringLiteral("Ana Reviewer")});
        QCOMPARE(rows.at(0).labels, QStringList{QStringLiteral("bug")});
        QCOMPARE(rows.at(1).ref, QStringLiteral("website#3"));
        QVERIFY(rows.at(1).assignees.isEmpty());
        QVERIFY(rows.at(0).canAdd);
    }

    void mergeRequestRowsReadTheData()
    {
        FakeContext context;
        context.source.status = mergeRequests();
        MergeRequestListModel model(context);
        model.follow(&context.source);
        const QList<MergeRequestListModel::MergeRequest> rows = model.mergeRequests();
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows.at(0).ref, QStringLiteral("backend!42"));
        QCOMPARE(rows.at(0).author, QStringLiteral("Bruno Author"));
        QCOMPARE(rows.at(2).ref, QStringLiteral("website!8"));
        QVERIFY(rows.at(0).pipeline.text.isEmpty());
        QVERIFY(!rows.at(0).approvals.answered);
        QVERIFY(context.client.requests().isEmpty());
    }

    void visibleRowsGetPipelineAndApprovals()
    {
        FakeContext context;
        context.source.status = mergeRequests();
        setUpBackend42(context);
        auto *model = new MergeRequestListModel(context);
        model->follow(&context.source);
        QWidget view;
        model->attach(&view);
        const QString key = model->keys().constFirst();
        QSignalSpy changed(model, &ListModel::rowChanged);

        // Hidden, it only remembers.
        model->setVisibleRows({key});
        QVERIFY(context.client.requests().isEmpty());

        view.show();
        QCOMPARE(context.client.requests().size(), 2);
        QTRY_VERIFY(model->mergeRequest(key).approvals.answered);
        QTRY_VERIFY(!model->mergeRequest(key).pipeline.text.isEmpty());
        QCOMPARE(model->mergeRequest(key).pipeline.role, StatusRole::Bad);
        QCOMPARE(model->mergeRequest(key).approvals.given, 2);
        QCOMPARE(model->mergeRequest(key).approvals.required, 3);
        QVERIFY(changed.contains(QVariantList{key}));

        // A row that leaves the shown ones drops its answers.
        model->setVisibleRows({});
        QVERIFY(model->mergeRequest(key).pipeline.text.isEmpty());
        QVERIFY(!model->mergeRequest(key).approvals.answered);
    }

    void onlyTheFirst25RowsAreAskedFor()
    {
        FakeContext context;
        context.source.status = statusOfRows(manyMergeRequests(30));
        auto *model = new MergeRequestListModel(context);
        model->follow(&context.source);
        QWidget view;
        model->attach(&view);
        view.show();
        model->setVisibleRows(model->keys());
        QCOMPARE(context.client.requests().size(), 2 * MergeRequestListModel::AskedRows);
    }

    void columnFieldsTurnRequestsOff()
    {
        FakeContext context;
        context.itemConfig.insert(QStringLiteral("showPipeline"), false);
        context.itemConfig.insert(QStringLiteral("showApprovals"), false);
        context.source.status = mergeRequests();
        auto *model = new MergeRequestListModel(context);
        model->follow(&context.source);
        QWidget view;
        model->attach(&view);
        view.show();
        model->setVisibleRows(model->keys());
        QVERIFY(context.client.requests().isEmpty());
    }

    void pipelineRowsReadTheData()
    {
        FakeContext context;
        context.itemConfig = pipelineConfig();
        context.source.status = pipelines();
        PipelineListModel model(context);
        model.follow(&context.source);
        const QList<PipelineListModel::Pipeline> rows = model.pipelines();
        QCOMPARE(rows.size(), 4);
        for (const PipelineListModel::Pipeline &pipeline : rows) {
            QCOMPARE(pipeline.key, QString::number(pipeline.id));
            QVERIFY(!pipeline.ref.isEmpty());
            QVERIFY(!pipeline.state.text.isEmpty());
        }
        QVERIFY(std::any_of(rows.cbegin(), rows.cend(),
                            [](const PipelineListModel::Pipeline &pipeline) { return pipeline.id == 9105; }));
    }

    void rowActions()
    {
        FakeContext context;
        context.source.status = issues();
        IssueListModel model(context);
        model.follow(&context.source);
        const IssueListModel::Issue first = model.issues().first();

        model.openPage(first.key);
        QCOMPARE(context.pages, QList<QUrl>{first.address});
        model.openWeb(first.key);
        QCOMPARE(m_opened, QList<QUrl>{first.address});
        model.copyLink(first.key);
        QCOMPARE(QGuiApplication::clipboard()->text(), first.address.toString());
        model.addToTask(first.key);
        QCOMPARE(context.added.size(), 1);
        QCOMPARE(context.added.first().typeId, QStringLiteral("gitlab.issue.summary"));

        model.openWeb(QStringLiteral("nothing"));
        model.addToTask(QStringLiteral("nothing"));
        QCOMPARE(m_opened.size(), 1);
        QCOMPARE(context.added.size(), 1);
    }

    // Whichever layouts the build compiled in: each view is made, draws every
    // kind of status, shows and hides, and goes.
    void everyLayoutDrawsEveryState()
    {
        FakeContext context;
        ItemStatus failed;
        failed.state = ItemStatus::State::NoToken;
        failed.message = QStringLiteral("Token not set");
        setUpBackend42(context);

        std::unique_ptr<QWidget> issueView(createIssueListView(context, nullptr));
        QVERIFY(issueView);
        issueView->show();
        context.source.change(failed);
        context.source.change(issues());
        context.source.change(statusOfRows({}));
        issueView.reset();

        std::unique_ptr<QWidget> mergeRequestView(createMergeRequestListView(context, nullptr));
        QVERIFY(mergeRequestView);
        mergeRequestView->show();
        context.source.change(mergeRequests());
        context.source.change(statusOfRows(manyMergeRequests(30)));
        QCoreApplication::processEvents();
        mergeRequestView->hide();
        mergeRequestView.reset();
        QCoreApplication::processEvents();

        context.itemConfig = pipelineConfig();
        std::unique_ptr<QWidget> pipelineView(createPipelineListView(context, nullptr));
        QVERIFY(pipelineView);
        pipelineView->show();
        context.source.change(pipelines());
        context.source.change(statusOfRows({}));
        pipelineView.reset();
    }

private:
    QList<QUrl> m_opened;
};

QTEST_MAIN(TestListModels)
#include "tst_list_models.moc"
