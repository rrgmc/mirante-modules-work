#include "GitLabUrls.h"

#include <QString>

namespace mirante::modules::gitlab {

namespace {

QString trimSlashes(QString text)
{
    text = text.trimmed();
    while (text.startsWith(QLatin1Char('/')))
        text.remove(0, 1);
    while (text.endsWith(QLatin1Char('/')))
        text.chop(1);
    return text;
}

// The "iid" key as a positive number, or 0. The form stores a number, but a
// file edited by hand may hold the number as text.
qint64 itemNumber(const QJsonObject &config)
{
    const QJsonValue value = config.value(QStringLiteral("iid"));
    if (value.isDouble())
        return value.toInteger() > 0 ? value.toInteger() : 0;
    if (value.isString()) {
        bool ok = false;
        const qint64 number = value.toString().trimmed().toLongLong(&ok);
        return ok && number > 0 ? number : 0;
    }
    return 0;
}

// <baseUrl>/<project>/-/<kind>/<iid>, without doubling a slash at the end of baseUrl.
QUrl pageUrl(const QUrl &baseUrl, const QJsonObject &config, const QJsonValue &defaultProject,
             const QString &kind)
{
    const QString project = projectPath(config, defaultProject);
    const qint64 iid = itemNumber(config);
    if (project.isEmpty() || iid == 0)
        return {};

    QString path = baseUrl.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    path += QLatin1Char('/') + project + QStringLiteral("/-/") + kind + QLatin1Char('/') + QString::number(iid);

    QUrl url = baseUrl;
    url.setPath(path);
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

} // namespace

QString projectPath(const QJsonObject &config, const QJsonValue &defaultProject)
{
    const QString project = trimSlashes(config.value(QStringLiteral("project")).toString());
    if (!project.isEmpty())
        return project;
    return trimSlashes(defaultProject.toString());
}

QUrl mergeRequestUrl(const QUrl &baseUrl, const QJsonObject &config, const QJsonValue &defaultProject)
{
    return pageUrl(baseUrl, config, defaultProject, QStringLiteral("merge_requests"));
}

QUrl issueUrl(const QUrl &baseUrl, const QJsonObject &config, const QJsonValue &defaultProject)
{
    return pageUrl(baseUrl, config, defaultProject, QStringLiteral("issues"));
}

} // namespace mirante::modules::gitlab
