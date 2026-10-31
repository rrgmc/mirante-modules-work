#include "GitHubUrls.h"

#include "GitHubSettings.h"

#include <QCoreApplication>
#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace mirante::modules::github {

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

// <webBase>/<repository>/<kind>/<number>.
QUrl pageUrl(const QUrl &webBase, const QJsonObject &config, const QJsonValue &defaultRepository,
             const QString &kind)
{
    const QString repository = repositoryOf(config, defaultRepository);
    const qint64 number = itemNumber(config);
    if (repository.isEmpty() || number == 0)
        return {};
    return webPageUrl(webBase, repository + QLatin1Char('/') + kind + QLatin1Char('/') + QString::number(number));
}

bool isGitHubCom(const QUrl &url)
{
    const QString host = url.host().toLower();
    return host == QLatin1String("github.com") || host == QLatin1String("www.github.com");
}

// A positive number, or 0.
qint64 numberIn(const QString &text)
{
    bool ok = false;
    const qint64 number = text.toLongLong(&ok);
    return ok && number > 0 ? number : 0;
}

} // namespace

QString repositoryOf(const QJsonObject &config, const QJsonValue &defaultRepository)
{
    const QString repository = trimSlashes(config.value(QStringLiteral("repository")).toString());
    if (!repository.isEmpty())
        return repository;
    return trimSlashes(defaultRepository.toString());
}

QString configuredBranch(const QJsonObject &config)
{
    return config.value(QStringLiteral("branch")).toString().trimmed();
}

QString configuredWorkflow(const QJsonObject &config)
{
    return config.value(QStringLiteral("workflow")).toString().trimmed();
}

qint64 itemNumber(const QJsonObject &config)
{
    const QJsonValue value = config.value(QStringLiteral("number"));
    if (value.isDouble())
        return value.toInteger() > 0 ? value.toInteger() : 0;
    if (value.isString())
        return numberIn(value.toString().trimmed());
    return 0;
}

QUrl pullRequestUrl(const QUrl &webBase, const QJsonObject &config, const QJsonValue &defaultRepository)
{
    return pageUrl(webBase, config, defaultRepository, QStringLiteral("pull"));
}

QUrl issueUrl(const QUrl &webBase, const QJsonObject &config, const QJsonValue &defaultRepository)
{
    return pageUrl(webBase, config, defaultRepository, QStringLiteral("issues"));
}

QUrl webPageUrl(const QUrl &webBase, const QString &path)
{
    QString full = webBase.path();
    while (full.endsWith(QLatin1Char('/')))
        full.chop(1);
    full += QLatin1Char('/') + trimSlashes(path);

    QUrl url = webBase;
    url.setPath(full);
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

std::optional<PageAddress> readPageAddress(const QUrl &url, const QList<QUrl> &webBases)
{
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        || url.host().isEmpty())
        return std::nullopt;

    // The longest base wins, so a server at a path is preferred to its host.
    PageAddress address;
    QString path;
    bool known = false;
    for (const QUrl &base : webBases) {
        const std::optional<QString> below = pathBelow(url, base);
        if (below && (!known || trimSlashes(base.path()).size() > trimSlashes(address.webBase.path()).size())) {
            address.webBase = base;
            path = *below;
            known = true;
        }
    }
    if (!known) {
        if (!isGitHubCom(url))
            return std::nullopt;
        address.webBase = QUrl(kDefaultBaseUrl);
        path = trimSlashes(url.path());
    }

    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return std::nullopt;

    // On the web base itself: the user's own pull requests and issues.
    const QString first = parts.first();
    if (first == QLatin1String("pulls") || first == QLatin1String("issues")) {
        if (parts.size() > 2)
            return std::nullopt;
        address.kind = first;
        if (parts.size() == 2) {
            address.filter = parts.at(1);
            if (address.filter != QLatin1String("assigned")
                && !(first == QLatin1String("pulls") && address.filter == QLatin1String("review-requested")))
                return std::nullopt;
        }
        return address;
    }

    if (parts.size() < 3)
        return std::nullopt;
    address.repository = parts.at(0) + QLatin1Char('/') + parts.at(1);
    address.kind = parts.at(2);
    const QStringList rest = parts.mid(3);

    if (address.kind == QLatin1String("pull")) {
        // A tab, such as /files, may follow the number. Words, such as
        // "new", are not the page of one pull request.
        address.number = rest.isEmpty() ? 0 : numberIn(rest.first());
        if (address.number == 0)
            return std::nullopt;
    } else if (address.kind == QLatin1String("issues")) {
        if (!rest.isEmpty()) {
            address.number = numberIn(rest.first());
            if (address.number == 0)
                return std::nullopt;
        }
    } else if (address.kind == QLatin1String("pulls")) {
        if (!rest.isEmpty())
            return std::nullopt;
    } else if (address.kind == QLatin1String("actions")) {
        if (rest.size() == 2 && rest.first() == QLatin1String("workflows")) {
            address.workflow = rest.at(1);
        } else if (rest.size() >= 2 && rest.first() == QLatin1String("runs")) {
            address.number = numberIn(rest.at(1));
            if (address.number == 0)
                return std::nullopt;
        } else if (!rest.isEmpty()) {
            return std::nullopt;
        }
    } else if (address.kind == QLatin1String("tree")) {
        // A branch with a slash is taken whole. QUrl::path() has already
        // decoded it, so it is not decoded again.
        address.branch = rest.join(QLatin1Char('/'));
        if (address.branch.isEmpty())
            return std::nullopt;
    } else {
        return std::nullopt;
    }
    return address;
}

