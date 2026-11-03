// The fakes and data that the list tests share: the responses in tests/data/,
// and the requests that the merge request list makes. Each test is a program
// of its own, so this header is in one file of each.
#pragma once

#include "../GitLabLists.h"

#include <mirante/Status.h>
#include <mirante/testing/FakeApiClient.h>
#include <mirante/testing/FakeItemContext.h>

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTest>
#include <QTime>
#include <QTimeZone>

#include <functional>
#include <vector>

namespace listtest {

using mirante::ApiReply;
using mirante::ItemStatus;
using mirante::testing::FakeApiClient;
using namespace mirante::modules::gitlab;

const QString kDash = QStringLiteral("–");
const QString kBase = QStringLiteral("https://gitlab.example");

// The GET key of a merge request or an issue of acme/backend.
inline QString backendKey(const QString &suffix)
{
    return QStringLiteral("GET api/v4/projects/acme%2Fbackend/") + suffix;
}

inline QJsonDocument readJson(const QString &name)
{
    QFile file(QFINDTESTDATA(QStringLiteral("tests/data/") + name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot read %s", qPrintable(name));
    return QJsonDocument::fromJson(file.readAll());
}

inline ItemStatus statusOfRows(const QJsonArray &rows)
{
    ItemStatus status;
    status.state = ItemStatus::State::Ok;
    status.data = QJsonObject{{QStringLiteral("rows"), rows}, {QStringLiteral("total"), int(rows.size())}};
    return status;
}

inline ItemStatus mergeRequests()
{
    return statusOfRows(readJson(QStringLiteral("merge_request_list.json")).array());
}

inline ItemStatus issues()
{
    return statusOfRows(readJson(QStringLiteral("issue_list.json")).array());
}

inline ItemStatus pipelines()
{
    return statusOfRows(readJson(QStringLiteral("pipeline_list.json")).array());
}

inline QJsonObject pipelineConfig()
{
    return QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")},
                       {QStringLiteral("source"), QStringLiteral("all")}};
}

// count merge requests of acme/backend, numbered from 1, where a higher number
// is newer. The array holds the oldest first.
inline QJsonArray manyMergeRequests(int count)
{
    QJsonArray rows;
    for (int iid = 1; iid <= count; ++iid) {
        rows.append(QJsonObject{
            {QStringLiteral("iid"), iid},
            {QStringLiteral("project_id"), 77},
            {QStringLiteral("title"), QStringLiteral("Change %1").arg(iid)},
            {QStringLiteral("updated_at"), QDateTime(QDate(2026, 10, 1), QTime(0, 0), QTimeZone::UTC)
                                               .addSecs(iid * 3600)
                                               .toString(Qt::ISODate)},
            {QStringLiteral("references"),
             QJsonObject{{QStringLiteral("full"), QStringLiteral("acme/backend!%1").arg(iid)}}}});
    }
    return rows;
}

// A status source that a test drives by hand.
class FakeStatusSource : public mirante::ItemStatusSource
{
public:
    ItemStatus status;

    ItemStatus current() const override { return status; }
    void refresh() override {}
    void subscribe(QObject *receiver, std::function<void(const ItemStatus &)> changed) override
    {
        m_subscribers.push_back({receiver, std::move(changed)});
    }

    void change(const ItemStatus &next)
    {
        status = next;
        for (const Subscriber &subscriber : m_subscribers) {
            if (subscriber.receiver)
                subscriber.changed(next);
        }
    }

private:
    struct Subscriber
    {
        QPointer<QObject> receiver;
        std::function<void(const ItemStatus &)> changed;
    };
    std::vector<Subscriber> m_subscribers;
};

struct AddedItem
{
    QString typeId;
    QString name;
    QJsonObject config;
};

class FakeContext : public mirante::testing::FakeItemContext
{
public:
    FakeContext()
    {
        itemConfig = QJsonObject{{QStringLiteral("scope"), QStringLiteral("project")},
                     {QStringLiteral("project"), QStringLiteral("own/config")}};
    }

    QJsonObject general{{QStringLiteral("baseUrl"), kBase}};
    QList<QUrl> pages;
    QList<AddedItem> added;
    FakeApiClient client{QUrl(kBase)};
    FakeStatusSource source;
    bool hasStatus = true;

    QJsonObject moduleSettings(const QString &screenId) const override
    {
        return screenId == QLatin1String("general") ? general : QJsonObject();
    }
    void showPage(const QUrl &url) override { pages << url; }
    QString addItemToTask(const QString &typeId, const QString &name, const QJsonObject &config) override
    {
        added.append(AddedItem{typeId, name, config});
        return QStringLiteral("new-item");
    }
    mirante::ApiClient *server() override { return &client; }
    mirante::ItemStatusSource *status() override { return hasStatus ? &source : nullptr; }
};

// Answers for acme/backend!42, with a failed pipeline and 2 of 3 approvals.
inline void setUpBackend42(FakeContext &context)
{
    QVERIFY(context.client.setJsonFile(backendKey(QStringLiteral("merge_requests/42")),
                                       QFINDTESTDATA("tests/data/merge_request.json")));
    QVERIFY(context.client.setJsonFile(backendKey(QStringLiteral("merge_requests/42/approvals")),
                                       QFINDTESTDATA("tests/data/approvals.json")));
}

inline void setPipelineAnswer(FakeContext &context, const QString &status)
{
    context.client.setJson(
        backendKey(QStringLiteral("merge_requests/42")),
        QJsonDocument(QJsonObject{{QStringLiteral("head_pipeline"), QJsonObject{{QStringLiteral("status"), status}}}}));
}

} // namespace listtest
