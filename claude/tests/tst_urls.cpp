// The address of the Claude web session type, and the message it shows instead.
#include "../ClaudeUrls.h"

#include "items/ItemTypeRegistry.h"

#include <mirante/ItemContext.h>
#include <mirante/ItemType.h>

#include <QJsonObject>
#include <QTest>
#include <QWidget>

#include <memory>
#include <optional>

namespace mirante::modules::claude {
void registerModule(mirante::ModuleRegistrar &registrar);
}

using mirante::modules::claude::sessionUrl;

namespace {

// An item context with a configuration and no connection. Each view it makes
// is a plain widget with the address or the message in a property.
class FakeContext : public mirante::ItemContext
{
public:
    QJsonObject itemConfig;

    QString itemName() const override { return QStringLiteral("Item"); }
    QJsonObject config() const override { return itemConfig; }
    QJsonValue projectSetting(const QString &) const override { return {}; }
    QJsonObject moduleSettings(const QString &) const override { return {}; }
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

QJsonObject urlConfig(const QString &url)
{
    return QJsonObject{{QStringLiteral("url"), url}};
}

} // namespace

class TestUrls : public QObject
{
    Q_OBJECT

private slots:
    void sessionAddress_data()
    {
        QTest::addColumn<QJsonObject>("itemConfig");
        QTest::addColumn<QUrl>("url");

        QTest::newRow("https") << urlConfig("https://claude.ai/code/session_1")
                               << QUrl(QStringLiteral("https://claude.ai/code/session_1"));
        QTest::newRow("http") << urlConfig("http://localhost:8080/") << QUrl(QStringLiteral("http://localhost:8080/"));
        QTest::newRow("spaces") << urlConfig(" https://claude.ai/ ") << QUrl(QStringLiteral("https://claude.ai/"));
        QTest::newRow("missing") << QJsonObject() << QUrl();
        QTest::newRow("empty") << urlConfig(QString()) << QUrl();
        QTest::newRow("no scheme") << urlConfig("claude.ai/code") << QUrl();
        QTest::newRow("file") << urlConfig("file:///C:/notes.txt") << QUrl();
        QTest::newRow("javascript") << urlConfig("javascript:alert(1)") << QUrl();
        QTest::newRow("no host") << urlConfig("https://") << QUrl();
    }

    void sessionAddress()
    {
        QFETCH(QJsonObject, itemConfig);
        QFETCH(QUrl, url);
        QCOMPARE(sessionUrl(itemConfig), url);
    }

    void typesDeclareRenderingAndConnection()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("claude"), &mirante::modules::claude::registerModule);

        QVERIFY(registry.type(QStringLiteral("claude.session.web")).rendering() == mirante::Rendering::Web);
    }

    void viewShowsPageOrMessage_data()
    {
        QTest::addColumn<QJsonObject>("itemConfig");
        QTest::addColumn<QUrl>("url");
        QTest::addColumn<QString>("message");

        const QString noAddress = QStringLiteral("Set the address in the item's settings.");
        QTest::newRow("address") << urlConfig("https://claude.ai/code/session_1")
                                 << QUrl(QStringLiteral("https://claude.ai/code/session_1")) << QString();
        QTest::newRow("missing") << QJsonObject() << QUrl() << noAddress;
        QTest::newRow("not http") << urlConfig("ftp://claude.ai/") << QUrl() << noAddress;
    }

    void viewShowsPageOrMessage()
    {
        QFETCH(QJsonObject, itemConfig);
        QFETCH(QUrl, url);
        QFETCH(QString, message);

        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("claude"), &mirante::modules::claude::registerModule);

        FakeContext context;
        context.itemConfig = itemConfig;

        QWidget parent;
        std::unique_ptr<QWidget> view(
            registry.type(QStringLiteral("claude.session.web")).createView(context, &parent));
        QVERIFY(view);
        QCOMPARE(view->parentWidget(), &parent);
        QCOMPARE(view->property("url").toUrl(), url);
        QCOMPARE(view->property("message").toString(), message);
    }
};

QTEST_MAIN(TestUrls)
#include "tst_urls.moc"
