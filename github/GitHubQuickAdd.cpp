#include "GitHubQuickAdd.h"

#include "GitHubClient.h"
#include "GitHubSettings.h"
#include "GitHubUrls.h"

#include <mirante/ApiClient.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace mirante::modules::github {

namespace {

// The most keys a finder searches, and the most pull requests it reads.
constexpr int kMaxKeys = 5;
constexpr int kMaxPullRequests = 100;

// The PR summary item of repository#number. Its name leaves the repository
// out when it is the task's own, as in "#42".
QuickAddItem summaryItem(const QString &repository, qint64 number, bool ownRepository)
{
    QuickAddItem item;
    item.typeId = QStringLiteral("github.pr.summary");
    item.name = (ownRepository ? QString() : repository) + QLatin1Char('#') + QString::number(number);
    item.config.insert(QStringLiteral("repository"), repository);
    item.config.insert(QStringLiteral("number"), number);
    return item;
}

// The Jira-style keys in texts, in upper case, in the order they first appear,
// at most kMaxKeys. The same pattern as the gitlab module's.
QStringList keysIn(const QStringList &texts)
{
    static const QRegularExpression pattern(QStringLiteral("[A-Za-z][A-Za-z0-9_]+-\\d+"));
    QStringList keys;
    for (const QString &text : texts) {
        QRegularExpressionMatchIterator it = pattern.globalMatch(text);
        while (it.hasNext()) {
            const QString key = it.next().captured().toUpper();
            if (keys.contains(key))
                continue;
            keys << key;
            if (keys.size() == kMaxKeys)
                return keys;
        }
    }
    return keys;
}

bool holds(const QString &text, const QString &key)
{
    return text.contains(key, Qt::CaseInsensitive);
}

QString headRef(const QJsonObject &pullRequest)
{
    return pullRequest.value(QStringLiteral("head")).toObject().value(QStringLiteral("ref")).toString();
}

} // namespace

void GitHubQuickAddHandler::propose(QuickAddRequest &request) const
{
    // A pull request's page below the profile's web address, or on
    // github.com, which readPageAddress() always reads.
    const QUrl base = webBaseOf(request.moduleSettings(kGeneralScreen));
    QList<QUrl> bases;
    if (base.isValid() && !base.host().isEmpty())
        bases << base;
    ApiClient *server = request.server();
    const std::optional<PageAddress> address = readPageAddress(QUrl(request.text().trimmed()), bases);
    if (!server || !address || address->kind != QLatin1String("pull") || address->number <= 0
        || address->repository.isEmpty()) {
        request.finish(std::nullopt);
        return;
    }

    const QString repository = address->repository;
    const qint64 number = address->number;
    QuickAddRequest *target = &request;
    GitHubClient(*server, request.owner())
        .pullRequest(repository, number, [target, repository, number](const ApiReply &reply) {
            if (!reply.ok()) {
                target->finish(std::nullopt);
                return;
            }
            const QJsonObject pullRequest = reply.json.object();

            QuickAddProposal proposal;
            proposal.title = pullRequest.value(QStringLiteral("title")).toString().trimmed();
            proposal.items.push_back(summaryItem(repository, number, true));

            QuickAddItem run;
            run.typeId = QStringLiteral("github.run");
            run.name = QLatin1Char('#') + QString::number(number) + QStringLiteral(" CI");
            run.config.insert(QStringLiteral("repository"), repository);
            run.config.insert(QStringLiteral("branchSource"), QStringLiteral("pullRequest"));
            run.config.insert(QStringLiteral("number"), number);
            proposal.items.push_back(run);

            proposal.project = ProjectMatch{kProjectSettingKey, repository};
            for (const QString &text : {proposal.title, headRef(pullRequest)}) {
                if (!text.isEmpty())
                    proposal.texts << text;
            }
            target->finish(std::move(proposal));
        });
}

