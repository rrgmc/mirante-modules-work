#include "GitHubUrls.h"

#include "GitHubSettings.h"

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
        if (rest.size() >= 2 && rest.first() == QLatin1String("workflows")) {
            address.workflow = rest.at(1);
        } else if (rest.size() >= 2 && rest.first() == QLatin1String("runs")) {
            address.number = numberIn(rest.at(1));
            if (address.number == 0)
                return std::nullopt;
        } else if (!rest.isEmpty()) {
            return std::nullopt;
        }
    } else if (address.kind == QLatin1String("tree")) {
        // A branch with a slash is taken whole.
        address.branch = QUrl::fromPercentEncoding(rest.join(QLatin1Char('/')).toUtf8());
        if (address.branch.isEmpty())
            return std::nullopt;
    } else {
        return std::nullopt;
    }
    return address;
}

std::optional<UrlMatch> matchUrl(const QString &typeId, const QUrl &url, const QList<QUrl> &webBases)
{
    // Stub until task T2 of the M11 plan fills the table of
    // README.md#items-from-a-link.
    Q_UNUSED(typeId)
    Q_UNUSED(url)
    Q_UNUSED(webBases)
    return std::nullopt;
}

} // namespace mirante::modules::github
