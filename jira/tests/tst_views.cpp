// The Jira issue view, its preview and the list view, with a fake context and
// a fake status source. Nothing opens for real: handlers stand in for the
// external browser. See README.md#views.
#include "../JiraIssueView.h"
#include "../JiraListView.h"
#include "../JiraStatus.h"

#include <mirante/ItemContext.h>
#include <mirante/Status.h>
#include <mirante/testing/FakeApiClient.h>
#include <mirante/widgets/ActionRow.h>
#include <mirante/widgets/ItemListView.h>
#include <mirante/widgets/StatusPill.h>
#include <mirante/widgets/SummaryView.h>

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollBar>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QWindow>

#include <functional>
#include <memory>

using mirante::ApiReply;
using mirante::ItemStatus;
using mirante::testing::FakeApiClient;
using mirante::widgets::ActionRow;
using mirante::widgets::ItemListView;
using mirante::widgets::StatusPill;
using mirante::widgets::StatusRole;
using mirante::widgets::SummaryView;

namespace jira = mirante::modules::jira;

namespace {

const QString kCloudBase = QStringLiteral("https://acme.atlassian.net");
const QString kDcBase = QStringLiteral("https://jira.example.org/jira");

class FakeSource : public mirante::ItemStatusSource
{
public:
    ItemStatus status;
    int refreshCount = 0;

    ItemStatus current() const override { return status; }
    void refresh() override { ++refreshCount; }
    void subscribe(QObject *receiver, std::function<void(const ItemStatus &)> changed) override
    {
        m_receiver = receiver;
        m_changed = std::move(changed);
    }

    void push(const ItemStatus &next)
    {
        status = next;
        if (m_receiver)
            m_changed(status);
    }

private:
    QPointer<QObject> m_receiver;
    std::function<void(const ItemStatus &)> m_changed;
};

class FakeContext : public mirante::ItemContext
{
public:
    QJsonObject itemConfig;
    QJsonObject projectSettings;
    QJsonObject general;
    FakeApiClient client;
    FakeSource source;
    bool hasSource = true;
    QList<QUrl> shownPages;
    struct Added
    {
        QString typeId;
        QString name;
        QJsonObject config;
    };
    QList<Added> added;

    QString itemName() const override { return QStringLiteral("Item"); }
    QJsonObject config() const override { return itemConfig; }
    QJsonValue projectSetting(const QString &key) const override { return projectSettings.value(key); }
    QJsonObject moduleSettings(const QString &screenId) const override
    {
        return screenId == QLatin1String("general") ? general : QJsonObject();
    }
    QString folderPath(const QString &) const override { return {}; }
    QWidget *createWebView(const QUrl &, QWidget *) override { return nullptr; }
    QWebEngineProfile *profile() override { return nullptr; }
    QWidget *createMessageView(const QString &, QWidget *) override { return nullptr; }
    void updateConfig(const QJsonObject &) override {}
    void showPage(const QUrl &url) override { shownPages << url; }
    QString addItemToTask(const QString &typeId, const QString &name, const QJsonObject &config) override
    {
        added.append({typeId, name, config});
        return QStringLiteral("new-item");
    }
    mirante::ApiClient *server() override { return &client; }
    mirante::ItemStatusSource *status() override { return hasSource ? &source : nullptr; }
};

// Stands in for the external browser.
class UrlCatcher : public QObject
{
    Q_OBJECT

public:
    QList<QUrl> urls;

public slots:
    void openUrl(const QUrl &url) { urls << url; }
};

// Catches every address that would open, until it goes.
class BrowserGuard
{
public:
    BrowserGuard()
    {
        QDesktopServices::setUrlHandler(QStringLiteral("http"), &catcher, "openUrl");
        QDesktopServices::setUrlHandler(QStringLiteral("https"), &catcher, "openUrl");
    }
    ~BrowserGuard()
    {
        QDesktopServices::unsetUrlHandler(QStringLiteral("http"));
        QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
    }
    UrlCatcher catcher;
};

// Holds Shift down through a window's events, which is where the application
// learns of it, until it goes.
class ShiftHeld
{
public:
    explicit ShiftHeld(QWindow *window)
        : m_window(window)
    {
        QTest::keyPress(m_window, Qt::Key_Shift, Qt::ShiftModifier);
    }
    ~ShiftHeld() { QTest::keyRelease(m_window, Qt::Key_Shift, Qt::NoModifier); }

private:
    QWindow *m_window;
};

QJsonObject readJson(const QString &file)
{
    QFile input(file);
    if (!input.open(QIODevice::ReadOnly))
        qFatal("Cannot read %s", qPrintable(file));
    return QJsonDocument::fromJson(input.readAll()).object();
}

QJsonObject cloudIssue() { return readJson(QFINDTESTDATA("data/issue-full-cloud.json")); }
QJsonObject dcIssue() { return readJson(QFINDTESTDATA("data/issue-full-dc.json")); }

ItemStatus statusOf(const QJsonObject &issue)
{
    ItemStatus status = jira::issueStatus(issue);
    status.data.insert(QStringLiteral("issue"), issue);
    return status;
}

ItemStatus listStatusOf(const QJsonObject &answer)
{
    ItemStatus status;
    status.state = ItemStatus::State::Ok;
    const QJsonArray rows = answer.value(QStringLiteral("issues")).toArray();
    status.data = QJsonObject{{QStringLiteral("rows"), rows}, {QStringLiteral("total"), int(rows.size())}};
    return status;
}

void useCloud(FakeContext &context, const QString &key)
{
    context.itemConfig = {{QStringLiteral("key"), key}};
    context.general = {{QStringLiteral("baseUrl"), kCloudBase}, {QStringLiteral("email"), QStringLiteral("a@example.com")}};
}

void useDataCenter(FakeContext &context, const QString &key)
{
    context.itemConfig = {{QStringLiteral("key"), key}};
    context.general = {{QStringLiteral("baseUrl"), kDcBase}};
}

QString issueRequest(const QString &root, const QString &key)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("expand"), QStringLiteral("renderedFields,names"));
    return FakeApiClient::key(QStringLiteral("GET"), root + QStringLiteral("/issue/") + key, query);
}

