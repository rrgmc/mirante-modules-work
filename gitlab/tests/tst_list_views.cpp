// The two GitLab lists, drawn from the responses in tests/data/. A handler stands
// in for the external browser, so nothing is opened.
#include "../GitLabIssueListView.h"
#include "../GitLabMergeRequestListView.h"

#include <mirante/ItemContext.h>
#include <mirante/Status.h>
#include <mirante/testing/FakeApiClient.h>
#include <mirante/widgets/ItemListView.h>

#include <QAbstractItemModel>
#include <QAction>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QPointer>
#include <QTableView>
#include <QTest>
#include <QTimeZone>
#include <QToolButton>

#include <functional>
#include <memory>
#include <vector>

using mirante::ApiReply;
using mirante::ItemStatus;
using mirante::testing::FakeApiClient;
using mirante::widgets::ItemListView;
using namespace mirante::modules::gitlab;

namespace {

enum MrColumn { MrRef, MrTitle, MrAuthor, MrPipeline, MrApprovals, MrUpdated };
enum IssueColumn { IssueRef, IssueTitle, IssueAssignees, IssueLabels, IssueUpdated };

const QString kDash = QStringLiteral("–");
const QString kBase = QStringLiteral("https://gitlab.example");

// The GET key of a merge request or an issue of acme/backend.
QString backendKey(const QString &suffix)
{
    return QStringLiteral("GET api/v4/projects/acme%2Fbackend/") + suffix;
}

QJsonDocument readJson(const QString &name)
{
    QFile file(QFINDTESTDATA(QStringLiteral("data/") + name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot read %s", qPrintable(name));
    return QJsonDocument::fromJson(file.readAll());
}

ItemStatus statusOfRows(const QJsonArray &rows)
{
    ItemStatus status;
    status.state = ItemStatus::State::Ok;
    status.data = QJsonObject{{QStringLiteral("rows"), rows}, {QStringLiteral("total"), int(rows.size())}};
    return status;
}

ItemStatus mergeRequests()
{
    return statusOfRows(readJson(QStringLiteral("merge_request_list.json")).array());
}

ItemStatus issues()
{
    return statusOfRows(readJson(QStringLiteral("issue_list.json")).array());
}

// count merge requests of acme/backend, numbered from 1, where a higher number
// is newer. The array holds the oldest first.
QJsonArray manyMergeRequests(int count)
{
    QJsonArray rows;
    for (int iid = 1; iid <= count; ++iid) {
        rows.append(QJsonObject{
            {QStringLiteral("iid"), iid},
            {QStringLiteral("project_id"), 77},
            {QStringLiteral("title"), QStringLiteral("Change %1").arg(iid)},
            {QStringLiteral("updated_at"), QDateTime(QDate(2026, 10, 1), QTime(0, 0), QTimeZone::UTC)
                                               .addSecs(iid * 3600)
                                               .toString(Qt::ISODate)},
            {QStringLiteral("references"),
             QJsonObject{{QStringLiteral("full"), QStringLiteral("acme/backend!%1").arg(iid)}}}});
    }
    return rows;
}

// A status source that a test drives by hand.
class FakeStatusSource : public mirante::ItemStatusSource
{
public:
    ItemStatus status;

    ItemStatus current() const override { return status; }
    void refresh() override {}
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

struct AddedItem
{
    QString typeId;
    QString name;
    QJsonObject config;
};

class FakeContext : public mirante::ItemContext
{
public:
    QJsonObject itemConfig{{QStringLiteral("scope"), QStringLiteral("project")},
                           {QStringLiteral("project"), QStringLiteral("own/config")}};
    QJsonObject general{{QStringLiteral("baseUrl"), kBase}};
    QList<QUrl> pages;
    QList<AddedItem> added;
    FakeApiClient client{QUrl(kBase)};
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
    QString addItemToTask(const QString &typeId, const QString &name, const QJsonObject &config) override
    {
        added.append(AddedItem{typeId, name, config});
        return QStringLiteral("new-item");
    }
    mirante::ApiClient *server() override { return &client; }
    mirante::ItemStatusSource *status() override { return hasStatus ? &source : nullptr; }
};

using View = std::unique_ptr<QWidget>;

ItemListView *list(const View &view)
{
    return qobject_cast<ItemListView *>(view.get());
}

QTableView *table(const View &view)
{
    return view->findChild<QTableView *>();
}

int rowCount(const View &view)
{
    return table(view)->model()->rowCount();
}

// The text of a cell, as the table is sorted now.
QString cellText(const View &view, int row, int column)
{
    return table(view)->model()->index(row, column).data().toString();
}

// The row whose Ref column reads ref, or -1.
int rowOf(const View &view, const QString &ref)
{
    for (int row = 0; row < rowCount(view); ++row) {
        if (cellText(view, row, 0) == ref)
            return row;
    }
    return -1;
}

QString cellOf(const View &view, const QString &ref, int column)
{
    return cellText(view, rowOf(view, ref), column);
}

void show(const View &view)
{
    view->resize(1200, 700);
    view->show();
    QCoreApplication::processEvents();
}

void clickRow(const View &view, const QString &ref)
{
    QTableView *tableView = table(view);
    const QModelIndex index = tableView->model()->index(rowOf(view, ref), 1);
    QTest::mouseClick(tableView->viewport(), Qt::LeftButton, {}, tableView->visualRect(index).center());
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

// Clicks an entry of the row menu of key, with these keys held.
void chooseFromMenu(const View &view, const QString &key, const QString &text,
                    Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    emit list(view)->rowMenuRequested(key, QPoint(20, 20));
    QMenu *menu = view->findChildren<QMenu *>().constLast();
    QAction *chosen = nullptr;
    for (QAction *action : menu->actions()) {
        if (action->text() == text)
            chosen = action;
    }
    QVERIFY2(chosen, qPrintable(text));
    QTest::mouseClick(menu, Qt::LeftButton, modifiers, menu->actionGeometry(chosen).center());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

bool menuEntryEnabled(const View &view, const QString &key, const QString &text)
{
    emit list(view)->rowMenuRequested(key, QPoint(20, 20));
    QMenu *menu = view->findChildren<QMenu *>().constLast();
    bool enabled = false;
    for (const QAction *action : menu->actions()) {
        if (action->text() == text)
            enabled = action->isEnabled();
    }
    menu->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return enabled;
}

// Answers for acme/backend!42, with a failed pipeline and 2 of 3 approvals.
void setUpBackend42(FakeContext &context)
{
    QVERIFY(context.client.setJsonFile(backendKey(QStringLiteral("merge_requests/42")),
                                       QFINDTESTDATA("data/merge_request.json")));
    QVERIFY(context.client.setJsonFile(backendKey(QStringLiteral("merge_requests/42/approvals")),
                                       QFINDTESTDATA("data/approvals.json")));
}

void setPipelineAnswer(FakeContext &context, const QString &status)
{
    context.client.setJson(
        backendKey(QStringLiteral("merge_requests/42")),
        QJsonDocument(QJsonObject{{QStringLiteral("head_pipeline"), QJsonObject{{QStringLiteral("status"), status}}}}));
}

} // namespace

class TestListViews : public QObject
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

    void mergeRequestRowsDrawFromData()
    {
        FakeContext context;
        context.source.status = mergeRequests();
        View view(createMergeRequestListView(context, nullptr));

        QCOMPARE(rowCount(view), 3);
        QCOMPARE(cellText(view, 0, MrRef), QStringLiteral("backend!42"));
        QCOMPARE(cellText(view, 0, MrTitle), QStringLiteral("Add the retry button to the pipeline panel"));
        QCOMPARE(cellText(view, 0, MrAuthor), QStringLiteral("Bruno Author"));
        QCOMPARE(cellText(view, 0, MrPipeline), kDash);
        QCOMPARE(cellText(view, 0, MrApprovals), kDash);
        const QDateTime updated = QDateTime::fromString(QStringLiteral("2026-10-28T16:03:10.118Z"), Qt::ISODateWithMs);
        QCOMPARE(cellText(view, 0, MrUpdated), QLocale().toString(updated.toLocalTime(), QLocale::ShortFormat));
        // A list can span projects.
        QCOMPARE(cellText(view, 2, MrRef), QStringLiteral("website!8"));
    }

    void issueRowsDrawFromData()
    {
        FakeContext context;
        context.source.status = issues();
        View view(createIssueListView(context, nullptr));
        show(view);

        QCOMPARE(rowCount(view), 2);
        QCOMPARE(cellText(view, 0, IssueRef), QStringLiteral("backend#7"));
        QCOMPARE(cellText(view, 0, IssueTitle), QStringLiteral("The pipeline panel does not show manual jobs"));
        QCOMPARE(cellText(view, 0, IssueAssignees), QStringLiteral("Ana Reviewer"));
        QCOMPARE(cellText(view, 0, IssueLabels), QStringLiteral("bug"));
        QCOMPARE(cellText(view, 1, IssueRef), QStringLiteral("website#3"));
        QCOMPARE(cellText(view, 1, IssueAssignees), QString());
        // An issue list asks for nothing more.
        QVERIFY(context.client.requests().isEmpty());
    }

    // By the date, newest first, whatever the order of the data.
    void rowsSortOnUpdatedNewestFirst()
    {
        const QJsonArray rows = readJson(QStringLiteral("merge_request_list.json")).array();
        FakeContext context;
        context.source.status = statusOfRows({rows.at(2), rows.at(0), rows.at(1)});
        View view(createMergeRequestListView(context, nullptr));

        QCOMPARE(cellText(view, 0, MrRef), QStringLiteral("backend!42"));
        QCOMPARE(cellText(view, 1, MrRef), QStringLiteral("backend!41"));
        QCOMPARE(cellText(view, 2, MrRef), QStringLiteral("website!8"));
    }

    // Without references the project comes from web_url, below a base path, and
    // the numeric project_id is not used.
    void projectComesFromWebUrl()
    {
        QJsonObject row = readJson(QStringLiteral("merge_request_list.json")).array().at(0).toObject();
        row.remove(QStringLiteral("references"));
        row.insert(QStringLiteral("web_url"),
                   QStringLiteral("https://gitlab.example/gitlab/group/sub/app/-/merge_requests/42"));

        FakeContext context;
        context.general.insert(QStringLiteral("baseUrl"), QStringLiteral("https://gitlab.example/gitlab"));
        context.source.status = statusOfRows({row});
        View view(createMergeRequestListView(context, nullptr));
        show(view);

        QCOMPARE(cellText(view, 0, MrRef), QStringLiteral("app!42"));
        QCOMPARE(context.client.requests().first().path,
                 QStringLiteral("api/v4/projects/group%2Fsub%2Fapp/merge_requests/42"));
    }

    // A row that names no project still draws, and nothing is asked for it.
    void rowWithoutAProject()
    {
        QJsonObject row = readJson(QStringLiteral("merge_request_list.json")).array().at(0).toObject();
        row.remove(QStringLiteral("references"));
        row.remove(QStringLiteral("web_url"));

        FakeContext context;
        context.source.status = statusOfRows({row});
        View view(createMergeRequestListView(context, nullptr));
        show(view);

        QCOMPARE(rowCount(view), 1);
        QCOMPARE(cellText(view, 0, MrRef), QStringLiteral("!42"));
        QVERIFY(context.client.requests().isEmpty());

        clickRow(view, QStringLiteral("!42"));
        QVERIFY(!list(view)->preview());
        QVERIFY(!menuEntryEnabled(view, QStringLiteral("row-0"), QStringLiteral("Add to task as item")));
        QVERIFY(!menuEntryEnabled(view, QStringLiteral("row-0"), QStringLiteral("Open page")));
    }

    // The two fields.

    void fieldsHideColumnsAndStopRequests_data()
    {
        QTest::addColumn<QJsonValue>("pipeline");
        QTest::addColumn<QJsonValue>("approvals");
        QTest::addColumn<bool>("showsPipeline");
        QTest::addColumn<bool>("showsApprovals");

        const QJsonValue absent(QJsonValue::Undefined);
        QTest::newRow("absent") << absent << absent << true << true;
        QTest::newRow("both on") << QJsonValue(true) << QJsonValue(true) << true << true;
        QTest::newRow("no pipeline") << QJsonValue(false) << absent << false << true;
        QTest::newRow("no approvals") << absent << QJsonValue(false) << true << false;
        QTest::newRow("neither") << QJsonValue(false) << QJsonValue(false) << false << false;
    }

    void fieldsHideColumnsAndStopRequests()
    {
        QFETCH(QJsonValue, pipeline);
        QFETCH(QJsonValue, approvals);
        QFETCH(bool, showsPipeline);
        QFETCH(bool, showsApprovals);

        FakeContext context;
        if (!pipeline.isUndefined())
            context.itemConfig.insert(QStringLiteral("showPipeline"), pipeline);
        if (!approvals.isUndefined())
            context.itemConfig.insert(QStringLiteral("showApprovals"), approvals);
        context.source.status = mergeRequests();
        View view(createMergeRequestListView(context, nullptr));
        show(view);

        QCOMPARE(table(view)->isColumnHidden(MrPipeline), !showsPipeline);
        QCOMPARE(table(view)->isColumnHidden(MrApprovals), !showsApprovals);
        QVERIFY(!table(view)->isColumnHidden(MrTitle));

        const QStringList keys = context.client.requestKeys();
        QCOMPARE(keys.contains(backendKey(QStringLiteral("merge_requests/42"))), showsPipeline);
        QCOMPARE(keys.contains(backendKey(QStringLiteral("merge_requests/42/approvals"))), showsApprovals);
        QCOMPARE(keys.size(), 3 * (int(showsPipeline) + int(showsApprovals)));
    }

    void narrowCellKeepsTheTitle()
    {
        FakeContext context;
        context.source.status = mergeRequests();
        View view(createMergeRequestListView(context, nullptr));
        view->resize(200, 400);
        view->show();
        QCoreApplication::processEvents();

        QVERIFY(!table(view)->isColumnHidden(MrTitle));
        QVERIFY(table(view)->isColumnHidden(MrAuthor));
        QVERIFY(table(view)->isColumnHidden(MrApprovals));

        view->resize(1400, 400);
        QCoreApplication::processEvents();
        for (int column = MrRef; column <= MrUpdated; ++column)
            QVERIFY(!table(view)->isColumnHidden(column));
    }

    // The further requests.

    void requestsStopAt25Rows()
    {
        FakeContext context;
        context.source.status = statusOfRows(manyMergeRequests(30));
        View view(createMergeRequestListView(context, nullptr));
        show(view);

        // Two requests for each of the 25 newest rows, in the order of the table.
        const QStringList keys = context.client.requestKeys();
        QCOMPARE(keys.size(), 50);
        QCOMPARE(keys.at(0), backendKey(QStringLiteral("merge_requests/30")));
        QCOMPARE(keys.at(1), backendKey(QStringLiteral("merge_requests/30/approvals")));
        QVERIFY(keys.contains(backendKey(QStringLiteral("merge_requests/6"))));
        QVERIFY(!keys.contains(backendKey(QStringLiteral("merge_requests/5"))));
        QVERIFY(!keys.contains(backendKey(QStringLiteral("merge_requests/5/approvals"))));
    }

    // The table's current order decides, not the order of the data.
    void requestsFollowTheTablesSort()
    {
        FakeContext context;
        context.source.status = statusOfRows(manyMergeRequests(30));
        View view(createMergeRequestListView(context, nullptr));
        show(view);
        table(view)->sortByColumn(MrUpdated, Qt::AscendingOrder);
        const int before = int(context.client.requests().size());

        context.source.change(statusOfRows(manyMergeRequests(30)));

        const QStringList keys = context.client.requestKeys().mid(before);
        QCOMPARE(keys.size(), 50);
        QCOMPARE(keys.at(0), backendKey(QStringLiteral("merge_requests/1")));
        QVERIFY(!keys.contains(backendKey(QStringLiteral("merge_requests/26"))));
    }

    void noRequestsWhileHidden()
    {
        FakeContext context;
        context.source.status = mergeRequests();
        View view(createMergeRequestListView(context, nullptr));
        // Never shown yet.
        context.source.change(mergeRequests());
        QVERIFY(context.client.requests().isEmpty());

        show(view);
        QCOMPARE(context.client.requests().size(), 6);
        QCoreApplication::processEvents();

        view->hide();
        context.source.change(mergeRequests());
        QCoreApplication::processEvents();
        QCOMPARE(context.client.requests().size(), 6);

        // Filled again when shown.
        view->show();
        QCOMPARE(context.client.requests().size(), 12);
    }

    void hidingDropsAnswersOnTheWay()
    {
        FakeContext context;
        setUpBackend42(context);
        context.source.status = mergeRequests();
        View view(createMergeRequestListView(context, nullptr));
        view->show();
        view->hide();
        QCoreApplication::processEvents();

        QCOMPARE(cellOf(view, QStringLiteral("backend!42"), MrPipeline), kDash);
        QCOMPARE(cellOf(view, QStringLiteral("backend!42"), MrApprovals), kDash);
    }

    void answersFillTheCells()
    {
        FakeContext context;
        setUpBackend42(context);
        context.source.status = mergeRequests();
        View view(createMergeRequestListView(context, nullptr));
        show(view);
        QCoreApplication::processEvents();

        QCOMPARE(cellOf(view, QStringLiteral("backend!42"), MrPipeline), QStringLiteral("Failed"));
        QCOMPARE(cellOf(view, QStringLiteral("backend!42"), MrApprovals), QStringLiteral("2/3"));
    }

    void approvalsWithoutARequirementShowTheCount()
    {
        FakeContext context;
        context.client.setJson(backendKey(QStringLiteral("merge_requests/42/approvals")),
                               QJsonDocument(QJsonObject{{QStringLiteral("approvals_required"), 0},
                                                         {QStringLiteral("approved_by"), QJsonArray{QJsonObject()}}}));
        context.source.status = mergeRequests();
        View view(createMergeRequestListView(context, nullptr));
        show(view);
        QCoreApplication::processEvents();

        QCOMPARE(cellOf(view, QStringLiteral("backend!42"), MrApprovals), QStringLiteral("1"));
    }

    // The other rows have no answer on the fake server, so they are 404s.
    void failedRequestLeavesTheDash()
    {
        FakeContext context;
        setUpBackend42(context);
        context.source.status = mergeRequests();
        View view(createMergeRequestListView(context, nullptr));
        show(view);
        QCoreApplication::processEvents();

        QCOMPARE(cellOf(view, QStringLiteral("backend!41"), MrPipeline), kDash);
        QCOMPARE(cellOf(view, QStringLiteral("backend!41"), MrApprovals), kDash);
        QCOMPARE(cellOf(view, QStringLiteral("backend!42"), MrPipeline), QStringLiteral("Failed"));
    }

    void mergeRequestWithoutAPipelineShowsADash()
    {
        FakeContext context;
        context.client.setJson(backendKey(QStringLiteral("merge_requests/42")), QJsonDocument(QJsonObject()));
        context.source.status = mergeRequests();
        View view(createMergeRequestListView(context, nullptr));
        show(view);
        QCoreApplication::processEvents();

        QCOMPARE(cellOf(view, QStringLiteral("backend!42"), MrPipeline), kDash);
    }

    // The last answer stays until the new one comes, and then gives way to it.
    void newerStatusReplacesOlderAnswers()
    {
        FakeContext context;
        setPipelineAnswer(context, QStringLiteral("failed"));
        context.source.status = mergeRequests();
        View view(createMergeRequestListView(context, nullptr));
        show(view);
        QCoreApplication::processEvents();
        QCOMPARE(cellOf(view, QStringLiteral("backend!42"), MrPipeline), QStringLiteral("Failed"));

        setPipelineAnswer(context, QStringLiteral("success"));
        context.source.change(mergeRequests());
        QCOMPARE(cellOf(view, QStringLiteral("backend!42"), MrPipeline), QStringLiteral("Failed"));
        QCoreApplication::processEvents();
        QCOMPARE(cellOf(view, QStringLiteral("backend!42"), MrPipeline), QStringLiteral("Passed"));
    }

    void failureAfterAnAnswerGoesBackToTheDash()
    {
        FakeContext context;
        setPipelineAnswer(context, QStringLiteral("failed"));
        context.source.status = mergeRequests();
        View view(createMergeRequestListView(context, nullptr));
        show(view);
        QCoreApplication::processEvents();

        ApiReply failure;
        failure.error = ApiReply::Error::Network;
        failure.message = QStringLiteral("No route");
        context.client.setReply(backendKey(QStringLiteral("merge_requests/42")), failure);
        context.source.change(mergeRequests());
        QCoreApplication::processEvents();
        QCOMPARE(cellOf(view, QStringLiteral("backend!42"), MrPipeline), kDash);
    }

    void answerForAGoneRowIsDropped()
    {
        const QJsonArray all = readJson(QStringLiteral("merge_request_list.json")).array();
        FakeContext context;
        setPipelineAnswer(context, QStringLiteral("failed"));
        context.source.status = statusOfRows(all);
        View view(createMergeRequestListView(context, nullptr));
        show(view);

        // backend!42 goes while its answer is on the way.
        context.source.change(statusOfRows({all.at(1), all.at(2)}));
        QCoreApplication::processEvents();
        QCOMPARE(rowCount(view), 2);
        QCOMPARE(rowOf(view, QStringLiteral("backend!42")), -1);

        // It comes back while hidden, and nothing was kept for it.
        view->hide();
        context.source.change(statusOfRows(all));
        QCOMPARE(cellOf(view, QStringLiteral("backend!42"), MrPipeline), kDash);
    }

    // A row that falls out of the first 25 goes back to a dash.
    void rowBeyondTheLimitLosesItsAnswer()
    {
        FakeContext context;
        for (int iid = 1; iid <= 26; ++iid) {
            context.client.setJson(backendKey(QStringLiteral("merge_requests/%1").arg(iid)),
                                   QJsonDocument(QJsonObject{{QStringLiteral("head_pipeline"),
                                                              QJsonObject{{QStringLiteral("status"),
                                                                           QStringLiteral("success")}}}}));
        }
        context.source.status = statusOfRows(manyMergeRequests(25));
        View view(createMergeRequestListView(context, nullptr));
        show(view);
        QCoreApplication::processEvents();
        QCOMPARE(cellOf(view, QStringLiteral("backend!1"), MrPipeline), QStringLiteral("Passed"));

        context.source.change(statusOfRows(manyMergeRequests(26)));
        QCOMPARE(cellOf(view, QStringLiteral("backend!1"), MrPipeline), kDash);
    }

    // The preview.

    void rowClickShowsThePreview()
    {
        FakeContext context;
        context.source.status = mergeRequests();
        View view(createMergeRequestListView(context, nullptr));
        show(view);
        QCoreApplication::processEvents();
        QVERIFY(!list(view)->preview());
        const int before = int(context.client.requests().size());

        clickRow(view, QStringLiteral("website!8"));

        QVERIFY(list(view)->preview());
        QCOMPARE(list(view)->selectedKey(), QStringLiteral("acme/website!8"));
        QVERIFY(context.client.requestKeys().mid(before).contains(
            QStringLiteral("GET api/v4/projects/acme%2Fwebsite/merge_requests/8")));
    }

    void issueRowClickShowsThePreview()
    {
        FakeContext context;
        context.source.status = issues();
        View view(createIssueListView(context, nullptr));
        show(view);

        clickRow(view, QStringLiteral("backend#7"));

        QVERIFY(list(view)->preview());
        QCOMPARE(list(view)->selectedKey(), QStringLiteral("acme/backend#7"));
        QVERIFY(context.client.requestKeys().contains(backendKey(QStringLiteral("issues/7"))));
    }

    void previewReadsAgainWhenTheStatusChanges()
    {
        const QString issueKey = backendKey(QStringLiteral("issues/7"));
        FakeContext context;
        context.source.status = issues();
        View view(createIssueListView(context, nullptr));
        show(view);
        clickRow(view, QStringLiteral("backend#7"));
        QCoreApplication::processEvents();
        QPointer<QWidget> preview = list(view)->preview();
        QCOMPARE(context.client.requestKeys().count(issueKey), 1);

        context.source.change(issues());
        QCoreApplication::processEvents();

        QCOMPARE(context.client.requestKeys().count(issueKey), 2);
        // The same widget, so its scroll position and any open dialog stay.
        QCOMPARE(list(view)->preview(), preview.data());
    }

    void previewOfAGoneRowStays()
    {
        const QString issueKey = backendKey(QStringLiteral("issues/7"));
        const QJsonArray all = readJson(QStringLiteral("issue_list.json")).array();
        FakeContext context;
        context.source.status = statusOfRows(all);
        View view(createIssueListView(context, nullptr));
        show(view);
        clickRow(view, QStringLiteral("backend#7"));
        QCoreApplication::processEvents();
        QPointer<QWidget> preview = list(view)->preview();

        context.source.change(statusOfRows({all.at(1)}));
        QCoreApplication::processEvents();

        QVERIFY(list(view)->selectedKey().isEmpty());
        QCOMPARE(list(view)->preview(), preview.data());
        QCOMPARE(context.client.requestKeys().count(issueKey), 1);

        // Selecting another replaces it.
        clickRow(view, QStringLiteral("website#3"));
        QVERIFY(list(view)->preview() != preview.data());
    }

    void closedPreviewDoesNotReadAndComesBack()
    {
        const QString issueKey = backendKey(QStringLiteral("issues/7"));
        FakeContext context;
        context.source.status = issues();
        View view(createIssueListView(context, nullptr));
        show(view);
        clickRow(view, QStringLiteral("backend#7"));
        QCoreApplication::processEvents();

        view->findChild<QToolButton *>(QStringLiteral("closePreview"))->click();
        QVERIFY(!list(view)->preview());
        context.source.change(issues());
        QCoreApplication::processEvents();
        QCOMPARE(context.client.requestKeys().count(issueKey), 1);

        clickRow(view, QStringLiteral("backend#7"));
        QVERIFY(list(view)->preview());
        QCOMPARE(context.client.requestKeys().count(issueKey), 2);
    }

    void previewWaitsWhileHidden()
    {
        const QString issueKey = backendKey(QStringLiteral("issues/7"));
        FakeContext context;
        context.source.status = issues();
        View view(createIssueListView(context, nullptr));
        show(view);
        clickRow(view, QStringLiteral("backend#7"));
        QCoreApplication::processEvents();

        view->hide();
        context.source.change(issues());
        QCOMPARE(context.client.requestKeys().count(issueKey), 1);

        view->show();
        QCOMPARE(context.client.requestKeys().count(issueKey), 2);
    }

    // The row menu.

    void addToTaskPassesTypeAndConfiguration_data()
    {
        QTest::addColumn<bool>("isMergeRequest");
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("typeId");
        QTest::addColumn<QString>("project");
        QTest::addColumn<int>("iid");

        QTest::newRow("merge request") << true << QStringLiteral("acme/website!8")
                                       << QStringLiteral("gitlab.mr.summary") << QStringLiteral("acme/website") << 8;
        QTest::newRow("issue") << false << QStringLiteral("acme/backend#7") << QStringLiteral("gitlab.issue.summary")
                               << QStringLiteral("acme/backend") << 7;
    }

    void addToTaskPassesTypeAndConfiguration()
    {
        QFETCH(bool, isMergeRequest);
        QFETCH(QString, key);
        QFETCH(QString, typeId);
        QFETCH(QString, project);
        QFETCH(int, iid);

        FakeContext context;
        context.source.status = isMergeRequest ? mergeRequests() : issues();
        View view(isMergeRequest ? createMergeRequestListView(context, nullptr)
                                 : createIssueListView(context, nullptr));
        show(view);

        chooseFromMenu(view, key, QStringLiteral("Add to task as item"));

        QCOMPARE(context.added.size(), 1);
        QCOMPARE(context.added.first().typeId, typeId);
        QCOMPARE(context.added.first().name, key);
        const QJsonObject expected{{QStringLiteral("project"), project}, {QStringLiteral("iid"), iid}};
        QCOMPARE(context.added.first().config, expected);
    }

    void openPageShowsThePage()
    {
        FakeContext context;
        context.source.status = mergeRequests();
        View view(createMergeRequestListView(context, nullptr));
        show(view);

        chooseFromMenu(view, QStringLiteral("acme/backend!42"), QStringLiteral("Open page"));

        const QUrl address(QStringLiteral("https://gitlab.example/acme/backend/-/merge_requests/42"));
        QCOMPARE(context.pages, QList<QUrl>{address});
        QVERIFY(m_opened.isEmpty());
    }

    void shiftOpensTheBrowserInstead()
    {
        FakeContext context;
        context.source.status = issues();
        View view(createIssueListView(context, nullptr));
        show(view);

        chooseFromMenu(view, QStringLiteral("acme/backend#7"), QStringLiteral("Open page"), Qt::ShiftModifier);

        QVERIFY(context.pages.isEmpty());
        QCOMPARE(m_opened, QList<QUrl>{QUrl(QStringLiteral("https://gitlab.example/acme/backend/-/issues/7"))});
    }

    // Without web_url the address is built from the base URL and the row.
    void openPageBuildsAMissingAddress()
    {
        QJsonObject row = readJson(QStringLiteral("issue_list.json")).array().at(1).toObject();
        row.remove(QStringLiteral("web_url"));
        FakeContext context;
        context.source.status = statusOfRows({row});
        View view(createIssueListView(context, nullptr));
        show(view);

        chooseFromMenu(view, QStringLiteral("acme/website#3"), QStringLiteral("Open page"));

        QCOMPARE(context.pages, QList<QUrl>{QUrl(QStringLiteral("https://gitlab.example/acme/website/-/issues/3"))});
    }

    void copyLinkCopiesTheAddress()
    {
        if (QGuiApplication::platformName() != QLatin1String("offscreen"))
            QSKIP("Only with the offscreen platform, so the real clipboard stays as it is.");
        FakeContext context;
        context.source.status = mergeRequests();
        View view(createMergeRequestListView(context, nullptr));
        show(view);

        chooseFromMenu(view, QStringLiteral("acme/website!8"), QStringLiteral("Copy link"));

        QCOMPARE(QGuiApplication::clipboard()->text(),
                 QStringLiteral("https://gitlab.example/acme/website/-/merge_requests/8"));
    }

    // The Web link.

    void webLink_data()
    {
        QTest::addColumn<bool>("isMergeRequest");
        QTest::addColumn<QJsonObject>("itemConfig");
        QTest::addColumn<QString>("baseUrl");
        QTest::addColumn<QUrl>("expected");

        const QJsonObject project{{QStringLiteral("scope"), QStringLiteral("project")},
                                  {QStringLiteral("project"), QStringLiteral("acme/backend")}};
        const QJsonObject group{{QStringLiteral("scope"), QStringLiteral("group")},
                                {QStringLiteral("group"), QStringLiteral("acme")}};
        const QJsonObject all{{QStringLiteral("scope"), QStringLiteral("all")}};
        const QJsonObject noGroup{{QStringLiteral("scope"), QStringLiteral("group")}};

        QTest::newRow("project mrs") << true << project << kBase
                                     << QUrl(kBase + QStringLiteral("/acme/backend/-/merge_requests"));
        QTest::newRow("project issues") << false << project << kBase
                                        << QUrl(kBase + QStringLiteral("/acme/backend/-/issues"));
        QTest::newRow("group mrs") << true << group << kBase
                                   << QUrl(kBase + QStringLiteral("/groups/acme/-/merge_requests"));
        QTest::newRow("all issues") << false << all << kBase << QUrl(kBase + QStringLiteral("/dashboard/issues"));
        QTest::newRow("incomplete scope") << true << noGroup << kBase << QUrl();
        QTest::newRow("not a web address") << true << project << QStringLiteral("ftp://x.example") << QUrl();
    }

    void webLink()
    {
        QFETCH(bool, isMergeRequest);
        QFETCH(QJsonObject, itemConfig);
        QFETCH(QString, baseUrl);
        QFETCH(QUrl, expected);

        FakeContext context;
        context.itemConfig = itemConfig;
        context.general.insert(QStringLiteral("baseUrl"), baseUrl);
        View view(isMergeRequest ? createMergeRequestListView(context, nullptr)
                                 : createIssueListView(context, nullptr));

        QCOMPARE(list(view)->webLink(), expected);
    }

    // Without data, and with old data.

    void noDataShowsLoading()
    {
        FakeContext context;
        View view(createMergeRequestListView(context, nullptr));
        show(view);
        QVERIFY(shownText(view).contains(QStringLiteral("Loading…")));
        QVERIFY(!table(view)->isVisible());
        QVERIFY(context.client.requests().isEmpty());
    }

    void noStatusSourceShowsLoading()
    {
        FakeContext context;
        context.hasStatus = false;
        View view(createIssueListView(context, nullptr));
        show(view);
        QVERIFY(shownText(view).contains(QStringLiteral("Loading…")));
    }

    void noDataShowsTheMessage()
    {
        FakeContext context;
        context.source.status.message = QStringLiteral("Set the group in the item's settings.");
        View view(createIssueListView(context, nullptr));
        show(view);
        QVERIFY(shownText(view).contains(context.source.status.message));
        QVERIFY(!table(view)->isVisible());

        // Data removes the message.
        context.source.change(issues());
        QVERIFY(!shownText(view).contains(QStringLiteral("Set the group")));
        QVERIFY(table(view)->isVisible());
    }

    void oldDataStaysThroughAnError()
    {
        FakeContext context;
        setUpBackend42(context);
        context.source.status = mergeRequests();
        View view(createMergeRequestListView(context, nullptr));
        show(view);
        QCoreApplication::processEvents();
        const int before = int(context.client.requests().size());

        ItemStatus error;
        error.state = ItemStatus::State::Error;
        error.message = QStringLiteral("The server is down");
        context.source.change(error);
        QCoreApplication::processEvents();

        QCOMPARE(rowCount(view), 3);
        QVERIFY(table(view)->isVisible());
        QVERIFY(!shownText(view).contains(error.message));
        QCOMPARE(cellOf(view, QStringLiteral("backend!42"), MrPipeline), QStringLiteral("Failed"));
        QCOMPARE(context.client.requests().size(), before);
    }

    void emptyListShowsTheTableAndALine_data()
    {
        QTest::addColumn<bool>("isMergeRequest");
        QTest::addColumn<QString>("text");
        QTest::newRow("merge requests") << true << QStringLiteral("No merge requests");
        QTest::newRow("issues") << false << QStringLiteral("No issues");
    }

    void emptyListShowsTheTableAndALine()
    {
        QFETCH(bool, isMergeRequest);
        QFETCH(QString, text);

        FakeContext context;
        context.source.status = statusOfRows({});
        View view(isMergeRequest ? createMergeRequestListView(context, nullptr)
                                 : createIssueListView(context, nullptr));
        show(view);

        QVERIFY(table(view)->isVisible());
        QCOMPARE(rowCount(view), 0);
        QVERIFY(shownText(view).contains(text));
        QVERIFY(context.client.requests().isEmpty());

        // The line goes with the first row.
        context.source.change(isMergeRequest ? mergeRequests() : issues());
        QVERIFY(!shownText(view).contains(text));
    }

private:
    QList<QUrl> m_opened;
};

QTEST_MAIN(TestListViews)
#include "tst_list_views.moc"
