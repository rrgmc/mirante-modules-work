#include "GitLabLists.h"

namespace mirante::modules::gitlab {

QUrlQuery listQuery(ListKind kind, const QString &preset, qint64 userId, const QString &filter)
{
    Q_UNUSED(kind)
    Q_UNUSED(preset)
    Q_UNUSED(userId)
    Q_UNUSED(filter)
    return {};
}

QUrl listWebUrl(ListKind kind, const QUrl &baseUrl, const QJsonObject &config, const QJsonValue &defaultProject)
{
    Q_UNUSED(kind)
    Q_UNUSED(baseUrl)
    Q_UNUSED(config)
    Q_UNUSED(defaultProject)
    return {};
}

void fetchMergeRequestListStatus(StatusJob &job)
{
    job.finish(ItemStatus());
}

void fetchIssueListStatus(StatusJob &job)
{
    job.finish(ItemStatus());
}

} // namespace mirante::modules::gitlab