// StatusPill has no Q_OBJECT, so findChildren() cannot look for it.
QList<StatusPill *> pillsOf(const QWidget *view)
{
    QList<StatusPill *> pills;
    const auto labels = view->findChildren<QLabel *>();
    for (QLabel *label : labels) {
        if (auto *pill = dynamic_cast<StatusPill *>(label))
            pills << pill;
    }
    return pills;
}

ActionRow *actionsOf(QWidget *view)
{
    return static_cast<SummaryView *>(view)->actions();
}

// The texts of the view's labels and rich text that nothing hides.
QString shownText(const QWidget *view)
{
    QStringList texts;
    const auto labels = view->findChildren<QLabel *>();
    for (const QLabel *label : labels) {
        if (!label->isHidden())
            texts << label->text();
    }
    const auto browsers = view->findChildren<QTextBrowser *>();
    for (const QTextBrowser *browser : browsers)
        texts << browser->toPlainText();
    return texts.join(QLatin1Char('\n'));
}

// Runs check on the dialog once its own event loop runs.
void whenDialogOpens(std::function<void(QDialog &)> check)
{
    QTimer::singleShot(0, qApp, [check = std::move(check)] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        check(*dialog);
    });
}

void writeComment(QDialog &dialog, const QString &text)
{
    dialog.findChild<QPlainTextEdit *>()->setPlainText(text);
    dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
}

QMenu *statusMenu(QWidget *view)
{
    auto *button = view->findChild<QPushButton *>(QStringLiteral("changeStatus"));
    return button ? button->menu() : nullptr;
}

QTableView *tableOf(QWidget *view)
{
    return view->findChild<QTableView *>();
}

// The row of the table that shows key, as it is sorted now.
int rowOf(QWidget *view, const QString &key)
{
    const QAbstractItemModel *model = tableOf(view)->model();
    for (int row = 0; row < model->rowCount(); ++row) {
        if (model->index(row, 0).data().toString() == key)
            return row;
    }
    return -1;
}

QMenu *openRowMenu(QWidget *view, const QString &key)
{
    auto *list = static_cast<ItemListView *>(view);
    emit list->rowMenuRequested(key, QPoint(0, 0));
    return view->findChild<QMenu *>(QStringLiteral("rowMenu"));
}

QAction *menuEntry(QMenu *menu, const QString &text)
{
    const auto entries = menu->actions();
    for (QAction *action : entries) {
        if (action->text() == text)
            return action;
    }
    return nullptr;
}

} // namespace

class TestViews : public QObject
{
    Q_OBJECT

private slots:
    // The issue view.

    void issueDrawsFromStatus()
    {
        FakeContext context;
        useCloud(context, QStringLiteral("OPS-7"));
        context.source.status = statusOf(cloudIssue());
        std::unique_ptr<QWidget> view(jira::createIssueView(context, nullptr));

        const QString text = shownText(view.get());
        QVERIFY(text.contains(QStringLiteral("OPS-7 Rotate the build server's certificates")));
        for (const char *expected : {"Medium", "Ana Example", "Bruno Sample", "Sprint 12", "Renew before November."})
            QVERIFY2(text.contains(QLatin1String(expected)), expected);

        const QList<StatusPill *> pills = pillsOf(view.get());
        QCOMPARE(pills.size(), 2);
        QCOMPARE(pills.at(0)->text(), QStringLiteral("In Review"));
        QCOMPARE(pills.at(0)->role(), StatusRole::Waiting);
        QCOMPARE(pills.at(1)->text(), QStringLiteral("Task"));
        QCOMPARE(pills.at(1)->role(), StatusRole::Neutral);

        // Sub-tasks and both directions of a link.
        QVERIFY(text.contains(QStringLiteral("Update the agents")));
        QVERIFY(text.contains(QStringLiteral("blocks ") + QChar(0x00B7) + QStringLiteral(" Move the backups")));
        QVERIFY(text.contains(QStringLiteral("relates to ") + QChar(0x00B7) + QStringLiteral(" Yearly audit")));
        QVERIFY(view->findChild<QToolButton *>(QStringLiteral("issue:OPS-8")));

        // No request: the data comes from the status cache.
        QVERIFY(context.client.requests().isEmpty());
    }

    void latestFiveCommentsNewestFirst()
    {
        FakeContext context;
        useCloud(context, QStringLiteral("OPS-7"));
        context.source.status = statusOf(cloudIssue());
        std::unique_ptr<QWidget> view(jira::createIssueView(context, nullptr));

        const QString text = shownText(view.get());
        QVERIFY(!text.contains(QStringLiteral("First comment")));
        const qsizetype sixth = text.indexOf(QStringLiteral("Sixth comment"));
        const qsizetype second = text.indexOf(QStringLiteral("Second comment"));
        QVERIFY(sixth >= 0);
        QVERIFY(second > sixth);
    }

    void issueOnDataCenter()
    {
        FakeContext context;
        useDataCenter(context, QStringLiteral("INFRA-1203"));
        context.source.status = statusOf(dcIssue());
        std::unique_ptr<QWidget> view(jira::createIssueView(context, nullptr));

        const QString text = shownText(view.get());
        for (const char *expected : {"Unassigned", "Carla Sample", "Platform, week 44", "Take a backup first",
                                     "Done on staging.", "No sub-tasks or links."})
            QVERIFY2(text.contains(QLatin1String(expected)), expected);
        QCOMPARE(pillsOf(view.get()).at(0)->role(), StatusRole::Good);
    }

