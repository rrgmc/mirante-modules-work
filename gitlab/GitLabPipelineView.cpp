#include "GitLabPipelineView.h"

#include "GitLabClient.h"
#include "GitLabPipelineLayout.h"
#include "GitLabUrls.h"
#include "GitLabViewSupport.h"

#include <mirante/ItemContext.h>

#include <QCoreApplication>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace mirante::modules::gitlab {

namespace {

// How often the model asks for a refresh while the shown pipeline is unfinished.
constexpr int kRefreshMs = 15000;

QString statusOf(const QJsonObject &object)
{
    return object.value(QStringLiteral("status")).toString();
}

// The statuses in which GitLab can cancel a pipeline.
bool canCancelStatus(const QString &status)
{
    static const QSet<QString> statuses{
        QStringLiteral("created"),   QStringLiteral("waiting_for_resource"), QStringLiteral("waiting_for_callback"),
        QStringLiteral("preparing"), QStringLiteral("pending"),              QStringLiteral("running"),
        QStringLiteral("scheduled"),
    };
    return statuses.contains(status);
}

// A pipeline that can still change by itself. "manual" waits for a person, so
// a blocked pipeline does not keep asking.
bool isUnfinished(const QString &status)
{
    static const QSet<QString> finished{
        QStringLiteral("success"), QStringLiteral("failed"), QStringLiteral("canceled"),
        QStringLiteral("skipped"), QStringLiteral("manual"),
    };
    return !status.isEmpty() && !finished.contains(status);
}

// The job statuses that GitLab can retry.
bool isFinishedJob(const QString &status)
{
    return status == QLatin1String("success") || status == QLatin1String("failed")
        || status == QLatin1String("canceled");
}

// A job that has a log: one that started, which a canceled job may not have.
bool hasRun(const QJsonObject &job)
{
    return job.value(QStringLiteral("started_at")).isString() || job.value(QStringLiteral("duration")).isDouble();
}

PipelineModel::Pipeline readPipeline(const QJsonObject &object, bool inGroup)
{
    PipelineModel::Pipeline pipeline;
    pipeline.pipeline = object;
    pipeline.id = object.value(QStringLiteral("id")).toInteger();
    pipeline.title = QCoreApplication::translate("gitlab", "Pipeline #%1").arg(pipeline.id);
    pipeline.state = statusPillOf(statusOf(object));
    const QString created = object.value(QStringLiteral("created_at")).toString();
    if (!created.isEmpty())
        pipeline.created = localDateTime(created);
    pipeline.duration = durationText(object.value(QStringLiteral("duration")));
    pipeline.inGroup = inGroup;
    return pipeline;
}

PipelineModel::Job readJob(const QJsonObject &object)
{
    PipelineModel::Job job;
    job.job = object;
    job.id = object.value(QStringLiteral("id")).toInteger();
    job.name = object.value(QStringLiteral("name")).toString();
    job.stage = object.value(QStringLiteral("stage")).toString();
    job.state = jobPillOf(object);
    job.duration = durationText(object.value(QStringLiteral("duration")));
    const QUrl log(object.value(QStringLiteral("web_url")).toString());
    if (hasRun(object) && isWebAddress(log))
        job.log = log;
    const QString status = statusOf(object);
    job.canRetry = isFinishedJob(status);
    job.canPlay = status == QLatin1String("manual");
    return job;
}

} // namespace

QString PipelineModel::retryJobAction(qint64 jobId)
{
    return QStringLiteral("retry-job-%1").arg(jobId);
}

QString PipelineModel::playJobAction(qint64 jobId)
{
    return QStringLiteral("play-job-%1").arg(jobId);
}

PipelineModel::PipelineModel(ItemContext &context)
    : SummaryModel(context)
{
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setObjectName(QStringLiteral("pipeline-refresh"));
    m_refreshTimer->setInterval(kRefreshMs);
    connect(m_refreshTimer, &QTimer::timeout, this, [this] { refresh(); });
}

QString PipelineModel::title() const
{
    return hasPipelines() ? shownPipeline().title : m_summary;
}

qsizetype PipelineModel::shownIndex() const
{
    for (qsizetype i = 0; i < m_pipelines.size(); ++i) {
        if (m_pipelines.at(i).id == m_selectedId)
            return i;
    }
    return 0;
}

PipelineModel::Pipeline PipelineModel::shownPipeline() const
{
    return hasPipelines() ? m_pipelines.at(shownIndex()) : Pipeline();
}

bool PipelineModel::hasGroup() const
{
    return std::count_if(m_pipelines.cbegin(), m_pipelines.cend(),
                         [](const Pipeline &pipeline) { return pipeline.inGroup; })
        > 1;
}

void PipelineModel::showPipeline(qsizetype index)
{
    if (index < 0 || index >= m_pipelines.size())
        return;
    // The latest is not held on to, so the model follows a newer one.
    m_selectedId = index == 0 ? 0 : m_pipelines.at(index).id;
    // An error of the last action was about the pipeline shown then.
    clearError();
    readShown();
    emit changed();
}

QStringList PipelineModel::stages() const
{
    QStringList stages;
    for (const Job &job : m_jobs) {
        if (!stages.contains(job.stage))
            stages << job.stage;
    }
    return stages;
}

bool PipelineModel::canRetry() const
{
    return hasPipelines() && canRetryPipeline(statusOf(shownPipeline().pipeline));
}

bool PipelineModel::canCancel() const
{
    return hasPipelines() && canCancelStatus(statusOf(shownPipeline().pipeline));
}

void PipelineModel::retry()
{
    if (!canRetry())
        return;
    runAction(RetryAction, [project = project(), pipeline = shownPipeline().id](const GitLabClient &client,
                                                                                 GitLabClient::Done done) {
        client.retryPipeline(project, pipeline, std::move(done));
    });
}

