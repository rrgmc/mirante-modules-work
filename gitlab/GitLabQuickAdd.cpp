#include "GitLabQuickAdd.h"

#include "GitLabClient.h"
#include "GitLabSettings.h"
#include "GitLabUrls.h"

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

namespace mirante::modules::gitlab {

namespace {

// The most keys a finder searches, and the most merge requests it reads per request.
constexpr int kMaxKeys = 5;
constexpr int kMaxMergeRequests = 100;

// The project path of a merge request in a list: before the "!" of
// references.full, or else from its web_url.
QString projectOf(const QJsonObject &mergeRequest)
{
    const QString full = mergeRequest.value(QStringLiteral("references")).toObject().value(QStringLiteral("full")).toString();
    const qsizetype bang = full.lastIndexOf(QLatin1Char('!'));
    if (bang > 0)
        return full.left(bang);
    const std::optional<PageAddress> address =
        readPageAddress(QUrl(mergeRequest.value(QStringLiteral("web_url")).toString()), {});
    return address ? address->project : QString();
}

// The MR summary item of project!iid. Its name leaves the project out when
// it is the task's own, as in "!42".
QuickAddItem summaryItem(const QString &project, qint64 iid, bool ownProject)
{
    QuickAddItem item;
    item.typeId = QStringLiteral("gitlab.mr.summary");
    item.name = (ownProject ? QString() : project) + QLatin1Char('!') + QString::number(iid);
    item.config.insert(QStringLiteral("project"), project);
    item.config.insert(QStringLiteral("iid"), iid);
    return item;
}

// The Jira-style keys in texts, in upper case, in the order they first appear,
// at most kMaxKeys.
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

bool holds(const QJsonObject &mergeRequest, const QString &field, const QString &key)
{
    return mergeRequest.value(field).toString().contains(key, Qt::CaseInsensitive);
}

} // namespace

void GitLabQuickAddHandler::propose(QuickAddRequest &request) const
{
    // Only a merge request's page on the profile's own server: an address on
    // another server would send its project and number to the wrong one.
    const QUrl base = baseUrlOf(request.moduleSettings(kGeneralScreen));
    const QUrl url(request.text());
    ApiClient *server = request.server();
    if (!server || !base.isValid() || base.host().isEmpty() || !pathBelow(url, base)) {
        request.finish(std::nullopt);
        return;
    }
    const std::optional<PageAddress> address = readPageAddress(url, {base});
    if (!address || address->kind != QLatin1String("merge_requests") || address->number == 0) {
        request.finish(std::nullopt);
        return;
    }

    const QString project = address->project;
    const qint64 iid = address->number;
    QuickAddRequest *target = &request;
    GitLabClient(*server, request.owner()).mergeRequest(project, iid, [target, project, iid](const ApiReply &reply) {
        if (!reply.ok()) {
            target->finish(std::nullopt);
            return;
        }
        const QJsonObject mergeRequest = reply.json.object();

        QuickAddProposal proposal;
        proposal.title = mergeRequest.value(QStringLiteral("title")).toString().trimmed();
        proposal.items.push_back(summaryItem(project, iid, true));

        QuickAddItem pipeline;
        pipeline.typeId = QStringLiteral("gitlab.pipeline");
        pipeline.name = QLatin1Char('!') + QString::number(iid) + QStringLiteral(" CI");
        pipeline.config.insert(QStringLiteral("project"), project);
        pipeline.config.insert(QStringLiteral("branchSource"), QStringLiteral("mergeRequest"));
        pipeline.config.insert(QStringLiteral("iid"), iid);
        proposal.items.push_back(pipeline);

        proposal.project = ProjectMatch{kProjectSettingKey, project};
        for (const QString &text : {proposal.title, mergeRequest.value(QStringLiteral("source_branch")).toString()}) {
            if (!text.isEmpty())
                proposal.texts << text;
        }
        target->finish(std::move(proposal));
    });
}

void GitLabRelatedItemFinder::find(RelatedItemRequest &request) const
{
    const QStringList keys = keysIn(request.texts());
    ApiClient *server = request.server();
    if (keys.isEmpty() || !server) {
        request.finish({});
        return;
    }

    // The answers by key, kept in the keys' order, so the items come out in a
    // fixed order whatever order the replies arrive in.
    struct Answers
    {
        QString ownProject;
        std::vector<QList<QJsonObject>> found;
        qsizetype waiting = 0;
    };
    auto answers = std::make_shared<Answers>();
    answers->ownProject = projectPath({}, request.projectSetting(kProjectSettingKey));
    answers->found.resize(size_t(keys.size()));

    RelatedItemRequest *target = &request;
    const auto finishWhenDone = [target, answers] {
        if (--answers->waiting > 0)
            return;
        std::vector<QuickAddItem> items;
        QSet<QString> seen;
        for (const QList<QJsonObject> &mergeRequests : answers->found) {
            for (const QJsonObject &mergeRequest : mergeRequests) {
                const QString project = projectOf(mergeRequest);
                const qint64 iid = mergeRequest.value(QStringLiteral("iid")).toInteger();
                if (project.isEmpty() || iid <= 0)
                    continue;
                const QString id = project.toLower() + QLatin1Char('!') + QString::number(iid);
                if (seen.contains(id))
                    continue;
                seen.insert(id);
                const bool own = project.compare(answers->ownProject, Qt::CaseInsensitive) == 0;
                items.push_back(summaryItem(project, iid, own));
            }
        }
        target->finish(std::move(items));
    };

    const GitLabClient client(*server, request.owner());
    if (!answers->ownProject.isEmpty()) {
        // One list of the project's open merge requests serves every key.
        QUrlQuery open;
        open.addQueryItem(QStringLiteral("state"), QStringLiteral("opened"));
        open.addQueryItem(QStringLiteral("order_by"), QStringLiteral("updated_at"));
        open.addQueryItem(QStringLiteral("sort"), QStringLiteral("desc"));
        answers->waiting = 1;
        client.projectMergeRequests(answers->ownProject, open, kMaxMergeRequests,
                                    [answers, keys, finishWhenDone](const ApiReply &reply) {
                                        const QJsonArray list = reply.ok() ? reply.json.array() : QJsonArray();
                                        for (qsizetype i = 0; i < keys.size(); ++i) {
                                            for (const QJsonValue &value : list) {
                                                const QJsonObject mergeRequest = value.toObject();
                                                if (holds(mergeRequest, QStringLiteral("title"), keys.at(i))
                                                    || holds(mergeRequest, QStringLiteral("source_branch"), keys.at(i)))
                                                    answers->found[size_t(i)] << mergeRequest;
                                            }
                                        }
                                        finishWhenDone();
                                    });
        return;
    }

    // Without a project, GitLab searches the titles of every project, one
    // request per key.
    answers->waiting = keys.size();
    for (qsizetype i = 0; i < keys.size(); ++i) {
        QUrlQuery search;
        search.addQueryItem(QStringLiteral("scope"), QStringLiteral("all"));
        search.addQueryItem(QStringLiteral("state"), QStringLiteral("opened"));
        search.addQueryItem(QStringLiteral("search"), keys.at(i));
        search.addQueryItem(QStringLiteral("in"), QStringLiteral("title"));
        client.mergeRequests(search, kMaxMergeRequests, [answers, key = keys.at(i), i, finishWhenDone](const ApiReply &reply) {
            if (reply.ok()) {
                // GitLab's search also matches words apart, so the key is checked again.
                for (const QJsonValue &value : reply.json.array()) {
                    const QJsonObject mergeRequest = value.toObject();
                    if (holds(mergeRequest, QStringLiteral("title"), key))
                        answers->found[size_t(i)] << mergeRequest;
                }
            }
            finishWhenDone();
        });
    }
}

} // namespace mirante::modules::gitlab
