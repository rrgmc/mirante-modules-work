// The default of the extension point "mergerequestlayout": a SummaryView with
// the merge request's state, Pipeline, Approvals, People and Labels. It is
// compiled only when the app names no extension for "mergerequestlayout". See
// README.md#layouts.
#include "GitLabMergeRequestLayout.h"
#include "GitLabViewSupport.h"

#include <mirante/widgets/ActionRow.h>
#include <mirante/widgets/FlowRow.h>
#include <mirante/widgets/SummaryView.h>

#include <QCoreApplication>
#include <QLabel>
#include <QLayout>
#include <QPushButton>

using mirante::widgets::StatusPill;
using mirante::widgets::StatusRole;

namespace mirante::modules::gitlab {

namespace {

const QStringList &sections()
{
    static const QStringList keys{QStringLiteral("pipeline"), QStringLiteral("approvals"), QStringLiteral("people"),
                                  QStringLiteral("labels")};
    return keys;
}

class MergeRequestLayout : public widgets::SummaryView
{
public:
    MergeRequestLayout(MergeRequestModel &model, QWidget *parent)
        : widgets::SummaryView(parent)
        , m_model(model)
    {
        setObjectName(defaultLayoutName(QStringLiteral("merge-request")));
        followActions(this, model);
        actions()->addAction(MergeRequestModel::RetryPipelineAction,
                             QCoreApplication::translate("gitlab", "Retry pipeline"),
                             QCoreApplication::translate("gitlab", "Retry"), [this] { m_model.retryPipeline(); });
        addOpenPage(this, model);
        connect(&model, &SummaryModel::changed, this, [this] { draw(); });
        draw();
    }

private:
    void draw()
    {
        if (!m_model.hasData()) {
            setMessage(m_model.message());
            return;
        }
        setMessage({});
        const QJsonObject mergeRequest = m_model.mergeRequest();

        setTitle(mergeRequest.value(QStringLiteral("title")).toString());
        setStateRow(stateRow(mergeRequest));
        actions()->setActionVisible(MergeRequestModel::RetryPipelineAction, m_model.canRetryPipeline());
        showPageAddress(this, m_model);

        setSection(QStringLiteral("pipeline"), QCoreApplication::translate("gitlab", "Pipeline"), pipelineSection());
        setSection(QStringLiteral("approvals"), QCoreApplication::translate("gitlab", "Approvals"),
                   approvalsSection());
        setSection(QStringLiteral("people"), QCoreApplication::translate("gitlab", "People"),
                   peopleSection(mergeRequest));
        setSection(QStringLiteral("labels"), QCoreApplication::translate("gitlab", "Labels"),
                   labelChips(mergeRequest.value(QStringLiteral("labels")).toArray()));
        for (const QString &section : sections())
            setSectionVisible(section, m_model.isSectionShown(section));
    }

    QWidget *stateRow(const QJsonObject &mergeRequest) const
    {
        auto *row = new QWidget;
        auto *flow = new widgets::FlowRow(row);
        flow->addWidget(pillWidget(m_model.state()));
        if (m_model.isDraft())
            flow->addWidget(new StatusPill(QCoreApplication::translate("gitlab", "Draft"), StatusRole::Waiting));
        flow->addWidget(plainWord(QStringLiteral("%1 → %2").arg(
            mergeRequest.value(QStringLiteral("source_branch")).toString(),
            mergeRequest.value(QStringLiteral("target_branch")).toString())));
        return row;
    }

    QWidget *pipelineSection() const
    {
        const QJsonObject pipeline = m_model.pipeline();
        if (pipeline.isEmpty())
            return plainLine(QCoreApplication::translate("gitlab", "No pipeline"));

        QWidget *column = newColumn();
        auto *top = new QWidget;
        auto *flow = new widgets::FlowRow(top);
        flow->addWidget(pillWidget(m_model.pipelineState()));
        flow->addWidget(plainWord(QStringLiteral("#%1").arg(pipeline.value(QStringLiteral("id")).toInteger())));
        if (QPushButton *web = webButton(QCoreApplication::translate("gitlab", "Web"), QStringLiteral("pipeline-web"),
                                         m_model.pipelineAddress()))
            flow->addWidget(web);
        column->layout()->addWidget(top);

        for (const MergeRequestModel::Job &job : m_model.failedJobs())
            column->layout()->addWidget(failedJobRow(job));
        return column;
    }

    // Its name, status, duration and a link to its log.
    static QWidget *failedJobRow(const MergeRequestModel::Job &job)
    {
        auto *row = new QWidget;
        auto *flow = new widgets::FlowRow(row);
        flow->addWidget(plainWord(job.name));
        flow->addWidget(pillWidget(job.state));
        if (!job.duration.isEmpty())
            flow->addWidget(plainWord(job.duration));
        if (QPushButton *button =
                webButton(QCoreApplication::translate("gitlab", "Log"), QStringLiteral("log-%1").arg(job.id), job.log))
            flow->addWidget(button);
        return row;
    }

    QWidget *approvalsSection() const
    {
        const MergeRequestModel::Approvals approvals = m_model.approvals();
        QWidget *column = newColumn();
        if (approvals.required > 0)
            column->layout()->addWidget(plainLine(QCoreApplication::translate("gitlab", "%1 of %2 approvals")
                                                      .arg(approvals.given)
                                                      .arg(approvals.required)));
        else if (approvals.given == 1)
            column->layout()->addWidget(plainLine(QCoreApplication::translate("gitlab", "1 approval")));
        else if (approvals.given > 1)
            column->layout()->addWidget(
                plainLine(QCoreApplication::translate("gitlab", "%1 approvals").arg(approvals.given)));
        else
            column->layout()->addWidget(plainLine(QCoreApplication::translate("gitlab", "No approvals")));
        if (approvals.given > 0)
            column->layout()->addWidget(plainLine(QCoreApplication::translate("gitlab", "Approved by %1")
                                                      .arg(approvals.approvers.join(QStringLiteral(", ")))));
        return column;
    }

    static QWidget *peopleSection(const QJsonObject &mergeRequest)
    {
        const auto namesOrNone = [](const QJsonValue &users) {
            const QString names = userNames(users.toArray());
            return names.isEmpty() ? QCoreApplication::translate("gitlab", "none") : names;
        };
        const QString author =
            mergeRequest.value(QStringLiteral("author")).toObject().value(QStringLiteral("name")).toString();

        QWidget *column = newColumn();
        column->layout()->addWidget(plainLine(QCoreApplication::translate("gitlab", "Author: %1").arg(author)));
        column->layout()->addWidget(plainLine(QCoreApplication::translate("gitlab", "Assignees: %1")
                                                  .arg(namesOrNone(mergeRequest.value(QStringLiteral("assignees"))))));
        column->layout()->addWidget(plainLine(QCoreApplication::translate("gitlab", "Reviewers: %1")
                                                  .arg(namesOrNone(mergeRequest.value(QStringLiteral("reviewers"))))));
        return column;
    }

    MergeRequestModel &m_model;
};

} // namespace

QWidget *makeMergeRequestLayout(MergeRequestModel &model, QWidget *parent)
{
    return new MergeRequestLayout(model, parent);
}

} // namespace mirante::modules::gitlab
