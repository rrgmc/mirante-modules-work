// The Jira issue view and the list view, with a fake context and a fake status
// source. Nothing opens for real: handlers stand in for the
// external browser. See README.md#views.
#include "../JiraIssueView.h"
#include "../JiraListView.h"
#include "../JiraStatus.h"

#include <mirante/ItemContext.h>
#include <mirante/Status.h>
#include <mirante/testing/FakeApiClient.h>
#include <mirante/widgets/ActionRow.h>
#include <mirante/widgets/CompactDate.h>
#include <mirante/widgets/ItemListView.h>
#include <mirante/widgets/StatusPill.h>
#include <mirante/widgets/SummaryView.h>

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDate>
#include <QDesktopServices>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollBar>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QTextDocument>
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

// The column of the row links without extra fields, and the role that
// ItemListView.cpp keeps its link ids in, since no public accessor gives them.
constexpr int kLinksColumn = 5;
constexpr int kLinkIdsRole = Qt::UserRole + 4;

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
        QVERIFY(view->findChild<QPushButton *>(QStringLiteral("openPage")));
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

        QVERIFY(!view->findChild<QPushButton *>(QStringLiteral("openPage"))->isHidden());
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

    // The view only reads. Changing an issue happens on its page.
    void changingAnIssueIsGone()
    {
        FakeContext context;
        useCloud(context, QStringLiteral("OPS-7"));
        context.source.status = statusOf(cloudIssue());
        std::unique_ptr<QWidget> view(jira::createIssueView(context, nullptr));

        QVERIFY(!view->findChild<QPushButton *>(QStringLiteral("changeStatus")));
        QVERIFY(!view->findChild<QPushButton *>(QStringLiteral("comment")));
        QVERIFY(view->findChild<QPushButton *>(QStringLiteral("openPage")));
        QVERIFY(view->findChild<QPushButton *>(QStringLiteral("web")));
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

    // The issues whose parent is this one.

    void childrenShowWithPillsAndLinks()
    {
        BrowserGuard browser;
        FakeContext context;
        useCloud(context, QStringLiteral("OPS-7"));
        ItemStatus status = statusOf(cloudIssue());
        const QJsonArray children = readJson(QFINDTESTDATA("data/search-children-cloud.json"))
                                        .value(QStringLiteral("issues"))
                                        .toArray();
        // As the status gives them: without the sub-task OPS-9.
        status.data.insert(QStringLiteral("children"), QJsonArray{children.at(1), children.at(2)});
        status.data.insert(QStringLiteral("childrenTotal"), 2);
        context.source.status = status;
        std::unique_ptr<QWidget> view(jira::createIssueView(context, nullptr));

        const QString text = shownText(view.get());
        QVERIFY(text.contains(QStringLiteral("Renew the staging certificate")));
        QVERIFY(text.contains(QStringLiteral("Renew the production certificate")));
        QVERIFY(!view->findChild<QLabel *>(QStringLiteral("childrenMore")));

        // After the two of the state row.
        const QList<StatusPill *> pills = pillsOf(view.get());
        QCOMPARE(pills.size(), 4);
        QCOMPARE(pills.at(2)->text(), QStringLiteral("In Progress"));
        QCOMPARE(pills.at(2)->role(), StatusRole::Waiting);
        QCOMPARE(pills.at(3)->text(), QStringLiteral("Done"));
        QCOMPARE(pills.at(3)->role(), StatusRole::Good);

        view->findChild<QToolButton *>(QStringLiteral("issue:OPS-12"))->click();
        QCOMPARE(browser.catcher.urls, QList<QUrl>{QUrl(kCloudBase + QStringLiteral("/browse/OPS-12"))});
    }

    void aLineCountsTheOtherChildren()
    {
        QJsonArray children;
        for (int i = 0; i < 50; ++i)
            children.append(QJsonObject{{QStringLiteral("key"), QStringLiteral("INFRA-%1").arg(2000 + i)}});

        FakeContext context;
        useDataCenter(context, QStringLiteral("INFRA-1203"));
        ItemStatus status = statusOf(dcIssue());
        status.data.insert(QStringLiteral("children"), children);
        status.data.insert(QStringLiteral("childrenTotal"), 73);
        context.source.status = status;
        std::unique_ptr<QWidget> view(jira::createIssueView(context, nullptr));

        QVERIFY(view->findChild<QToolButton *>(QStringLiteral("issue:INFRA-2049")));
        QVERIFY(!shownText(view.get()).contains(QStringLiteral("No sub-tasks or links.")));
        auto *more = view->findChild<QLabel *>(QStringLiteral("childrenMore"));
        QVERIFY(more);
        QCOMPARE(more->text(), QStringLiteral("23 more"));
    }

    // A failed search leaves children out of the data.
    void withoutChildrenTheSectionStaysAsItWas()
    {
        FakeContext context;
        useCloud(context, QStringLiteral("OPS-7"));
        context.source.status = statusOf(cloudIssue());
        std::unique_ptr<QWidget> view(jira::createIssueView(context, nullptr));
        QVERIFY(view->findChild<QToolButton *>(QStringLiteral("issue:OPS-9")));
        QVERIFY(!view->findChild<QLabel *>(QStringLiteral("childrenMore")));
        QCOMPARE(pillsOf(view.get()).size(), 2);
    }

    // A new answer with the same issue but other children still draws.
    void newChildrenDrawAgain()
    {
        FakeContext context;
        useCloud(context, QStringLiteral("OPS-7"));
        context.source.status = statusOf(cloudIssue());
        std::unique_ptr<QWidget> view(jira::createIssueView(context, nullptr));
        QVERIFY(!view->findChild<QToolButton *>(QStringLiteral("issue:OPS-40")));

        ItemStatus next = statusOf(cloudIssue());
        next.data.insert(QStringLiteral("children"),
                         QJsonArray{QJsonObject{{QStringLiteral("key"), QStringLiteral("OPS-40")}}});
        next.data.insert(QStringLiteral("childrenTotal"), 1);
        context.source.push(next);
        QVERIFY(view->findChild<QToolButton *>(QStringLiteral("issue:OPS-40")));
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

    // The list.

    void listDrawsFromStatus()
    {
        FakeContext context;
        useCloud(context, QString());
        context.itemConfig = {{QStringLiteral("preset"), QStringLiteral("myOpen")}};
        context.source.status = listStatusOf(readJson(QFINDTESTDATA("data/search-list-cloud.json")));
        std::unique_ptr<QWidget> view(jira::createListView(context, nullptr));

        const QAbstractItemModel *model = tableOf(view.get())->model();
        QCOMPARE(model->columnCount(), 6);
        QCOMPARE(model->headerData(0, Qt::Horizontal).toString(), QStringLiteral("Key"));
        QCOMPARE(model->headerData(1, Qt::Horizontal).toString(), QStringLiteral("Summary"));
        QCOMPARE(model->headerData(2, Qt::Horizontal).toString(), QStringLiteral("Status"));
        QCOMPARE(model->headerData(3, Qt::Horizontal).toString(), QStringLiteral("Assignee"));
        QCOMPARE(model->headerData(4, Qt::Horizontal).toString(), QStringLiteral("Updated"));
        QCOMPARE(model->headerData(5, Qt::Horizontal).toString(), QStringLiteral("Links"));
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

    // There is no preview any more, so selecting a row asks for nothing.
    void selectingARowShowsNoPreview()
    {
        FakeContext context;
        useCloud(context, QString());
        context.source.status = listStatusOf(readJson(QFINDTESTDATA("data/search-list-cloud.json")));
        std::unique_ptr<QWidget> view(jira::createListView(context, nullptr));
        auto *list = static_cast<ItemListView *>(view.get());

        tableOf(view.get())->selectRow(rowOf(view.get(), QStringLiteral("OPS-7")));
        QCOMPARE(list->selectedKey(), QStringLiteral("OPS-7"));
        QVERIFY(!list->preview());
        QVERIFY(context.client.requests().isEmpty());
    }

    void rowLinks_data()
    {
        QTest::addColumn<bool>("hasSite");
        QTest::addColumn<QStringList>("ids");
        QTest::newRow("with a site") << true
                                     << QStringList{QStringLiteral("web"), QStringLiteral("open"), QStringLiteral("add")};
        QTest::newRow("without a site") << false << QStringList{QStringLiteral("add")};
    }

    void rowLinks()
    {
        QFETCH(bool, hasSite);
        QFETCH(QStringList, ids);
        FakeContext context;
        useCloud(context, QString());
        if (!hasSite)
            context.general.remove(QStringLiteral("baseUrl"));
        context.source.status = listStatusOf(readJson(QFINDTESTDATA("data/search-list-cloud.json")));
        std::unique_ptr<QWidget> view(jira::createListView(context, nullptr));

        const QModelIndex cell = tableOf(view.get())->model()->index(0, kLinksColumn);
        QCOMPARE(cell.data(kLinkIdsRole).toStringList(), ids);
        QVERIFY(cell.data().toString().isEmpty());
    }

    void rowLinkActions()
    {
        BrowserGuard browser;
        FakeContext context;
        useCloud(context, QString());
        context.source.status = listStatusOf(readJson(QFINDTESTDATA("data/search-list-cloud.json")));
        std::unique_ptr<QWidget> view(jira::createListView(context, nullptr));
        view->show();
        QVERIFY(QTest::qWaitForWindowExposed(view.get()));
        auto *list = static_cast<ItemListView *>(view.get());
        const QUrl page(kCloudBase + QStringLiteral("/browse/OPS-9"));

        emit list->rowLinkActivated(QStringLiteral("OPS-9"), QStringLiteral("web"));
        QCOMPARE(browser.catcher.urls, QList<QUrl>{page});
        QVERIFY(context.shownPages.isEmpty());

        emit list->rowLinkActivated(QStringLiteral("OPS-9"), QStringLiteral("open"));
        QCOMPARE(context.shownPages, QList<QUrl>{page});
        QCOMPARE(browser.catcher.urls.size(), 1);

        // Shift takes Open to the browser.
        {
            const ShiftHeld shift(view->windowHandle());
            emit list->rowLinkActivated(QStringLiteral("OPS-9"), QStringLiteral("open"));
        }
        QCOMPARE(context.shownPages.size(), 1);
        QCOMPARE(browser.catcher.urls.size(), 2);

        emit list->rowLinkActivated(QStringLiteral("OPS-9"), QStringLiteral("add"));
        QCOMPARE(context.added.size(), 1);
        QCOMPARE(context.added.at(0).typeId, QStringLiteral("jira.issue.summary"));
        QCOMPARE(context.added.at(0).name, QStringLiteral("OPS-9"));
        QCOMPARE(context.added.at(0).config, (QJsonObject{{QStringLiteral("key"), QStringLiteral("OPS-9")}}));
    }

    // Links have no text, so the column sorts by key, as the Key column does.
    void linksColumnSortsByKey()
    {
        FakeContext context;
        useCloud(context, QString());
        context.source.status = listStatusOf(readJson(QFINDTESTDATA("data/search-list-cloud.json")));
        std::unique_ptr<QWidget> view(jira::createListView(context, nullptr));

        tableOf(view.get())->sortByColumn(kLinksColumn, Qt::AscendingOrder);
        const QAbstractItemModel *model = tableOf(view.get())->model();
        QCOMPARE(model->index(0, 0).data().toString(), QStringLiteral("OPS-7"));
        QCOMPARE(model->index(1, 0).data().toString(), QStringLiteral("OPS-9"));
        QCOMPARE(model->index(2, 0).data().toString(), QStringLiteral("OPS-10"));
    }

    void compactDatesSortByTheRealDate()
    {
        const auto issue = [](const QString &key, const QString &updated) {
            return QJsonObject{{QStringLiteral("key"), key},
                               {QStringLiteral("fields"), QJsonObject{{QStringLiteral("updated"), updated}}}};
        };
        // By text, "2025-..." would come after "01-..." and "12-..." the other way.
        const int thisYear = QDate::currentDate().year();
        const QString early = QStringLiteral("%1-01-15T10:00:00.000+0000").arg(thisYear);
        const QString late = QStringLiteral("%1-02-15T10:00:00.000+0000").arg(thisYear);
        const QString older = QStringLiteral("%1-12-15T10:00:00.000+0000").arg(thisYear - 1);
        const QJsonArray rows{issue(QStringLiteral("A-1"), early), issue(QStringLiteral("A-2"), older),
                              issue(QStringLiteral("A-3"), late),
                              issue(QStringLiteral("A-4"), QStringLiteral("not a date"))};

        FakeContext context;
        useCloud(context, QString());
        context.source.status = listStatusOf(QJsonObject{{QStringLiteral("issues"), rows}});
        std::unique_ptr<QWidget> view(jira::createListView(context, nullptr));

        const QAbstractItemModel *model = tableOf(view.get())->model();
        QCOMPARE(model->index(0, 0).data().toString(), QStringLiteral("A-3"));
        QCOMPARE(model->index(1, 0).data().toString(), QStringLiteral("A-1"));
        QCOMPARE(model->index(2, 0).data().toString(), QStringLiteral("A-2"));
        QCOMPARE(model->index(0, 4).data().toString(), mirante::widgets::compactDate(jira::readJiraDate(late)));
        QCOMPARE(model->index(2, 4).data().toString(), mirante::widgets::compactDate(jira::readJiraDate(older)));
        // A date it cannot read shows as it came.
        QCOMPARE(model->index(rowOf(view.get(), QStringLiteral("A-4")), 4).data().toString(), QStringLiteral("not a date"));
    }

    void statusFilter()
    {
        FakeContext context;
        useCloud(context, QString());
        context.source.status = listStatusOf(readJson(QFINDTESTDATA("data/search-list-cloud.json")));
        std::unique_ptr<QWidget> view(jira::createListView(context, nullptr));

        auto *filter = view->findChild<QComboBox *>(QStringLiteral("filter.2"));
        QVERIFY(filter);
        QCOMPARE(filter->count(), 4);
        QCOMPARE(filter->itemText(0), QStringLiteral("All"));
        filter->setCurrentIndex(filter->findText(QStringLiteral("To Do")));

        const QAbstractItemModel *model = tableOf(view.get())->model();
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(model->index(0, 0).data().toString(), QStringLiteral("OPS-10"));
    }

    void pageSizeFromTheField_data()
    {
        QTest::addColumn<QJsonValue>("value");
        QTest::addColumn<int>("expected");
        QTest::newRow("absent") << QJsonValue(QJsonValue::Undefined) << 50;
        QTest::newRow("set") << QJsonValue(2) << 2;
        QTest::newRow("zero") << QJsonValue(0) << 50;
        QTest::newRow("negative") << QJsonValue(-3) << 50;
        QTest::newRow("not a number") << QJsonValue(QStringLiteral("x")) << 50;
    }

    void pageSizeFromTheField()
    {
        QFETCH(QJsonValue, value);
        QFETCH(int, expected);
        FakeContext context;
        useCloud(context, QString());
        if (!value.isUndefined())
            context.itemConfig.insert(QStringLiteral("pageSize"), value);
        context.source.status = listStatusOf(readJson(QFINDTESTDATA("data/search-list-cloud.json")));
        std::unique_ptr<QWidget> view(jira::createListView(context, nullptr));

        auto *list = static_cast<ItemListView *>(view.get());
        QCOMPARE(list->pageSize(), expected);
        QCOMPARE(list->visibleKeys().size(), qMin(expected, 3));
    }

    void addToTaskPassesTypeAndConfiguration()
    {
        FakeContext context;
        useCloud(context, QString());
        context.source.status = listStatusOf(readJson(QFINDTESTDATA("data/search-list-cloud.json")));
        std::unique_ptr<QWidget> view(jira::createListView(context, nullptr));

        QMenu *menu = openRowMenu(view.get(), QStringLiteral("OPS-9"));
        QVERIFY(menu);
        QCOMPARE(menu->actions().size(), 4);
        QCOMPARE(menu->actions().at(0)->text(), QStringLiteral("Web"));
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

        menuEntry(menu, QStringLiteral("Web"))->trigger();
        QCOMPARE(context.shownPages.size(), 1);
        QCOMPARE(browser.catcher.urls.size(), 2);

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
