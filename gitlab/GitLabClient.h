// The GitLab REST API v4, one method per endpoint the module uses, over the
// ApiClient of a profile's server. See README.md#the-client.
#pragma once

#include <mirante/ApiClient.h>

#include <QString>
#include <QUrlQuery>

#include <functional>

class QObject;

namespace mirante::modules::gitlab {

// A thin, copyable wrapper: it builds the paths, queries and bodies, and
// passes each ApiReply on unchanged. Project and group paths, such as
// "group/project", are percent-encoded into the path. Lists follow the Link
// header up to maxRows rows. Callbacks are dropped if receiver is destroyed.
class GitLabClient
{
public:
    using Done = std::function<void(const ApiReply &)>;

    GitLabClient(ApiClient &server, QObject *receiver);

    // A project or group path as an :id of the API: "group/project" becomes
    // "group%2Fproject".
    static QString encodePath(const QString &path);

    void currentUser(Done done) const;

    void mergeRequest(const QString &project, qint64 iid, Done done) const;
    void approvals(const QString &project, qint64 iid, Done done) const;
    // The merge requests visible to the user, of one project, or of one
    // group. filter holds the API's parameters, such as state=opened.
    void mergeRequests(const QUrlQuery &filter, int maxRows, Done done) const;
    void projectMergeRequests(const QString &project, const QUrlQuery &filter, int maxRows, Done done) const;
    void groupMergeRequests(const QString &group, const QUrlQuery &filter, int maxRows, Done done) const;

    // The latest pipeline of a branch. GitLab answers 404 when there is none.
    void latestPipeline(const QString &project, const QString &branch, Done done) const;
    void pipelineJobs(const QString &project, qint64 pipelineId, int maxRows, Done done) const;

    void issue(const QString &project, qint64 iid, Done done) const;
    // The issues visible to the user. filter as for mergeRequests().
    void issues(const QUrlQuery &filter, int maxRows, Done done) const;

    // Light actions. M6 calls them from the summaries.
    void retryPipeline(const QString &project, qint64 pipelineId, Done done) const;
    void cancelPipeline(const QString &project, qint64 pipelineId, Done done) const;
    void retryJob(const QString &project, qint64 jobId, Done done) const;
    void approve(const QString &project, qint64 iid, Done done) const;
    void commentOnMergeRequest(const QString &project, qint64 iid, const QString &text, Done done) const;
    void commentOnIssue(const QString &project, qint64 iid, const QString &text, Done done) const;

private:
    void get(const QString &path, const QUrlQuery &query, Done done) const;
    void list(const QString &path, QUrlQuery query, int maxRows, Done done) const;
    void post(const QString &path, const QJsonDocument &body, Done done) const;

    ApiClient *m_server;
    QObject *m_receiver;
};

} // namespace mirante::modules::gitlab
