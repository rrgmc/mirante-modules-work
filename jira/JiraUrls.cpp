#include "JiraUrls.h"

#include <QString>

namespace mirante::modules::jira {

QUrl issueUrl(const QUrl &baseUrl, const QJsonObject &config)
{
    const QString key = config.value(QStringLiteral("key")).toString().trimmed();
    if (key.isEmpty())
        return {};

    // Keep a slash at the end of the base URL from doubling.
    QString path = baseUrl.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    path += QStringLiteral("/browse/") + key;

    QUrl url = baseUrl;
    url.setPath(path);
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

} // namespace mirante::modules::jira
