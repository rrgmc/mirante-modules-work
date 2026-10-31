// The GitHub REST API, one method per endpoint the module uses, over the
// ApiClient of a profile's server. See README.md#the-client.
#pragma once

#include <mirante/ApiClient.h>

#include <QString>
#include <QUrlQuery>

#include <functional>

class QObject;

namespace mirante::modules::github {

// A thin, copyable wrapper: it builds the paths, queries and bodies, and
// passes each ApiReply on unchanged. A repository is "owner/repo"; its owner
// and its name are percent-encoded each on its own. Lists follow the Link
// header up to maxRows rows, and name the key of the rows in answers that
// wrap them. Callbacks are dropped if receiver is destroyed.
class GitHubClient
{
public:
    using Done = std::function<void(const ApiReply &)>;

    GitHubClient(ApiClient &server, QObject *receiver);

    // A value as one part of a path or a query: "a b/c" becomes "a%20b%2Fc".
    // A branch in a query is encoded this way, so that "+" and "&" in its
    // name reach GitHub whole.
    static QString encode(const QString &text);
    // "owner/repo" as the path "repos/owner/repo", each part encoded. A
    // repository without a slash is encoded whole.
    static QString repositoryPath(const QString &repository);
    // The repository of an API address that holds ".../repos/<owner>/<repo>",
    // such as a search row's repository_url, as "owner/repo". Empty if url
    // holds none.
    static QString repositoryOfApiUrl(const QString &url);

    void currentUser(Done done) const;
    // The repository, with its default_branch.
    void repository(const QString &repository, Done done) const;

    void pullRequest(const QString &repository, qint64 number, Done done) const;
    // The reviews of a pull request, oldest first, up to 100.
    void pullRequestReviews(const QString &repository, qint64 number, Done done) const;
    // The pull requests of a repository. query holds the API's parameters,
    // such as state=open&sort=updated&direction=desc.
    void repositoryPullRequests(const QString &repository, const QUrlQuery &query, int maxRows, Done done) const;

    // The check runs of a commit, up to 100. The answer is the object, with
    // all rows under "check_runs".
    void checkRuns(const QString &repository, const QString &sha, Done done) const;
    // The combined commit status of a commit: its state and its statuses.
    void combinedStatus(const QString &repository, const QString &sha, Done done) const;

    void issue(const QString &repository, qint64 number, Done done) const;
    // One page of an issue's comments, oldest first: page is 1 for the first.
    void issueComments(const QString &repository, qint64 number, int perPage, int page, Done done) const;

    // The issues and pull requests that match a search query, such as
    // "is:pr is:open review-requested:@me", newest update first, up to
    // maxRows. The answer is the object, with all rows under "items".
    void searchIssues(const QString &query, int maxRows, Done done) const;

    // The workflow runs of a repository, newest first, or of one workflow
    // file, such as "ci.yml", when workflow is not empty. query holds the
    // API's parameters, such as branch=<b>, actor=<login> or
    // event=pull_request, each value encoded with encode(). The answer is
    // the object, with all rows under "workflow_runs".
    void workflowRuns(const QString &repository, const QString &workflow, const QUrlQuery &query, int maxRows,
                      Done done) const;
    // The jobs of a run's latest attempt, up to 100. The answer is the
    // object, with all rows under "jobs".
    void runJobs(const QString &repository, qint64 runId, Done done) const;

    // The Actions actions, which the run view calls. See decision 0070.
    void rerunRun(const QString &repository, qint64 runId, Done done) const;
    void rerunFailedJobs(const QString &repository, qint64 runId, Done done) const;
    void cancelRun(const QString &repository, qint64 runId, Done done) const;
    void rerunJob(const QString &repository, qint64 jobId, Done done) const;

private:
    void get(const QString &path, const QUrlQuery &query, Done done) const;
    // A list: followLinks with maxRows, per_page of at most 100 and of at
    // most maxRows unless query names its own, and arrayKey for an answer
    // that wraps its rows.
    void list(const QString &path, QUrlQuery query, int maxRows, const QString &arrayKey, Done done) const;
    void post(const QString &path, Done done) const;

    ApiClient *m_server;
    QObject *m_receiver;
};

} // namespace mirante::modules::github
