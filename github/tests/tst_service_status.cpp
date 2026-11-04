// The GitHub Status type: reading the status page's summary, and its view.
// The summary in data/service_summary.json was taken from githubstatus.com
// and changed by hand to hold an open incident, a resolved one, a planned
// maintenance, components in each state, a group and a component shown only
// when degraded.
#include "../GitHubServiceStatus.h"

#include <mirante/testing/FakeItemContext.h>
#include <mirante/widgets/StatusPill.h>
#include <mirante/widgets/SummaryView.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QTest>

#include <functional>
#include <memory>

namespace github = mirante::modules::github;
using mirante::ItemStatus;

namespace {

QJsonObject summaryFile()
{
    QFile file(QFINDTESTDATA(QStringLiteral("tests/data/service_summary.json")));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

// A status entry that the test sets and that tells its subscribers.
class FakeStatusSource : public mirante::ItemStatusSource
{
public:
    ItemStatus status;
    int refreshes = 0;
    std::function<void(const ItemStatus &)> listener;

    ItemStatus current() const override { return status; }
    void refresh() override { ++refreshes; }
    void subscribe(QObject *, std::function<void(const ItemStatus &)> changed) override
    {
        listener = std::move(changed);
    }
    void set(const ItemStatus &next)
    {
        status = next;
        if (listener)
            listener(next);
    }
};

QStringList labelTexts(const QWidget &view)
{
    QStringList texts;
    for (const QLabel *label : view.findChildren<QLabel *>())
        texts << label->text();
    return texts;
}

bool hasLabelContaining(const QWidget &view, const QString &text)
{
    for (const QString &label : labelTexts(view)) {
        if (label.contains(text))
            return true;
    }
    return false;
}

} // namespace

class TestServiceStatus : public QObject
{
    Q_OBJECT

private slots:
    void readsTheIndicator()
    {
        const ItemStatus status = github::readServiceSummary(summaryFile());
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QCOMPARE(status.summary, QStringLiteral("Partial System Outage"));
        QCOMPARE(status.data.value(QStringLiteral("indicator")).toString(), QStringLiteral("major"));
        QCOMPARE(status.data.value(QStringLiteral("page")).toString(), QStringLiteral("https://www.githubstatus.com"));
        QVERIFY(status.updatedAt.isValid());
    }

    void readsTheComponents()
    {
        const QJsonArray components =
            github::readServiceSummary(summaryFile()).data.value(QStringLiteral("components")).toArray();
        // Without the group and the component shown only when degraded.
        QCOMPARE(components.size(), 4);
        const QJsonObject actions = components.at(1).toObject();
        QCOMPARE(actions.value(QStringLiteral("name")).toString(), QStringLiteral("Actions"));
        QCOMPARE(actions.value(QStringLiteral("level")).toString(), QStringLiteral("major"));
        QCOMPARE(actions.value(QStringLiteral("text")).toString(), QStringLiteral("Partial outage"));
        QCOMPARE(components.at(2).toObject().value(QStringLiteral("level")).toString(), QStringLiteral("minor"));
        QCOMPARE(components.at(0).toObject().value(QStringLiteral("level")).toString(), QStringLiteral("none"));
    }

    void readsTheOpenIncidents()
    {
        const ItemStatus status = github::readServiceSummary(summaryFile());
        const QJsonArray incidents = status.data.value(QStringLiteral("incidents")).toArray();
        QCOMPARE(incidents.size(), 1);
        const QJsonObject incident = incidents.first().toObject();
        QCOMPARE(incident.value(QStringLiteral("name")).toString(), QStringLiteral("Incident with Actions and Pages"));
        QCOMPARE(incident.value(QStringLiteral("impact")).toString(), QStringLiteral("major"));
        QCOMPARE(incident.value(QStringLiteral("status")).toString(), QStringLiteral("identified"));
        QCOMPARE(incident.value(QStringLiteral("url")).toString(), QStringLiteral("https://stspg.io/pjgptg3cfc6n"));
        // The newest update, which the summary lists first.
        QCOMPARE(incident.value(QStringLiteral("message")).toString(),
                 QStringLiteral("We are seeing delays in Actions jobs and Pages builds."));
    }

    void anOpenIncidentNeedsAttention()
    {
        const ItemStatus status = github::readServiceSummary(summaryFile());
        QCOMPARE(status.attention.size(), 1);
        QCOMPARE(status.attention.first().key, QStringLiteral("incident:xr3myzj7ms51"));
        QCOMPARE(status.attention.first().text, QStringLiteral("GitHub: Incident with Actions and Pages"));
        QVERIFY(status.attention.first().notify);
    }

    void aMinorIncidentDoesNotNotify()
    {
        QJsonObject summary = summaryFile();
        QJsonArray incidents = summary.value(QStringLiteral("incidents")).toArray();
        QJsonObject incident = incidents.first().toObject();
        incident.insert(QStringLiteral("impact"), QStringLiteral("minor"));
        incidents.replace(0, incident);
        summary.insert(QStringLiteral("incidents"), incidents);
        const ItemStatus status = github::readServiceSummary(summary);
        QCOMPARE(status.attention.size(), 1);
        QVERIFY(!status.attention.first().notify);
    }

    void aDegradedPageWithoutIncidentsNeedsAttention()
    {
        QJsonObject summary = summaryFile();
        summary.insert(QStringLiteral("incidents"), QJsonArray());
        summary.insert(QStringLiteral("status"), QJsonObject{{QStringLiteral("indicator"), QStringLiteral("minor")},
                                                             {QStringLiteral("description"),
                                                              QStringLiteral("Minor Service Outage")}});
        const ItemStatus status = github::readServiceSummary(summary);
        QCOMPARE(status.attention.size(), 1);
        QCOMPARE(status.attention.first().key, QStringLiteral("indicator:minor"));
        QVERIFY(!status.attention.first().notify);
    }

