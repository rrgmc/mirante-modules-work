// Stubs until task T4 of the M11 plan implements README.md#lists.
#include "GitHubLists.h"

namespace mirante::modules::github {

int listLimit(const QJsonObject &config)
{
    Q_UNUSED(config)
    return kDefaultListLimit;
}

int listPageSize(const QJsonObject &config)
{
    Q_UNUSED(config)
    return kDefaultListPageSize;
}

QString scopeProblem(const QJsonObject &config, const QJsonValue &defaultRepository)
{
    Q_UNUSED(config)
    Q_UNUSED(defaultRepository)
    return {};
}

QString searchQuery(ListKind kind, const QJsonObject &config, const QJsonValue &defaultRepository)
{
    Q_UNUSED(kind)
    Q_UNUSED(config)
    Q_UNUSED(defaultRepository)
    return {};
}

QUrl listWebUrl(ListKind kind, const QUrl &webBase, const QJsonObject &config, const QJsonValue &defaultRepository)
{
    Q_UNUSED(kind)
    Q_UNUSED(webBase)
    Q_UNUSED(config)
    Q_UNUSED(defaultRepository)
    return {};
}

QUrl runListWebUrl(const QUrl &webBase, const QJsonObject &config, const QJsonValue &defaultRepository)
{
    Q_UNUSED(webBase)
    Q_UNUSED(config)
    Q_UNUSED(defaultRepository)
    return {};
}

QString listDescription(ListKind kind, const QJsonObject &config, const QJsonValue &defaultRepository)
{
    Q_UNUSED(kind)
    Q_UNUSED(config)
    Q_UNUSED(defaultRepository)
    return {};
}

QString runListDescription(const QJsonObject &config, const QJsonValue &defaultRepository)
{
    Q_UNUSED(config)
    Q_UNUSED(defaultRepository)
    return {};
}

void fetchPullRequestListStatus(StatusJob &job)
{
    job.finish(ItemStatus());
}

void fetchIssueListStatus(StatusJob &job)
{
    job.finish(ItemStatus());
}

void fetchRunListStatus(StatusJob &job)
{
    job.finish(ItemStatus());
}

} // namespace mirante::modules::github
