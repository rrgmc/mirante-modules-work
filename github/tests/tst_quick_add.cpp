// Quick add from a pull request's address, and related pull requests. Task T7 of the M11 plan fills it.
#include "../GitHubQuickAdd.h"

#include <QTest>

class TestQuickAdd : public QObject
{
    Q_OBJECT

private slots:
    // Builds against the module's interface until the tests are written.
    void builds()
    {
        mirante::modules::github::GitHubQuickAddHandler handler;
        Q_UNUSED(handler)
    }
};

QTEST_GUILESS_MAIN(TestQuickAdd)
#include "tst_quick_add.moc"
