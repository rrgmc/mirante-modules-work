// The summary views of pull requests, runs and issues. Task T5 of the M11 plan fills it.
#include "../GitHubViewSupport.h"

#include <QTest>

class TestSummaryViews : public QObject
{
    Q_OBJECT

private slots:
    // Builds against the module's interface until the tests are written.
    void builds()
    {
        QVERIFY(mirante::modules::github::isWebAddress(QUrl(QStringLiteral("https://github.com"))));
    }
};

QTEST_MAIN(TestSummaryViews)
#include "tst_summary_views.moc"
