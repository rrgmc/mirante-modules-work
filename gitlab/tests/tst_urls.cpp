// The addresses of the GitLab page types, and the messages they show instead.
#include "../GitLabUrls.h"

#include "items/ItemTypeRegistry.h"

#include <mirante/ItemContext.h>
#include <mirante/ItemType.h>

#include <QJsonObject>
#include <QTest>
#include <QWidget>

#include <memory>
#include <optional>

namespace mirante::modules::gitlab {
void registerModule(mirante::ModuleRegistrar &registrar);
}

using mirante::modules::gitlab::issueUrl;
using mirante::modules::gitlab::mergeRequestUrl;
using mirante::modules::gitlab::projectPath;

namespace {

// An item context with a configuration, a project setting and an optional
// connection. Each view it makes is a plain widget with the address or the
// message in a property.
class FakeContext : public mirante::ItemContext
{
public:
    QJsonObject itemConfig;
    QJsonObject projectSettings;
    std::optional<mirante::ConnectionDetails> details;
    mutable QStringList askedKinds;

    QString itemName() const override { return QStringLiteral("Item"); }
    QJsonObject config() const override { return itemConfig; }
    QJsonValue projectSetting(const QString &key) const override { return projectSettings.value(key); }
    QJsonObject moduleSettings(const QString &) const override { return {}; }

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

    QWebEngineProfile *profile() override { return nullptr; }

    QWidget *createMessageView(const QString &text, QWidget *parent) override
    {
        auto *view = new QWidget(parent);
        view->setProperty("message", text);
        return view;
    }

    void updateConfig(const QJsonObject &config) override { itemConfig = config; }
};

mirante::ConnectionDetails gitlabConnection(const QString &baseUrl)
{
    mirante::ConnectionDetails details;
    details.id = QStringLiteral("c1");
    details.kind = QStringLiteral("gitlab");
    details.name = QStringLiteral("GitLab");
    details.baseUrl = QUrl(baseUrl);
    return details;
}

QJsonObject config(const QString &project, const QJsonValue &iid)
{
    return QJsonObject{{QStringLiteral("project"), project}, {QStringLiteral("iid"), iid}};
}

const QString noConnection = QStringLiteral("No GitLab connection in this profile.");
const QString noConfig = QStringLiteral("Set the project and number in the item's settings.");

} // namespace

class TestUrls : public QObject
{
    Q_OBJECT

private slots:
    void mergeRequestAddress()
    {
        QCOMPARE(mergeRequestUrl(QUrl(QStringLiteral("https://gitlab.com")), config("group/project", 42), {}),
                 QUrl(QStringLiteral("https://gitlab.com/group/project/-/merge_requests/42")));
    }

    void issueAddress()
    {
        QCOMPARE(issueUrl(QUrl(QStringLiteral("https://gitlab.com")), config("group/project", 7), {}),
                 QUrl(QStringLiteral("https://gitlab.com/group/project/-/issues/7")));
    }

    // A trailing slash on the base URL is not doubled, and a base path is kept.
    void baseUrlSlashAndPath()
    {
        QCOMPARE(mergeRequestUrl(QUrl(QStringLiteral("https://gitlab.com/")), config("a/b", 1), {}),
                 QUrl(QStringLiteral("https://gitlab.com/a/b/-/merge_requests/1")));
        QCOMPARE(issueUrl(QUrl(QStringLiteral("https://example.org/gitlab/")), config("a/b/c", 3), {}),
                 QUrl(QStringLiteral("https://example.org/gitlab/a/b/c/-/issues/3")));
    }

    // Slashes around the project path are dropped, and a number stored as text works.
    void projectSlashesAndTextNumber()
    {
        QCOMPARE(mergeRequestUrl(QUrl(QStringLiteral("https://gitlab.com")), config(" /a/b/ ", "12"), {}),
                 QUrl(QStringLiteral("https://gitlab.com/a/b/-/merge_requests/12")));
    }

    // An empty project uses the project's "project" setting; a set one wins.
    void projectFallsBackToSetting()
    {
        const QJsonValue setting(QStringLiteral("team/app"));
        QCOMPARE(projectPath(config(QString(), 1), setting), QStringLiteral("team/app"));
        QCOMPARE(projectPath(config(QStringLiteral("own/repo"), 1), setting), QStringLiteral("own/repo"));
        QCOMPARE(projectPath(QJsonObject(), setting), QStringLiteral("team/app"));
        QCOMPARE(projectPath(QJsonObject(), QJsonValue()), QString());
        QCOMPARE(issueUrl(QUrl(QStringLiteral("https://gitlab.com")), config(QString(), 5), setting),
                 QUrl(QStringLiteral("https://gitlab.com/team/app/-/issues/5")));
    }

