// Reading pasted Jira addresses: issue keys from browse, project and board
// pages, searches, the server with and without a connection, and addresses
// that are refused.
#include "../JiraUrls.h"

#include "items/ItemTypeRegistry.h"

#include <mirante/ItemType.h>

#include <QJsonObject>
#include <QTest>

#include <optional>

namespace mirante::modules::jira {
void registerModule(mirante::ModuleRegistrar &registrar);
}

using mirante::modules::jira::matchUrl;
using mirante::modules::jira::readPageAddress;

namespace {

QUrl url(const char *text) { return QUrl(QString::fromUtf8(text)); }

} // namespace

class TestFromUrl : public QObject
{
    Q_OBJECT

private slots:
    void browseOnCloud()
    {
        for (const char *type : {"jira.issue.summary", "jira.issue.page"}) {
            const auto match =
                matchUrl(QString::fromUtf8(type), url("https://acme.atlassian.net/browse/PROJ-123"), {});
            QVERIFY(match);
            QCOMPARE(match->config, (QJsonObject{{QStringLiteral("key"), QStringLiteral("PROJ-123")}}));
            QCOMPARE(match->name, QStringLiteral("PROJ-123"));
            QCOMPARE(match->baseUrl, url("https://acme.atlassian.net"));
            QVERIFY(!match->generic);
        }
    }

    // Jira Cloud's project pages and boards name the issue elsewhere.
    void projectAndBoardPages()
    {
        const auto project = readPageAddress(
            url("https://acme.atlassian.net/jira/software/projects/PROJ/issues/PROJ-9?filter=all"), {});
        QVERIFY(project);
        QCOMPARE(project->key, QStringLiteral("PROJ-9"));
        const auto board = readPageAddress(
            url("https://acme.atlassian.net/jira/software/projects/PROJ/boards/1?selectedIssue=PROJ-10"), {});
        QVERIFY(board);
        QCOMPARE(board->key, QStringLiteral("PROJ-10"));
    }

    // A key in lower case is read in upper case.
    void lowerCaseKey()
    {
        const auto address = readPageAddress(url("https://acme.atlassian.net/browse/proj-5"), {});
        QVERIFY(address);
        QCOMPARE(address->key, QStringLiteral("PROJ-5"));
    }

    // A Data Center server at a path is read with its connection's base URL.
    void dataCenterWithConnection()
    {
        const QList<QUrl> bases{url("https://jira.example.org/jira")};
        const auto match = matchUrl(QStringLiteral("jira.issue.page"),
                                    url("https://jira.example.org/jira/browse/OPS-1"), bases);
        QVERIFY(match);
        QCOMPARE(match->baseUrl, url("https://jira.example.org/jira"));
        QCOMPARE(match->config.value(QStringLiteral("key")).toString(), QStringLiteral("OPS-1"));
    }

    // Without a connection, /browse/<key> is enough, and Jira is taken to be
    // installed at the path before it.
    void serverWithoutConnection()
    {
        const auto atPath = readPageAddress(url("https://tracker.example.net/jira/browse/OPS-2"), {});
        QVERIFY(atPath);
        QCOMPARE(atPath->baseUrl, url("https://tracker.example.net/jira"));
        const auto atRoot = readPageAddress(url("https://tracker.example.net/browse/OPS-3"), {});
        QVERIFY(atRoot);
        QCOMPARE(atRoot->baseUrl, url("https://tracker.example.net"));
        // Other pages of an unknown server are not taken.
        QVERIFY(!readPageAddress(url("https://tracker.example.net/issues/OPS-4"), {}));
        QVERIFY(!readPageAddress(url("https://tracker.example.net/board?selectedIssue=OPS-4"), {}));
    }

    void search()
    {
        const auto match = matchUrl(QStringLiteral("jira.list"),
                                    url("https://acme.atlassian.net/issues/?jql=project%20%3D%20PROJ"), {});
        QVERIFY(match);
        QCOMPARE(match->config, (QJsonObject{{QStringLiteral("preset"), QStringLiteral("jql")},
                                             {QStringLiteral("jql"), QStringLiteral("project = PROJ")}}));
        QCOMPARE(match->name, QStringLiteral("Jira search"));
        // A search is not an issue, and an issue is not a search.
        QVERIFY(!matchUrl(QStringLiteral("jira.issue.summary"),
                          url("https://acme.atlassian.net/issues/?jql=project%20%3D%20PROJ"), {}));
        QVERIFY(!matchUrl(QStringLiteral("jira.list"), url("https://acme.atlassian.net/browse/PROJ-1"), {}));
    }

    void refused()
    {
        const QString type = QStringLiteral("jira.issue.summary");
        QVERIFY(!matchUrl(type, url("https://acme.atlassian.net/browse/PROJ"), {}));
        QVERIFY(!matchUrl(type, url("https://acme.atlassian.net/browse/PROJ-0"), {}));
        QVERIFY(!matchUrl(type, url("https://acme.atlassian.net/jira/your-work"), {}));
        QVERIFY(!matchUrl(type, url("https://example.com/PROJ-1"), {}));
        QVERIFY(!matchUrl(type, url("mailto:someone@acme.atlassian.net"), {}));
        QVERIFY(!matchUrl(type, QUrl(), {}));
    }

    void registeredTypes()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("jira"), &mirante::modules::jira::registerModule);
        const QUrl page = url("https://acme.atlassian.net/browse/PROJ-1");
        QStringList taking;
        for (const mirante::ItemType *type : registry.types()) {
            if (type->fromUrl(page, {}))
                taking << type->id();
        }
        taking.sort();
        QCOMPARE(taking, (QStringList{QStringLiteral("jira.issue.page"), QStringLiteral("jira.issue.summary")}));
    }
};

QTEST_MAIN(TestFromUrl)
#include "tst_from_url.moc"