void GitHubRelatedItemFinder::find(RelatedItemRequest &request) const
{
    const QStringList keys = keysIn(request.texts());
    ApiClient *server = request.server();
    if (keys.isEmpty() || !server) {
        request.finish({});
        return;
    }

    const QString ownRepository = repositoryOf({}, request.projectSetting(kProjectSettingKey));
    const GitHubClient client(*server, request.owner());
    RelatedItemRequest *target = &request;

    // The matches of one reply, by key and in the keys' order, each pull
    // request once.
    const auto finishWith = [target, keys, ownRepository](const std::vector<QList<QJsonObject>> &found) {
        std::vector<QuickAddItem> items;
        QSet<QString> seen;
        for (const QList<QJsonObject> &rows : found) {
            for (const QJsonObject &row : rows) {
                const QString repository = ownRepository.isEmpty()
                    ? GitHubClient::repositoryOfApiUrl(row.value(QStringLiteral("repository_url")).toString())
                    : ownRepository;
                const qint64 number = row.value(QStringLiteral("number")).toInteger();
                if (repository.isEmpty() || number <= 0)
                    continue;
                const QString id = repository.toLower() + QLatin1Char('#') + QString::number(number);
                if (seen.contains(id))
                    continue;
                seen.insert(id);
                const bool own = repository.compare(ownRepository, Qt::CaseInsensitive) == 0;
                items.push_back(summaryItem(repository, number, own));
            }
        }
        target->finish(std::move(items));
    };

    if (!ownRepository.isEmpty()) {
        // One list of the repository's open pull requests serves every key.
        QUrlQuery open;
        open.addQueryItem(QStringLiteral("state"), QStringLiteral("open"));
        open.addQueryItem(QStringLiteral("sort"), QStringLiteral("updated"));
        open.addQueryItem(QStringLiteral("direction"), QStringLiteral("desc"));
        client.repositoryPullRequests(
            ownRepository, open, kMaxPullRequests, [keys, finishWith](const ApiReply &reply) {
                const QJsonArray list = reply.ok() ? reply.json.array() : QJsonArray();
                std::vector<QList<QJsonObject>> found(size_t(keys.size()));
                for (qsizetype i = 0; i < keys.size(); ++i) {
                    for (const QJsonValue &value : list) {
                        const QJsonObject pullRequest = value.toObject();
                        if (holds(pullRequest.value(QStringLiteral("title")).toString(), keys.at(i))
                            || holds(headRef(pullRequest), keys.at(i)))
                            found[size_t(i)] << pullRequest;
                    }
                }
                finishWith(found);
            });
        return;
    }

    // Without a repository, one search over the profile's repositories. A
    // search without "repo:" would cover all of GitHub, so none are asked.
    const QStringList repositories = repositoryNames(request.moduleSettings(kGeneralScreen));
    if (repositories.isEmpty()) {
        request.finish({});
        return;
    }
    QString query = QStringLiteral("is:pr is:open in:title (") + keys.join(QStringLiteral(" OR ")) + QLatin1Char(')');
    for (const QString &repository : repositories)
        query += QStringLiteral(" repo:") + repository;
    client.searchIssues(query, kMaxPullRequests, [keys, finishWith](const ApiReply &reply) {
        const QJsonArray list =
            reply.ok() ? reply.json.object().value(QStringLiteral("items")).toArray() : QJsonArray();
        std::vector<QList<QJsonObject>> found(size_t(keys.size()));
        for (qsizetype i = 0; i < keys.size(); ++i) {
            // GitHub's search also matches words apart, so the key is checked again.
            for (const QJsonValue &value : list) {
                const QJsonObject pullRequest = value.toObject();
                if (holds(pullRequest.value(QStringLiteral("title")).toString(), keys.at(i)))
                    found[size_t(i)] << pullRequest;
            }
        }
        finishWith(found);
    });
}

} // namespace mirante::modules::github
