#include "GitHubRunView.h"

#include "GitHubClient.h"
#include "GitHubRunLayout.h"
#include "GitHubStatus.h"
#include "GitHubUrls.h"
#include "GitHubViewSupport.h"

#include <mirante/ItemContext.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace mirante::modules::github {

namespace {

// How often the model asks for a refresh while the shown run has not completed.
constexpr int kRefreshMs = 15000;

bool isCompleted(const QString &status)
{
    return status == QLatin1String("completed");
}

QString statusOf(const QJsonObject &object)
{
    return object.value(QStringLiteral("status")).toString();
}

QString conclusionOf(const QJsonObject &object)
{
    return object.value(QStringLiteral("conclusion")).toString();
}

QDateTime createdOf(const QJsonObject &run)
{
    return QDateTime::fromString(run.value(QStringLiteral("created_at")).toString(), Qt::ISODateWithMs);
}

RunModel::Run readRun(const QJsonObject &object, const QString &branch, bool inGroup)
{
    RunModel::Run run;
    run.run = object;
    run.id = object.value(QStringLiteral("id")).toInteger();
    run.workflow = object.value(QStringLiteral("name")).toString();
    run.title = QCoreApplication::translate("github", "%1 #%2")
                    .arg(run.workflow)
                    .arg(object.value(QStringLiteral("run_number")).toInteger());
    run.state = runPillOf(object);
    run.branch = branch.isEmpty() ? object.value(QStringLiteral("head_branch")).toString() : branch;
    const QString started = object.value(QStringLiteral("run_started_at")).toString().isEmpty()
        ? object.value(QStringLiteral("created_at")).toString()
        : object.value(QStringLiteral("run_started_at")).toString();
    if (!started.isEmpty())
        run.started = localDateTime(started);
    if (isCompleted(statusOf(object)))
        run.duration = durationBetween(started, object.value(QStringLiteral("updated_at")).toString());
    run.inGroup = inGroup;
    return run;
}

RunModel::Job readJob(const QJsonObject &object)
{
    RunModel::Job job;
    job.job = object;
    job.id = object.value(QStringLiteral("id")).toInteger();
    job.name = object.value(QStringLiteral("name")).toString();
    job.state = runPillOf(object);
    const QString started = object.value(QStringLiteral("started_at")).toString();
    job.duration = durationBetween(started, object.value(QStringLiteral("completed_at")).toString());
    const QUrl log(object.value(QStringLiteral("html_url")).toString());
    if (!started.isEmpty() && isWebAddress(log))
        job.log = log;
    job.canRerun = isCompleted(statusOf(object));
    return job;
}

} // namespace

QString RunModel::rerunJobAction(qint64 jobId)
{
    return QStringLiteral("rerun-job-%1").arg(jobId);
}

RunModel::RunModel(ItemContext &context)
    : SummaryModel(context)
{
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setObjectName(QStringLiteral("run-refresh"));
    m_refreshTimer->setInterval(kRefreshMs);
    connect(m_refreshTimer, &QTimer::timeout, this, [this] { refresh(); });
}

QString RunModel::title() const
{
    return hasRuns() ? shownRun().title : m_summary;
}

qsizetype RunModel::shownIndex() const
{
    const qint64 wanted = m_selectedId != 0 ? m_selectedId : m_leadId;
    for (qsizetype i = 0; i < m_runs.size(); ++i) {
        if (m_runs.at(i).id == wanted)
            return i;
    }
    for (qsizetype i = 0; i < m_runs.size(); ++i) {
        if (m_runs.at(i).id == m_leadId)
            return i;
    }
    return 0;
}

RunModel::Run RunModel::shownRun() const
{
    return hasRuns() ? m_runs.at(shownIndex()) : Run();
}

bool RunModel::hasGroup() const
{
    return std::count_if(m_runs.cbegin(), m_runs.cend(), [](const Run &run) { return run.inGroup; }) > 1;
}

void RunModel::showRun(qsizetype index)
{
    if (index < 0 || index >= m_runs.size())
        return;
    // The lead is not held on to, so the model follows a newer one.
    const qint64 id = m_runs.at(index).id;
    m_selectedId = id == m_leadId ? 0 : id;
    // An error of the last action was about the run shown then.
    clearError();
    readShown();
    emit changed();
}

bool RunModel::canRerunFailed() const
{
    const Run run = shownRun();
    return hasRuns() && isCompleted(statusOf(run.run)) && isFailedConclusion(conclusionOf(run.run));
}

bool RunModel::canRerun() const
{
    return hasRuns() && isCompleted(statusOf(shownRun().run));
}

bool RunModel::canCancel() const
{
    return hasRuns() && canCancelRun(statusOf(shownRun().run));
}

void RunModel::rerunFailed()
{
    if (!canRerunFailed())
        return;
    runAction(RerunFailedAction, [repository = repository(), run = shownRun().id](const GitHubClient &client,
                                                                                    GitHubClient::Done done) {
        client.rerunFailedJobs(repository, run, std::move(done));
    });
}

void RunModel::rerun()
{
    if (!canRerun())
        return;
    runAction(RerunAction, [repository = repository(), run = shownRun().id](const GitHubClient &client,
                                                                              GitHubClient::Done done) {
        client.rerunRun(repository, run, std::move(done));
    });
}

void RunModel::cancel()
{
    if (!canCancel())
        return;
    runAction(CancelAction, [repository = repository(), run = shownRun().id](const GitHubClient &client,
                                                                               GitHubClient::Done done) {
        client.cancelRun(repository, run, std::move(done));
    });
}

