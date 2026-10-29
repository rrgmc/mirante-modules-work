#include "ClaudeUrls.h"

#include <QCoreApplication>
#include <QString>

namespace mirante::modules::claude {

QUrl sessionUrl(const QJsonObject &config)
{
    const QUrl url(config.value(QStringLiteral("url")).toString().trimmed(), QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty())
        return {};
    if (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))
        return {};
    return url;
}

std::optional<UrlMatch> matchUrl(const QUrl &url)
{
    const QUrl session = sessionUrl(QJsonObject{{QStringLiteral("url"), url.toString()}});
    const QString host = session.host().toLower();
    if (host != QLatin1String("claude.ai") && !host.endsWith(QLatin1String(".claude.ai")))
        return std::nullopt;

    UrlMatch match;
    match.config.insert(QStringLiteral("url"), session.toString());
    match.name = QCoreApplication::translate("claude", "Claude session");
    return match;
}

} // namespace mirante::modules::claude
