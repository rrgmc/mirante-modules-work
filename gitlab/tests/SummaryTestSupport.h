// The fakes and data that the summary tests share: acme/backend!42, #7 and
// the pipelines of tests/data/, as the status tests read them. Each test is a
// program of its own, so this header is in one file of each.
#pragma once

#include "../GitLabStatus.h"

#include <mirante/testing/FakeApiClient.h>
#include <mirante/testing/FakeItemContext.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QStringList>
#include <QTest>

#include <functional>
#include <vector>

namespace summarytest {

using mirante::ApiReply;
using mirante::ItemStatus;
using mirante::testing::FakeApiClient;
using namespace mirante::modules::gitlab;

const QString kProject = QStringLiteral("api/v4/projects/acme%2Fbackend");
const QString kMergeRequest = kProject + QStringLiteral("/merge_requests/42");
const QString kIssue = kProject + QStringLiteral("/issues/7");
const QString kPipeline = kProject + QStringLiteral("/pipelines/9001");
const QString kMrAddress = QStringLiteral("https://gitlab.example/acme/backend/-/merge_requests/42");
const QString kIssueAddress = QStringLiteral("https://gitlab.example/acme/backend/-/issues/7");
const QString kPipelineAddress = QStringLiteral("https://gitlab.example/acme/backend/-/pipelines/9001");
const QString kScopeMessage = QStringLiteral("The token cannot do this. It needs the api scope.");

inline QJsonDocument readJson(const QString &name)
{
    QFile file(QFINDTESTDATA(QStringLiteral("tests/data/") + name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot read %s", qPrintable(name));
    return QJsonDocument::fromJson(file.readAll());
}

inline QJsonObject readObject(const QString &name)
{
    return readJson(name).object();
}

// A status source that a test drives by hand.
class FakeStatusSource : public mirante::ItemStatusSource
{
public:
    ItemStatus status;
    int refreshes = 0;

    ItemStatus current() const override { return status; }
    void refresh() override { ++refreshes; }
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

class FakeContext : public mirante::testing::FakeItemContext
{
public:
    FakeContext()
    {
        itemConfig = QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")}, {QStringLiteral("iid"), 42}};
    }

    QJsonObject general{{QStringLiteral("baseUrl"), QStringLiteral("https://built.example")}};
    QList<QUrl> pages;
    FakeApiClient client{QUrl(QStringLiteral("https://built.example"))};
    FakeStatusSource source;
    bool hasStatus = true;

    QJsonObject moduleSettings(const QString &screenId) const override
    {
        return screenId == QLatin1String("general") ? general : QJsonObject();
    }
    void showPage(const QUrl &url) override { pages << url; }
    mirante::ApiClient *server() override { return &client; }
    mirante::ItemStatusSource *status() override { return hasStatus ? &source : nullptr; }
};

inline ItemStatus mergeRequestWith(const QJsonObject &mergeRequest, const QJsonObject &approvals, const QJsonObject &user)
{
    return mergeRequestStatus(mergeRequest, approvals, user);
}

// acme/backend!42 with a failed head pipeline, approved by others.
inline ItemStatus failedMergeRequest()
{
    ItemStatus status = mergeRequestWith(readObject(QStringLiteral("merge_request.json")),
                                         readObject(QStringLiteral("approvals.json")),
                                         readObject(QStringLiteral("user.json")));
    status.data.insert(QStringLiteral("jobs"), readJson(QStringLiteral("pipeline_jobs.json")).array());
    return status;
}

// A merge request whose head pipeline has this status.
inline ItemStatus mergeRequestWithPipeline(const QString &pipelineStatus)
{
    QJsonObject mergeRequest = readObject(QStringLiteral("merge_request.json"));
    QJsonObject pipeline = mergeRequest.value(QStringLiteral("head_pipeline")).toObject();
    pipeline.insert(QStringLiteral("status"), pipelineStatus);
    mergeRequest.insert(QStringLiteral("head_pipeline"), pipeline);
    return mergeRequestWith(mergeRequest, readObject(QStringLiteral("approvals.json")),
                            readObject(QStringLiteral("user.json")));
}

// acme/backend#7 with the notes, leaving them out if notes is null.
inline ItemStatus openIssue(const QJsonValue &notes = QJsonValue(QJsonValue::Undefined))
{
    ItemStatus status = issueStatus(readObject(QStringLiteral("issue.json")));
    if (!notes.isUndefined())
        status.data.insert(QStringLiteral("notes"), notes);
    return status;
}

inline ItemStatus pipelineWithStatus(const QString &state)
{
    QJsonObject pipeline = readObject(QStringLiteral("pipeline_latest.json"));
    pipeline.insert(QStringLiteral("status"), state);
    return pipelineStatus(QStringLiteral("feature/retry-button"), pipeline,
                          readJson(QStringLiteral("pipeline_jobs.json")).array());
}

// 9001 failed, with 9000 failed and 8999 running in its group, and 8990 and
// 8950 before it, as the status makes it from data/pipelines.json.
inline ItemStatus groupStatus()
{
    const QJsonArray recent = readJson(QStringLiteral("pipelines.json")).array();
    const PipelineGroup group = pipelineGroup(readObject(QStringLiteral("pipeline_latest.json")), recent);
    QJsonArray siblings;
    const QStringList files{QStringLiteral("pipeline_jobs_sibling.json"), QStringLiteral("pipeline_jobs_running.json")};
    for (qsizetype i = 0; i < group.siblings.size(); ++i) {
        siblings.append(QJsonObject{{QStringLiteral("pipeline"), group.siblings.at(i)},
                                    {QStringLiteral("jobs"), readJson(files.at(i)).array()}});
    }
    return pipelineStatus(QStringLiteral("feature/retry-button"), group.latest,
                          readJson(QStringLiteral("pipeline_jobs.json")).array(), siblings, group.older);
}

inline QJsonObject note(const QString &body)
{
    return QJsonObject{{QStringLiteral("body"), body},
                       {QStringLiteral("author"), QJsonObject{{QStringLiteral("name"), QStringLiteral("Bruno")}}},
                       {QStringLiteral("created_at"), QStringLiteral("2026-10-26T12:00:00.000Z")}};
}

inline ApiReply failure(ApiReply::Error error, int httpStatus, const QString &message)
{
    ApiReply reply;
    reply.error = error;
    reply.httpStatus = httpStatus;
    reply.message = message;
    return reply;
}

inline void succeed(FakeContext &context, const QString &key)
{
    context.client.setJson(key, QJsonDocument(QJsonObject()));
}

} // namespace summarytest