void RunModel::rerunJob(qint64 jobId)
{
    runAction(rerunJobAction(jobId), [repository = repository(), jobId](const GitHubClient &client,
                                                                          GitHubClient::Done done) {
        client.rerunJob(repository, jobId, std::move(done));
    });
}

void RunModel::read(const ItemStatus &status)
{
    m_summary = status.summary;
    m_branch = status.data.value(QStringLiteral("branch")).toString();
    m_linkedPullRequest = status.data.value(QStringLiteral("pullRequest")).toObject();
    readRuns(status.data);
    if (m_runs.isEmpty()) {
        // A branch without runs: the status says so.
        m_selectedId = 0;
        m_jobs.clear();
        setPullRequest({});
        setPageAddress({});
        updateRefresh();
        return;
    }
    readShown();
}

void RunModel::viewVisibilityChanged(bool visible)
{
    Q_UNUSED(visible)
    updateRefresh();
}

// All runs of the status, newest first, as the status sorts a group: by
// created_at, then by id. A date that cannot be read counts as the oldest.
void RunModel::readRuns(const QJsonObject &answer)
{
    m_runs.clear();
    m_statusJobs.clear();
    const QJsonObject lead = answer.value(QStringLiteral("run")).toObject();
    if (lead.isEmpty()) {
        m_leadId = 0;
        return;
    }
    m_runs << readRun(lead, m_branch, true);
    m_leadId = m_runs.constFirst().id;
    m_statusJobs.insert(m_leadId, answer.value(QStringLiteral("jobs")));
    for (const QJsonValue &value : answer.value(QStringLiteral("siblings")).toArray()) {
        const QJsonObject sibling = value.toObject();
        m_runs << readRun(sibling.value(QStringLiteral("run")).toObject(), m_branch, true);
        m_statusJobs.insert(m_runs.constLast().id, sibling.value(QStringLiteral("jobs")));
    }
    for (const QJsonValue &value : answer.value(QStringLiteral("older")).toArray())
        m_runs << readRun(value.toObject(), m_branch, false);

    std::stable_sort(m_runs.begin(), m_runs.end(), [](const Run &a, const Run &b) {
        const QDateTime first = createdOf(a.run);
        const QDateTime second = createdOf(b.run);
        if (first.isValid() != second.isValid())
            return first.isValid();
        if (first != second)
            return first > second;
        return a.id > b.id;
    });
}

void RunModel::readShown()
{
    const Run shown = shownRun();
    setPullRequest(shown.run);
    setPageAddress(webAddress(shown.run, {}));
    readJobsOf(shown.id, m_statusJobs.value(shown.id));
    updateRefresh();
}

// The jobs in the status, else those read before while a new read runs, else
// "Loading".
void RunModel::readJobsOf(qint64 runId, const QJsonValue &jobs)
{
    // An answer on its way is for what was shown before.
    const int read = ++m_jobReads;
    if (jobs.isArray()) {
        setJobs(jobs.toArray());
        return;
    }

    if (m_readJobs.contains(runId)) {
        setJobs(m_readJobs.value(runId));
    } else {
        m_jobs.clear();
        m_jobsState = Jobs::Loading;
    }
    ApiClient *client = server();
    if (!client) {
        m_jobs.clear();
        m_jobsState = Jobs::Unreadable;
        return;
    }
    GitHubClient(*client, this).runJobs(repository(), runId, [this, read, runId](const ApiReply &reply) {
        if (read != m_jobReads)
            return;
        if (reply.ok()) {
            const QJsonArray jobs = reply.json.object().value(QStringLiteral("jobs")).toArray();
            m_readJobs.insert(runId, jobs);
            setJobs(jobs);
        } else if (!m_readJobs.contains(runId)) {
            m_jobs.clear();
            m_jobsState = Jobs::Unreadable;
        } else {
            return;
        }
        emit changed();
    });
}

void RunModel::setJobs(const QJsonArray &jobs)
{
    m_jobs.clear();
    for (const QJsonValue &value : jobs)
        m_jobs << readJob(value.toObject());
    m_jobsState = Jobs::Shown;
}

// The linked pull request for a pull request's branch; else the shown run's
// first pull request, if it has one.
void RunModel::setPullRequest(const QJsonObject &run)
{
    QUrl address;
    const bool linked = config().value(QStringLiteral("branchSource")).toString() == QLatin1String("pullRequest");
    if (linked) {
        address = webAddress(m_linkedPullRequest, pullRequestUrl(webBase(), config(), defaultRepository()));
    } else {
        const QJsonArray pullRequests = run.value(QStringLiteral("pull_requests")).toArray();
        const qint64 number =
            pullRequests.isEmpty() ? 0 : pullRequests.first().toObject().value(QStringLiteral("number")).toInteger();
        const QString repository = this->repository();
        if (number > 0 && !repository.isEmpty())
            address = webPageUrl(webBase(), QStringLiteral("%1/pull/%2").arg(repository).arg(number));
    }
    m_pullRequestAddress = isWebAddress(address) ? address : QUrl();
}

void RunModel::updateRefresh()
{
    const bool running = hasRuns() && !isCompleted(statusOf(shownRun().run));
    if (!isViewVisible() || !running)
        m_refreshTimer->stop();
    else if (!m_refreshTimer->isActive())
        m_refreshTimer->start();
}

QWidget *createRunView(ItemContext &context, QWidget *parent)
{
    auto *model = new RunModel(context);
    model->follow(context.status());
    QWidget *view = makeRunLayout(*model, parent);
    model->attach(view);
    return view;
}

} // namespace mirante::modules::github
