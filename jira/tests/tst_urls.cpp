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

// An item context with a configuration and the values of the Jira General
// Configuration of the item's profile. Each view it makes is a plain widget
// with the address or the message in a property.
class FakeContext : public mirante::ItemContext
{
public:
    QJsonObject itemConfig;
    QJsonObject general;
    mutable QStringList askedScreens;

    QString itemName() const override { return QStringLiteral("Item"); }
    QJsonObject config() const override { return itemConfig; }
    QJsonValue projectSetting(const QString &) const override { return {}; }
    QJsonObject moduleSettings(const QString &screenId) const override
    {
        askedScreens << screenId;
        return screenId == QLatin1String("general") ? general : QJsonObject();
    }

    QString folderPath(const QString &) const override { return {}; }

    QWidget *createWebView(const QUrl &url, QWidget *parent) override
    {
        auto *view = new QWidget(parent);
        view->setProperty("url", url);
        return view;
    }

    QWebEngineProfile *profile() override { return nullptr; }

    QWidget *createMessageView(const QString &text, QWidget *parent) override
    {
        auto *view = new QWidget(parent);
        view->setProperty("message", text);
        return view;
    }

    void updateConfig(const QJsonObject &config) override { itemConfig = config; }
};

QJsonObject site(const QString &baseUrl)
{
    return QJsonObject{{QStringLiteral("baseUrl"), baseUrl}};
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

    void typesDeclareRendering()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("jira"), &mirante::modules::jira::registerModule);

        for (const mirante::ItemType *type : registry.types()) {
            const bool isWeb = type->id() == QStringLiteral("jira.issue.page");
            QVERIFY2((type->rendering() == mirante::Rendering::Web) == isWeb, qPrintable(type->id()));
            QVERIFY2(registry.usesProfile(type->id()), qPrintable(type->id()));
        }
    }

    void viewShowsPageOrMessage_data()
    {
        QTest::addColumn<QString>("baseUrl");
        QTest::addColumn<QJsonObject>("itemConfig");
        QTest::addColumn<QUrl>("url");
        QTest::addColumn<QString>("message");

        const QString noSite =
            QStringLiteral("Set the Jira base URL in Modules > Jira > General Configuration for this profile.");
        const QString noKey = QStringLiteral("Set the issue key in the item's settings.");
        const QString server = QStringLiteral("https://jira.example.com/");
        QTest::newRow("cloud") << QStringLiteral("https://acme.atlassian.net") << keyConfig("PROJ-1")
                               << QUrl(QStringLiteral("https://acme.atlassian.net/browse/PROJ-1")) << QString();
        QTest::newRow("data center") << server << keyConfig("PROJ-2")
                                     << QUrl(QStringLiteral("https://jira.example.com/browse/PROJ-2")) << QString();
        QTest::newRow("without a site") << QString() << keyConfig("PROJ-1") << QUrl() << noSite;
        QTest::newRow("with a bad site") << QStringLiteral("jira") << keyConfig("PROJ-1") << QUrl() << noSite;
        QTest::newRow("without key") << server << QJsonObject() << QUrl() << noKey;
        QTest::newRow("blank key") << server << keyConfig(" ") << QUrl() << noKey;
    }

    void viewShowsPageOrMessage()
    {
        QFETCH(QString, baseUrl);
        QFETCH(QJsonObject, itemConfig);
        QFETCH(QUrl, url);
        QFETCH(QString, message);

        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("jira"), &mirante::modules::jira::registerModule);

        FakeContext context;
        context.itemConfig = itemConfig;
        if (!baseUrl.isEmpty())
            context.general = site(baseUrl);

        QWidget parent;
        std::unique_ptr<QWidget> view(registry.type(QStringLiteral("jira.issue.page")).createView(context, &parent));
        QVERIFY(view);
        QCOMPARE(view->parentWidget(), &parent);
        QVERIFY(context.askedScreens.contains(QStringLiteral("general")));
        QCOMPARE(view->property("url").toUrl(), url);
        QCOMPARE(view->property("message").toString(), message);
    }

    void nativeTypesHaveNoView()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("jira"), &mirante::modules::jira::registerModule);

        FakeContext context;
        context.general = site(QStringLiteral("https://jira.example.com"));
        context.itemConfig = keyConfig("PROJ-1");
        for (const mirante::ItemType *type : registry.types()) {
            if (type->rendering() == mirante::Rendering::Native)
                QVERIFY2(!type->createView(context, nullptr), qPrintable(type->id()));
        }
    }
};

QTEST_MAIN(TestUrls)
#include "tst_urls.moc"
