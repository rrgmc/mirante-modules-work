// The fakes and data that the list tests share: the responses in tests/data/,
// and the requests that the pull request list makes. Each test is a program of
// its own, so this header is in one file of each.
#pragma once

#include "../GitHubLists.h"

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
using namespace mirante::modules::github;

const QString kDash = QStringLiteral("–");
const QString kBase = QStringLiteral("https://github.example");
// The head commit of pull_request.json.
const QString kHeadSha = QStringLiteral("04c4ebf52e7033a83eb38e9741c8ace05bf8617b");

// The GET key of a request about acme/backend.
inline QString backendKey(const QString &suffix)
{
    return QStringLiteral("GET repos/acme/backend/") + suffix;
}

inline QString pullKey(int number)
{
    return backendKey(QStringLiteral("pulls/%1").arg(number));
}

inline QString reviewsKey(int number)
{
    return backendKey(QStringLiteral("pulls/%1/reviews?per_page=100").arg(number));
}

inline QString checkRunsKey(const QString &sha = kHeadSha)
{
    return backendKey(QStringLiteral("commits/%1/check-runs?per_page=100").arg(sha));
}

inline QString combinedStatusKey(const QString &sha = kHeadSha)
{
    return backendKey(QStringLiteral("commits/%1/status").arg(sha));
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

inline ItemStatus statusOfRows(const QJsonArray &rows)
{
    ItemStatus status;
    status.state = ItemStatus::State::Ok;
    status.data = QJsonObject{{QStringLiteral("rows"), rows}, {QStringLiteral("total"), int(rows.size())}};
    return status;
}

inline QJsonArray pullRequestRows()
{
    return readJson(QStringLiteral("pr_search.json")).object().value(QStringLiteral("items")).toArray();
}

inline ItemStatus pullRequests()
{
    return statusOfRows(pullRequestRows());
}

inline ItemStatus issues()
{
    return statusOfRows(readJson(QStringLiteral("issue_search.json")).object().value(QStringLiteral("items")).toArray());
}

inline QJsonArray runRows()
{
    return readJson(QStringLiteral("run_list.json")).object().value(QStringLiteral("workflow_runs")).toArray();
}

inline ItemStatus runs()
{
    return statusOfRows(runRows());
}

inline QJsonObject runConfig()
{
    return QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/backend")},
                       {QStringLiteral("source"), QStringLiteral("all")}};
}

// count pull requests of acme/backend, numbered from 1, where a higher number
// is newer. The array holds the oldest first.
inline QJsonArray manyPullRequests(int count)
{
    QJsonArray rows;
    for (int number = 1; number <= count; ++number) {
        rows.append(QJsonObject{
            {QStringLiteral("number"), number},
            {QStringLiteral("repository_url"), QStringLiteral("https://api.github.com/repos/acme/backend")},
            {QStringLiteral("title"), QStringLiteral("Change %1").arg(number)},
            {QStringLiteral("updated_at"), QDateTime(QDate(2026, 10, 1), QTime(0, 0), QTimeZone::UTC)
                                               .addSecs(number * 3600)
                                               .toString(Qt::ISODate)}});
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
        itemConfig = QJsonObject{{QStringLiteral("scope"), QStringLiteral("repository")},
                     {QStringLiteral("repository"), QStringLiteral("own/config")}};
    }

    QJsonObject general{{QStringLiteral("baseUrl"), kBase}};
    QJsonValue repositorySetting;
    QList<QUrl> pages;
    QList<AddedItem> added;
    FakeApiClient client{QUrl(kBase + QStringLiteral("/api/v3/"))};
    FakeStatusSource source;
    bool hasStatus = true;

    QJsonValue projectSetting(const QString &key) const override
    {
        return key == QLatin1String("repository") ? repositorySetting : QJsonValue();
    }
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

// Answers for acme/backend#42: checks failed, by the combined status, and one
// approval.
inline void setUpBackend42(FakeContext &context)
{
    QVERIFY(context.client.setJsonFile(pullKey(42), dataFile(QStringLiteral("pull_request.json"))));
    QVERIFY(context.client.setJsonFile(reviewsKey(42), dataFile(QStringLiteral("pull_request_reviews.json"))));
    QVERIFY(context.client.setJsonFile(checkRunsKey(), dataFile(QStringLiteral("check_runs.json"))));
    QVERIFY(context.client.setJsonFile(combinedStatusKey(), dataFile(QStringLiteral("combined_status.json"))));
}

// A pull request whose head is sha, and one check run with this conclusion on it.
inline void setChecksAnswer(FakeContext &context, int number, const QString &sha, const QString &conclusion)
{
    context.client.setJson(pullKey(number),
                           QJsonDocument(QJsonObject{{QStringLiteral("head"), QJsonObject{{QStringLiteral("sha"), sha}}}}));
    context.client.setJson(
        checkRunsKey(sha),
        QJsonDocument(QJsonObject{{QStringLiteral("check_runs"),
                                   QJsonArray{QJsonObject{{QStringLiteral("status"), QStringLiteral("completed")},
                                                          {QStringLiteral("conclusion"), conclusion}}}}}));
}

} // namespace listtest
