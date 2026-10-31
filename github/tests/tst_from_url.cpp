// Items from a pasted link: matchUrl() for every row of README.md#items-from-a-link. Task T2 of the M11 plan fills it.
#include "../GitHubUrls.h"

#include <QTest>

class TestFromUrl : public QObject
{
    Q_OBJECT

private slots:
    // Builds against the module's interface until the tests are written.
    void builds()
    {
        const QUrl home(QStringLiteral("https://github.com/acme/backend"));
        QVERIFY(!mirante::modules::github::matchUrl(QStringLiteral("github.run"), home, {}));
    }
};

QTEST_GUILESS_MAIN(TestFromUrl)
#include "tst_from_url.moc"
