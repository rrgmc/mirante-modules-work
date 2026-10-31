// The addresses of GitHub pages built from an item's configuration, and the
// reading of a pasted address.
#include "../GitHubUrls.h"

#include <QJsonObject>
#include <QTest>

using namespace mirante::modules::github;

namespace {

const QUrl kGitHub(QStringLiteral("https://github.com"));
const QUrl kEnterprise(QStringLiteral("https://example.org/github/"));

QJsonObject item(const QString &repository, const QJsonValue &number)
{
    return {{QStringLiteral("repository"), repository}, {QStringLiteral("number"), number}};
}

} // namespace

class TestUrls : public QObject
{
    Q_OBJECT

private slots:
    void configReaders()
    {
        QCOMPARE(repositoryOf(item(QStringLiteral(" /acme/backend/ "), 1), {}), QStringLiteral("acme/backend"));
        QCOMPARE(repositoryOf({}, QStringLiteral("acme/web")), QStringLiteral("acme/web"));
        QVERIFY(repositoryOf({}, {}).isEmpty());

        QCOMPARE(itemNumber(item({}, 42)), qint64(42));
        QCOMPARE(itemNumber(item({}, QStringLiteral(" 42 "))), qint64(42));
        QCOMPARE(itemNumber(item({}, -1)), qint64(0));
        QCOMPARE(itemNumber({}), qint64(0));

        QCOMPARE(configuredBranch({{QStringLiteral("branch"), QStringLiteral(" main ")}}), QStringLiteral("main"));
        QVERIFY(configuredBranch({}).isEmpty());
        QCOMPARE(configuredWorkflow({{QStringLiteral("workflow"), QStringLiteral(" ci.yml ")}}),
                 QStringLiteral("ci.yml"));
    }

    void pageAddresses()
    {
        QCOMPARE(pullRequestUrl(kGitHub, item(QStringLiteral("acme/backend"), 42), {}),
                 QUrl(QStringLiteral("https://github.com/acme/backend/pull/42")));
        QCOMPARE(issueUrl(kEnterprise, item({}, 7), QStringLiteral("acme/backend")),
                 QUrl(QStringLiteral("https://example.org/github/acme/backend/issues/7")));
        QVERIFY(pullRequestUrl(kGitHub, item({}, 42), {}).isEmpty());
        QVERIFY(issueUrl(kGitHub, item(QStringLiteral("acme/backend"), 0), {}).isEmpty());
        QCOMPARE(webPageUrl(kEnterprise, QStringLiteral("/acme/backend/actions")),
                 QUrl(QStringLiteral("https://example.org/github/acme/backend/actions")));
    }

    void readsPageAddresses()
    {
        std::optional<PageAddress> address =
            readPageAddress(QUrl(QStringLiteral("https://github.com/Acme/Backend/pull/42/files?x=1#y")), {});
        QVERIFY(address);
        QCOMPARE(address->webBase, kGitHub);
        QCOMPARE(address->repository, QStringLiteral("Acme/Backend"));
        QCOMPARE(address->kind, QStringLiteral("pull"));
        QCOMPARE(address->number, qint64(42));

        address = readPageAddress(QUrl(QStringLiteral("https://example.org/github/acme/backend/tree/feature/a-b")),
                                  {kEnterprise});
        QVERIFY(address);
        QCOMPARE(address->webBase, kEnterprise);
        QCOMPARE(address->kind, QStringLiteral("tree"));
        QCOMPARE(address->branch, QStringLiteral("feature/a-b"));

        address = readPageAddress(QUrl(QStringLiteral("https://github.com/acme/backend/actions/workflows/ci.yml")), {});
        QVERIFY(address);
        QCOMPARE(address->kind, QStringLiteral("actions"));
        QCOMPARE(address->workflow, QStringLiteral("ci.yml"));

        address = readPageAddress(QUrl(QStringLiteral("https://github.com/acme/backend/actions/runs/123/job/4")), {});
        QVERIFY(address);
        QCOMPARE(address->number, qint64(123));

        address = readPageAddress(QUrl(QStringLiteral("https://github.com/pulls/review-requested")), {});
        QVERIFY(address);
        QVERIFY(address->repository.isEmpty());
        QCOMPARE(address->kind, QStringLiteral("pulls"));
        QCOMPARE(address->filter, QStringLiteral("review-requested"));
    }

    void refusesOtherPages_data()
    {
        QTest::addColumn<QString>("url");
        QTest::newRow("home page") << QStringLiteral("https://github.com/acme/backend");
        QTest::newRow("new pull request") << QStringLiteral("https://github.com/acme/backend/pull/new/feature");
        QTest::newRow("compare") << QStringLiteral("https://github.com/acme/backend/compare/main...feature");
        QTest::newRow("new issue") << QStringLiteral("https://github.com/acme/backend/issues/new");
        QTest::newRow("other server") << QStringLiteral("https://gitlab.com/acme/backend/pull/1");
        QTest::newRow("issues review requested") << QStringLiteral("https://github.com/issues/review-requested");
        QTest::newRow("not the web") << QStringLiteral("ftp://github.com/acme/backend/pull/1");
    }

    void refusesOtherPages()
    {
        QFETCH(QString, url);
        QVERIFY(!readPageAddress(QUrl(url), {kEnterprise}));
    }
};

QTEST_GUILESS_MAIN(TestUrls)
#include "tst_urls.moc"
