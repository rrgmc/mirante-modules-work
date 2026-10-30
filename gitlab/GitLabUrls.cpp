#include "GitLabUrls.h"

#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QUrlQuery>

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

// <baseUrl>/<project>/-/<rest>, without doubling a slash at the end of baseUrl.
QUrl projectPageUrl(const QUrl &baseUrl, const QString &project, const QString &rest)
{
    QString path = baseUrl.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    path += QLatin1Char('/') + project + QStringLiteral("/-/") + rest;

    QUrl url = baseUrl;
    url.setPath(path);
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

// <baseUrl>/<project>/-/<kind>/<iid>.
QUrl pageUrl(const QUrl &baseUrl, const QJsonObject &config, const QJsonValue &defaultProject,
             const QString &kind)
{
    const QString project = projectPath(config, defaultProject);
    const qint64 iid = itemNumber(config);
    if (project.isEmpty() || iid == 0)
        return {};
    return projectPageUrl(baseUrl, project, kind + QLatin1Char('/') + QString::number(iid));
}

} // namespace

QString projectPath(const QJsonObject &config, const QJsonValue &defaultProject)
{
    const QString project = trimSlashes(config.value(QStringLiteral("project")).toString());
    if (!project.isEmpty())
        return project;
    return trimSlashes(defaultProject.toString());
}

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

QUrl mergeRequestUrl(const QUrl &baseUrl, const QJsonObject &config, const QJsonValue &defaultProject)
{
    return pageUrl(baseUrl, config, defaultProject, QStringLiteral("merge_requests"));
}

QUrl issueUrl(const QUrl &baseUrl, const QJsonObject &config, const QJsonValue &defaultProject)
{
    return pageUrl(baseUrl, config, defaultProject, QStringLiteral("issues"));
}

std::optional<PageAddress> readPageAddress(const QUrl &url, const QList<QUrl> &baseUrls)
{
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        || url.host().isEmpty())
        return std::nullopt;

    // The longest base wins, so a server at a path is preferred to its host.
    PageAddress address;
    QString path;
    bool known = false;
    for (const QUrl &base : baseUrls) {
        const std::optional<QString> below = pathBelow(url, base);
        if (below && (!known || trimSlashes(base.path()).size() > trimSlashes(address.baseUrl.path()).size())) {
            address.baseUrl = base;
            path = *below;
            known = true;
        }
    }
    if (!known) {
        // The "/-/" in the path is GitLab's own, so it is enough on any server.
        QUrl server;
        server.setScheme(scheme);
        server.setHost(url.host());
        server.setPort(url.port());
        address.baseUrl = server;
        path = trimSlashes(url.path());
    }

    const qsizetype dash = path.indexOf(QStringLiteral("/-/"));
    if (dash <= 0)
        return std::nullopt;
    address.project = path.left(dash);
    if (!address.project.contains(QLatin1Char('/')))
        return std::nullopt;

    const QStringList rest = path.mid(dash + 3).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (rest.isEmpty())
        return std::nullopt;
    address.kind = rest.first();
    if (address.kind != QLatin1String("merge_requests") && address.kind != QLatin1String("issues")
        && address.kind != QLatin1String("pipelines"))
        return std::nullopt;
    if (rest.size() > 1) {
        // A tab of the page, such as /diffs, may follow the number. Words,
        // such as "new", are not the page of one item.
        bool ok = false;
        address.number = rest.at(1).toLongLong(&ok);
        if (!ok || address.number <= 0)
            return std::nullopt;
    }
    if (address.kind == QLatin1String("pipelines"))
        address.ref = QUrlQuery(url).queryItemValue(QStringLiteral("ref"), QUrl::FullyDecoded).trimmed();
    return address;
}

std::optional<UrlMatch> matchUrl(const QString &typeId, const QUrl &url, const QList<QUrl> &baseUrls)
{
    const std::optional<PageAddress> address = readPageAddress(url, baseUrls);
    if (!address)
        return std::nullopt;

    const QString project = address->project;
    const bool mergeRequest = address->kind == QLatin1String("merge_requests");
    const bool issue = address->kind == QLatin1String("issues");
    const bool pipelines = address->kind == QLatin1String("pipelines");
    const bool one = address->number > 0;
    const QString number = QString::number(address->number);

    UrlMatch match;
    match.baseUrl = address->baseUrl;
    match.config.insert(QStringLiteral("project"), project);
    if (typeId == QLatin1String("gitlab.mr.summary") || typeId == QLatin1String("gitlab.mr.page")) {
        if (!mergeRequest || !one)
            return std::nullopt;
        match.config.insert(QStringLiteral("iid"), address->number);
        match.name = project + QLatin1Char('!') + number;
    } else if (typeId == QLatin1String("gitlab.issue.summary") || typeId == QLatin1String("gitlab.issue.page")) {
        if (!issue || !one)
            return std::nullopt;
        match.config.insert(QStringLiteral("iid"), address->number);
        match.name = project + QLatin1Char('#') + number;
    } else if (typeId == QLatin1String("gitlab.mr.list") || typeId == QLatin1String("gitlab.issue.list")) {
        const bool wanted = typeId == QLatin1String("gitlab.mr.list") ? mergeRequest : issue;
        if (!wanted || one)
            return std::nullopt;
        match.config.insert(QStringLiteral("scope"), QStringLiteral("project"));
        match.config.insert(QStringLiteral("preset"), QStringLiteral("allOpen"));
        match.name = project + (mergeRequest ? QStringLiteral(" MRs") : QStringLiteral(" issues"));
    } else if (typeId == QLatin1String("gitlab.pipeline")) {
        // The type follows a branch, not one pipeline, so a pipeline's id is
        // not kept. A merge request's page follows that merge request.
        if (mergeRequest && one) {
            match.config.insert(QStringLiteral("branchSource"), QStringLiteral("mergeRequest"));
            match.config.insert(QStringLiteral("iid"), address->number);
            match.name = project + QLatin1Char('!') + number + QStringLiteral(" CI");
        } else if (pipelines) {
            const QString branch = address->ref.isEmpty() ? kDefaultBranch : address->ref;
            match.config.insert(QStringLiteral("branchSource"), QStringLiteral("fixed"));
            match.config.insert(QStringLiteral("branch"), branch);
            match.name = project + QLatin1Char(' ') + branch + QStringLiteral(" CI");
        } else {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }
    return match;
}

} // namespace mirante::modules::gitlab
