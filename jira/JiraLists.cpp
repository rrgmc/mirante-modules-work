#include "JiraLists.h"

namespace mirante::modules::jira {

std::optional<QString> listJql(const QString &preset, const QString &jql, const QString &projectKey)
{
    Q_UNUSED(preset)
    Q_UNUSED(jql)
    Q_UNUSED(projectKey)
    return std::nullopt;
}

QUrl listWebUrl(const QUrl &baseUrl, const QString &jql)
{
    Q_UNUSED(baseUrl)
    Q_UNUSED(jql)
    return {};
}

void fetchListStatus(StatusJob &job)
{
    job.finish(ItemStatus());
}

} // namespace mirante::modules::jira
