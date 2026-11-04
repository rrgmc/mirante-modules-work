// The GitLab Status type: reading the status.io answer for status.gitlab.com,
// and its view. The answer in data/service_status.json was taken from
// api.status.io and changed by hand to hold an open incident with two
// messages, a planned maintenance and components in several states.
#include "../GitLabServiceStatus.h"

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

namespace gitlab = mirante::modules::gitlab;
using mirante::ItemStatus;

namespace {

QJsonObject answerFile()
{
    QFile file(QFINDTESTDATA(QStringLiteral("tests/data/service_status.json")));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

// The answer with its result changed by edit.
QJsonObject answerWith(const std::function<void(QJsonObject &result)> &edit)
{
    QJsonObject answer = answerFile();
    QJsonObject result = answer.value(QStringLiteral("result")).toObject();
    edit(result);
    answer.insert(QStringLiteral("result"), result);
    return answer;
}

QJsonObject operational(QJsonObject &result)
{
    result.insert(QStringLiteral("incidents"), QJsonArray());
    result.insert(QStringLiteral("status_overall"),
                  QJsonObject{{QStringLiteral("status"), QStringLiteral("Operational")},
                              {QStringLiteral("status_code"), 100}});
    return result;
}

// A status entry that the test sets and that tells its subscribers.
class FakeStatusSource : public mirante::ItemStatusSource
{
public:
    ItemStatus status;
    std::function<void(const ItemStatus &)> listener;

    ItemStatus current() const override { return status; }
    void refresh() override {}
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
    void asksTheGitLabPage()
    {
        QCOMPARE(gitlab::serviceSummaryUrl().toString(),
                 QStringLiteral("https://api.status.io/1.0/status/5b36dc6502d06804c08349f7"));
    }

    void readsTheOverallStatus()
    {
        const ItemStatus status = gitlab::readServiceSummary(answerFile());
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QCOMPARE(status.summary, QStringLiteral("Partial service disruption"));
        QCOMPARE(status.data.value(QStringLiteral("indicator")).toString(), QStringLiteral("major"));
        QCOMPARE(status.data.value(QStringLiteral("page")).toString(), QStringLiteral("https://status.gitlab.com"));
    }

    void readsTheComponents()
    {
        const QJsonArray components =
            gitlab::readServiceSummary(answerFile()).data.value(QStringLiteral("components")).toArray();
        QCOMPARE(components.size(), 4);
        QStringList levels;
        for (const QJsonValue &value : components)
            levels << value.toObject().value(QStringLiteral("level")).toString();
        QCOMPARE(levels, (QStringList{QStringLiteral("none"), QStringLiteral("minor"), QStringLiteral("none"),
                                      QStringLiteral("major")}));
        QCOMPARE(components.at(1).toObject().value(QStringLiteral("text")).toString(),
                 QStringLiteral("Degraded performance"));
    }

    void readsTheIncidentFromItsNewestMessage()
    {
        const QJsonArray incidents =
            gitlab::readServiceSummary(answerFile()).data.value(QStringLiteral("incidents")).toArray();
        QCOMPARE(incidents.size(), 1);
        const QJsonObject incident = incidents.first().toObject();
        QCOMPARE(incident.value(QStringLiteral("name")).toString(),
                 QStringLiteral("Delays in hosted runners on Linux"));
        QCOMPARE(incident.value(QStringLiteral("status")).toString(), QStringLiteral("identified"));
        QCOMPARE(incident.value(QStringLiteral("impact")).toString(), QStringLiteral("major"));
        QCOMPARE(incident.value(QStringLiteral("updated")).toString(), QStringLiteral("2026-11-04T14:45:00.000Z"));
        QCOMPARE(incident.value(QStringLiteral("message")).toString(),
                 QStringLiteral("The cause is known and a fix is being rolled out."));
        QCOMPARE(incident.value(QStringLiteral("url")).toString(),
                 QStringLiteral("https://status.gitlab.com/pages/incident/5b36dc6502d06804c08349f7/"
                                "6729a0c1e3a1b20530a1f001"));
    }

    void anOpenIncidentNeedsAttention()
    {
        const ItemStatus status = gitlab::readServiceSummary(answerFile());
        QCOMPARE(status.attention.size(), 1);
        QCOMPARE(status.attention.first().key, QStringLiteral("incident:6729a0c1e3a1b20530a1f001"));
        QCOMPARE(status.attention.first().text, QStringLiteral("GitLab: Delays in hosted runners on Linux"));
        QVERIFY(status.attention.first().notify);
    }

    void aDegradedPageWithoutIncidentsNeedsAttention()
    {
        const ItemStatus status = gitlab::readServiceSummary(answerWith([](QJsonObject &result) {
            result.insert(QStringLiteral("incidents"), QJsonArray());
            result.insert(QStringLiteral("status_overall"),
                          QJsonObject{{QStringLiteral("status"), QStringLiteral("Degraded Performance")},
                                      {QStringLiteral("status_code"), 300}});
        }));
        QCOMPARE(status.attention.size(), 1);
        QCOMPARE(status.attention.first().key, QStringLiteral("indicator:minor"));
        QCOMPARE(status.attention.first().text, QStringLiteral("GitLab: Degraded performance"));
        QVERIFY(!status.attention.first().notify);
    }

    void aWorkingPageNeedsNoAttention()
    {
        const ItemStatus status = gitlab::readServiceSummary(answerWith([](QJsonObject &result) {
            operational(result);
        }));
        QVERIFY(status.attention.isEmpty());
        QCOMPARE(status.summary, QStringLiteral("All systems operational"));
        QCOMPARE(status.data.value(QStringLiteral("indicator")).toString(), QStringLiteral("none"));
    }

    void readsThePlannedMaintenance()
    {
        const QJsonArray maintenances =
            gitlab::readServiceSummary(answerFile()).data.value(QStringLiteral("maintenances")).toArray();
        QCOMPARE(maintenances.size(), 1);
        const QJsonObject maintenance = maintenances.first().toObject();
        QCOMPARE(maintenance.value(QStringLiteral("name")).toString(), QStringLiteral("Database upgrade"));
        QCOMPARE(maintenance.value(QStringLiteral("status")).toString(), QStringLiteral("scheduled"));
        QCOMPARE(maintenance.value(QStringLiteral("start")).toString(), QStringLiteral("2026-11-08T02:00:00.000Z"));
        QCOMPARE(maintenance.value(QStringLiteral("end")).toString(), QStringLiteral("2026-11-08T04:00:00.000Z"));
    }

    void anEmptyAnswerStillReads()
    {
        const ItemStatus status = gitlab::readServiceSummary(QJsonObject());
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QCOMPARE(status.data.value(QStringLiteral("indicator")).toString(), QStringLiteral("none"));
        QVERIFY(status.attention.isEmpty());
    }

    void viewShowsTheStatus()
    {
        FakeStatusSource source;
        source.status = gitlab::readServiceSummary(answerFile());
        mirante::testing::FakeItemContext context;
        context.statusSource = &source;
        std::unique_ptr<QWidget> view(gitlab::createServiceStatusView(context, nullptr));

        QVERIFY(hasLabelContaining(*view, QStringLiteral("Partial service disruption")));
        // StatusPill has no Q_OBJECT, so it is found as a QLabel.
        const auto *indicator =
            dynamic_cast<mirante::widgets::StatusPill *>(view->findChild<QLabel *>(QStringLiteral("indicator")));
        QVERIFY(indicator);
        QCOMPARE(indicator->role(), mirante::widgets::StatusRole::Bad);
        // The status page is the cell header's web link.
        QCOMPARE(context.webLink, QUrl(QStringLiteral("https://status.gitlab.com")));
        QVERIFY(hasLabelContaining(*view, QStringLiteral("Delays in hosted runners on Linux")));
        QVERIFY(hasLabelContaining(*view, QStringLiteral("Database upgrade")));
        QVERIFY(labelTexts(*view).contains(QStringLiteral("API")));
        QVERIFY(!labelTexts(*view).contains(QStringLiteral("Website")));
    }

    void viewFollowsNewStatus()
    {
        FakeStatusSource source;
        mirante::testing::FakeItemContext context;
        context.statusSource = &source;
        std::unique_ptr<QWidget> view(gitlab::createServiceStatusView(context, nullptr));
        QVERIFY(hasLabelContaining(*view, QStringLiteral("Loading")));

        source.set(gitlab::readServiceSummary(answerWith([](QJsonObject &result) { operational(result); })));
        QVERIFY(hasLabelContaining(*view, QStringLiteral("All systems operational")));
        QVERIFY(hasLabelContaining(*view, QStringLiteral("No open incidents.")));
    }
};

QTEST_MAIN(TestServiceStatus)
#include "tst_service_status.moc"