    void fieldsHideSections_data()
    {
        QTest::addColumn<QString>("field");
        QTest::addColumn<QString>("section");
        QTest::newRow("details") << QStringLiteral("showDetails") << QStringLiteral("details");
        QTest::newRow("description") << QStringLiteral("showDescription") << QStringLiteral("description");
        QTest::newRow("comments") << QStringLiteral("showComments") << QStringLiteral("comments");
        QTest::newRow("links") << QStringLiteral("showLinks") << QStringLiteral("links");
    }

    void fieldsHideSections()
    {
        QFETCH(QString, field);
        QFETCH(QString, section);
        const QStringList sections{QStringLiteral("details"), QStringLiteral("description"),
                                   QStringLiteral("comments"), QStringLiteral("links")};
        FakeContext context;
        useCloud(context, QStringLiteral("OPS-7"));
        context.source.status = statusOf(cloudIssue());

        // On when the key is absent.
        std::unique_ptr<QWidget> shown(jira::createIssueView(context, nullptr));
        for (const QString &name : sections)
            QVERIFY2(static_cast<SummaryView *>(shown.get())->isSectionVisible(name), qPrintable(name));

        context.itemConfig.insert(field, false);
        std::unique_ptr<QWidget> view(jira::createIssueView(context, nullptr));
        for (const QString &name : sections)
            QCOMPARE(static_cast<SummaryView *>(view.get())->isSectionVisible(name), name != section);

        // The title, state and actions stay.
        QVERIFY(shownText(view.get()).contains(QStringLiteral("OPS-7 Rotate")));
        QVERIFY(view->findChild<QPushButton *>(QStringLiteral("comment")));
    }

    void withoutDataShowsWhyOrLoading()
    {
        FakeContext context;
        useCloud(context, QStringLiteral("OPS-7"));
        std::unique_ptr<QWidget> view(jira::createIssueView(context, nullptr));
        QVERIFY(shownText(view.get()).contains(QStringLiteral("Loading\u2026")));

        ItemStatus failed;
        failed.state = ItemStatus::State::NoToken;
        failed.message = QStringLiteral("Token not set");
        context.source.push(failed);
        QVERIFY(shownText(view.get()).contains(QStringLiteral("Token not set")));

        context.source.push(statusOf(cloudIssue()));
        const QString text = shownText(view.get());
        QVERIFY(text.contains(QStringLiteral("OPS-7 Rotate")));
        QVERIFY(!view->findChildren<QLabel *>().isEmpty());
    }

    // The cache keeps the data of the last good status with an error.
    void oldDataStaysWithAnError()
    {
        FakeContext context;
        useCloud(context, QStringLiteral("OPS-7"));
        context.source.status = statusOf(cloudIssue());
        std::unique_ptr<QWidget> view(jira::createIssueView(context, nullptr));

        ItemStatus failed = context.source.status;
        failed.state = ItemStatus::State::Error;
        failed.message = QStringLiteral("No answer");
        context.source.push(failed);

        const auto *body = view->findChild<QPushButton *>(QStringLiteral("comment"));
        QVERIFY(!body->isHidden());
        QVERIFY(shownText(view.get()).contains(QStringLiteral("OPS-7 Rotate")));
    }

    void serverTextIsNeverMarkup()
    {
        QJsonObject issue = cloudIssue();
        QJsonObject fields = issue.value(QStringLiteral("fields")).toObject();
        fields.insert(QStringLiteral("summary"), QStringLiteral("<b>Bold</b> <img src=\"https://evil.example/a.png\">"));
        QJsonObject assignee = fields.value(QStringLiteral("assignee")).toObject();
        assignee.insert(QStringLiteral("displayName"), QStringLiteral("<i>Ana</i>"));
        fields.insert(QStringLiteral("assignee"), assignee);
        issue.insert(QStringLiteral("fields"), fields);

        FakeContext context;
        useCloud(context, QStringLiteral("OPS-7"));
        context.source.status = statusOf(issue);
        std::unique_ptr<QWidget> view(jira::createIssueView(context, nullptr));

        int seen = 0;
        const auto labels = view->findChildren<QLabel *>();
        for (const QLabel *label : labels) {
            if (label->text().contains(QLatin1Char('<'))) {
                ++seen;
                QCOMPARE(label->textFormat(), Qt::PlainText);
            }
        }
        QCOMPARE(seen, 2);
    }

    // Change status.

    void transitionsAreFetchedWhenTheMenuOpens()
    {
        FakeContext context;
        useCloud(context, QStringLiteral("OPS-7"));
        context.source.status = statusOf(cloudIssue());
        const QString request = FakeApiClient::key(QStringLiteral("GET"), QStringLiteral("rest/api/3/issue/OPS-7/transitions"));
        QVERIFY(context.client.setJsonFile(request, QFINDTESTDATA("data/transitions-cloud.json")));
        context.client.setJson(FakeApiClient::key(QStringLiteral("POST"), QStringLiteral("rest/api/3/issue/OPS-7/transitions")),
                               QJsonDocument());

        std::unique_ptr<QWidget> view(jira::createIssueView(context, nullptr));
        QMenu *menu = statusMenu(view.get());
        QVERIFY(menu);
        QVERIFY(context.client.requests().isEmpty());

        emit menu->aboutToShow();
        QCOMPARE(context.client.requestKeys(), QStringList{request});
        QTRY_COMPARE(menu->actions().size(), 2);
        QCOMPARE(menu->actions().at(0)->text(), QStringLiteral("Start Progress"));
        // "&" in a name is not a shortcut.
        QCOMPARE(menu->actions().at(1)->text(), QStringLiteral("Review && Merge"));

        // A menu entry clears the error of the last action itself.
        actionsOf(view.get())->showError(QStringLiteral("Old error"));
        menu->actions().at(1)->trigger();
        const FakeApiClient::Request &posted = context.client.requests().last();
        QCOMPARE(posted.method, QStringLiteral("POST"));
        QCOMPARE(posted.path, QStringLiteral("rest/api/3/issue/OPS-7/transitions"));
        QCOMPARE(posted.body.object().value(QStringLiteral("transition")).toObject().value(QStringLiteral("id")).toString(),
                 QStringLiteral("31"));
        QVERIFY(actionsOf(view.get())->error().isEmpty());
        QVERIFY(!view->findChild<QPushButton *>(QStringLiteral("changeStatus"))->isEnabled());

        QTRY_COMPARE(context.source.refreshCount, 1);
        QVERIFY(view->findChild<QPushButton *>(QStringLiteral("changeStatus"))->isEnabled());

        // Asked again at the next opening, since a move changes what is allowed.
        emit menu->aboutToShow();
        QCOMPARE(context.client.requests().last().path, QStringLiteral("rest/api/3/issue/OPS-7/transitions"));
        QCOMPARE(context.client.requests().last().method, QStringLiteral("GET"));
        QTRY_COMPARE(menu->actions().size(), 2);
    }

