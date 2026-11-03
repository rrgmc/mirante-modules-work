#include "GitLabMergeRequestView.h"

#include "GitLabClient.h"
#include "GitLabMergeRequestLayout.h"
#include "GitLabUrls.h"
#include "GitLabViewSupport.h"

#include <mirante/ItemContext.h>

#include <QJsonArray>

#include <utility>

namespace mirante::modules::gitlab {

MergeRequestModel::MergeRequestModel(ItemContext &context)
    : SummaryModel(context)
{
}

Pill MergeRequestModel::state() const
{
    return statePillOf(m_mergeRequest.value(QStringLiteral("state")).toString());
}

bool MergeRequestModel::isDraft() const
{
    return m_mergeRequest.value(QStringLiteral("state")).toString() == QLatin1String("opened")
        && m_mergeRequest.value(QStringLiteral("draft")).toBool();
}

Pill MergeRequestModel::pipelineState() const
{
    return statusPillOf(m_pipeline.value(QStringLiteral("status")).toString());
}

QUrl MergeRequestModel::pipelineAddress() const
{
    return webAddress(m_pipeline, {});
}

bool MergeRequestModel::canRetryPipeline() const
{
    return gitlab::canRetryPipeline(m_pipeline.value(QStringLiteral("status")).toString());
}

void MergeRequestModel::retryPipeline()
{
    if (!canRetryPipeline())
        return;
    runAction(RetryPipelineAction,
              [project = project(), pipeline = m_pipeline.value(QStringLiteral("id")).toInteger()](
                  const GitLabClient &client, GitLabClient::Done done) {
                  client.retryPipeline(project, pipeline, std::move(done));
              });
}

void MergeRequestModel::read(const ItemStatus &status)
{
    m_mergeRequest = status.data.value(QStringLiteral("mergeRequest")).toObject();
    m_pipeline = m_mergeRequest.value(QStringLiteral("head_pipeline")).toObject();

    m_failedJobs.clear();
    for (const QJsonValue &value : status.data.value(QStringLiteral("jobs")).toArray()) {
        const QJsonObject job = value.toObject();
        if (job.value(QStringLiteral("status")).toString() != QLatin1String("failed"))
            continue;
        const QUrl log(job.value(QStringLiteral("web_url")).toString());
        m_failedJobs << Job{job,
                            job.value(QStringLiteral("id")).toInteger(),
                            job.value(QStringLiteral("name")).toString(),
                            jobPillOf(job),
                            durationText(job.value(QStringLiteral("duration"))),
                            isWebAddress(log) ? log : QUrl()};
    }

    const QJsonObject approvals = status.data.value(QStringLiteral("approvals")).toObject();
    const QJsonArray entries = approvals.value(QStringLiteral("approved_by")).toArray();
    m_approvals = {};
    m_approvals.given = int(entries.size());
    m_approvals.required = approvals.value(QStringLiteral("approvals_required")).toInt();
    for (const QJsonValue &entry : entries) {
        const QString name =
            entry.toObject().value(QStringLiteral("user")).toObject().value(QStringLiteral("name")).toString();
        if (!name.isEmpty())
            m_approvals.approvers << name;
    }

    setPageAddress(webAddress(m_mergeRequest, mergeRequestUrl(baseUrl(), config(), defaultProject())));
}

QWidget *createMergeRequestView(ItemContext &context, QWidget *parent)
{
    auto *model = new MergeRequestModel(context);
    model->follow(context.status());
    QWidget *view = makeMergeRequestLayout(*model, parent);
    model->attach(view);
    return view;
}

} // namespace mirante::modules::gitlab
