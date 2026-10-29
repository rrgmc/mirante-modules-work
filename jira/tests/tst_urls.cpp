// The address of the Jira issue page type, and the messages it shows instead.
#include "../JiraUrls.h"

#include "items/ItemTypeRegistry.h"

#include <mirante/ItemContext.h>
#include <mirante/ItemType.h>

#include <QJsonObject>
#include <QTest>
#include <QWidget>

#include <memory>
#include <optional>

namespace mirante::modules::jira {
void registerModule(mirante::ModuleRegistrar &registrar);
}

using mirante::modules::jira::issueUrl;

namespace {

// An item context with a configuration and an optional connection. Each view
// it makes is a plain widget with the address or the message in a property.
class FakeContext : public mirante::ItemContext
{
public:
    QJsonObject itemConfig;
    std::optional<mirante::ConnectionDetails> details;
    mutable QStringList askedKinds;

    QString itemName() const override { return QStringLiteral("Item"); }
    QJsonObject config() const override { return itemConfig; }
    QJsonValue projectSetting(const QString &) const override { return {}; }

    std::optional<mirante::ConnectionDetails> connection(const QStringList &kinds) const override
    {
        askedKinds = kinds;
        return details;
    }

    QString folderPath(const QString &) const override { return {}; }

    QWidget *createWebView(const QUrl &url, QWidget *parent) override
    {
        auto *view = new QWidget(parent);
        view->setProperty("url", url);
        return view;
    }

    QWebEngineProfile *profile(const QUrl &) override { return nullptr; }

    QWidget *createMessageView(const QString &text, QWidget *parent) override
    {
        auto *view = new QWidget(parent);
        view->setProperty("message", text);
        return view;
    }

    void updateConfig(const QJsonObject &config) override { itemConfig = config; }
};

mirante::ConnectionDetails jiraConnection(const QString &kind, const QString &baseUrl)
{
    mirante::ConnectionDetails details;
    details.id = QStringLiteral("c1");
    details.kind = kind;
    details.name = QStringLiteral("Jira");
    details.baseUrl = QUrl(baseUrl);
    return details;
}

QJsonObject keyConfig(const QString &key)
{
    return QJsonObject{{QStringLiteral("key"), key}};
}

} // namespace

class TestUrls : public QObject
{
    Q_OBJECT

private slots:
    void issueAddress()
    {
        QCOMPARE(issueUrl(QUrl(QStringLiteral("https://example.atlassian.net")), keyConfig("PROJ-123")),
                 QUrl(QStringLiteral("https://example.atlassian.net/browse/PROJ-123")));
    }

    // A trailing slash on the base URL is not doubled, a base path is kept,
    // and spaces around the key are dropped.
    void baseUrlSlashAndPath()
    {
        QCOMPARE(issueUrl(QUrl(QStringLiteral("https://example.atlassian.net/")), keyConfig(" AB-1 ")),
                 QUrl(QStringLiteral("https://example.atlassian.net/browse/AB-1")));
        QCOMPARE(issueUrl(QUrl(QStringLiteral("https://jira.example.org/jira/")), keyConfig("AB-2")),
                 QUrl(QStringLiteral("https://jira.example.org/jira/browse/AB-2")));
    }

    void missingKeyGivesNoAddress()
    {
        const QUrl base(QStringLiteral("https://example.atlassian.net"));
        QVERIFY(issueUrl(base, QJsonObject()).isEmpty());
        QVERIFY(issueUrl(base, keyConfig(QString())).isEmpty());
        QVERIFY(issueUrl(base, keyConfig("  ")).isEmpty());
    }

    void typesDeclareRenderingAndConnection()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("jira"), &mirante::modules::jira::registerModule);

        const QStringList kinds{QStringLiteral("jira.cloud"), QStringLiteral("jira.dc")};
        for (const mirante::ItemType *type : registry.types()) {
            const bool isWeb = type->id() == QStringLiteral("jira.issue.page");
            QVERIFY2((type->rendering() == mirante::Rendering::Web) == isWeb, qPrintable(type->id()));
            QCOMPARE(type->connectionKinds(), kinds);
        }
    }

    void viewShowsPageOrMessage_data()
    {
        QTest::addColumn<QString>("connectionKind");
        QTest::addColumn<QJsonObject>("itemConfig");
        QTest::addColumn<QUrl>("url");
        QTest::addColumn<QString>("message");

        const QString noConnection = QStringLiteral("No Jira connection in this group.");
        const QString noKey = QStringLiteral("Set the issue key in the item's settings.");
        QTest::newRow("cloud") << QStringLiteral("jira.cloud") << keyConfig("PROJ-1")
                               << QUrl(QStringLiteral("https://jira.example.com/browse/PROJ-1")) << QString();
        QTest::newRow("data center") << QStringLiteral("jira.dc") << keyConfig("PROJ-2")
                                     << QUrl(QStringLiteral("https://jira.example.com/browse/PROJ-2")) << QString();
        QTest::newRow("without connection") << QString() << keyConfig("PROJ-1") << QUrl() << noConnection;
        QTest::newRow("without key") << QStringLiteral("jira.cloud") << QJsonObject() << QUrl() << noKey;
        QTest::newRow("blank key") << QStringLiteral("jira.dc") << keyConfig(" ") << QUrl() << noKey;
    }

    void viewShowsPageOrMessage()
    {
        QFETCH(QString, connectionKind);
        QFETCH(QJsonObject, itemConfig);
        QFETCH(QUrl, url);
        QFETCH(QString, message);

        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("jira"), &mirante::modules::jira::registerModule);

        FakeContext context;
        context.itemConfig = itemConfig;
        if (!connectionKind.isEmpty())
            context.details = jiraConnection(connectionKind, QStringLiteral("https://jira.example.com/"));

        QWidget parent;
        std::unique_ptr<QWidget> view(registry.type(QStringLiteral("jira.issue.page")).createView(context, &parent));
        QVERIFY(view);
        QCOMPARE(view->parentWidget(), &parent);
        QCOMPARE(context.askedKinds, (QStringList{QStringLiteral("jira.cloud"), QStringLiteral("jira.dc")}));
        QCOMPARE(view->property("url").toUrl(), url);
        QCOMPARE(view->property("message").toString(), message);
    }

    void nativeTypesHaveNoView()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("jira"), &mirante::modules::jira::registerModule);

        FakeContext context;
        context.details = jiraConnection(QStringLiteral("jira.cloud"), QStringLiteral("https://jira.example.com"));
        context.itemConfig = keyConfig("PROJ-1");
        for (const mirante::ItemType *type : registry.types()) {
            if (type->rendering() == mirante::Rendering::Native)
                QVERIFY2(!type->createView(context, nullptr), qPrintable(type->id()));
        }
    }
};

QTEST_MAIN(TestUrls)
#include "tst_urls.moc"
