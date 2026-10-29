#include "JiraUrls.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QUrlQuery>

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

namespace {

// An issue key, such as PROJ-123, in upper case, or empty.
QString issueKey(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Z][A-Z0-9_]*-[1-9][0-9]*$"),
                                            QRegularExpression::CaseInsensitiveOption);
    const QString key = text.trimmed();
    return pattern.match(key).hasMatch() ? key.toUpper() : QString();
}

// The server of url, without a path.
QUrl serverOf(const QUrl &url)
{
    QUrl server;
    server.setScheme(url.scheme().toLower());
    server.setHost(url.host());
    server.setPort(url.port());
    return server;
}

} // namespace

std::optional<PageAddress> readPageAddress(const QUrl &url, const QList<QUrl> &baseUrls)
{
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        || url.host().isEmpty())
        return std::nullopt;

    PageAddress address;
    QString path;
    bool known = false;
    for (const QUrl &base : baseUrls) {
        const std::optional<QString> below = pathBelow(url, base);
        if (below && (!known || base.path().size() > address.baseUrl.path().size())) {
            address.baseUrl = base;
            path = *below;
            known = true;
        }
    }
    if (!known && url.host().endsWith(QLatin1String(".atlassian.net"), Qt::CaseInsensitive)) {
        address.baseUrl = serverOf(url);
        path = url.path();
        known = true;
    }
    if (!known)
        path = url.path();

    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (qsizetype i = 0; i + 1 < parts.size(); ++i) {
        const bool browse = parts.at(i) == QLatin1String("browse");
        if (!browse && !(known && parts.at(i) == QLatin1String("issues")))
            continue;
        const QString key = issueKey(parts.at(i + 1));
        if (key.isEmpty())
            continue;
        if (!known) {
            // /browse/<key> is Jira's own, so it is enough on any server.
            // Jira is installed at the path before it.
            address.baseUrl = serverOf(url);
            address.baseUrl.setPath(QLatin1Char('/') + parts.mid(0, i).join(QLatin1Char('/')));
            if (i == 0)
                address.baseUrl.setPath(QString());
        }
        address.key = key;
        return address;
    }
    if (!known)
        return std::nullopt;

    const QUrlQuery query(url);
    address.key = issueKey(query.queryItemValue(QStringLiteral("selectedIssue"), QUrl::FullyDecoded));
    if (address.key.isEmpty())
        address.jql = query.queryItemValue(QStringLiteral("jql"), QUrl::FullyDecoded).trimmed();
    if (address.key.isEmpty() && address.jql.isEmpty())
        return std::nullopt;
    return address;
}

std::optional<UrlMatch> matchUrl(const QString &typeId, const QUrl &url, const QList<QUrl> &baseUrls)
{
    const std::optional<PageAddress> address = readPageAddress(url, baseUrls);
    if (!address)
        return std::nullopt;

    UrlMatch match;
    match.baseUrl = address->baseUrl;
    if (typeId == QLatin1String("jira.issue.summary") || typeId == QLatin1String("jira.issue.page")) {
        if (address->key.isEmpty())
            return std::nullopt;
        match.config.insert(QStringLiteral("key"), address->key);
        match.name = address->key;
    } else if (typeId == QLatin1String("jira.list")) {
        if (address->jql.isEmpty())
            return std::nullopt;
        match.config.insert(QStringLiteral("preset"), QStringLiteral("jql"));
        match.config.insert(QStringLiteral("jql"), address->jql);
        match.name = QCoreApplication::translate("jira", "Jira search");
    } else {
        return std::nullopt;
    }
    return match;
}

} // namespace mirante::modules::jira