    void failedTransitionShowsTheMessage()
    {
        FakeContext context;
        useCloud(context, QStringLiteral("OPS-7"));
        context.source.status = statusOf(cloudIssue());
        QVERIFY(context.client.setJsonFile(
            FakeApiClient::key(QStringLiteral("GET"), QStringLiteral("rest/api/3/issue/OPS-7/transitions")),
            QFINDTESTDATA("data/transitions-cloud.json")));
        ApiReply refused;
        refused.error = ApiReply::Error::Forbidden;
        refused.httpStatus = 403;
        refused.message = QStringLiteral("Forbidden (403)");
        context.client.setReply(FakeApiClient::key(QStringLiteral("POST"), QStringLiteral("rest/api/3/issue/OPS-7/transitions")),
                                refused);

        std::unique_ptr<QWidget> view(jira::createIssueView(context, nullptr));
        QMenu *menu = statusMenu(view.get());
        emit menu->aboutToShow();
        QTRY_COMPARE(menu->actions().size(), 2);
        menu->actions().at(0)->trigger();

        QTRY_VERIFY(actionsOf(view.get())->error().contains(QStringLiteral("Forbidden (403)")));
        QCOMPARE(context.source.refreshCount, 0);
        QVERIFY(view->findChild<QPushButton *>(QStringLiteral("changeStatus"))->isEnabled());
    }

    void failedTransitionsListShowsTheMessage()
    {
        FakeContext context;
        useCloud(context, QStringLiteral("OPS-7"));
        context.source.status = statusOf(cloudIssue());
        context.client.failAll(ApiReply::Error::Network, QStringLiteral("No answer"));

        std::unique_ptr<QWidget> view(jira::createIssueView(context, nullptr));
        QMenu *menu = statusMenu(view.get());
        emit menu->aboutToShow();
        QTRY_COMPARE(actionsOf(view.get())->error(), QStringLiteral("No answer"));
        QCOMPARE(menu->actions().size(), 1);
        QVERIFY(!menu->actions().at(0)->isEnabled());
    }

    // Comment.

    void commentIsPostedAndRefreshes_data()
    {
        QTest::addColumn<bool>("cloud");
        QTest::addColumn<QString>("path");
        QTest::newRow("cloud") << true << QStringLiteral("rest/api/3/issue/OPS-7/comment");
        QTest::newRow("data center") << false << QStringLiteral("rest/api/2/issue/OPS-7/comment");
    }

    void commentIsPostedAndRefreshes()
    {
        QFETCH(bool, cloud);
        QFETCH(QString, path);
        FakeContext context;
        cloud ? useCloud(context, QStringLiteral("OPS-7")) : useDataCenter(context, QStringLiteral("OPS-7"));
        context.source.status = statusOf(cloudIssue());
        context.client.setJson(FakeApiClient::key(QStringLiteral("POST"), path), QJsonDocument());
        std::unique_ptr<QWidget> view(jira::createIssueView(context, nullptr));

        whenDialogOpens([](QDialog &dialog) { writeComment(dialog, QStringLiteral("Looks good")); });
        view->findChild<QPushButton *>(QStringLiteral("comment"))->click();

        QCOMPARE(context.client.requests().size(), 1);
        const FakeApiClient::Request &posted = context.client.requests().last();
        QCOMPARE(posted.path, path);
        if (cloud) {
            QCOMPARE(posted.body.object().value(QStringLiteral("body")).toObject().value(QStringLiteral("type")).toString(),
                     QStringLiteral("doc"));
        } else {
            QCOMPARE(posted.body.object().value(QStringLiteral("body")).toString(), QStringLiteral("Looks good"));
        }
        QTRY_COMPARE(context.source.refreshCount, 1);
        QVERIFY(actionsOf(view.get())->error().isEmpty());
    }

    void cancelledCommentSendsNothing()
    {
        FakeContext context;
        useCloud(context, QStringLiteral("OPS-7"));
        context.source.status = statusOf(cloudIssue());
        std::unique_ptr<QWidget> view(jira::createIssueView(context, nullptr));

        whenDialogOpens([](QDialog &dialog) { dialog.reject(); });
        view->findChild<QPushButton *>(QStringLiteral("comment"))->click();
        QVERIFY(context.client.requests().isEmpty());
    }

    void failedCommentShowsTheMessage()
    {
        FakeContext context;
        useCloud(context, QStringLiteral("OPS-7"));
        context.source.status = statusOf(cloudIssue());
        context.client.failAll(ApiReply::Error::Http, QStringLiteral("Server error (500)"));
        std::unique_ptr<QWidget> view(jira::createIssueView(context, nullptr));

        whenDialogOpens([](QDialog &dialog) { writeComment(dialog, QStringLiteral("Hello")); });
        view->findChild<QPushButton *>(QStringLiteral("comment"))->click();

        QTRY_COMPARE(actionsOf(view.get())->error(), QStringLiteral("Server error (500)"));
        QCOMPARE(context.source.refreshCount, 0);
    }

