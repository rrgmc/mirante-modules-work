// The interface of the extension point "mergerequestlayout": the native view
// of a GitLab merge request. GitLabMergeRequestLayoutDefault.cpp implements it,
// and so does any extension that an app names for "mergerequestlayout" in its
// manifest. See README.md#layouts.
#pragma once

#include "GitLabSummaryModel.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

class QWidget;

namespace mirante::modules::gitlab {

class MergeRequestModel : public SummaryModel
{
public:
    // The id of the action, for isBusy() and busyChanged().
    static inline const QString RetryPipelineAction = QStringLiteral("retry-pipeline");

    // A failed job of the head pipeline.
    struct Job
    {
        // GitLab's job object.
        QJsonObject job;
        qint64 id = 0;
        QString name;
        Pill state;
        // Such as "5 min 2 s", or empty.
        QString duration;
        // Its log on the server, or empty.
        QUrl log;
    };

    struct Approvals
    {
        int given = 0;
        // 0 when the project requires none.
        int required = 0;
        // The names of those who approved.
        QStringList approvers;
    };

    // Called by the module only.
    explicit MergeRequestModel(ItemContext &context);

    // GitLab's merge request object, as GET projects/<p>/merge_requests/<iid>
    // answers it.
    QJsonObject mergeRequest() const { return m_mergeRequest; }
    // "Open", "Merged", "Closed" or "Locked".
    Pill state() const;
    // Open and a draft.
    bool isDraft() const;

    // The head pipeline, GitLab's object, or empty when there is none.
    QJsonObject pipeline() const { return m_pipeline; }
    Pill pipelineState() const;
    // The pipeline's page, or empty.
    QUrl pipelineAddress() const;
    // The pipeline's failed jobs, in the order of the data.
    QList<Job> failedJobs() const { return m_failedJobs; }

    Approvals approvals() const { return m_approvals; }

    // Whether retryPipeline() applies: the head pipeline failed or was canceled.
    bool canRetryPipeline() const;
    void retryPipeline();

protected:
    void read(const ItemStatus &status) override;

private:
    QJsonObject m_mergeRequest;
    QJsonObject m_pipeline;
    QList<Job> m_failedJobs;
    Approvals m_approvals;
};

// Makes the view of model, a child of parent. It draws the model now, and again
// at each changed(). Its sections are "pipeline", "approvals", "people" and
// "labels", which isSectionShown() turns on and off.
QWidget *makeMergeRequestLayout(MergeRequestModel &model, QWidget *parent);

} // namespace mirante::modules::gitlab
