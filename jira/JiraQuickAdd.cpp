#include "JiraQuickAdd.h"

#include "JiraClient.h"
#include "JiraSettings.h"
#include "JiraUrls.h"

#include <mirante/ApiClient.h>
#include <mirante/UrlMatch.h>

#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace mirante::modules::jira {

namespace {

// The most keys the finder asks the server about.
constexpr int kMaxFoundKeys = 5;

const QString kSummaryType = QStringLiteral("jira.issue.summary");

// The text is a bare key, such as "proj-123". The key in upper case, or empty.
QString bareKey(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z][A-Za-z0-9_]+-\\d+$"));
    const QString key = text.trimmed();
    return pattern.match(key).hasMatch() ? key.toUpper() : QString();
}

// The keys in text, such as ABC-123 in "feature/abc-123-login", in upper case
// and in the order they appear. A key does not start or end inside a word.
QStringList keysIn(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral("(?<![A-Za-z0-9_])[A-Za-z][A-Za-z0-9_]+-\\d+(?![0-9])"));
    QStringList keys;
    for (auto match = pattern.globalMatch(text); match.hasNext();)
        keys << match.next().captured(0).toUpper();
    return keys;
}

// The project part of a key: "PROJ" of "PROJ-123".
QString prefixOf(const QString &key) { return key.left(key.lastIndexOf(QLatin1Char('-'))); }

// The Jira Issue (summary) item of a key, as the item dialog makes it from
// the issue's address.
QuickAddItem summaryItem(const QString &key)
{
    return {kSummaryType, key, QJsonObject{{QStringLiteral("key"), key}}};
}

// The issue's summary only: the request needs nothing else.
void askForIssue(ApiClient &server, const QJsonObject &general, const QString &key, QObject *receiver,
                 JiraDone done)
{
    JiraClient(server, deploymentOf(general))
        .issue(key, {QStringLiteral("summary")}, receiver, std::move(done), QStringList());
}

} // namespace

void JiraQuickAddHandler::propose(QuickAddRequest &request) const
{
    const QJsonObject general = request.moduleSettings(kGeneralScreen);
    const QUrl site = baseUrlOf(general);
    ApiClient *server = request.server();
    if (site.isEmpty() || !server) {
        request.finish(std::nullopt);
        return;
    }

    // A bare key, else an issue's address on the profile's site, read as
    // fromUrl() reads it. An address on another site belongs to another
    // profile, so its key is not sent here.
    QString key = bareKey(request.text());
    if (key.isEmpty()) {
        const QUrl url(request.text());
        const std::optional<UrlMatch> match = matchUrl(kSummaryType, url, {site});
        if (match && pathBelow(url, site))
            key = match->config.value(QStringLiteral("key")).toString();
    }
    if (key.isEmpty()) {
        request.finish(std::nullopt);
        return;
    }

    QuickAddRequest *owner = &request;
    askForIssue(*server, general, key, request.owner(), [owner, key](const ApiReply &reply) {
        if (!reply.ok()) {
            owner->finish(std::nullopt);
            return;
        }
        const QString summary = reply.json.object()
                                    .value(QStringLiteral("fields"))
                                    .toObject()
                                    .value(QStringLiteral("summary"))
                                    .toString()
                                    .trimmed();
        QuickAddProposal proposal;
        proposal.title = summary.isEmpty() ? key : summary;
        proposal.items.push_back(summaryItem(key));
        proposal.project = ProjectMatch{QStringLiteral("projectKey"), prefixOf(key)};
        proposal.texts = QStringList{key};
        owner->finish(std::move(proposal));
    });
}

namespace {

// One run of the finder: the keys asked about, the answers so far, in the
// order of the keys, and how many are still to come.
struct Finding
{
    RelatedItemRequest *request = nullptr;
    QStringList keys;
    std::vector<bool> found;
    int pending = 0;
};

} // namespace

void JiraRelatedItemFinder::find(RelatedItemRequest &request) const
{
    const QJsonObject general = request.moduleSettings(kGeneralScreen);
    ApiClient *server = request.server();
    if (baseUrlOf(general).isEmpty() || !server) {
        request.finish({});
        return;
    }

    // The project's keys only, if it names its Jira project.
    const QString projectKey = request.projectSetting(QStringLiteral("projectKey")).toString().trimmed();
    QStringList keys;
    QSet<QString> seen;
    for (const QString &text : request.texts()) {
        for (const QString &key : keysIn(text)) {
            if (keys.size() >= kMaxFoundKeys || seen.contains(key))
                continue;
            if (!projectKey.isEmpty() && prefixOf(key).compare(projectKey, Qt::CaseInsensitive) != 0)
                continue;
            seen.insert(key);
            keys << key;
        }
    }
    if (keys.isEmpty()) {
        request.finish({});
        return;
    }

    auto finding = std::make_shared<Finding>();
    finding->request = &request;
    finding->keys = keys;
    finding->found.assign(size_t(keys.size()), false);
    finding->pending = int(keys.size());
    for (qsizetype i = 0; i < keys.size(); ++i) {
        askForIssue(*server, general, keys.at(i), request.owner(), [finding, i](const ApiReply &reply) {
            finding->found[size_t(i)] = reply.ok();
            if (--finding->pending > 0)
                return;
            std::vector<QuickAddItem> items;
            for (qsizetype k = 0; k < finding->keys.size(); ++k) {
                if (finding->found[size_t(k)])
                    items.push_back(summaryItem(finding->keys.at(k)));
            }
            finding->request->finish(std::move(items));
        });
    }
}

} // namespace mirante::modules::jira