    void missingProjectOrNumberGivesNoAddress()
    {
        const QUrl base(QStringLiteral("https://gitlab.com"));
        QVERIFY(mergeRequestUrl(base, config(QString(), 1), {}).isEmpty());
        QVERIFY(mergeRequestUrl(base, config("a/b", QJsonValue()), {}).isEmpty());
        QVERIFY(mergeRequestUrl(base, config("a/b", 0), {}).isEmpty());
        QVERIFY(mergeRequestUrl(base, config("a/b", -3), {}).isEmpty());
        QVERIFY(issueUrl(base, config("a/b", "x"), {}).isEmpty());
        QVERIFY(issueUrl(base, QJsonObject(), {}).isEmpty());
    }

    void typesDeclareRenderingAndConnection()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("gitlab"), &mirante::modules::gitlab::registerModule);

        const QStringList web{QStringLiteral("gitlab.mr.page"), QStringLiteral("gitlab.issue.page")};
        for (const mirante::ItemType *type : registry.types()) {
            const bool isWeb = web.contains(type->id());
            QVERIFY2((type->rendering() == mirante::Rendering::Web) == isWeb, qPrintable(type->id()));
            QCOMPARE(type->connectionKinds(), QStringList{QStringLiteral("gitlab")});
        }
    }

    void viewsShowPagesOrMessages_data()
    {
        QTest::addColumn<QString>("typeId");
        QTest::addColumn<bool>("hasConnection");
        QTest::addColumn<QJsonObject>("itemConfig");
        QTest::addColumn<QString>("defaultProject");
        QTest::addColumn<QUrl>("url");
        QTest::addColumn<QString>("message");

        const QString mr = QStringLiteral("gitlab.mr.page");
        const QString issue = QStringLiteral("gitlab.issue.page");
        QTest::newRow("mr") << mr << true << config("g/p", 4) << QString()
                            << QUrl(QStringLiteral("https://gitlab.example.com/g/p/-/merge_requests/4")) << QString();
        QTest::newRow("issue") << issue << true << config("g/p", 9) << QString()
                               << QUrl(QStringLiteral("https://gitlab.example.com/g/p/-/issues/9")) << QString();
        QTest::newRow("mr from setting") << mr << true << config(QString(), 4) << QStringLiteral("s/p")
                                         << QUrl(QStringLiteral("https://gitlab.example.com/s/p/-/merge_requests/4"))
                                         << QString();
        QTest::newRow("mr without connection") << mr << false << config("g/p", 4) << QString() << QUrl()
                                               << noConnection;
        QTest::newRow("issue without connection") << issue << false << config("g/p", 9) << QString() << QUrl()
                                                  << noConnection;
        QTest::newRow("mr without project") << mr << true << config(QString(), 4) << QString() << QUrl()
                                            << noConfig;
        QTest::newRow("mr without number") << mr << true << config("g/p", QJsonValue()) << QString() << QUrl()
                                           << noConfig;
        QTest::newRow("issue without project") << issue << true << config(QString(), 9) << QString() << QUrl()
                                               << noConfig;
        QTest::newRow("issue without number") << issue << true << config("g/p", QJsonValue()) << QString()
                                              << QUrl() << noConfig;
    }

    void viewsShowPagesOrMessages()
    {
        QFETCH(QString, typeId);
        QFETCH(bool, hasConnection);
        QFETCH(QJsonObject, itemConfig);
        QFETCH(QString, defaultProject);
        QFETCH(QUrl, url);
        QFETCH(QString, message);

        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("gitlab"), &mirante::modules::gitlab::registerModule);

        FakeContext context;
        context.itemConfig = itemConfig;
        if (!defaultProject.isEmpty())
            context.projectSettings.insert(QStringLiteral("project"), defaultProject);
        if (hasConnection)
            context.details = gitlabConnection(QStringLiteral("https://gitlab.example.com/"));

        QWidget parent;
        std::unique_ptr<QWidget> view(registry.type(typeId).createView(context, &parent));
        QVERIFY(view);
        QCOMPARE(view->parentWidget(), &parent);
        QCOMPARE(context.askedKinds, QStringList{QStringLiteral("gitlab")});
        QCOMPARE(view->property("url").toUrl(), url);
        QCOMPARE(view->property("message").toString(), message);
    }

    void nativeTypesHaveNoView()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("gitlab"), &mirante::modules::gitlab::registerModule);

        FakeContext context;
        context.details = gitlabConnection(QStringLiteral("https://gitlab.example.com"));
        context.itemConfig = config("g/p", 1);
        for (const mirante::ItemType *type : registry.types()) {
            if (type->rendering() == mirante::Rendering::Native)
                QVERIFY2(!type->createView(context, nullptr), qPrintable(type->id()));
        }
    }
};

QTEST_MAIN(TestUrls)
#include "tst_urls.moc"