std::optional<UrlMatch> matchUrl(const QString &typeId, const QUrl &url, const QList<QUrl> &webBases)
{
    const std::optional<PageAddress> address = readPageAddress(url, webBases);
    if (!address)
        return std::nullopt;

    const QString &repository = address->repository;
    const QString &kind = address->kind;
    const bool own = repository.isEmpty();
    const bool pullRequest = kind == QLatin1String("pull");
    const bool issue = kind == QLatin1String("issues") && address->number > 0;
    const bool actions = kind == QLatin1String("actions");
    const bool oneRun = actions && address->number > 0;
    const QString number = QString::number(address->number);
    // " <file>" in the names of a workflow's run and run list.
    const QString workflow = address->workflow.isEmpty() ? QString() : QLatin1Char(' ') + address->workflow;

    UrlMatch match;
    match.baseUrl = address->webBase;
    if (!own)
        match.config.insert(QStringLiteral("repository"), repository);

    if (typeId == QLatin1String("github.pr.summary") || typeId == QLatin1String("github.pr.page")) {
        if (!pullRequest)
            return std::nullopt;
        match.config.insert(QStringLiteral("number"), address->number);
        match.name = repository + QLatin1Char('#') + number;
    } else if (typeId == QLatin1String("github.issue.summary") || typeId == QLatin1String("github.issue.page")) {
        if (!issue)
            return std::nullopt;
        match.config.insert(QStringLiteral("number"), address->number);
        match.name = repository + QLatin1Char('#') + number;
    } else if (typeId == QLatin1String("github.pr.list") || typeId == QLatin1String("github.issue.list")) {
        const bool pulls = typeId == QLatin1String("github.pr.list");
        if (kind != (pulls ? QLatin1String("pulls") : QLatin1String("issues")) || address->number > 0)
            return std::nullopt;
        if (own) {
            // On the web base itself: the user's own pull requests or issues,
            // in all repositories.
            QString preset = QStringLiteral("createdByMe");
            if (address->filter == QLatin1String("assigned")) {
                preset = QStringLiteral("assignedToMe");
                match.name = pulls ? QCoreApplication::translate("github", "PRs assigned to me")
                                   : QCoreApplication::translate("github", "Issues assigned to me");
            } else if (address->filter == QLatin1String("review-requested")) {
                preset = QStringLiteral("reviewRequested");
                match.name = QCoreApplication::translate("github", "PRs to review");
            } else {
                match.name = pulls ? QCoreApplication::translate("github", "My PRs")
                                   : QCoreApplication::translate("github", "My issues");
            }
            match.config.insert(QStringLiteral("scope"), QStringLiteral("all"));
            match.config.insert(QStringLiteral("preset"), preset);
        } else {
            match.config.insert(QStringLiteral("scope"), QStringLiteral("repository"));
            match.config.insert(QStringLiteral("preset"), QStringLiteral("allOpen"));
            match.name = repository + (pulls ? QStringLiteral(" PRs") : QStringLiteral(" issues"));
        }
    } else if (typeId == QLatin1String("github.run")) {
        // The type follows a branch, not one run, so the page of one run is
        // not taken. A pull request's page follows that pull request.
        if (pullRequest) {
            match.config.insert(QStringLiteral("branchSource"), QStringLiteral("pullRequest"));
            match.config.insert(QStringLiteral("number"), address->number);
            match.name = repository + QLatin1Char('#') + number + QStringLiteral(" CI");
        } else if (actions && !oneRun) {
            // The default branch, of all workflows or of one.
            match.config.insert(QStringLiteral("branchSource"), QStringLiteral("fixed"));
            match.config.insert(QStringLiteral("branch"), QString());
            if (!address->workflow.isEmpty())
                match.config.insert(QStringLiteral("workflow"), address->workflow);
            match.name = repository + workflow + QStringLiteral(" CI");
        } else if (kind == QLatin1String("tree")) {
            match.config.insert(QStringLiteral("branchSource"), QStringLiteral("fixed"));
            match.config.insert(QStringLiteral("branch"), address->branch);
            match.name = repository + QLatin1Char(' ') + address->branch + QStringLiteral(" CI");
        } else {
            return std::nullopt;
        }
    } else if (typeId == QLatin1String("github.run.list")) {
        // The page of one run shows the repository's runs, so it gives the
        // list of all of them; the run's id is not kept.
        if (!actions)
            return std::nullopt;
        match.config.insert(QStringLiteral("source"), QStringLiteral("all"));
        if (!address->workflow.isEmpty())
            match.config.insert(QStringLiteral("workflow"), address->workflow);
        match.name = repository + workflow + QStringLiteral(" runs");
    } else {
        return std::nullopt;
    }
    return match;
}

} // namespace mirante::modules::github