    // The dialog's event loop can outlive the view.
    void viewGoingDuringACommentSendsNothing()
    {
        FakeContext context;
        useCloud(context, QStringLiteral("OPS-7"));
        context.source.status = statusOf(cloudIssue());
        auto *view = jira::createIssueView(context, nullptr);
        view->show();

        whenDialogOpens([view](QDialog &) { delete view; });
        view->findChild<QPushButton *>(QStringLiteral("comment"))->click();
        QVERIFY(context.client.requests().isEmpty());
    }

    // Open page and the Web link.

    void openPageShowsThePage()
    {
        BrowserGuard browser;
        FakeContext context;
        useDataCenter(context, QStringLiteral("INFRA-1203"));
        context.source.status = statusOf(dcIssue());
        std::unique_ptr<QWidget> view(jira::createIssueView(context, nullptr));

        view->findChild<QPushButton *>(QStringLiteral("openPage"))->click();
        QCOMPARE(context.shownPages, QList<QUrl>{QUrl(kDcBase + QStringLiteral("/browse/INFRA-1203"))});
        QVERIFY(browser.catcher.urls.isEmpty());
    }

    void shiftOpensTheBrowserInstead()
    {
        BrowserGuard browser;
        FakeContext context;
        useCloud(context, QStringLiteral("OPS-7"));
        context.source.status = statusOf(cloudIssue());
        std::unique_ptr<QWidget> view(jira::createIssueView(context, nullptr));
        view->show();
        QVERIFY(QTest::qWaitForWindowExposed(view.get()));

        const ShiftHeld shift(view->windowHandle());
        QVERIFY(QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier));

