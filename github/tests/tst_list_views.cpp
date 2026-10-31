// The list views of pull requests, issues and runs. Task T6 of the M11 plan fills it.
#include "../GitHubListSupport.h"

#include <QJsonObject>
#include <QTest>

class TestListViews : public QObject
{
    Q_OBJECT

private slots:
    // Builds against the module's interface until the tests are written.
    void builds()
    {
        const QJsonObject row{
            {QStringLiteral("repository_url"), QStringLiteral("https://api.github.com/repos/cli/cli")}};
        QCOMPARE(mirante::modules::github::rowRepository(row), QStringLiteral("cli/cli"));
    }
};

QTEST_MAIN(TestListViews)
#include "tst_list_views.moc"
