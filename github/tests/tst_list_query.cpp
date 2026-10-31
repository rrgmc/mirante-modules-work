// The search queries, Web links and status of the lists. Task T4 of the M11 plan fills it.
#include "../GitHubLists.h"

#include <QTest>

class TestListQuery : public QObject
{
    Q_OBJECT

private slots:
    // Builds against the module's interface until the tests are written.
    void builds()
    {
        QCOMPARE(mirante::modules::github::kDefaultListLimit, 100);
    }
};

QTEST_GUILESS_MAIN(TestListQuery)
#include "tst_list_query.moc"