void PipelineModel::cancel()
{
    if (!canCancel())
        return;
    runAction(CancelAction, [project = project(), pipeline = shownPipeline().id](const GitLabClient &client,
                                                                                  GitLabClient::Done done) {
        client.cancelPipeline(project, pipeline, std::move(done));
    });
}

void PipelineModel::trigger()
{
    if (!canTrigger())
        return;
    // The new pipeline will be the latest, so show that when it comes.
    m_selectedId = 0;
    runAction(TriggerAction, [project = project(), branch = m_branch](const GitLabClient &client,
                                                                       GitLabClient::Done done) {
        client.createPipeline(project, branch, std::move(done));
    });
}

void PipelineModel::retryJob(qint64 jobId)
{
    runAction(retryJobAction(jobId), [project = project(), jobId](const GitLabClient &client, GitLabClient::Done done) {
        client.retryJob(project, jobId, std::move(done));
    });
}

void PipelineModel::playJob(qint64 jobId)
{
    runAction(playJobAction(jobId), [project = project(), jobId](const GitLabClient &client, GitLabClient::Done done) {
        client.playJob(project, jobId, std::move(done));
    });
}

void PipelineModel::read(const ItemStatus &status)
{
    m_summary = status.summary;
    m_branch = status.data.value(QStringLiteral("branch")).toString();
    setMergeRequest(status.data.value(QStringLiteral("mergeRequest")).toObject());
    readPipelines(status.data);
    if (m_pipelines.isEmpty()) {
        // A branch without pipelines: the status says so.
        m_selectedId = 0;
        m_jobs.clear();
        setPageAddress({});
        updateRefresh();
        return;
    }
    readShown();
}

void PipelineModel::viewVisibilityChanged(bool visible)
{
    Q_UNUSED(visible)
    updateRefresh();
}

// Newest first: the latest, the rest of its group, then the older ones.
void PipelineModel::readPipelines(const QJsonObject &answer)
{
    m_pipelines.clear();
    m_statusJobs.clear();
    const QJsonObject latest = answer.value(QStringLiteral("pipeline")).toObject();
    if (latest.isEmpty())
        return;
    m_pipelines << readPipeline(latest, true);
    m_statusJobs.insert(m_pipelines.constLast().id, answer.value(QStringLiteral("jobs")));
    for (const QJsonValue &value : answer.value(QStringLiteral("siblings")).toArray()) {
        const QJsonObject sibling = value.toObject();
        m_pipelines << readPipeline(sibling.value(QStringLiteral("pipeline")).toObject(), true);
        m_statusJobs.insert(m_pipelines.constLast().id, sibling.value(QStringLiteral("jobs")));
    }
    for (const QJsonValue &value : answer.value(QStringLiteral("older")).toArray())
        m_pipelines << readPipeline(value.toObject(), false);
}

void PipelineModel::readShown()
{
    const Pipeline shown = shownPipeline();
    setPageAddress(webAddress(shown.pipeline, {}));
    readJobsOf(shown.id, m_statusJobs.value(shown.id));
    updateRefresh();
}

// The jobs in the status, else those read before while a new read runs, else
// "Loading".
void PipelineModel::readJobsOf(qint64 pipelineId, const QJsonValue &jobs)
{
    // An answer on its way is for what was shown before.
    const int read = ++m_jobReads;
    if (jobs.isArray()) {
        setJobs(jobs.toArray());
        return;
    }

    if (m_readJobs.contains(pipelineId)) {
        setJobs(m_readJobs.value(pipelineId));
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
    GitLabClient(*client, this)
        .pipelineJobs(project(), pipelineId, GetOptions().maxRows, [this, read, pipelineId](const ApiReply &reply) {
            if (read != m_jobReads)
                return;
            if (reply.ok()) {
                m_readJobs.insert(pipelineId, reply.json.array());
                setJobs(reply.json.array());
            } else if (!m_readJobs.contains(pipelineId)) {
                m_jobs.clear();
                m_jobsState = Jobs::Unreadable;
            } else {
                return;
            }
            emit changed();
        });
}

// In the order the jobs were created, which does not depend on how GitLab
// sorts its answer.
void PipelineModel::setJobs(const QJsonArray &jobs)
{
    m_jobs.clear();
    for (const QJsonValue &value : jobs)
        m_jobs << readJob(value.toObject());
    std::stable_sort(m_jobs.begin(), m_jobs.end(), [](const Job &a, const Job &b) { return a.id < b.id; });
    m_jobsState = Jobs::Shown;
}

// From the configuration for a merge request's branch, else the open merge
// request of the branch, if the status found one.
void PipelineModel::setMergeRequest(const QJsonObject &mergeRequest)
{
    const bool linked = config().value(QStringLiteral("branchSource")).toString() == QLatin1String("mergeRequest");
    const QUrl built = linked ? mergeRequestUrl(baseUrl(), config(), defaultProject()) : QUrl();
    const QUrl address = webAddress(mergeRequest, built);
    m_mergeRequestAddress = isWebAddress(address) ? address : QUrl();
}

void PipelineModel::updateRefresh()
{
    const bool unfinished = hasPipelines() && isUnfinished(statusOf(shownPipeline().pipeline));
    if (!isViewVisible() || !unfinished)
        m_refreshTimer->stop();
    else if (!m_refreshTimer->isActive())
        m_refreshTimer->start();
}

QWidget *createPipelineView(ItemContext &context, QWidget *parent)
{
    auto *model = new PipelineModel(context);
    model->follow(context.status());
    QWidget *view = makePipelineLayout(*model, parent);
    model->attach(view);
    return view;
}

} // namespace mirante::modules::gitlab
