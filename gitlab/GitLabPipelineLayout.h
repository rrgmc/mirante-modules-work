// The interface of the extension point "pipelinelayout": the native view of
// the pipelines of a branch. GitLabPipelineLayoutDefault.cpp implements it, and
// so does any extension that an app names for "pipelinelayout" in its manifest.
// See README.md#layouts.
#pragma once

#include "GitLabSummaryModel.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

class QTimer;
class QWidget;

namespace mirante::modules::gitlab {

// The pipeline model shows one pipeline at a time: at first the latest, then
// any pipeline the layout chooses with showPipeline(). Its actions, jobs and
// page are the shown pipeline's.
class PipelineModel : public SummaryModel
{
public:
    // The ids of the actions, for isBusy() and busyChanged().
    static inline const QString RetryAction = QStringLiteral("retry-pipeline");
    static inline const QString CancelAction = QStringLiteral("cancel-pipeline");
    static inline const QString TriggerAction = QStringLiteral("trigger-pipeline");
    // retry-job-<id> and play-job-<id>.
    static QString retryJobAction(qint64 jobId);
    static QString playJobAction(qint64 jobId);

    struct Pipeline
    {
        // GitLab's pipeline object.
        QJsonObject pipeline;
        qint64 id = 0;
        // Such as "Pipeline #812".
        QString title;
        Pill state;
        // When it was created, as a short local date and time, or empty.
        QString created;
        // Such as "5 min 2 s", or empty.
        QString duration;
        // Of the latest pipeline's group.
        bool inGroup = false;
    };

    struct Job
    {
        // GitLab's job object.
        QJsonObject job;
        qint64 id = 0;
        QString name;
        QString stage;
        Pill state;
        // Such as "5 min 2 s", or empty.
        QString duration;
        // Its log on the server, once it has run, or empty.
        QUrl log;
        // Finished, so retryJob() applies.
        bool canRetry = false;
        // Manual, so playJob() applies.
        bool canPlay = false;
    };

    enum class Jobs {
        Loading,     // the shown pipeline's jobs are being read
        Shown,       // jobs() has them, possibly none
        Unreadable,  // they could not be read
    };

    // Called by the module only.
    explicit PipelineModel(ItemContext &context);

    // False for a branch without pipelines. title() then says so; a new
    // pipeline can still be triggered.
    bool hasPipelines() const { return !m_pipelines.isEmpty(); }
    // The shown pipeline's title, or the status's summary without pipelines.
    QString title() const;
    // The item's branch.
    QString branch() const { return m_branch; }

    // Every pipeline of the status, newest first: the latest, the rest of its
    // group, then the older ones.
    QList<Pipeline> pipelines() const { return m_pipelines; }
    // The index in pipelines() of the shown pipeline.
    qsizetype shownIndex() const;
    Pipeline shownPipeline() const;
    // Whether more than one pipeline is of the group.
    bool hasGroup() const;
    // Shows another pipeline, and clears the error. Out of range does nothing.
    // The choice stays through a new status while it is still there.
    void showPipeline(qsizetype index);

    Jobs jobsState() const { return m_jobsState; }
    // The shown pipeline's jobs, in the order they were created.
    QList<Job> jobs() const { return m_jobs; }
    // The stages of jobs(), in the order of their first job.
    QStringList stages() const;

    // Whether each action applies to the shown pipeline.
    bool canRetry() const;
    bool canCancel() const;
    // A branch is known, so a new pipeline can start on it.
    bool canTrigger() const { return !m_branch.isEmpty(); }
    // The merge request of the branch, or empty.
    QUrl mergeRequestAddress() const { return m_mergeRequestAddress; }

    // The actions. Cancel and trigger do not ask first; a layout that wants a
    // confirmation asks before it calls them. trigger() shows the latest
    // pipeline again, so the new one shows when it comes.
    void retry();
    void cancel();
    void trigger();
    void retryJob(qint64 jobId);
    void playJob(qint64 jobId);

protected:
    void read(const ItemStatus &status) override;
    void viewVisibilityChanged(bool visible) override;

private:
    void readPipelines(const QJsonObject &answer);
    void readShown();
    void readJobsOf(qint64 pipelineId, const QJsonValue &jobs);
    void setJobs(const QJsonArray &jobs);
    void setMergeRequest(const QJsonObject &mergeRequest);
    void updateRefresh();

    QList<Pipeline> m_pipelines;
    QHash<qint64, QJsonValue> m_statusJobs;
    QHash<qint64, QJsonArray> m_readJobs;
    QList<Job> m_jobs;
    Jobs m_jobsState = Jobs::Loading;
    QString m_summary;
    QString m_branch;
    QUrl m_mergeRequestAddress;
    // The pipeline the user chose, or 0 for the latest.
    qint64 m_selectedId = 0;
    // Only the answer to the latest read of a pipeline's jobs counts.
    int m_jobReads = 0;
    QTimer *m_refreshTimer = nullptr;
};

// Makes the view of model, a child of parent. It draws the model now, and again
// at each changed(). Its section is "jobs".
QWidget *makePipelineLayout(PipelineModel &model, QWidget *parent);

} // namespace mirante::modules::gitlab
