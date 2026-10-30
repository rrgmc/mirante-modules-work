// The query of each list preset, and the address of its page. See
// README.md#status.
#include "../JiraLists.h"
#include "../JiraUrls.h"

#include <QTest>
#include <QUrlQuery>

using mirante::modules::jira::listJql;
using mirante::modules::jira::listLimit;
using mirante::modules::jira::listPageSize;
using mirante::modules::jira::listWebUrl;

namespace {

const QString kMyOpen = QStringLiteral("assignee = currentUser() AND resolution = Unresolved ORDER BY updated DESC");

} // namespace

class TestListJql : public QObject
{
    Q_OBJECT

private slots:
    void presets_data()
    {
        QTest::addColumn<QString>("preset");
        QTest::addColumn<QString>("jql");
        QTest::addColumn<QString>("projectKey");
        QTest::addColumn<QString>("expected");

        const QString sprint = QStringLiteral(" AND sprint in openSprints() ORDER BY rank");
        const QString recent = QStringLiteral(" AND updated >= -7d ORDER BY updated DESC");

        QTest::newRow("my open") << QStringLiteral("myOpen") << QString() << QString() << kMyOpen;
        QTest::newRow("no preset") << QString() << QString() << QString() << kMyOpen;
        QTest::newRow("sprint") << QStringLiteral("currentSprint") << QString() << QStringLiteral("OPS")
                                << QStringLiteral("project = \"OPS\"") + sprint;
        QTest::newRow("recent") << QStringLiteral("recentlyUpdated") << QString() << QStringLiteral("OPS")
                                << QStringLiteral("project = \"OPS\"") + recent;
        QTest::newRow("key is trimmed") << QStringLiteral("recentlyUpdated") << QString() << QStringLiteral(" OPS ")
                                        << QStringLiteral("project = \"OPS\"") + recent;
        QTest::newRow("jql") << QStringLiteral("jql") << QStringLiteral(" labels = \"a\" ") << QStringLiteral("OPS")
                             << QStringLiteral("labels = \"a\"");

        // A value cannot end the string and change the query.
        QTest::newRow("quote") << QStringLiteral("recentlyUpdated") << QString()
                               << QStringLiteral("OPS\" OR project != \"X")
                               << QStringLiteral("project = \"OPS\\\" OR project != \\\"X\"") + recent;
        QTest::newRow("backslash before a quote")
            << QStringLiteral("currentSprint") << QString() << QStringLiteral("OPS\\\" OR 1=1")
            << QStringLiteral("project = \"OPS\\\\\\\" OR 1=1\"") + sprint;
        QTest::newRow("line break") << QStringLiteral("currentSprint") << QString() << QStringLiteral("OPS\nX")
                                    << QStringLiteral("project = \"OPS\\nX\"") + sprint;
    }

    void presets()
    {
        QFETCH(QString, preset);
        QFETCH(QString, jql);
        QFETCH(QString, projectKey);
        QFETCH(QString, expected);
        const std::optional<QString> result = listJql(preset, jql, projectKey);
        QVERIFY(result);
        QCOMPARE(*result, expected);
    }

    void missingValues_data()
    {
        QTest::addColumn<QString>("preset");
        QTest::addColumn<QString>("jql");
        QTest::addColumn<QString>("projectKey");
        QTest::newRow("sprint") << QStringLiteral("currentSprint") << QStringLiteral("x") << QString();
        QTest::newRow("recent") << QStringLiteral("recentlyUpdated") << QStringLiteral("x") << QStringLiteral("  ");
        QTest::newRow("jql") << QStringLiteral("jql") << QString() << QStringLiteral("OPS");
        QTest::newRow("blank jql") << QStringLiteral("jql") << QStringLiteral("  \n") << QStringLiteral("OPS");
    }

    void missingValues()
    {
        QFETCH(QString, preset);
        QFETCH(QString, jql);
        QFETCH(QString, projectKey);
        QVERIFY(!listJql(preset, jql, projectKey));
    }

    void limitAndPageSize_data()
    {
        QTest::addColumn<QJsonObject>("config");
        QTest::addColumn<int>("limit");
        QTest::addColumn<int>("pageSize");
        QTest::newRow("absent") << QJsonObject() << 100 << 50;
        QTest::newRow("set") << QJsonObject{{QStringLiteral("limit"), 30}, {QStringLiteral("pageSize"), 10}} << 30 << 10;
        QTest::newRow("zero") << QJsonObject{{QStringLiteral("limit"), 0}, {QStringLiteral("pageSize"), 0}} << 100 << 50;
        QTest::newRow("negative") << QJsonObject{{QStringLiteral("limit"), -5}, {QStringLiteral("pageSize"), -1}} << 100 << 50;
        QTest::newRow("text") << QJsonObject{{QStringLiteral("limit"), QStringLiteral("9")}} << 100 << 50;
    }

    void limitAndPageSize()
    {
        QFETCH(QJsonObject, config);
        QFETCH(int, limit);
        QFETCH(int, pageSize);
        QCOMPARE(listLimit(config), limit);
        QCOMPARE(listPageSize(config), pageSize);
    }

    void webUrl_data()
    {
        QTest::addColumn<QString>("base");
        QTest::addColumn<QString>("start");
        QTest::newRow("site") << QStringLiteral("https://acme.atlassian.net")
                              << QStringLiteral("https://acme.atlassian.net/issues/?jql=");
        QTest::newRow("slash") << QStringLiteral("https://acme.atlassian.net/")
                               << QStringLiteral("https://acme.atlassian.net/issues/?jql=");
        QTest::newRow("path") << QStringLiteral("https://jira.example.org/jira/")
                              << QStringLiteral("https://jira.example.org/jira/issues/?jql=");
    }

    void webUrl()
    {
        QFETCH(QString, base);
        QFETCH(QString, start);
        // Each of these characters would change the address if it stayed as it is.
        const QString jql = QStringLiteral("summary ~ \"a+b & c#d 100%\" ORDER BY updated DESC");
        const QUrl url = listWebUrl(QUrl(base), jql);
        QVERIFY(url.isValid());
        QVERIFY2(url.toString(QUrl::FullyEncoded).startsWith(start), qPrintable(url.toString(QUrl::FullyEncoded)));
        QVERIFY(!url.hasFragment());
        QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("jql"), QUrl::FullyDecoded), jql);
    }

    void webUrlWithoutSite()
    {
        QVERIFY(listWebUrl(QUrl(), QStringLiteral("project = OPS")).isEmpty());
    }

    // A pasted address of the page makes the same list again.
    void webUrlReadsBack()
    {
        const QUrl base(QStringLiteral("https://acme.atlassian.net"));
        const QString jql = QStringLiteral("project = \"OPS\" AND labels = \"a+b\"");
        const std::optional<mirante::modules::jira::PageAddress> address =
            mirante::modules::jira::readPageAddress(listWebUrl(base, jql), {base});
        QVERIFY(address);
        QCOMPARE(address->jql, jql);
    }
};

QTEST_GUILESS_MAIN(TestListJql)
#include "tst_list_jql.moc"
