#include "GitLabMergeRequestView.h"

#include "GitLabStatus.h"
#include "GitLabUrls.h"
#include "GitLabViewSupport.h"

#include <mirante/ItemContext.h>
#include <mirante/widgets/ActionRow.h>
#include <mirante/widgets/FlowRow.h>

#include <QCoreApplication>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QString>

#include <utility>

using mirante::widgets::StatusPill;
using mirante::widgets::StatusRole;

namespace mirante::modules::gitlab {

namespace {

const QString kRetryPipeline = QStringLiteral("retry-pipeline");

class MergeRequestView : public GitLabSummary
{
public:
    MergeRequestView(ItemContext &context, QWidget *parent)
        : GitLabSummary(context, parent)
    {
        actions()->addAction(kRetryPipeline, QCoreApplication::translate("gitlab", "Retry pipeline"),
                             QCoreApplication::translate("gitlab", "Retry"), [this] {
            runAction(kRetryPipeline, [project = project(), pipeline = m_pipelineId](const GitLabClient &client,
                                                                                     GitLabClient::Done done) {
                client.retryPipeline(project, pipeline, std::move(done));
            });
        });
        addOpenPage();
    }

protected:
    void draw(const ItemStatus &status) override
    {
        const QJsonObject mergeRequest = status.data.value(QStringLiteral("mergeRequest")).toObject();
        const QJsonObject approvals = status.data.value(QStringLiteral("approvals")).toObject();
        const QJsonObject pipeline = mergeRequest.value(QStringLiteral("head_pipeline")).toObject();
        const bool open = mergeRequest.value(QStringLiteral("state")).toString() == QLatin1String("opened");

        setTitle(mergeRequest.value(QStringLiteral("title")).toString());
        setStateRow(stateRow(mergeRequest, open));

        m_pipelineId = pipeline.value(QStringLiteral("id")).toInteger();
        actions()->setActionVisible(kRetryPipeline,
                                    canRetryPipeline(pipeline.value(QStringLiteral("status")).toString()));
        setPageAddress(webAddress(mergeRequest, mergeRequestUrl(baseUrl(), config(), defaultProject())));

        setSection(QStringLiteral("pipeline"), QCoreApplication::translate("gitlab", "Pipeline"),
                   pipelineSection(pipeline, status.data.value(QStringLiteral("jobs")).toArray()));
        setSection(QStringLiteral("approvals"), QCoreApplication::translate("gitlab", "Approvals"),
                   approvalsSection(approvals));
        setSection(QStringLiteral("people"), QCoreApplication::translate("gitlab", "People"),
                   peopleSection(mergeRequest));
        setSection(QStringLiteral("labels"), QCoreApplication::translate("gitlab", "Labels"),
                   labelChips(mergeRequest.value(QStringLiteral("labels")).toArray()));

        applyField(QStringLiteral("pipeline"), QStringLiteral("showPipeline"));
        applyField(QStringLiteral("approvals"), QStringLiteral("showApprovals"));
        applyField(QStringLiteral("people"), QStringLiteral("showPeople"));
        applyField(QStringLiteral("labels"), QStringLiteral("showLabels"));
    }

private:
    static QWidget *stateRow(const QJsonObject &mergeRequest, bool open)
    {
        auto *row = new QWidget;
        auto *flow = new widgets::FlowRow(row);
        flow->addWidget(statePill(mergeRequest.value(QStringLiteral("state")).toString()));
        if (open && mergeRequest.value(QStringLiteral("draft")).toBool())
            flow->addWidget(new StatusPill(QCoreApplication::translate("gitlab", "Draft"), StatusRole::Waiting));
        flow->addWidget(plainWord(QStringLiteral("%1 → %2").arg(
            mergeRequest.value(QStringLiteral("source_branch")).toString(),
            mergeRequest.value(QStringLiteral("target_branch")).toString())));
        return row;
    }

    QWidget *pipelineSection(const QJsonObject &pipeline, const QJsonArray &jobs)
    {
        if (pipeline.isEmpty())
            return plainLine(QCoreApplication::translate("gitlab", "No pipeline"));

        QWidget *column = newColumn();
        auto *top = new QWidget;
        auto *flow = new widgets::FlowRow(top);
        const QString state = pipeline.value(QStringLiteral("status")).toString();
        flow->addWidget(new StatusPill(statusText(state), statusRole(state)));
        flow->addWidget(plainWord(QStringLiteral("#%1").arg(pipeline.value(QStringLiteral("id")).toInteger())));
        if (QPushButton *web = webButton(QCoreApplication::translate("gitlab", "Web"), QStringLiteral("pipeline-web"),
                                         webAddress(pipeline, {})))
            flow->addWidget(web);
        column->layout()->addWidget(top);

        for (const QJsonValue &value : jobs) {
            const QJsonObject job = value.toObject();
            if (job.value(QStringLiteral("status")).toString() == QLatin1String("failed"))
                column->layout()->addWidget(failedJobRow(job));
        }
        return column;
    }

    // Its name, status, duration and a link to its log.
    static QWidget *failedJobRow(const QJsonObject &job)
    {
        auto *row = new QWidget;
        auto *flow = new widgets::FlowRow(row);
        flow->addWidget(plainWord(job.value(QStringLiteral("name")).toString()));
        flow->addWidget(jobPill(job));
        const QString duration = durationText(job.value(QStringLiteral("duration")));
        if (!duration.isEmpty())
            flow->addWidget(plainWord(duration));
        const QString log = QStringLiteral("log-%1").arg(job.value(QStringLiteral("id")).toInteger());
        if (QPushButton *button = webButton(QCoreApplication::translate("gitlab", "Log"), log,
                                            QUrl(job.value(QStringLiteral("web_url")).toString())))
            flow->addWidget(button);
        return row;
    }

    static QWidget *approvalsSection(const QJsonObject &approvals)
    {
        const QJsonArray entries = approvals.value(QStringLiteral("approved_by")).toArray();
        const int required = approvals.value(QStringLiteral("approvals_required")).toInt();
        const int given = int(entries.size());

        QJsonArray users;
        for (const QJsonValue &entry : entries)
            users.append(entry.toObject().value(QStringLiteral("user")));

        QWidget *column = newColumn();
        if (required > 0)
            column->layout()->addWidget(
                plainLine(QCoreApplication::translate("gitlab", "%1 of %2 approvals").arg(given).arg(required)));
        else if (given == 1)
            column->layout()->addWidget(plainLine(QCoreApplication::translate("gitlab", "1 approval")));
        else if (given > 1)
            column->layout()->addWidget(plainLine(QCoreApplication::translate("gitlab", "%1 approvals").arg(given)));
        else
            column->layout()->addWidget(plainLine(QCoreApplication::translate("gitlab", "No approvals")));
        if (given > 0)
            column->layout()->addWidget(
                plainLine(QCoreApplication::translate("gitlab", "Approved by %1").arg(userNames(users))));
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

    qint64 m_pipelineId = 0;
};

} // namespace

QWidget *createMergeRequestView(ItemContext &context, QWidget *parent)
{
    auto *view = new MergeRequestView(context, parent);
    view->follow(context.status());
    return view;
}

} // namespace mirante::modules::gitlab
