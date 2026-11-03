// The fakes and data that the summary tests share: acme/widgets pull request
// 42 and issue 7, and the runs of workflow_runs.json and
// workflow_runs_failed.json, as the status tests read them from tests/data/.
// Each test is a program of its own, so this header is in one file of each.
#pragma once

#include "../GitHubStatus.h"

#include <mirante/testing/FakeApiClient.h>
#include <mirante/testing/FakeItemContext.h>

#include <QFile>
#include <QHash>
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
using namespace mirante::modules::github;

const QString kRuns = QStringLiteral("repos/acme/widgets/actions/runs/");
const QString kPrAddress = QStringLiteral("https://github.com/acme/widgets/pull/42");
const QString kBuiltPrAddress = QStringLiteral("https://built.example/acme/widgets/pull/42");
const QString kIssueAddress = QStringLiteral("https://github.com/acme/widgets/issues/7");
const QString kRunAddress = QStringLiteral("https://github.com/acme/widgets/actions/runs/9103");
const QString kTokenMessage = QStringLiteral("The token cannot do this. It needs write access to Actions.");

inline QString jobsKey(qint64 runId)
{
    return QStringLiteral("GET ") + kRuns + QStringLiteral("%1/jobs?filter=latest&per_page=100").arg(runId);
}

inline QString postKey(qint64 runId, const QString &action)
{
    return QStringLiteral("POST ") + kRuns + QStringLiteral("%1/").arg(runId) + action;
}

inline QString dataFile(const QString &name)
{
    return QFINDTESTDATA(QStringLiteral("tests/data/") + name);
}

inline QJsonDocument readJson(const QString &name)
{
    QFile file(dataFile(name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot read %s", qPrintable(name));
    return QJsonDocument::fromJson(file.readAll());
}

inline QJsonObject readObject(const QString &name)
{
    return readJson(name).object();
}

inline QJsonArray readArray(const QString &name)
{
    return readJson(name).array();
}

// The rows under key of an answer that wraps them.
inline QJsonArray rowsOf(const QString &name, const QString &key)
{
    return readObject(name).value(key).toArray();
}

inline QJsonArray checkRuns()
{
    return rowsOf(QStringLiteral("check_runs.json"), QStringLiteral("check_runs"));
}

inline QJsonArray jobsOf(const QString &name)
{
    return rowsOf(name, QStringLiteral("jobs"));
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
        itemConfig = QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/widgets")},
                     {QStringLiteral("number"), 42}};
    }

    QJsonObject general{{QStringLiteral("baseUrl"), QStringLiteral("https://built.example")}};
    QList<QUrl> pages;
    FakeApiClient client{QUrl(QStringLiteral("https://api.built.example/"))};
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

// Pull request 42, open, with the reviews of reviewsFile and the checks of
// check_runs.json and combinedFile.
inline ItemStatus pullRequest(const QJsonObject &object = readObject(QStringLiteral("pull_request.json")),
                       const QString &reviewsFile = QStringLiteral("pull_request_reviews.json"),
                       const QJsonArray &runs = checkRuns(),
                       const QString &combinedFile = QStringLiteral("combined_status.json"))
{
    return pullRequestStatus(object, readArray(reviewsFile), readObject(QStringLiteral("user.json")), runs,
                             readObject(combinedFile));
}

// Issue 7. Its comments, newest first, if comments is not empty.
inline ItemStatus issue(const QJsonArray &comments = {}, const QJsonObject &object = readObject(QStringLiteral("issue.json")))
{
    return issueStatus(object, comments);
}

inline QJsonObject comment(const QString &body, const QString &login = QStringLiteral("bruno-author"))
{
    return QJsonObject{{QStringLiteral("body"), body},
                       {QStringLiteral("user"), QJsonObject{{QStringLiteral("login"), login}}},
                       {QStringLiteral("created_at"), QStringLiteral("2026-10-26T12:00:00Z")}};
}

inline QJsonArray runsOf(const QString &name)
{
    return rowsOf(name, QStringLiteral("workflow_runs"));
}

// The group of workflow_runs_failed.json: CI 9103 failed and leads, with Lint
// 9102 timed out and Docs 9101 passed beside it, and CI 9090 before it.
inline ItemStatus failedGroup(bool siblingJobs = true)
{
    const RunGroup group = runGroup(runsOf(QStringLiteral("workflow_runs_failed.json")));
    const QHash<qint64, QString> files{{9102, QStringLiteral("run_jobs_lint.json")},
                                       {9101, QStringLiteral("run_jobs_codeql.json")}};
    QJsonArray siblings;
    for (const QJsonValue &value : group.siblings) {
        const QJsonObject run = value.toObject();
        QJsonObject entry{{QStringLiteral("run"), run}};
        if (siblingJobs)
            entry.insert(QStringLiteral("jobs"), jobsOf(files.value(run.value(QStringLiteral("id")).toInteger())));
        siblings.append(entry);
    }
    return runStatus(QStringLiteral("main"), group.lead, jobsOf(QStringLiteral("run_jobs_failed.json")), siblings,
                     group.older);
}

// The group of workflow_runs.json: Unit and Integration Tests leads, with Lint
// and Code Scanning, all passed and of pull request 42, and five runs before.
inline ItemStatus passedGroup()
{
    const RunGroup group = runGroup(runsOf(QStringLiteral("workflow_runs.json")));
    const QStringList files{QStringLiteral("run_jobs_lint.json"), QStringLiteral("run_jobs_codeql.json")};
    QJsonArray siblings;
    for (qsizetype i = 0; i < group.siblings.size(); ++i)
        siblings.append(QJsonObject{{QStringLiteral("run"), group.siblings.at(i)},
                                    {QStringLiteral("jobs"), jobsOf(files.at(i))}});
    return runStatus(QStringLiteral("feature/skip-refresh"), group.lead, jobsOf(QStringLiteral("run_jobs.json")),
                     siblings, group.older);
}

// CI 9103 alone, with this status and conclusion.
inline ItemStatus singleRun(const QString &state, const QString &conclusion,
                     const QJsonArray &jobs = jobsOf(QStringLiteral("run_jobs_failed.json")))
{
    QJsonObject run = runsOf(QStringLiteral("workflow_runs_failed.json")).at(1).toObject();
    run.insert(QStringLiteral("status"), state);
    run.insert(QStringLiteral("conclusion"), conclusion.isEmpty() ? QJsonValue(QJsonValue::Null) : conclusion);
    return runStatus(QStringLiteral("main"), run, jobs);
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