        view->findChild<QPushButton *>(QStringLiteral("openPage"))->click();
        QVERIFY(context.shownPages.isEmpty());
        QCOMPARE(browser.catcher.urls, QList<QUrl>{QUrl(kCloudBase + QStringLiteral("/browse/OPS-7"))});
    }

    void webLinkOfTheIssue()
    {
        BrowserGuard browser;
        FakeContext context;
        useDataCenter(context, QStringLiteral("INFRA-1203"));
        context.source.status = statusOf(dcIssue());
        std::unique_ptr<QWidget> view(jira::createIssueView(context, nullptr));

        const QUrl expected(kDcBase + QStringLiteral("/browse/INFRA-1203"));
        QCOMPARE(actionsOf(view.get())->webLink(), expected);
        view->findChild<QPushButton *>(QStringLiteral("web"))->click();
        QCOMPARE(browser.catcher.urls, QList<QUrl>{expected});
    }

    void linkedIssueOpensInTheBrowser()
    {
        BrowserGuard browser;
        FakeContext context;
        useCloud(context, QStringLiteral("OPS-7"));
        context.source.status = statusOf(cloudIssue());
        std::unique_ptr<QWidget> view(jira::createIssueView(context, nullptr));

        view->findChild<QToolButton *>(QStringLiteral("issue:SEC-3"))->click();
        QCOMPARE(browser.catcher.urls, QList<QUrl>{QUrl(kCloudBase + QStringLiteral("/browse/SEC-3"))});
    }

    // The rendered HTML.

    void richTextOpensNothingByItself()
    {
        FakeContext context;
        useCloud(context, QStringLiteral("OPS-7"));
        context.source.status = statusOf(cloudIssue());
        std::unique_ptr<QWidget> view(jira::createIssueView(context, nullptr));
        const auto browsers = view->findChildren<QTextBrowser *>();
        QVERIFY(!browsers.isEmpty());
        for (const QTextBrowser *browser : browsers) {
            QVERIFY(!browser->openLinks());
            QVERIFY(!browser->openExternalLinks());
        }
    }

    // A remote image, and a file on this machine or a share, stay unread.
    void imagesAreNeverFetched()
    {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString file = folder.filePath(QStringLiteral("local.png"));
        QImage local(16, 16, QImage::Format_RGB32);
        local.fill(Qt::red);
        QVERIFY(local.save(file));
        const QUrl localUrl = QUrl::fromLocalFile(file);
        const QUrl remote(QStringLiteral("https://tracker.example.net/pixel.png"));

        QJsonObject issue = cloudIssue();
        QJsonObject rendered = issue.value(QStringLiteral("renderedFields")).toObject();
        rendered.insert(QStringLiteral("description"),
                        QStringLiteral("<p>Text</p><img src=\"%1\"><img src=\"%2\">")
                            .arg(remote.toString(), localUrl.toString()));
        issue.insert(QStringLiteral("renderedFields"), rendered);

        FakeContext context;
        useCloud(context, QStringLiteral("OPS-7"));
        context.source.status = statusOf(issue);
        std::unique_ptr<QWidget> view(jira::createIssueView(context, nullptr));
        view->resize(500, 400);
        view->show();
        QVERIFY(QTest::qWaitForWindowExposed(view.get()));

        QTextBrowser *browser = nullptr;
        const auto browsers = view->findChildren<QTextBrowser *>();
        for (QTextBrowser *candidate : browsers) {
            if (candidate->toPlainText().contains(QStringLiteral("Text")))
                browser = candidate;
        }
        QVERIFY(browser);
        for (const QUrl &url : {remote, localUrl}) {
            const QVariant resource = browser->document()->resource(QTextDocument::ImageResource, url);
            QVERIFY(resource.isValid());
            QCOMPARE(resource.typeId(), int(QMetaType::QImage));
            QCOMPARE(resource.value<QImage>().size(), QSize(1, 1));
        }
        QVERIFY(context.client.requests().isEmpty());
    }

    // The view around the text scrolls, and the text itself does not.
    void richTextGrowsToItsText()
    {
        FakeContext context;
        useCloud(context, QStringLiteral("OPS-7"));
        QJsonObject issue = cloudIssue();
        QJsonObject rendered = issue.value(QStringLiteral("renderedFields")).toObject();
        rendered.insert(QStringLiteral("description"), QStringLiteral("<p>Long line of words</p>").repeated(40));
        issue.insert(QStringLiteral("renderedFields"), rendered);
        context.source.status = statusOf(issue);
        std::unique_ptr<QWidget> view(jira::createIssueView(context, nullptr));
        view->resize(400, 300);
        view->show();
        QVERIFY(QTest::qWaitForWindowExposed(view.get()));

        QTextBrowser *browser = view->findChild<QTextBrowser *>();
        QVERIFY(browser);
        QTRY_VERIFY(browser->height() >= int(browser->document()->size().height()));
        QVERIFY(browser->height() > 300);
        QCOMPARE(browser->verticalScrollBar()->maximum(), 0);

        // A narrower cell wraps the text, so it grows.
        const int wide = browser->height();
        rendered.insert(QStringLiteral("description"),
                        QStringLiteral("<p>Long line of words that wraps in a narrow cell</p>").repeated(40));
        issue.insert(QStringLiteral("renderedFields"), rendered);
        context.source.push(statusOf(issue));
        const int before = browser->height();
        QVERIFY(before >= wide);
        view->resize(160, 300);
        QTRY_VERIFY(browser->height() > before);
    }

    void richTextLinks_data()
    {
        QTest::addColumn<QString>("base");
        QTest::addColumn<QString>("href");
        QTest::addColumn<QString>("opened");
        const QString site = QStringLiteral("https://jira.example.org/jira");
        QTest::newRow("absolute") << site << QStringLiteral("https://other.example/a") << QStringLiteral("https://other.example/a");
        QTest::newRow("http") << site << QStringLiteral("http://other.example/a") << QStringLiteral("http://other.example/a");
        QTest::newRow("from the root") << site << QStringLiteral("/jira/browse/OPS-1") << QStringLiteral("https://jira.example.org/jira/browse/OPS-1");
        QTest::newRow("below the base") << site << QStringLiteral("browse/OPS-1") << QStringLiteral("https://jira.example.org/jira/browse/OPS-1");
        QTest::newRow("file") << site << QStringLiteral("file:///C:/Windows/notepad.exe") << QString();
        QTest::newRow("share") << site << QStringLiteral("file://server/share/a.exe") << QString();
        QTest::newRow("script") << site << QStringLiteral("javascript:alert(1)") << QString();
        QTest::newRow("custom") << site << QStringLiteral("ms-settings:privacy") << QString();
        QTest::newRow("data") << site << QStringLiteral("data:text/html,x") << QString();
    }

    void richTextLinks()
    {
        QFETCH(QString, base);
        QFETCH(QString, href);
        QFETCH(QString, opened);

        BrowserGuard browser;
        // Other schemes would reach the system, so they count as a failure here.
        QDesktopServices::setUrlHandler(QStringLiteral("file"), &browser.catcher, "openUrl");
        QDesktopServices::setUrlHandler(QStringLiteral("javascript"), &browser.catcher, "openUrl");
        QDesktopServices::setUrlHandler(QStringLiteral("ms-settings"), &browser.catcher, "openUrl");
        QDesktopServices::setUrlHandler(QStringLiteral("data"), &browser.catcher, "openUrl");
        auto cleanup = qScopeGuard([] {
            for (const char *scheme : {"file", "javascript", "ms-settings", "data"})
                QDesktopServices::unsetUrlHandler(QLatin1String(scheme));
        });

        FakeContext context;
        useDataCenter(context, QStringLiteral("INFRA-1203"));
        context.general.insert(QStringLiteral("baseUrl"), base);
        context.source.status = statusOf(dcIssue());
        std::unique_ptr<QWidget> view(jira::createIssueView(context, nullptr));

        auto *rich = view->findChild<QTextBrowser *>();
        QVERIFY(rich);
        emit rich->anchorClicked(QUrl(href));
        if (opened.isEmpty())
            QVERIFY(browser.catcher.urls.isEmpty());
        else
            QCOMPARE(browser.catcher.urls, QList<QUrl>{QUrl(opened)});
    }

    // A preview.

    void previewReadsTheIssueItself()
    {
        FakeContext context;
        useCloud(context, QString());
        QVERIFY(context.client.setJsonFile(issueRequest(QStringLiteral("rest/api/3"), QStringLiteral("OPS-7")),
                                           QFINDTESTDATA("data/issue-full-cloud.json")));
        std::unique_ptr<QWidget> preview(jira::createIssuePreview(context, QStringLiteral("OPS-7"), nullptr));

        QVERIFY(shownText(preview.get()).contains(QStringLiteral("Loading\u2026")));
        QTRY_VERIFY(shownText(preview.get()).contains(QStringLiteral("OPS-7 Rotate")));
        QVERIFY(shownText(preview.get()).contains(QStringLiteral("Sprint 12")));
        QCOMPARE(context.client.requests().size(), 1);
        QCOMPARE(actionsOf(preview.get())->webLink(), QUrl(kCloudBase + QStringLiteral("/browse/OPS-7")));
        // A list's own status does not stand in for the issue.
        QCOMPARE(context.source.refreshCount, 0);
    }

    void previewShowsAFailedRead()
    {
        FakeContext context;
        useCloud(context, QString());
        std::unique_ptr<QWidget> preview(jira::createIssuePreview(context, QStringLiteral("OPS-999"), nullptr));
        QTRY_VERIFY(shownText(preview.get()).contains(QStringLiteral("Not found (404)")));
    }

    void previewKeepsItsDataWithAFailedRead()
    {
        FakeContext context;
        useCloud(context, QString());
        QVERIFY(context.client.setJsonFile(issueRequest(QStringLiteral("rest/api/3"), QStringLiteral("OPS-7")),
                                           QFINDTESTDATA("data/issue-full-cloud.json")));
        std::unique_ptr<QWidget> preview(jira::createIssuePreview(context, QStringLiteral("OPS-7"), nullptr));
        QTRY_VERIFY(shownText(preview.get()).contains(QStringLiteral("OPS-7 Rotate")));

        context.client.failAll(ApiReply::Error::Network, QStringLiteral("No answer"));
        jira::reloadIssuePreview(preview.get());
        QTRY_COMPARE(actionsOf(preview.get())->error(), QStringLiteral("No answer"));
        QVERIFY(shownText(preview.get()).contains(QStringLiteral("OPS-7 Rotate")));
        QVERIFY(!preview->findChild<QPushButton *>(QStringLiteral("comment"))->isHidden());

        // The next good read takes the message away.
        context.client.failAll(ApiReply::Error::None);
        jira::reloadIssuePreview(preview.get());
        QTRY_VERIFY(actionsOf(preview.get())->error().isEmpty());
    }

    void previewReadsAgainAfterAnAction()
    {
        FakeContext context;
        useCloud(context, QString());
        const QString read = issueRequest(QStringLiteral("rest/api/3"), QStringLiteral("OPS-7"));
        QVERIFY(context.client.setJsonFile(read, QFINDTESTDATA("data/issue-full-cloud.json")));
        context.client.setJson(FakeApiClient::key(QStringLiteral("POST"), QStringLiteral("rest/api/3/issue/OPS-7/comment")),
                               QJsonDocument());
        std::unique_ptr<QWidget> preview(jira::createIssuePreview(context, QStringLiteral("OPS-7"), nullptr));
        QTRY_VERIFY(shownText(preview.get()).contains(QStringLiteral("OPS-7 Rotate")));

        whenDialogOpens([](QDialog &dialog) { writeComment(dialog, QStringLiteral("Hello")); });
        preview->findChild<QPushButton *>(QStringLiteral("comment"))->click();

        QTRY_COMPARE(context.client.requests().size(), 3);
        QCOMPARE(context.client.requestKeys().last(), read);
        // The list's rows follow as well.
        QCOMPARE(context.source.refreshCount, 1);
    }

    // The list.

    void listDrawsFromStatus()
    {
        FakeContext context;
        useCloud(context, QString());
        context.itemConfig = {{QStringLiteral("preset"), QStringLiteral("myOpen")}};
        context.source.status = listStatusOf(readJson(QFINDTESTDATA("data/search-list-cloud.json")));
        std::unique_ptr<QWidget> view(jira::createListView(context, nullptr));

        const QAbstractItemModel *model = tableOf(view.get())->model();
        QCOMPARE(model->columnCount(), 5);
        QCOMPARE(model->headerData(0, Qt::Horizontal).toString(), QStringLiteral("Key"));
        QCOMPARE(model->headerData(1, Qt::Horizontal).toString(), QStringLiteral("Summary"));
        QCOMPARE(model->headerData(2, Qt::Horizontal).toString(), QStringLiteral("Status"));
        QCOMPARE(model->headerData(3, Qt::Horizontal).toString(), QStringLiteral("Assignee"));
        QCOMPARE(model->headerData(4, Qt::Horizontal).toString(), QStringLiteral("Updated"));
        QCOMPARE(model->rowCount(), 3);

        // Sorted on Updated, newest first.
        QCOMPARE(model->index(0, 0).data().toString(), QStringLiteral("OPS-10"));
        QCOMPARE(model->index(1, 0).data().toString(), QStringLiteral("OPS-7"));
        QCOMPARE(model->index(2, 0).data().toString(), QStringLiteral("OPS-9"));

        QCOMPARE(model->index(0, 1).data().toString(), QStringLiteral("Move the backups to the new bucket"));
        QCOMPARE(model->index(0, 2).data().toString(), QStringLiteral("To Do"));
        QCOMPARE(model->index(0, 3).data().toString(), QStringLiteral("Unassigned"));
        QCOMPARE(model->index(2, 3).data().toString(), QStringLiteral("Bruno Sample"));

        // By number, and not as text.
        tableOf(view.get())->sortByColumn(0, Qt::AscendingOrder);
        QCOMPARE(model->index(0, 0).data().toString(), QStringLiteral("OPS-7"));
        QCOMPARE(model->index(2, 0).data().toString(), QStringLiteral("OPS-10"));

        QVERIFY(context.client.requests().isEmpty());
    }

    void statusCategoryGivesTheRole()
    {
        QCOMPARE(jira::statusCategoryRole(QStringLiteral("done")), StatusRole::Good);
        QCOMPARE(jira::statusCategoryRole(QStringLiteral("indeterminate")), StatusRole::Waiting);
        QCOMPARE(jira::statusCategoryRole(QStringLiteral("new")), StatusRole::Neutral);
        QCOMPARE(jira::statusCategoryRole(QString()), StatusRole::Neutral);
    }

    void listWithoutRowsShowsWhyOrLoading()
    {
        FakeContext context;
        useCloud(context, QString());
        context.itemConfig = {{QStringLiteral("preset"), QStringLiteral("currentSprint")}};
        std::unique_ptr<QWidget> view(jira::createListView(context, nullptr));
        QVERIFY(shownText(view.get()).contains(QStringLiteral("Loading\u2026")));

        ItemStatus problem;
        problem.message = QStringLiteral("Set the Jira project key in the project's settings.");
        context.source.push(problem);
        QVERIFY(shownText(view.get()).contains(problem.message));
        QVERIFY(static_cast<ItemListView *>(view.get())->webLink().isEmpty());
    }

    void listKeepsItsRowsWithAnError()
    {
        FakeContext context;
        useCloud(context, QString());
        context.source.status = listStatusOf(readJson(QFINDTESTDATA("data/search-list-cloud.json")));
        std::unique_ptr<QWidget> view(jira::createListView(context, nullptr));

        ItemStatus failed = context.source.status;
        failed.state = ItemStatus::State::Error;
        failed.message = QStringLiteral("No answer");
        context.source.push(failed);
        QCOMPARE(tableOf(view.get())->model()->rowCount(), 3);
        QVERIFY(!shownText(view.get()).contains(QStringLiteral("No answer")));
    }

    void listWebLink()
    {
        BrowserGuard browser;
        FakeContext context;
        useDataCenter(context, QString());
        context.itemConfig = {{QStringLiteral("preset"), QStringLiteral("recentlyUpdated")}};
        context.projectSettings = {{QStringLiteral("projectKey"), QStringLiteral("OPS")}};
        context.source.status = listStatusOf(readJson(QFINDTESTDATA("data/search-list-dc.json")));
        std::unique_ptr<QWidget> view(jira::createListView(context, nullptr));

        const QUrl link = static_cast<ItemListView *>(view.get())->webLink();
        QCOMPARE(link.path(), QStringLiteral("/jira/issues/"));
        QCOMPARE(QUrlQuery(link).queryItemValue(QStringLiteral("jql"), QUrl::FullyDecoded),
                 QStringLiteral("project = \"OPS\" AND updated >= -7d ORDER BY updated DESC"));

        view->findChild<QPushButton *>(QStringLiteral("web"))->click();
        QCOMPARE(browser.catcher.urls, QList<QUrl>{link});
    }

    void clickingARowShowsItsPreview()
    {
        FakeContext context;
        useCloud(context, QString());
        context.source.status = listStatusOf(readJson(QFINDTESTDATA("data/search-list-cloud.json")));
        QVERIFY(context.client.setJsonFile(issueRequest(QStringLiteral("rest/api/3"), QStringLiteral("OPS-7")),
                                           QFINDTESTDATA("data/issue-full-cloud.json")));
        std::unique_ptr<QWidget> view(jira::createListView(context, nullptr));
        auto *list = static_cast<ItemListView *>(view.get());
        QVERIFY(!list->preview());

        tableOf(view.get())->selectRow(rowOf(view.get(), QStringLiteral("OPS-7")));
        QVERIFY(list->preview());
        QTRY_VERIFY(shownText(list->preview()).contains(QStringLiteral("OPS-7 Rotate")));
        QPointer<QWidget> first = list->preview();

        // New rows read the preview again, and keep it.
        const qsizetype reads = context.client.requests().size();
        context.source.push(context.source.status);
        QCOMPARE(list->preview(), first.data());
        QCOMPARE(context.client.requests().size(), reads + 1);

        // Close clears the selection, so the same row brings the preview back.
        view->findChild<QToolButton *>(QStringLiteral("closePreview"))->click();
        QVERIFY(!list->preview());
        tableOf(view.get())->selectRow(rowOf(view.get(), QStringLiteral("OPS-7")));
        QVERIFY(list->preview());
    }

    void previewStaysWhenItsRowGoes()
    {
        FakeContext context;
        useCloud(context, QString());
        const QJsonObject answer = readJson(QFINDTESTDATA("data/search-list-cloud.json"));
        context.source.status = listStatusOf(answer);
        std::unique_ptr<QWidget> view(jira::createListView(context, nullptr));
        auto *list = static_cast<ItemListView *>(view.get());

        tableOf(view.get())->selectRow(rowOf(view.get(), QStringLiteral("OPS-7")));
        QPointer<QWidget> preview = list->preview();
        QVERIFY(preview);

        context.source.push(listStatusOf(QJsonObject{{QStringLiteral("issues"), QJsonArray()}}));
        QVERIFY(list->selectedKey().isEmpty());
        QCOMPARE(list->preview(), preview.data());
    }

    void addToTaskPassesTypeAndConfiguration()
    {
        FakeContext context;
        useCloud(context, QString());
        context.source.status = listStatusOf(readJson(QFINDTESTDATA("data/search-list-cloud.json")));
        std::unique_ptr<QWidget> view(jira::createListView(context, nullptr));

        QMenu *menu = openRowMenu(view.get(), QStringLiteral("OPS-9"));
        QVERIFY(menu);
        QAction *add = menuEntry(menu, QStringLiteral("Add to task as item"));
        QVERIFY(add);
        add->trigger();

        QCOMPARE(context.added.size(), 1);
        QCOMPARE(context.added.at(0).typeId, QStringLiteral("jira.issue.summary"));
        QCOMPARE(context.added.at(0).name, QStringLiteral("OPS-9"));
        QCOMPARE(context.added.at(0).config, (QJsonObject{{QStringLiteral("key"), QStringLiteral("OPS-9")}}));
        menu->close();
    }

    void rowMenuOpensAndCopies()
    {
        BrowserGuard browser;
        FakeContext context;
        useDataCenter(context, QString());
        context.source.status = listStatusOf(readJson(QFINDTESTDATA("data/search-list-dc.json")));
        std::unique_ptr<QWidget> view(jira::createListView(context, nullptr));
        view->show();
        QVERIFY(QTest::qWaitForWindowExposed(view.get()));
        const QUrl page(kDcBase + QStringLiteral("/browse/INFRA-1204"));

        QMenu *menu = openRowMenu(view.get(), QStringLiteral("INFRA-1204"));
        QVERIFY(menu);
        menuEntry(menu, QStringLiteral("Open page"))->trigger();
        QCOMPARE(context.shownPages, QList<QUrl>{page});

        // Shift opens the browser, and does not show the page.
        {
            const ShiftHeld shift(view->windowHandle());
            menuEntry(menu, QStringLiteral("Open page"))->trigger();
        }
        QCOMPARE(context.shownPages.size(), 1);
        QCOMPARE(browser.catcher.urls, QList<QUrl>{page});

        menuEntry(menu, QStringLiteral("Copy link"))->trigger();
        QCOMPARE(QGuiApplication::clipboard()->text(), page.toString());
        menu->close();
    }

    void listWithoutSourceSaysLoading()
    {
        FakeContext context;
        useCloud(context, QString());
        context.hasSource = false;
        std::unique_ptr<QWidget> view(jira::createListView(context, nullptr));
        QVERIFY(shownText(view.get()).contains(QStringLiteral("Loading\u2026")));
    }
};

QTEST_MAIN(TestViews)
#include "tst_views.moc"