    void aWorkingPageNeedsNoAttention()
    {
        QJsonObject summary = summaryFile();
        summary.insert(QStringLiteral("incidents"), QJsonArray());
        summary.insert(QStringLiteral("status"), QJsonObject{{QStringLiteral("indicator"), QStringLiteral("none")},
                                                             {QStringLiteral("description"),
                                                              QStringLiteral("All Systems Operational")}});
        const ItemStatus status = github::readServiceSummary(summary);
        QVERIFY(status.attention.isEmpty());
        QCOMPARE(status.summary, QStringLiteral("All Systems Operational"));
    }

    void readsThePlannedMaintenance()
    {
        const QJsonArray maintenances =
            github::readServiceSummary(summaryFile()).data.value(QStringLiteral("maintenances")).toArray();
        QCOMPARE(maintenances.size(), 1);
        const QJsonObject maintenance = maintenances.first().toObject();
        QCOMPARE(maintenance.value(QStringLiteral("name")).toString(), QStringLiteral("Database maintenance"));
        QCOMPARE(maintenance.value(QStringLiteral("start")).toString(), QStringLiteral("2026-11-08T02:00:00.000Z"));
        QCOMPARE(maintenance.value(QStringLiteral("end")).toString(), QStringLiteral("2026-11-08T04:00:00.000Z"));
    }

    void anEmptyAnswerStillReads()
    {
        const ItemStatus status = github::readServiceSummary(QJsonObject());
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QCOMPARE(status.data.value(QStringLiteral("indicator")).toString(), QStringLiteral("none"));
        QVERIFY(status.attention.isEmpty());
    }

    void viewShowsLoadingFirst()
    {
        FakeStatusSource source;
        mirante::testing::FakeItemContext context;
        context.statusSource = &source;
        std::unique_ptr<QWidget> view(github::createServiceStatusView(context, nullptr));
        QVERIFY(hasLabelContaining(*view, QStringLiteral("Loading")));
    }

    void viewShowsTheStatus()
    {
        FakeStatusSource source;
        source.status = github::readServiceSummary(summaryFile());
        mirante::testing::FakeItemContext context;
        context.statusSource = &source;
        std::unique_ptr<QWidget> view(github::createServiceStatusView(context, nullptr));

        QVERIFY(hasLabelContaining(*view, QStringLiteral("Partial System Outage")));
        // StatusPill has no Q_OBJECT, so it is found as a QLabel.
        const auto *indicator =
            dynamic_cast<mirante::widgets::StatusPill *>(view->findChild<QLabel *>(QStringLiteral("indicator")));
        QVERIFY(indicator);
        QCOMPARE(indicator->text(), QStringLiteral("Major"));
        // The status page is the cell header's web link.
        QCOMPARE(context.webLink, QUrl(QStringLiteral("https://www.githubstatus.com")));
        QCOMPARE(indicator->role(), mirante::widgets::StatusRole::Bad);
        QVERIFY(hasLabelContaining(*view, QStringLiteral("Incident with Actions and Pages")));
        QVERIFY(hasLabelContaining(*view, QStringLiteral("We are seeing delays")));
        QVERIFY(hasLabelContaining(*view, QStringLiteral("Database maintenance")));
        // Only the components that do not work, by default.
        QVERIFY(hasLabelContaining(*view, QStringLiteral("Actions")));
        QVERIFY(!labelTexts(*view).contains(QStringLiteral("Git Operations")));
    }

    void viewFollowsTheSettings()
    {
        FakeStatusSource source;
        source.status = github::readServiceSummary(summaryFile());
        mirante::testing::FakeItemContext context;
        context.statusSource = &source;
        context.itemConfig = QJsonObject{{QStringLiteral("showAllComponents"), true},
                                         {QStringLiteral("showMaintenance"), false}};
        std::unique_ptr<QWidget> view(github::createServiceStatusView(context, nullptr));
        auto *summary = dynamic_cast<mirante::widgets::SummaryView *>(view.get());
        QVERIFY(summary);
        QVERIFY(labelTexts(*view).contains(QStringLiteral("Git Operations")));
        QVERIFY(!summary->isSectionVisible(QStringLiteral("maintenance")));
        QVERIFY(summary->isSectionVisible(QStringLiteral("components")));
    }

    void viewFollowsNewStatus()
    {
        FakeStatusSource source;
        mirante::testing::FakeItemContext context;
        context.statusSource = &source;
        std::unique_ptr<QWidget> view(github::createServiceStatusView(context, nullptr));

        QJsonObject summary = summaryFile();
        summary.insert(QStringLiteral("incidents"), QJsonArray());
        summary.insert(QStringLiteral("status"), QJsonObject{{QStringLiteral("indicator"), QStringLiteral("none")},
                                                             {QStringLiteral("description"),
                                                              QStringLiteral("All Systems Operational")}});
        source.set(github::readServiceSummary(summary));
        QVERIFY(hasLabelContaining(*view, QStringLiteral("All Systems Operational")));
        QVERIFY(hasLabelContaining(*view, QStringLiteral("No open incidents.")));

        // An error keeps the data shown.
        ItemStatus failed;
        failed.state = ItemStatus::State::Error;
        failed.message = QStringLiteral("No answer");
        source.set(failed);
        QVERIFY(hasLabelContaining(*view, QStringLiteral("All Systems Operational")));
    }
};

QTEST_MAIN(TestServiceStatus)
#include "tst_service_status.moc"
