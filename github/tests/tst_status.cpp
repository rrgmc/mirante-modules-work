// The status of pull requests, issues and runs, on answers in tests/data/. Task T3 of the M11 plan fills it.
#include "../GitHubStatus.h"

#include <QTest>

class TestStatus : public QObject
{
    Q_OBJECT

private slots:
    // Builds against the module's interface until the tests are written.
    void builds()
    {
        QVERIFY(mirante::modules::github::isFailedConclusion(QStringLiteral("timed_out")));
    }
};

QTEST_GUILESS_MAIN(TestStatus)
#include "tst_status.moc"
