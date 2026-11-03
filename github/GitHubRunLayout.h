// The interface of the extension point "runlayout": the native view of the
// Actions runs of a commit. GitHubRunLayoutDefault.cpp implements it, and so
// does any extension that an app names for "runlayout" in its manifest. See
// README.md#layouts.
#pragma once

#include "GitHubSummaryModel.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QUrl>

class QTimer;
class QWidget;

namespace mirante::modules::github {

// The run model shows one run at a time: at first the lead run of the group,
// then any run the layout chooses with showRun(). Its actions, jobs and page are
// the shown run's.
class RunModel : public SummaryModel
{
public:
    // The ids of the actions, for isBusy() and busyChanged().
    static inline const QString RerunFailedAction = QStringLiteral("rerun-failed");
    static inline const QString RerunAction = QStringLiteral("rerun-run");
    static inline const QString CancelAction = QStringLiteral("cancel-run");
    // rerun-job-<id>.
    static QString rerunJobAction(qint64 jobId);

    struct Run
    {
        // GitHub's workflow run object.
        QJsonObject run;
        qint64 id = 0;
        // Its workflow's name and its number, such as "CI #812".
        QString title;
        // Its workflow's name alone, such as "CI".
        QString workflow;
        Pill state;
        // Its branch: the item's, else the run's head_branch.
        QString branch;
        // When it started, as a short local date and time, or empty.
        QString started;
        // From its start to its last update, such as "5 min 2 s", once it has
        // completed; else empty.
        QString duration;
        // Of the lead's group, which ran for the same commit and event.
        bool inGroup = false;
    };

    struct Job
    {
        // GitHub's job object.
        QJsonObject job;
        qint64 id = 0;
        QString name;
        Pill state;
        // From its start to its end, or empty.
        QString duration;
        // Its log on the site, once it has started, or empty.
        QUrl log;
        // Completed, so rerunJob() applies.
        bool canRerun = false;
    };

    enum class Jobs {
        Loading,     // the shown run's jobs are being read
        Shown,       // jobs() has them, possibly none
        Unreadable,  // they could not be read
    };

    // Called by the module only.
    explicit RunModel(ItemContext &context);

    // False for a branch without runs. title() then says so, and nothing else
    // applies.
    bool hasRuns() const { return !m_runs.isEmpty(); }
    // The shown run's title, or the status's summary without runs.
    QString title() const;

    // Every run of the status, the group and the older runs, newest first.
    QList<Run> runs() const { return m_runs; }
    // The index in runs() of the shown run.
    qsizetype shownIndex() const;
    Run shownRun() const;
    // Whether more than one run is of the group.
    bool hasGroup() const;
    // Shows another run, and clears the error. Out of range does nothing. The
    // choice stays through a new status while that run is still there.
    void showRun(qsizetype index);

    Jobs jobsState() const { return m_jobsState; }
    // The shown run's jobs, in the order of GitHub's answer.
    QList<Job> jobs() const { return m_jobs; }

    // Whether each action applies to the shown run.
    bool canRerunFailed() const;
    bool canRerun() const;
    bool canCancel() const;
    // The pull request of the shown run, or empty.
    QUrl pullRequestAddress() const { return m_pullRequestAddress; }

    // The actions on the shown run. Cancel does not ask first; a layout that
    // wants a confirmation asks before it calls cancel().
    void rerunFailed();
    void rerun();
    void cancel();
    void rerunJob(qint64 jobId);

protected:
    void read(const ItemStatus &status) override;
    void viewVisibilityChanged(bool visible) override;

private:
    void readRuns(const QJsonObject &answer);
    void readShown();
    void readJobsOf(qint64 runId, const QJsonValue &jobs);
    void setJobs(const QJsonArray &jobs);
    void setPullRequest(const QJsonObject &run);
    void updateRefresh();

    QList<Run> m_runs;
    QHash<qint64, QJsonValue> m_statusJobs;
    QHash<qint64, QJsonArray> m_readJobs;
    QList<Job> m_jobs;
    Jobs m_jobsState = Jobs::Loading;
    QString m_summary;
    QString m_branch;
    QJsonObject m_linkedPullRequest;
    QUrl m_pullRequestAddress;
    qint64 m_leadId = 0;
    // The run the user chose, or 0 for the lead.
    qint64 m_selectedId = 0;
    // Only the answer to the latest read of a run's jobs counts.
    int m_jobReads = 0;
    QTimer *m_refreshTimer = nullptr;
};

// Makes the view of model, a child of parent. It draws the model now, and again
// at each changed(). Its section is "jobs".
QWidget *makeRunLayout(RunModel &model, QWidget *parent);

} // namespace mirante::modules::github
