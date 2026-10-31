// The GitHub lists, drawn from the responses in tests/data/. A handler stands
// in for the external browser, so nothing is opened.
#include "../GitHubIssueListView.h"
#include "../GitHubListSupport.h"
#include "../GitHubLists.h"
#include "../GitHubPullRequestListView.h"
#include "../GitHubRunListView.h"

#include <mirante/ItemContext.h>
#include <mirante/Status.h>
#include <mirante/testing/FakeApiClient.h>
#include <mirante/widgets/CompactDate.h>
#include <mirante/widgets/ItemListView.h>
#include <mirante/widgets/SummaryView.h>

#include <QAbstractItemModel>
#include <QAction>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QTableView>
#include <QTest>
#include <QTimeZone>

#include <functional>
#include <memory>
#include <vector>

using mirante::ApiReply;
using mirante::ItemStatus;
using mirante::testing::FakeApiClient;
using mirante::widgets::ItemListView;
using namespace mirante::modules::github;

namespace {

enum PrColumn { PrRef, PrTitle, PrAuthor, PrChecks, PrReviews, PrUpdated, PrLinks };
enum IssueColumn { IssueRef, IssueTitle, IssueAssignees, IssueLabels, IssueUpdated, IssueLinks };
enum RunColumn { RunNumber, RunWorkflow, RunStatus, RunBranch, RunActor, RunStarted, RunLinks };

const QString kDash = QStringLiteral("–");
const QString kBase = QStringLiteral("https://github.example");
// The head commit of pull_request.json.
const QString kHeadSha = QStringLiteral("04c4ebf52e7033a83eb38e9741c8ace05bf8617b");

// The GET key of a request about acme/backend.
QString backendKey(const QString &suffix)
{
    return QStringLiteral("GET repos/acme/backend/") + suffix;
}

QString pullKey(int number)
{
    return backendKey(QStringLiteral("pulls/%1").arg(number));
}

QString reviewsKey(int number)
{
    return backendKey(QStringLiteral("pulls/%1/reviews?per_page=100").arg(number));
}

QString checkRunsKey(const QString &sha = kHeadSha)
{
    return backendKey(QStringLiteral("commits/%1/check-runs?per_page=100").arg(sha));
}

QString combinedStatusKey(const QString &sha = kHeadSha)
{
    return backendKey(QStringLiteral("commits/%1/status").arg(sha));
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

ItemStatus statusOfRows(const QJsonArray &rows)
{
    ItemStatus status;
    status.state = ItemStatus::State::Ok;
    status.data = QJsonObject{{QStringLiteral("rows"), rows}, {QStringLiteral("total"), int(rows.size())}};
    return status;
}

QJsonArray pullRequestRows()
{
    return readJson(QStringLiteral("pr_search.json")).object().value(QStringLiteral("items")).toArray();
}

ItemStatus pullRequests()
{
    return statusOfRows(pullRequestRows());
}

ItemStatus issues()
{
    return statusOfRows(readJson(QStringLiteral("issue_search.json")).object().value(QStringLiteral("items")).toArray());
}

QJsonArray runRows()
{
    return readJson(QStringLiteral("run_list.json")).object().value(QStringLiteral("workflow_runs")).toArray();
}

ItemStatus runs()
{
    return statusOfRows(runRows());
}

QJsonObject runConfig()
{
    return QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/backend")},
                       {QStringLiteral("source"), QStringLiteral("all")}};
}

// count pull requests of acme/backend, numbered from 1, where a higher number
// is newer. The array holds the oldest first.
QJsonArray manyPullRequests(int count)
{
    QJsonArray rows;
    for (int number = 1; number <= count; ++number) {
        rows.append(QJsonObject{
            {QStringLiteral("number"), number},
            {QStringLiteral("repository_url"), QStringLiteral("https://api.github.com/repos/acme/backend")},
            {QStringLiteral("title"), QStringLiteral("Change %1").arg(number)},
            {QStringLiteral("updated_at"), QDateTime(QDate(2026, 10, 1), QTime(0, 0), QTimeZone::UTC)
                                               .addSecs(number * 3600)
                                               .toString(Qt::ISODate)}});
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
    QJsonObject itemConfig{{QStringLiteral("scope"), QStringLiteral("repository")},
                           {QStringLiteral("repository"), QStringLiteral("own/config")}};
    QJsonObject general{{QStringLiteral("baseUrl"), kBase}};
    QJsonValue repositorySetting;
    QList<QUrl> pages;
    QList<AddedItem> added;
    FakeApiClient client{QUrl(kBase + QStringLiteral("/api/v3/"))};
    FakeStatusSource source;
    bool hasStatus = true;

    QString itemName() const override { return QStringLiteral("Item"); }
    QJsonObject config() const override { return itemConfig; }
    QJsonValue projectSetting(const QString &key) const override
    {
        return key == QLatin1String("repository") ? repositorySetting : QJsonValue();
    }
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

// The row whose first column reads ref, or -1.
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

// Delivers the answers, and those of the requests they lead to.
void settle()
{
    for (int round = 0; round < 5; ++round)
        QCoreApplication::processEvents();
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

// Sends a move to the viewport without moving the real cursor.
void moveMouse(QWidget *viewport, const QPoint &pos)
{
    QMouseEvent event(QEvent::MouseMove, pos, viewport->mapToGlobal(pos), Qt::NoButton, Qt::NoButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(viewport, &event);
}

// The middle of each link in a cell, found from where the cursor changes.
QList<QPoint> linkCentres(QTableView *table, int row, int column)
{
    const QRect cell = table->visualRect(table->model()->index(row, column));
    QList<QPoint> centres;
    int start = -1;
    for (int x = cell.left(); x <= cell.right() + 1; ++x) {
        const QPoint pos(x, cell.center().y());
        moveMouse(table->viewport(), pos);
        const bool onLink = table->viewport()->cursor().shape() == Qt::PointingHandCursor;
        if (onLink && start < 0)
            start = x;
        if (!onLink && start >= 0) {
            centres.append(QPoint((start + x) / 2, cell.center().y()));
            start = -1;
        }
    }
    moveMouse(table->viewport(), QPoint(cell.left(), cell.top()));
    return centres;
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

QStringList menuEntries(const View &view, const QString &key)
{
    emit list(view)->rowMenuRequested(key, QPoint(20, 20));
    QMenu *menu = view->findChildren<QMenu *>().constLast();
    QStringList texts;
    for (const QAction *action : menu->actions())
        texts << action->text();
    menu->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return texts;
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

// Answers for acme/backend#42: checks failed, by the combined status, and one
// approval.
void setUpBackend42(FakeContext &context)
{
    QVERIFY(context.client.setJsonFile(pullKey(42), dataFile(QStringLiteral("pull_request.json"))));
    QVERIFY(context.client.setJsonFile(reviewsKey(42), dataFile(QStringLiteral("pull_request_reviews.json"))));
    QVERIFY(context.client.setJsonFile(checkRunsKey(), dataFile(QStringLiteral("check_runs.json"))));
    QVERIFY(context.client.setJsonFile(combinedStatusKey(), dataFile(QStringLiteral("combined_status.json"))));
}

// A pull request whose head is sha, and one check run with this conclusion on it.
void setChecksAnswer(FakeContext &context, int number, const QString &sha, const QString &conclusion)
{
    context.client.setJson(pullKey(number),
                           QJsonDocument(QJsonObject{{QStringLiteral("head"), QJsonObject{{QStringLiteral("sha"), sha}}}}));
    context.client.setJson(
        checkRunsKey(sha),
        QJsonDocument(QJsonObject{{QStringLiteral("check_runs"),
                                   QJsonArray{QJsonObject{{QStringLiteral("status"), QStringLiteral("completed")},
                                                          {QStringLiteral("conclusion"), conclusion}}}}}));
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

    void rowRepositoryReadsTheApiAddress()
    {
        const QJsonObject row{
            {QStringLiteral("repository_url"), QStringLiteral("https://api.github.com/repos/cli/cli")}};
        QCOMPARE(rowRepository(row), QStringLiteral("cli/cli"));
        QCOMPARE(rowRepository(QJsonObject()), QString());
    }

    // Drawing.

    void pullRequestRowsDrawFromData()
    {
        FakeContext context;
        context.source.status = pullRequests();
        View view(createPullRequestListView(context, nullptr));

        QCOMPARE(rowCount(view), 3);
        QCOMPARE(cellText(view, 0, PrRef), QStringLiteral("backend#42"));
        QCOMPARE(cellText(view, 0, PrTitle), QStringLiteral("Add retries to the upload client"));
        QCOMPARE(cellText(view, 0, PrAuthor), QStringLiteral("ana-dev"));
        QCOMPARE(cellText(view, 0, PrChecks), kDash);
        QCOMPARE(cellText(view, 0, PrReviews), kDash);
        const QDateTime updated = QDateTime::fromString(QStringLiteral("2026-10-29T16:40:02Z"), Qt::ISODateWithMs);
        QCOMPARE(cellText(view, 0, PrUpdated), mirante::widgets::compactDate(updated));
        // A list can span repositories.
        QCOMPARE(cellText(view, 1, PrRef), QStringLiteral("website#318"));
        QCOMPARE(cellText(view, 2, PrRef), QStringLiteral("backend#37"));
        QCOMPARE(cellText(view, 2, PrAuthor), QStringLiteral("dependabot[bot]"));
    }

    void issueRowsDrawFromData()
    {
        FakeContext context;
        context.source.status = issues();
        View view(createIssueListView(context, nullptr));
        show(view);

        QCOMPARE(rowCount(view), 2);
        QCOMPARE(cellText(view, 0, IssueRef), QStringLiteral("backend#51"));
        QCOMPARE(cellText(view, 0, IssueTitle), QStringLiteral("Uploads stop after the token expires"));
        QCOMPARE(cellText(view, 0, IssueAssignees), QStringLiteral("ana-dev"));
        QCOMPARE(cellText(view, 0, IssueLabels), QStringLiteral("bug, priority: high"));
        QCOMPARE(cellText(view, 1, IssueRef), QStringLiteral("website#12"));
        QCOMPARE(cellText(view, 1, IssueAssignees), QStringLiteral("ana-dev, bruno-ux"));
        QCOMPARE(cellText(view, 1, IssueLabels), QStringLiteral("documentation"));
        // An issue list asks for nothing more.
        QVERIFY(context.client.requests().isEmpty());
    }

    // By the date, newest first, whatever the order of the data.
    void rowsSortOnUpdatedNewestFirst()
    {
        const QJsonArray rows = pullRequestRows();
        FakeContext context;
        context.source.status = statusOfRows({rows.at(2), rows.at(0), rows.at(1)});
        View view(createPullRequestListView(context, nullptr));

        QCOMPARE(cellText(view, 0, PrRef), QStringLiteral("backend#42"));
        QCOMPARE(cellText(view, 1, PrRef), QStringLiteral("website#318"));
        QCOMPARE(cellText(view, 2, PrRef), QStringLiteral("backend#37"));
    }

    // Ref sorts by the whole repository, then by the number as a number.
    void refSortsByRepositoryAndNumber()
    {
        QJsonArray rows = manyPullRequests(12);
        FakeContext context;
        context.source.status = statusOfRows(rows);
        View view(createPullRequestListView(context, nullptr));
        table(view)->sortByColumn(PrRef, Qt::AscendingOrder);

        QCOMPARE(cellText(view, 0, PrRef), QStringLiteral("backend#1"));
        QCOMPARE(cellText(view, 1, PrRef), QStringLiteral("backend#2"));
        QCOMPARE(cellText(view, 11, PrRef), QStringLiteral("backend#12"));
    }

    // A row that names no repository still draws, and nothing is asked for it.
    void rowWithoutARepository()
    {
        QJsonObject row = pullRequestRows().at(0).toObject();
        row.remove(QStringLiteral("repository_url"));
        row.remove(QStringLiteral("html_url"));

        FakeContext context;
        context.source.status = statusOfRows({row});
        View view(createPullRequestListView(context, nullptr));
        show(view);

        QCOMPARE(rowCount(view), 1);
        QCOMPARE(cellText(view, 0, PrRef), QStringLiteral("#42"));
        QVERIFY(context.client.requests().isEmpty());

        clickRow(view, QStringLiteral("#42"));
        QVERIFY(context.client.requests().isEmpty());
        QVERIFY(linkCentres(table(view), 0, PrLinks).isEmpty());
        QVERIFY(!menuEntryEnabled(view, QStringLiteral("row-0"), QStringLiteral("Web")));
        QVERIFY(!menuEntryEnabled(view, QStringLiteral("row-0"), QStringLiteral("Add to task as item")));
        QVERIFY(!menuEntryEnabled(view, QStringLiteral("row-0"), QStringLiteral("Open page")));
        QVERIFY(!menuEntryEnabled(view, QStringLiteral("row-0"), QStringLiteral("Copy link")));
    }

    // The two fields.

    void fieldsHideColumnsAndStopRequests_data()
    {
        QTest::addColumn<QJsonValue>("checks");
        QTest::addColumn<QJsonValue>("reviews");
        QTest::addColumn<bool>("showsChecks");
        QTest::addColumn<bool>("showsReviews");

        const QJsonValue absent(QJsonValue::Undefined);
        QTest::newRow("absent") << absent << absent << true << true;
        QTest::newRow("both on") << QJsonValue(true) << QJsonValue(true) << true << true;
        QTest::newRow("no checks") << QJsonValue(false) << absent << false << true;
        QTest::newRow("no reviews") << absent << QJsonValue(false) << true << false;
        QTest::newRow("neither") << QJsonValue(false) << QJsonValue(false) << false << false;
    }

    void fieldsHideColumnsAndStopRequests()
    {
        QFETCH(QJsonValue, checks);
        QFETCH(QJsonValue, reviews);
        QFETCH(bool, showsChecks);
        QFETCH(bool, showsReviews);

        FakeContext context;
        setUpBackend42(context);
        if (!checks.isUndefined())
            context.itemConfig.insert(QStringLiteral("showChecks"), checks);
        if (!reviews.isUndefined())
            context.itemConfig.insert(QStringLiteral("showReviews"), reviews);
        context.source.status = pullRequests();
        View view(createPullRequestListView(context, nullptr));
        show(view);
        settle();

        QCOMPARE(table(view)->isColumnHidden(PrChecks), !showsChecks);
        QCOMPARE(table(view)->isColumnHidden(PrReviews), !showsReviews);
        QVERIFY(!table(view)->isColumnHidden(PrTitle));

        const QStringList keys = context.client.requestKeys();
        QCOMPARE(keys.contains(pullKey(42)), showsChecks);
        QCOMPARE(keys.contains(checkRunsKey()), showsChecks);
        QCOMPARE(keys.contains(combinedStatusKey()), showsChecks);
        QCOMPARE(keys.contains(reviewsKey(42)), showsReviews);
        // The two of #42's head commit come on top of one or two for each row.
        QCOMPARE(keys.size(), 3 * (int(showsChecks) + int(showsReviews)) + 2 * int(showsChecks));
    }

    void narrowCellKeepsTheTitle()
    {
        FakeContext context;
        context.source.status = pullRequests();
        View view(createPullRequestListView(context, nullptr));
        view->resize(200, 400);
        view->show();
        QCoreApplication::processEvents();

        QVERIFY(!table(view)->isColumnHidden(PrTitle));
        QVERIFY(table(view)->isColumnHidden(PrAuthor));
        QVERIFY(table(view)->isColumnHidden(PrReviews));

        view->resize(1400, 400);
        QCoreApplication::processEvents();
        for (int column = PrRef; column <= PrUpdated; ++column)
            QVERIFY(!table(view)->isColumnHidden(column));
    }

    // Author goes first, then Reviews, Updated, Checks and Ref.
    void columnsHideInOrder()
    {
        FakeContext context;
        context.source.status = pullRequests();
        View view(createPullRequestListView(context, nullptr));
        view->show();
        const QList<int> order{PrAuthor, PrReviews, PrUpdated, PrChecks, PrRef};
        int hiddenBefore = 0;
        for (int width = 1400; width >= 100; width -= 20) {
            view->resize(width, 400);
            QCoreApplication::processEvents();
            int hidden = 0;
            while (hidden < order.size() && table(view)->isColumnHidden(order.at(hidden)))
                ++hidden;
            // Only a prefix of the order is hidden.
            for (int later = hidden; later < order.size(); ++later)
                QVERIFY2(!table(view)->isColumnHidden(order.at(later)), qPrintable(QString::number(width)));
            QVERIFY(hidden >= hiddenBefore);
            hiddenBefore = hidden;
        }
        QVERIFY(hiddenBefore > 1);
    }

    // The further requests.

    void requestsStopAt25Rows()
    {
        FakeContext context;
        context.source.status = statusOfRows(manyPullRequests(30));
        View view(createPullRequestListView(context, nullptr));
        show(view);

        // Two requests for each of the 25 newest rows, in the order of the table.
        const QStringList keys = context.client.requestKeys();
        QCOMPARE(keys.size(), 50);
        QCOMPARE(keys.at(0), pullKey(30));
        QCOMPARE(keys.at(1), reviewsKey(30));
        QVERIFY(keys.contains(pullKey(6)));
        QVERIFY(!keys.contains(pullKey(5)));
        QVERIFY(!keys.contains(reviewsKey(5)));
    }

    // The table's current order decides, not the order of the data.
    void requestsFollowTheTablesSort()
    {
        FakeContext context;
        context.source.status = statusOfRows(manyPullRequests(30));
        View view(createPullRequestListView(context, nullptr));
        show(view);
        table(view)->sortByColumn(PrUpdated, Qt::AscendingOrder);
        const int before = int(context.client.requests().size());

        context.source.change(statusOfRows(manyPullRequests(30)));

        const QStringList keys = context.client.requestKeys().mid(before);
        QCOMPARE(keys.size(), 50);
        QCOMPARE(keys.at(0), pullKey(1));
        QVERIFY(!keys.contains(pullKey(26)));
    }

    void noRequestsWhileHidden()
    {
        FakeContext context;
        context.source.status = pullRequests();
        View view(createPullRequestListView(context, nullptr));
        // Never shown yet.
        context.source.change(pullRequests());
        QVERIFY(context.client.requests().isEmpty());

        show(view);
        QCOMPARE(context.client.requests().size(), 6);
        QCoreApplication::processEvents();

        view->hide();
        context.source.change(pullRequests());
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
        context.source.status = pullRequests();
        View view(createPullRequestListView(context, nullptr));
        view->show();
        view->hide();
        settle();

        QCOMPARE(cellOf(view, QStringLiteral("backend#42"), PrChecks), kDash);
        QCOMPARE(cellOf(view, QStringLiteral("backend#42"), PrReviews), kDash);
        // The checks of the head commit are not asked for either.
        QVERIFY(!context.client.requestKeys().contains(checkRunsKey()));
    }

    // Hidden between the pull request's answer and those of its checks.
    void hidingDropsTheChecksOnTheWay()
    {
        FakeContext context;
        setUpBackend42(context);
        context.source.status = pullRequests();
        View view(createPullRequestListView(context, nullptr));
        view->show();
        for (int round = 0; round < 5 && !context.client.requestKeys().contains(checkRunsKey()); ++round)
            QCoreApplication::processEvents();
        QVERIFY(context.client.requestKeys().contains(checkRunsKey()));
        QCOMPARE(cellOf(view, QStringLiteral("backend#42"), PrChecks), kDash);
        view->hide();
        settle();

        QCOMPARE(cellOf(view, QStringLiteral("backend#42"), PrChecks), kDash);
        // The reviews came with the pull request, before the view hid.
        QCOMPARE(cellOf(view, QStringLiteral("backend#42"), PrReviews), QStringLiteral("1 approved"));
    }

    void answersFillTheCells()
    {
        FakeContext context;
        setUpBackend42(context);
        context.source.status = pullRequests();
        View view(createPullRequestListView(context, nullptr));
        show(view);
        settle();

        // The pull request's head, then its check runs and combined status.
        const QStringList keys = context.client.requestKeys();
        QVERIFY(keys.indexOf(checkRunsKey()) > keys.indexOf(pullKey(42)));
        QVERIFY(keys.contains(combinedStatusKey()));
        QCOMPARE(cellOf(view, QStringLiteral("backend#42"), PrChecks), QStringLiteral("Failed"));
        QCOMPARE(cellOf(view, QStringLiteral("backend#42"), PrReviews), QStringLiteral("1 approved"));
    }

    void checksCells_data()
    {
        QTest::addColumn<QString>("checkRuns");
        QTest::addColumn<QString>("combinedStatus");
        QTest::addColumn<QString>("expected");

        QTest::newRow("both, a status failed") << QStringLiteral("check_runs.json")
                                               << QStringLiteral("combined_status.json") << QStringLiteral("Failed");
        QTest::newRow("no statuses") << QStringLiteral("check_runs.json") << QStringLiteral("combined_status_empty.json")
                                     << QStringLiteral("Passed");
        // A failed request leaves its part out.
        QTest::newRow("check runs only") << QStringLiteral("check_runs.json") << QString() << QStringLiteral("Passed");
        QTest::newRow("combined status only") << QString() << QStringLiteral("combined_status.json")
                                              << QStringLiteral("Failed");
        QTest::newRow("both failed") << QString() << QString() << kDash;
        QTest::newRow("no checks at all") << QStringLiteral("listview_check_runs_empty.json")
                                          << QStringLiteral("combined_status_empty.json") << kDash;
        QTest::newRow("running") << QStringLiteral("listview_check_runs_running.json")
                                 << QStringLiteral("combined_status_empty.json") << QStringLiteral("Running");
    }

    void checksCells()
    {
        QFETCH(QString, checkRuns);
        QFETCH(QString, combinedStatus);
        QFETCH(QString, expected);

        FakeContext context;
        QVERIFY(context.client.setJsonFile(pullKey(42), dataFile(QStringLiteral("pull_request.json"))));
        if (!checkRuns.isEmpty())
            QVERIFY(context.client.setJsonFile(checkRunsKey(), dataFile(checkRuns)));
        if (!combinedStatus.isEmpty())
            QVERIFY(context.client.setJsonFile(combinedStatusKey(), dataFile(combinedStatus)));
        context.source.status = pullRequests();
        View view(createPullRequestListView(context, nullptr));
        show(view);
        settle();

        QCOMPARE(cellOf(view, QStringLiteral("backend#42"), PrChecks), expected);
    }

    void reviewsCells_data()
    {
        QTest::addColumn<QJsonDocument>("answer");
        QTest::addColumn<QString>("expected");

        const auto review = [](const QString &login, const QString &state, int minute) {
            return QJsonObject{{QStringLiteral("user"), QJsonObject{{QStringLiteral("login"), login}}},
                               {QStringLiteral("state"), state},
                               {QStringLiteral("submitted_at"), QStringLiteral("2026-10-28T10:%1:00Z").arg(minute, 2, 10, QLatin1Char('0'))}};
        };
        QTest::newRow("one approval") << readJson(QStringLiteral("pull_request_reviews.json"))
                                      << QStringLiteral("1 approved");
        QTest::newRow("two approvals") << QJsonDocument(QJsonArray{review(QStringLiteral("ana"), QStringLiteral("APPROVED"), 1),
                                                                   review(QStringLiteral("bia"), QStringLiteral("APPROVED"), 2)})
                                       << QStringLiteral("2 approved");
        QTest::newRow("changes requested") << readJson(QStringLiteral("pull_request_reviews_changes.json"))
                                           << QStringLiteral("changes requested");
        QTest::newRow("comments only") << QJsonDocument(QJsonArray{review(QStringLiteral("ana"), QStringLiteral("COMMENTED"), 1)})
                                       << kDash;
        QTest::newRow("none") << QJsonDocument(QJsonArray()) << kDash;
    }

    void reviewsCells()
    {
        QFETCH(QJsonDocument, answer);
        QFETCH(QString, expected);

        FakeContext context;
        context.client.setJson(reviewsKey(42), answer);
        context.source.status = pullRequests();
        View view(createPullRequestListView(context, nullptr));
        show(view);
        QCoreApplication::processEvents();

        QCOMPARE(cellOf(view, QStringLiteral("backend#42"), PrReviews), expected);
    }

    // The other rows have no answer on the fake server, so they are 404s.
    void failedRequestLeavesTheDash()
    {
        FakeContext context;
        setUpBackend42(context);
        context.source.status = pullRequests();
        View view(createPullRequestListView(context, nullptr));
        show(view);
        settle();

        QCOMPARE(cellOf(view, QStringLiteral("backend#37"), PrChecks), kDash);
        QCOMPARE(cellOf(view, QStringLiteral("backend#37"), PrReviews), kDash);
        QCOMPARE(cellOf(view, QStringLiteral("backend#42"), PrChecks), QStringLiteral("Failed"));
        // A failed pull request asks nothing about its head.
        QCOMPARE(context.client.requestKeys().filter(QStringLiteral("/commits/")).size(), 2);
    }

    void pullRequestWithoutAHeadShowsADash()
    {
        FakeContext context;
        context.client.setJson(pullKey(42), QJsonDocument(QJsonObject()));
        context.source.status = pullRequests();
        View view(createPullRequestListView(context, nullptr));
        show(view);
        settle();

        QCOMPARE(cellOf(view, QStringLiteral("backend#42"), PrChecks), kDash);
        QVERIFY(context.client.requestKeys().filter(QStringLiteral("/commits/")).isEmpty());
    }

    // The last answer stays until the new one comes, and then gives way to it.
    void newerStatusReplacesOlderAnswers()
    {
        FakeContext context;
        setChecksAnswer(context, 42, kHeadSha, QStringLiteral("failure"));
        context.source.status = pullRequests();
        View view(createPullRequestListView(context, nullptr));
        show(view);
        settle();
        QCOMPARE(cellOf(view, QStringLiteral("backend#42"), PrChecks), QStringLiteral("Failed"));

        setChecksAnswer(context, 42, kHeadSha, QStringLiteral("success"));
        context.source.change(pullRequests());
        QCOMPARE(cellOf(view, QStringLiteral("backend#42"), PrChecks), QStringLiteral("Failed"));
        settle();
        QCOMPARE(cellOf(view, QStringLiteral("backend#42"), PrChecks), QStringLiteral("Passed"));
    }

    void failureAfterAnAnswerGoesBackToTheDash()
    {
        FakeContext context;
        setChecksAnswer(context, 42, kHeadSha, QStringLiteral("failure"));
        context.client.setJson(reviewsKey(42), readJson(QStringLiteral("pull_request_reviews.json")));
        context.source.status = pullRequests();
        View view(createPullRequestListView(context, nullptr));
        show(view);
        settle();
        QCOMPARE(cellOf(view, QStringLiteral("backend#42"), PrReviews), QStringLiteral("1 approved"));

        ApiReply failure;
        failure.error = ApiReply::Error::Network;
        failure.message = QStringLiteral("No route");
        context.client.setReply(pullKey(42), failure);
        context.client.setReply(reviewsKey(42), failure);
        context.source.change(pullRequests());
        QCoreApplication::processEvents();
        QCOMPARE(cellOf(view, QStringLiteral("backend#42"), PrChecks), kDash);
        QCOMPARE(cellOf(view, QStringLiteral("backend#42"), PrReviews), kDash);
    }

    void answerForAGoneRowIsDropped()
    {
        const QJsonArray all = pullRequestRows();
        FakeContext context;
        setChecksAnswer(context, 42, kHeadSha, QStringLiteral("failure"));
        context.source.status = statusOfRows(all);
        View view(createPullRequestListView(context, nullptr));
        view->resize(1200, 700);
        view->show();

        // backend#42 goes while its answer is on the way.
        context.source.change(statusOfRows({all.at(1), all.at(2)}));
        settle();
        QCOMPARE(rowCount(view), 2);
        QCOMPARE(rowOf(view, QStringLiteral("backend#42")), -1);
        QVERIFY(!context.client.requestKeys().contains(checkRunsKey()));

        // It comes back while hidden, and nothing was kept for it.
        view->hide();
        context.source.change(statusOfRows(all));
        QCOMPARE(cellOf(view, QStringLiteral("backend#42"), PrChecks), kDash);
    }

    // A row that falls out of the first 25 goes back to a dash.
    void rowBeyondTheLimitLosesItsAnswer()
    {
        FakeContext context;
        for (int number = 1; number <= 26; ++number)
            setChecksAnswer(context, number, QStringLiteral("sha%1").arg(number), QStringLiteral("success"));
        context.source.status = statusOfRows(manyPullRequests(25));
        View view(createPullRequestListView(context, nullptr));
        show(view);
        settle();
        QCOMPARE(cellOf(view, QStringLiteral("backend#1"), PrChecks), QStringLiteral("Passed"));

        context.source.change(statusOfRows(manyPullRequests(26)));
        QCOMPARE(cellOf(view, QStringLiteral("backend#1"), PrChecks), kDash);
    }

    // No preview.

    void rowClickAsksForNothing_data()
    {
        QTest::addColumn<bool>("isPullRequest");
        QTest::addColumn<QString>("ref");
        QTest::newRow("pull request") << true << QStringLiteral("website#318");
        QTest::newRow("issue") << false << QStringLiteral("backend#51");
    }

    void rowClickAsksForNothing()
    {
        QFETCH(bool, isPullRequest);
        QFETCH(QString, ref);

        FakeContext context;
        context.source.status = isPullRequest ? pullRequests() : issues();
        View view(isPullRequest ? createPullRequestListView(context, nullptr) : createIssueListView(context, nullptr));
        show(view);
        QCoreApplication::processEvents();
        const int before = int(context.client.requests().size());

        clickRow(view, ref);

        QVERIFY(table(view)->selectionModel()->hasSelection());
        QCOMPARE(int(context.client.requests().size()), before);
        for (QWidget *child : view->findChildren<QWidget *>())
            QVERIFY(!dynamic_cast<mirante::widgets::SummaryView *>(child));
    }

    // The links of a row.

    void linksComeLastAndInOrder_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<int>("column");
        QTest::addColumn<QUrl>("address");
        QTest::addColumn<QString>("typeId");
        QTest::addColumn<QString>("name");

        QTest::newRow("pull request") << 0 << int(PrLinks) << QUrl(QStringLiteral("https://github.com/acme/backend/pull/42"))
                                      << QStringLiteral("github.pr.summary") << QStringLiteral("acme/backend#42");
        QTest::newRow("issue") << 1 << int(IssueLinks) << QUrl(QStringLiteral("https://github.com/acme/backend/issues/51"))
                               << QStringLiteral("github.issue.summary") << QStringLiteral("acme/backend#51");
        QTest::newRow("run") << 2 << int(RunLinks)
                             << QUrl(QStringLiteral("https://github.com/acme/backend/actions/runs/18800000004"))
                             << QStringLiteral("github.run") << QStringLiteral("acme/backend#42 CI");
    }

    // Web, Open and Add, each clicked with the mouse.
    void linksComeLastAndInOrder()
    {
        QFETCH(int, kind);
        QFETCH(int, column);
        QFETCH(QUrl, address);
        QFETCH(QString, typeId);
        QFETCH(QString, name);

        FakeContext context;
        if (kind == 2)
            context.itemConfig = runConfig();
        context.source.status = kind == 0 ? pullRequests() : kind == 1 ? issues() : runs();
        View view(kind == 0   ? createPullRequestListView(context, nullptr)
                  : kind == 1 ? createIssueListView(context, nullptr)
                              : createRunListView(context, nullptr));
        show(view);
        QVERIFY(QTest::qWaitForWindowExposed(view.get()));
        QCoreApplication::processEvents();
        const int before = int(context.client.requests().size());

        QCOMPARE(column, table(view)->model()->columnCount() - 1);
        const QList<QPoint> links = linkCentres(table(view), 0, column);
        QCOMPARE(links.size(), 3);

        QTest::mouseClick(table(view)->viewport(), Qt::LeftButton, {}, links.at(0));
        QCOMPARE(m_opened, QList<QUrl>{address});
        QVERIFY(context.pages.isEmpty());

        QTest::mouseClick(table(view)->viewport(), Qt::LeftButton, {}, links.at(1));
        QCOMPARE(context.pages, QList<QUrl>{address});
        QCOMPARE(m_opened.size(), 1);

        QTest::mouseClick(table(view)->viewport(), Qt::LeftButton, {}, links.at(2));
        QCOMPARE(context.added.size(), 1);
        QCOMPARE(context.added.first().typeId, typeId);
        QCOMPARE(context.added.first().name, name);

        // None of them asks the server for anything.
        QCOMPARE(int(context.client.requests().size()), before);
    }

    void shiftOnOpenUsesTheBrowser()
    {
        FakeContext context;
        context.source.status = issues();
        View view(createIssueListView(context, nullptr));
        show(view);
        QVERIFY(QTest::qWaitForWindowExposed(view.get()));

        const QList<QPoint> links = linkCentres(table(view), 0, IssueLinks);
        QCOMPARE(links.size(), 3);
        QTest::mouseClick(table(view)->viewport(), Qt::LeftButton, Qt::ShiftModifier, links.at(1));

        QVERIFY(context.pages.isEmpty());
        QCOMPARE(m_opened, QList<QUrl>{QUrl(QStringLiteral("https://github.com/acme/backend/issues/51"))});
    }

    // The links have no text to sort by, so the rows fall in the order of their ref.
    void sortingByLinksIsStable()
    {
        FakeContext context;
        context.source.status = statusOfRows(manyPullRequests(12));
        View view(createPullRequestListView(context, nullptr));
        show(view);

        table(view)->sortByColumn(PrLinks, Qt::AscendingOrder);
        QCOMPARE(cellText(view, 0, PrRef), QStringLiteral("backend#1"));
        QCOMPARE(cellText(view, 1, PrRef), QStringLiteral("backend#2"));
        QCOMPARE(cellText(view, 11, PrRef), QStringLiteral("backend#12"));

        table(view)->sortByColumn(PrLinks, Qt::DescendingOrder);
        QCOMPARE(cellText(view, 0, PrRef), QStringLiteral("backend#12"));
    }

    // Sizes.

    void pageSizeComesFromTheItem_data()
    {
        QTest::addColumn<QJsonValue>("value");
        QTest::addColumn<int>("expected");

        QTest::newRow("absent") << QJsonValue(QJsonValue::Undefined) << 50;
        QTest::newRow("set") << QJsonValue(10) << 10;
        QTest::newRow("zero") << QJsonValue(0) << 50;
        QTest::newRow("below zero") << QJsonValue(-1) << 50;
    }

    void pageSizeComesFromTheItem()
    {
        QFETCH(QJsonValue, value);
        QFETCH(int, expected);

        const std::vector<QWidget *(*)(mirante::ItemContext &, QWidget *)> factories{
            &createPullRequestListView, &createIssueListView, &createRunListView};
        for (const auto factory : factories) {
            FakeContext context;
            if (!value.isUndefined())
                context.itemConfig.insert(QStringLiteral("pageSize"), value);
            View view(factory(context, nullptr));
            QCOMPARE(list(view)->pageSize(), expected);
        }
    }

    // The further requests follow the page.

    void fillFollowsThePage()
    {
        FakeContext context;
        context.itemConfig.insert(QStringLiteral("pageSize"), 10);
        context.source.status = statusOfRows(manyPullRequests(30));
        View view(createPullRequestListView(context, nullptr));
        show(view);

        // The newest 10.
        QStringList keys = context.client.requestKeys();
        QCOMPARE(keys.size(), 20);
        QVERIFY(keys.contains(pullKey(30)));
        QVERIFY(keys.contains(pullKey(21)));
        QVERIFY(!keys.contains(pullKey(20)));
        QCoreApplication::processEvents();

        view->findChild<QPushButton *>(QStringLiteral("nextPage"))->click();
        keys = context.client.requestKeys().mid(20);
        QCOMPARE(keys.size(), 20);
        QVERIFY(keys.contains(pullKey(20)));
        QVERIFY(keys.contains(pullKey(11)));
        QVERIFY(!keys.contains(pullKey(21)));
    }

    // At most 25 of a longer page, and a new sort asks only for the rows it adds.
    void fillStopsAt25OfThePage()
    {
        FakeContext context;
        context.source.status = statusOfRows(manyPullRequests(30));
        View view(createPullRequestListView(context, nullptr));
        show(view);
        QCOMPARE(int(context.client.requests().size()), 50);
        QCoreApplication::processEvents();

        table(view)->sortByColumn(PrUpdated, Qt::AscendingOrder);

        // The oldest 25 are 1 to 25. Of these, 6 to 25 are there already.
        const QStringList keys = context.client.requestKeys().mid(50);
        QCOMPARE(keys.size(), 10);
        QVERIFY(keys.contains(pullKey(1)));
        QVERIFY(keys.contains(reviewsKey(5)));
        QVERIFY(!keys.contains(pullKey(6)));
    }

    // What is on the way for a row that leaves the page is dropped, and the row goes
    // back to a dash. What is on the way for the new page arrives.
    void leavingThePageCancelsItsRequests()
    {
        FakeContext context;
        for (int number = 1; number <= 4; ++number)
            setChecksAnswer(context, number, QStringLiteral("sha%1").arg(number), QStringLiteral("success"));
        context.itemConfig.insert(QStringLiteral("pageSize"), 2);
        context.source.status = statusOfRows(manyPullRequests(4));
        View view(createPullRequestListView(context, nullptr));
        show(view);

        view->findChild<QPushButton *>(QStringLiteral("nextPage"))->click();
        settle();
        QCOMPARE(cellOf(view, QStringLiteral("backend#4"), PrChecks), kDash);
        QCOMPARE(cellOf(view, QStringLiteral("backend#2"), PrChecks), QStringLiteral("Passed"));

        // Back on the first page, it asks again, and the other page loses its answers.
        view->findChild<QPushButton *>(QStringLiteral("previousPage"))->click();
        QCOMPARE(cellOf(view, QStringLiteral("backend#2"), PrChecks), kDash);
        settle();
        QCOMPARE(cellOf(view, QStringLiteral("backend#4"), PrChecks), QStringLiteral("Passed"));
    }

    // Sorted so that an answer moves its row off the first 25. That must not ask
    // again, or the rows would keep trading places.
    void answersThatMoveRowsAskForNothingMore()
    {
        FakeContext context;
        for (int number = 1; number <= 30; ++number)
            context.client.setJson(reviewsKey(number), QJsonDocument(QJsonArray()));
        context.itemConfig.insert(QStringLiteral("showChecks"), false);
        context.source.status = statusOfRows(manyPullRequests(30));
        View view(createPullRequestListView(context, nullptr));
        show(view);
        QCoreApplication::processEvents();

        table(view)->sortByColumn(PrReviews, Qt::AscendingOrder);
        const int before = int(context.client.requests().size());
        for (int round = 0; round < 5; ++round)
            QCoreApplication::processEvents();
        QCOMPARE(int(context.client.requests().size()), before);
    }

    // Dates.

    void datesAreCompactAndSortByTheDate()
    {
        const int year = QDate::currentDate().year();
        const QDateTime thisYear(QDate(year, 1, 2), QTime(9, 30), QTimeZone::LocalTime);
        const QDateTime lastYear(QDate(year - 1, 12, 30), QTime(9, 30), QTimeZone::LocalTime);
        QJsonArray rows = manyPullRequests(3);
        const auto dated = [&rows](int index, const QDateTime &when) {
            QJsonObject row = rows.at(index).toObject();
            row.insert(QStringLiteral("updated_at"), when.toUTC().toString(Qt::ISODate));
            rows.replace(index, row);
        };
        dated(0, lastYear);
        dated(1, thisYear);
        QJsonObject missing = rows.at(2).toObject();
        missing.remove(QStringLiteral("updated_at"));
        rows.replace(2, missing);

        FakeContext context;
        context.source.status = statusOfRows(rows);
        View view(createPullRequestListView(context, nullptr));

        QCOMPARE(cellText(view, 0, PrRef), QStringLiteral("backend#2"));
        QCOMPARE(cellText(view, 0, PrUpdated), mirante::widgets::compactDate(thisYear));
        QCOMPARE(cellText(view, 1, PrRef), QStringLiteral("backend#1"));
        QCOMPARE(cellText(view, 1, PrUpdated), QStringLiteral("%1-12-30").arg(year - 1));
        // A missing date is the oldest.
        QCOMPARE(cellText(view, 2, PrRef), QStringLiteral("backend#3"));
        QCOMPARE(cellText(view, 2, PrUpdated), kDash);
    }

    // The run list.

    void runRowsDrawFromData()
    {
        FakeContext context;
        context.itemConfig = runConfig();
        context.source.status = runs();
        View view(createRunListView(context, nullptr));
        show(view);

        // Sorted on the run, newest first.
        QCOMPARE(rowCount(view), 4);
        QCOMPARE(cellText(view, 0, RunNumber), QStringLiteral("#1207"));
        QCOMPARE(cellText(view, 0, RunWorkflow), QStringLiteral("CI"));
        QCOMPARE(cellText(view, 0, RunStatus), QStringLiteral("Running"));
        QCOMPARE(cellText(view, 0, RunBranch), QStringLiteral("feature/upload-retries"));
        QCOMPARE(cellText(view, 0, RunActor), QStringLiteral("ana-dev"));
        const QDateTime started = QDateTime::fromString(QStringLiteral("2026-10-29T16:40:10Z"), Qt::ISODate);
        QCOMPARE(cellText(view, 0, RunStarted), mirante::widgets::compactDate(started));

        QCOMPARE(cellText(view, 1, RunNumber), QStringLiteral("#1206"));
        QCOMPARE(cellText(view, 1, RunStatus), QStringLiteral("Passed"));
        // Run numbers count per workflow; the newer run sorts first.
        QCOMPARE(cellText(view, 2, RunNumber), QStringLiteral("#88"));
        QCOMPARE(cellText(view, 2, RunWorkflow), QStringLiteral("Release"));
        QCOMPARE(cellText(view, 2, RunStatus), QStringLiteral("Failed"));
        const QDateTime restarted = QDateTime::fromString(QStringLiteral("2026-10-29T11:04:01Z"), Qt::ISODate);
        QCOMPARE(cellText(view, 2, RunStarted), mirante::widgets::compactDate(restarted));
        QCOMPARE(cellText(view, 3, RunNumber), QStringLiteral("#1205"));
        QCOMPARE(cellText(view, 3, RunStatus), QStringLiteral("Cancelled"));

        // A run list asks for nothing more.
        QVERIFY(context.client.requests().isEmpty());
    }

    // Without run_started_at, Started is when the run was created.
    void runStartedFallsBackToCreated()
    {
        QJsonObject row = runRows().at(1).toObject();
        row.remove(QStringLiteral("run_started_at"));
        row.remove(QStringLiteral("actor"));
        FakeContext context;
        context.itemConfig = runConfig();
        context.source.status = statusOfRows({row});
        View view(createRunListView(context, nullptr));

        const QDateTime created = QDateTime::fromString(QStringLiteral("2026-10-29T11:02:33Z"), Qt::ISODate);
        QCOMPARE(cellText(view, 0, RunStarted), mirante::widgets::compactDate(created));
        QCOMPARE(cellText(view, 0, RunActor), kDash);
    }

    void runsSortByIdAsNumbers()
    {
        const auto row = [](qint64 id, int number) {
            return QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("run_number"), number}};
        };
        FakeContext context;
        context.itemConfig = runConfig();
        context.source.status = statusOfRows({row(99, 9), row(1000, 3), row(250, 30)});
        View view(createRunListView(context, nullptr));

        QCOMPARE(cellText(view, 0, RunNumber), QStringLiteral("#3"));
        QCOMPARE(cellText(view, 1, RunNumber), QStringLiteral("#30"));
        QCOMPARE(cellText(view, 2, RunNumber), QStringLiteral("#9"));
    }

    void runColumnsHideInOrder()
    {
        FakeContext context;
        context.itemConfig = runConfig();
        context.source.status = runs();
        View view(createRunListView(context, nullptr));
        view->show();
        const QList<int> order{RunActor, RunStarted, RunWorkflow, RunNumber, RunStatus};
        int hiddenBefore = 0;
        for (int width = 1400; width >= 100; width -= 20) {
            view->resize(width, 400);
            QCoreApplication::processEvents();
            int hidden = 0;
            while (hidden < order.size() && table(view)->isColumnHidden(order.at(hidden)))
                ++hidden;
            for (int later = hidden; later < order.size(); ++later)
                QVERIFY2(!table(view)->isColumnHidden(order.at(later)), qPrintable(QString::number(width)));
            QVERIFY(hidden >= hiddenBefore);
            hiddenBefore = hidden;
        }
        QVERIFY(hiddenBefore > 1);
        QVERIFY(!table(view)->isColumnHidden(RunBranch));
    }

    void runFilterOnStatus()
    {
        FakeContext context;
        context.itemConfig = runConfig();
        context.source.status = runs();
        View view(createRunListView(context, nullptr));
        show(view);

        auto *filter = view->findChild<QComboBox *>(QStringLiteral("filter.%1").arg(int(RunStatus)));
        QVERIFY(filter);
        QCOMPARE(filter->count(), 5);
        QCOMPARE(filter->itemText(0), QStringLiteral("All"));

        filter->setCurrentIndex(filter->findText(QStringLiteral("Failed")));
        QCOMPARE(rowCount(view), 1);
        QCOMPARE(cellText(view, 0, RunNumber), QStringLiteral("#88"));

        // It survives new rows.
        context.source.change(runs());
        QCOMPARE(rowCount(view), 1);
        QCOMPARE(filter->currentText(), QStringLiteral("Failed"));
    }

    void runFilterOnWorkflow()
    {
        FakeContext context;
        context.itemConfig = runConfig();
        context.source.status = runs();
        View view(createRunListView(context, nullptr));
        show(view);

        auto *filter = view->findChild<QComboBox *>(QStringLiteral("filter.%1").arg(int(RunWorkflow)));
        QVERIFY(filter);
        QCOMPARE(filter->count(), 3);

        filter->setCurrentIndex(filter->findText(QStringLiteral("CI")));
        QCOMPARE(rowCount(view), 3);
        QCOMPARE(cellText(view, 2, RunNumber), QStringLiteral("#1205"));
    }

    void runAdd_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("workflow");
        QTest::addColumn<QString>("name");
        QTest::addColumn<QJsonObject>("config");

        const QString repository = QStringLiteral("acme/backend");
        QTest::newRow("pull request") << QStringLiteral("18800000004") << QString()
                                      << QStringLiteral("acme/backend#42 CI")
                                      << QJsonObject{{QStringLiteral("repository"), repository},
                                                     {QStringLiteral("branchSource"), QStringLiteral("pullRequest")},
                                                     {QStringLiteral("number"), 42}};
        QTest::newRow("push") << QStringLiteral("18800000003") << QString() << QStringLiteral("acme/backend main CI")
                              << QJsonObject{{QStringLiteral("repository"), repository},
                                             {QStringLiteral("branchSource"), QStringLiteral("fixed")},
                                             {QStringLiteral("branch"), QStringLiteral("main")}};
        QTest::newRow("by hand") << QStringLiteral("18800000001") << QString()
                                 << QStringLiteral("acme/backend fix-timeouts CI")
                                 << QJsonObject{{QStringLiteral("repository"), repository},
                                                {QStringLiteral("branchSource"), QStringLiteral("fixed")},
                                                {QStringLiteral("branch"), QStringLiteral("fix-timeouts")}};
        QTest::newRow("workflow kept") << QStringLiteral("18800000004") << QStringLiteral("ci.yml")
                                       << QStringLiteral("acme/backend#42 CI")
                                       << QJsonObject{{QStringLiteral("repository"), repository},
                                                      {QStringLiteral("branchSource"), QStringLiteral("pullRequest")},
                                                      {QStringLiteral("number"), 42},
                                                      {QStringLiteral("workflow"), QStringLiteral("ci.yml")}};
    }

    void runAdd()
    {
        QFETCH(QString, key);
        QFETCH(QString, workflow);
        QFETCH(QString, name);
        QFETCH(QJsonObject, config);

        FakeContext context;
        context.itemConfig = runConfig();
        if (!workflow.isEmpty())
            context.itemConfig.insert(QStringLiteral("workflow"), workflow);
        context.source.status = runs();
        View view(createRunListView(context, nullptr));
        show(view);

        emit list(view)->rowLinkActivated(key, QStringLiteral("add"));

        QCOMPARE(context.added.size(), 1);
        QCOMPARE(context.added.first().typeId, QStringLiteral("github.run"));
        QCOMPARE(context.added.first().name, name);
        QCOMPARE(context.added.first().config, config);

        chooseFromMenu(view, key, QStringLiteral("Add to task as item"));
        QCOMPARE(context.added.size(), 2);
        QCOMPARE(context.added.last().config, config);
    }

    // A pull_request run without an entry, such as one from a fork, follows its
    // branch.
    void runOfAPullRequestWithoutAnEntryFollowsTheBranch()
    {
        QJsonObject row = runRows().at(0).toObject();
        row.insert(QStringLiteral("pull_requests"), QJsonArray());
        FakeContext context;
        context.itemConfig = runConfig();
        context.source.status = statusOfRows({row});
        View view(createRunListView(context, nullptr));
        show(view);

        chooseFromMenu(view, QStringLiteral("18800000004"), QStringLiteral("Add to task as item"));
        QCOMPARE(context.added.first().name, QStringLiteral("acme/backend feature/upload-retries CI"));
        QCOMPARE(context.added.first().config.value(QStringLiteral("branchSource")).toString(), QStringLiteral("fixed"));
    }

    // The repository may come from the project's setting.
    void runRepositoryFromTheProject()
    {
        FakeContext context;
        context.itemConfig = QJsonObject{{QStringLiteral("source"), QStringLiteral("all")}};
        context.repositorySetting = QStringLiteral("acme/backend");
        context.source.status = runs();
        View view(createRunListView(context, nullptr));
        show(view);

        QCOMPARE(list(view)->webLink(), QUrl(kBase + QStringLiteral("/acme/backend/actions")));
        chooseFromMenu(view, QStringLiteral("18800000003"), QStringLiteral("Add to task as item"));
        QCOMPARE(context.added.first().config.value(QStringLiteral("repository")).toString(),
                 QStringLiteral("acme/backend"));
    }

    void runWebAndOpenUseTheRowsAddress()
    {
        FakeContext context;
        context.itemConfig = runConfig();
        context.source.status = runs();
        View view(createRunListView(context, nullptr));
        show(view);

        const QUrl address(QStringLiteral("https://github.com/acme/backend/actions/runs/18800000002"));
        emit list(view)->rowLinkActivated(QStringLiteral("18800000002"), QStringLiteral("web"));
        QCOMPARE(m_opened, QList<QUrl>{address});

        emit list(view)->rowLinkActivated(QStringLiteral("18800000002"), QStringLiteral("open"));
        QCOMPARE(context.pages, QList<QUrl>{address});
    }

    void runAddressIsBuiltWithoutHtmlUrl()
    {
        FakeContext context;
        context.itemConfig = runConfig();
        context.source.status = statusOfRows({QJsonObject{{QStringLiteral("id"), 5}}});
        View view(createRunListView(context, nullptr));
        show(view);

        chooseFromMenu(view, QStringLiteral("5"), QStringLiteral("Web"));
        QCOMPARE(m_opened, QList<QUrl>{QUrl(kBase + QStringLiteral("/acme/backend/actions/runs/5"))});
        // A row without a branch has nothing to add.
        QVERIFY(!menuEntryEnabled(view, QStringLiteral("5"), QStringLiteral("Add to task as item")));
        QCOMPARE(cellText(view, 0, RunNumber), kDash);
    }

    void runWebLink_data()
    {
        QTest::addColumn<QJsonObject>("itemConfig");
        QTest::addColumn<QUrl>("expected");

        QJsonObject workflow = runConfig();
        workflow.insert(QStringLiteral("workflow"), QStringLiteral("ci.yml"));
        QTest::newRow("all workflows") << runConfig() << QUrl(kBase + QStringLiteral("/acme/backend/actions"));
        QTest::newRow("one workflow") << workflow
                                      << QUrl(kBase + QStringLiteral("/acme/backend/actions/workflows/ci.yml"));
        QTest::newRow("no repository") << QJsonObject{{QStringLiteral("source"), QStringLiteral("all")}} << QUrl();
    }

    void runWebLink()
    {
        QFETCH(QJsonObject, itemConfig);
        QFETCH(QUrl, expected);

        FakeContext context;
        context.itemConfig = itemConfig;
        View view(createRunListView(context, nullptr));
        QCOMPARE(list(view)->webLink(), expected);
    }

    void runDescription()
    {
        FakeContext context;
        context.itemConfig = QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/backend")},
                                         {QStringLiteral("source"), QStringLiteral("pullRequest")},
                                         {QStringLiteral("number"), 42}};
        context.source.status = runs();
        View view(createRunListView(context, nullptr));
        show(view);

        const auto *description = view->findChild<QLabel *>(QStringLiteral("description"));
        QVERIFY(description && !description->isHidden());
        QCOMPARE(description->text(), runListDescription(context.itemConfig, QJsonValue()));
        QVERIFY(description->text().contains(QStringLiteral("#42")));
    }

    // Without a repository there is no link, and the status's message shows.
    void runIncompleteConfiguration()
    {
        FakeContext context;
        context.itemConfig = QJsonObject{{QStringLiteral("source"), QStringLiteral("all")}};
        context.source.status.message = QStringLiteral("Set the repository in the item's settings.");
        View view(createRunListView(context, nullptr));
        show(view);

        QVERIFY(list(view)->webLink().isEmpty());
        QVERIFY(shownText(view).contains(context.source.status.message));
        QVERIFY(!table(view)->isVisible());
        QVERIFY(context.client.requests().isEmpty());
    }

    // The row menu.

    void rowMenuHasItsEntries()
    {
        FakeContext context;
        context.source.status = pullRequests();
        View view(createPullRequestListView(context, nullptr));
        show(view);

        QCOMPARE(menuEntries(view, QStringLiteral("acme/backend#42")),
                 (QStringList{QStringLiteral("Web"), QStringLiteral("Open page"), QStringLiteral("Add to task as item"),
                              QStringLiteral("Copy link")}));
        QVERIFY(menuEntryEnabled(view, QStringLiteral("acme/backend#42"), QStringLiteral("Copy link")));
    }

    void addToTaskPassesTypeAndConfiguration_data()
    {
        QTest::addColumn<bool>("isPullRequest");
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("typeId");
        QTest::addColumn<QString>("repository");
        QTest::addColumn<int>("number");

        QTest::newRow("pull request") << true << QStringLiteral("acme/website#318") << QStringLiteral("github.pr.summary")
                                      << QStringLiteral("acme/website") << 318;
        QTest::newRow("issue") << false << QStringLiteral("acme/backend#51") << QStringLiteral("github.issue.summary")
                               << QStringLiteral("acme/backend") << 51;
    }

    void addToTaskPassesTypeAndConfiguration()
    {
        QFETCH(bool, isPullRequest);
        QFETCH(QString, key);
        QFETCH(QString, typeId);
        QFETCH(QString, repository);
        QFETCH(int, number);

        FakeContext context;
        context.source.status = isPullRequest ? pullRequests() : issues();
        View view(isPullRequest ? createPullRequestListView(context, nullptr) : createIssueListView(context, nullptr));
        show(view);

        chooseFromMenu(view, key, QStringLiteral("Add to task as item"));

        QCOMPARE(context.added.size(), 1);
        QCOMPARE(context.added.first().typeId, typeId);
        QCOMPARE(context.added.first().name, key);
        const QJsonObject expected{{QStringLiteral("repository"), repository}, {QStringLiteral("number"), number}};
        QCOMPARE(context.added.first().config, expected);
    }

    void webOpensTheBrowser()
    {
        FakeContext context;
        context.source.status = pullRequests();
        View view(createPullRequestListView(context, nullptr));
        show(view);

        chooseFromMenu(view, QStringLiteral("acme/backend#42"), QStringLiteral("Web"));

        QCOMPARE(m_opened, QList<QUrl>{QUrl(QStringLiteral("https://github.com/acme/backend/pull/42"))});
        QVERIFY(context.pages.isEmpty());
    }

    void openPageShowsThePage()
    {
        FakeContext context;
        context.source.status = pullRequests();
        View view(createPullRequestListView(context, nullptr));
        show(view);

        chooseFromMenu(view, QStringLiteral("acme/backend#42"), QStringLiteral("Open page"));

        QCOMPARE(context.pages, QList<QUrl>{QUrl(QStringLiteral("https://github.com/acme/backend/pull/42"))});
        QVERIFY(m_opened.isEmpty());
    }

    void shiftOpensTheBrowserInstead()
    {
        FakeContext context;
        context.source.status = issues();
        View view(createIssueListView(context, nullptr));
        show(view);

        chooseFromMenu(view, QStringLiteral("acme/backend#51"), QStringLiteral("Open page"), Qt::ShiftModifier);

        QVERIFY(context.pages.isEmpty());
        QCOMPARE(m_opened, QList<QUrl>{QUrl(QStringLiteral("https://github.com/acme/backend/issues/51"))});
    }

    // Without html_url the address is built from the web address and the row.
    void openPageBuildsAMissingAddress_data()
    {
        QTest::addColumn<bool>("isPullRequest");
        QTest::addColumn<QString>("key");
        QTest::addColumn<QUrl>("expected");
        QTest::newRow("pull request") << true << QStringLiteral("acme/website#318")
                                      << QUrl(kBase + QStringLiteral("/acme/website/pull/318"));
        QTest::newRow("issue") << false << QStringLiteral("acme/website#12")
                               << QUrl(kBase + QStringLiteral("/acme/website/issues/12"));
    }

    void openPageBuildsAMissingAddress()
    {
        QFETCH(bool, isPullRequest);
        QFETCH(QString, key);
        QFETCH(QUrl, expected);

        const QJsonArray rows = isPullRequest ? pullRequestRows()
                                              : readJson(QStringLiteral("issue_search.json"))
                                                    .object()
                                                    .value(QStringLiteral("items"))
                                                    .toArray();
        QJsonObject row = rows.at(1).toObject();
        row.remove(QStringLiteral("html_url"));
        FakeContext context;
        context.source.status = statusOfRows({row});
        View view(isPullRequest ? createPullRequestListView(context, nullptr) : createIssueListView(context, nullptr));
        show(view);

        chooseFromMenu(view, key, QStringLiteral("Open page"));

        QCOMPARE(context.pages, QList<QUrl>{expected});
    }

    void copyLinkCopiesTheAddress()
    {
        if (QGuiApplication::platformName() != QLatin1String("offscreen"))
            QSKIP("Only with the offscreen platform, so the real clipboard stays as it is.");
        FakeContext context;
        context.source.status = pullRequests();
        View view(createPullRequestListView(context, nullptr));
        show(view);

        chooseFromMenu(view, QStringLiteral("acme/website#318"), QStringLiteral("Copy link"));

        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("https://github.com/acme/website/pull/318"));
    }

    // The Web link and the description.

    void webLink_data()
    {
        QTest::addColumn<bool>("isPullRequest");
        QTest::addColumn<QJsonObject>("itemConfig");
        QTest::addColumn<QString>("baseUrl");
        QTest::addColumn<QString>("path");

        const QJsonObject repository{{QStringLiteral("scope"), QStringLiteral("repository")},
                                     {QStringLiteral("repository"), QStringLiteral("acme/backend")}};
        const QJsonObject owner{{QStringLiteral("scope"), QStringLiteral("owner")},
                                {QStringLiteral("owner"), QStringLiteral("acme")}};
        const QJsonObject noOwner{{QStringLiteral("scope"), QStringLiteral("owner")}};

        QTest::newRow("repository pulls") << true << repository << kBase << QStringLiteral("/acme/backend/pulls");
        QTest::newRow("repository issues") << false << repository << kBase << QStringLiteral("/acme/backend/issues");
        QTest::newRow("owner pulls") << true << owner << kBase << QStringLiteral("/pulls");
        QTest::newRow("incomplete scope") << true << noOwner << kBase << QString();
        QTest::newRow("not a web address") << true << repository << QStringLiteral("ftp://x.example") << QString();
    }

    void webLink()
    {
        QFETCH(bool, isPullRequest);
        QFETCH(QJsonObject, itemConfig);
        QFETCH(QString, baseUrl);
        QFETCH(QString, path);

        FakeContext context;
        context.itemConfig = itemConfig;
        context.general.insert(QStringLiteral("baseUrl"), baseUrl);
        View view(isPullRequest ? createPullRequestListView(context, nullptr) : createIssueListView(context, nullptr));

        const QUrl link = list(view)->webLink();
        if (path.isEmpty()) {
            QVERIFY(link.isEmpty());
            return;
        }
        const ListKind kind = isPullRequest ? ListKind::PullRequests : ListKind::Issues;
        QCOMPARE(link, listWebUrl(kind, QUrl(baseUrl), itemConfig, QJsonValue()));
        QCOMPARE(link.path(), path);
        QVERIFY(link.query().startsWith(QStringLiteral("q=")));
    }

    void description_data()
    {
        QTest::addColumn<bool>("isPullRequest");
        QTest::addColumn<QString>("preset");
        QTest::newRow("pull requests") << true << QStringLiteral("reviewRequested");
        QTest::newRow("issues") << false << QStringLiteral("assignedToMe");
    }

    void description()
    {
        QFETCH(bool, isPullRequest);
        QFETCH(QString, preset);

        FakeContext context;
        context.itemConfig.insert(QStringLiteral("preset"), preset);
        View view(isPullRequest ? createPullRequestListView(context, nullptr) : createIssueListView(context, nullptr));
        show(view);

        const auto *description = view->findChild<QLabel *>(QStringLiteral("description"));
        QVERIFY(description && !description->isHidden());
        const ListKind kind = isPullRequest ? ListKind::PullRequests : ListKind::Issues;
        QCOMPARE(description->text(), listDescription(kind, context.itemConfig, QJsonValue()));
        QVERIFY(description->text().contains(QStringLiteral("own/config")));
    }

    // Without data, and with old data.

    void noDataShowsLoading()
    {
        FakeContext context;
        View view(createPullRequestListView(context, nullptr));
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
        context.source.status.message = QStringLiteral("Set the owner in the item's settings.");
        View view(createIssueListView(context, nullptr));
        show(view);
        QVERIFY(shownText(view).contains(context.source.status.message));
        QVERIFY(!table(view)->isVisible());

        // Data removes the message.
        context.source.change(issues());
        QVERIFY(!shownText(view).contains(QStringLiteral("Set the owner")));
        QVERIFY(table(view)->isVisible());
    }

    void oldDataStaysThroughAnError()
    {
        FakeContext context;
        setUpBackend42(context);
        context.source.status = pullRequests();
        View view(createPullRequestListView(context, nullptr));
        show(view);
        settle();
        const int before = int(context.client.requests().size());

        ItemStatus error;
        error.state = ItemStatus::State::Error;
        error.message = QStringLiteral("The server is down");
        context.source.change(error);
        QCoreApplication::processEvents();

        QCOMPARE(rowCount(view), 3);
        QVERIFY(table(view)->isVisible());
        QVERIFY(!shownText(view).contains(error.message));
        QCOMPARE(cellOf(view, QStringLiteral("backend#42"), PrChecks), QStringLiteral("Failed"));
        QCOMPARE(cellOf(view, QStringLiteral("backend#42"), PrReviews), QStringLiteral("1 approved"));
        QCOMPARE(context.client.requests().size(), before);
    }

    void emptyListShowsTheTableAndALine_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<QString>("text");
        QTest::newRow("pull requests") << 0 << QStringLiteral("No pull requests");
        QTest::newRow("issues") << 1 << QStringLiteral("No issues");
        QTest::newRow("runs") << 2 << QStringLiteral("No runs");
    }

    void emptyListShowsTheTableAndALine()
    {
        QFETCH(int, kind);
        QFETCH(QString, text);

        FakeContext context;
        if (kind == 2)
            context.itemConfig = runConfig();
        context.source.status = statusOfRows({});
        View view(kind == 0   ? createPullRequestListView(context, nullptr)
                  : kind == 1 ? createIssueListView(context, nullptr)
                              : createRunListView(context, nullptr));
        show(view);

        QVERIFY(table(view)->isVisible());
        QCOMPARE(rowCount(view), 0);
        QVERIFY(shownText(view).contains(text));
        QVERIFY(context.client.requests().isEmpty());

        // The line goes with the first row.
        context.source.change(kind == 0 ? pullRequests() : kind == 1 ? issues() : runs());
        QVERIFY(!shownText(view).contains(text));
    }

private:
    QList<QUrl> m_opened;
};

QTEST_MAIN(TestListViews)
#include "tst_list_views.moc"
