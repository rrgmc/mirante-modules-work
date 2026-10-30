#include "GitLabPipelineView.h"

#include "GitLabViewSupport.h"

#include <mirante/ItemContext.h>
#include <mirante/widgets/ActionRow.h>
#include <mirante/widgets/FlowRow.h>

#include <QLabel>
#include <QLayout>
#include <QList>
#include <QMessageBox>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <utility>

using mirante::widgets::StatusPill;

namespace mirante::modules::gitlab {

namespace {

const QString kRetryPipeline = QStringLiteral("retry-pipeline");
const QString kCancelPipeline = QStringLiteral("cancel-pipeline");

// The statuses in which GitLab can cancel a pipeline.
bool canCancel(const QString &status)
{
    static const QSet<QString> statuses{
        QStringLiteral("created"),   QStringLiteral("waiting_for_resource"), QStringLiteral("waiting_for_callback"),
        QStringLiteral("preparing"), QStringLiteral("pending"),              QStringLiteral("running"),
        QStringLiteral("scheduled"),
    };
    return statuses.contains(status);
}

class PipelineView : public GitLabSummary
{
public:
    PipelineView(ItemContext &context, QWidget *parent)
        : GitLabSummary(context, {}, parent)
    {
        actions()->addAction(kRetryPipeline, tr("Retry pipeline"), tr("Retry"), [this] {
            runAction(kRetryPipeline, [project = project(), pipeline = m_pipelineId](const GitLabClient &client,
                                                                                     GitLabClient::Done done) {
                client.retryPipeline(project, pipeline, std::move(done));
            });
        });
        actions()->addAction(kCancelPipeline, tr("Cancel pipeline"), tr("Cancel"), [this] { cancelPipeline(); });
        addOpenPage();
    }

protected:
    void draw(const ItemStatus &status) override
    {
        const QJsonObject pipeline = status.data.value(QStringLiteral("pipeline")).toObject();
        if (pipeline.isEmpty()) {
            // A branch without pipelines: the status says so.
            setPageAddress({});
            setMessage(status.summary);
            return;
        }

        const QString state = pipeline.value(QStringLiteral("status")).toString();
        m_pipelineId = pipeline.value(QStringLiteral("id")).toInteger();
        setTitle(tr("Pipeline #%1").arg(m_pipelineId));
        setStateRow(stateRow(state, status.data.value(QStringLiteral("branch")).toString(),
                             pipeline.value(QStringLiteral("duration"))));
        actions()->setActionVisible(kRetryPipeline, canRetryPipeline(state));
        actions()->setActionVisible(kCancelPipeline, canCancel(state));
        setPageAddress(webAddress(pipeline, {}));
        drawStages(status.data.value(QStringLiteral("jobs")).toArray());
    }

private:
    static QWidget *stateRow(const QString &state, const QString &branch, const QJsonValue &duration)
    {
        auto *row = new QWidget;
        auto *flow = new widgets::FlowRow(row);
        flow->addWidget(new StatusPill(statusText(state), statusRole(state)));
        flow->addWidget(plainWord(branch));
        const QString time = durationText(duration);
        if (!time.isEmpty())
            flow->addWidget(plainWord(time));
        return row;
    }

    // One section per stage, in the order the jobs were created, so the
    // order does not depend on how GitLab sorts its answer. A stage the
    // pipeline no longer has stays hidden, because a section cannot be removed.
    void drawStages(const QJsonArray &jobs)
    {
        QList<QJsonObject> sorted;
        for (const QJsonValue &value : jobs)
            sorted << value.toObject();
        std::stable_sort(sorted.begin(), sorted.end(), [](const QJsonObject &a, const QJsonObject &b) {
            return a.value(QStringLiteral("id")).toInteger() < b.value(QStringLiteral("id")).toInteger();
        });

        QStringList stages;
        QList<QWidget *> columns;
        for (const QJsonObject &job : std::as_const(sorted)) {
            const QString stage = job.value(QStringLiteral("stage")).toString();
            qsizetype index = stages.indexOf(stage);
            if (index < 0) {
                index = stages.size();
                stages << stage;
                columns << newColumn();
            }
            columns[index]->layout()->addWidget(jobRow(job, true));
        }

        QSet<QString> shown;
        for (qsizetype i = 0; i < stages.size(); ++i) {
            const QString key = QStringLiteral("stage:") + stages[i];
            setSection(key, stages[i], columns[i]);
            setSectionVisible(key, true);
            shown.insert(key);
        }
        for (const QString &key : std::as_const(m_stages)) {
            if (!shown.contains(key))
                setSectionVisible(key, false);
        }
        m_stages.unite(shown);
    }

    void cancelPipeline()
    {
        const qint64 pipeline = m_pipelineId;
        // On the heap, because the view can be made again while the question
        // is open, and it then deletes the question too.
        const QPointer<QMessageBox> question =
            new QMessageBox(QMessageBox::Question, tr("Cancel pipeline"), tr("Cancel pipeline #%1?").arg(pipeline),
                            QMessageBox::Yes | QMessageBox::No, this);
        question->setDefaultButton(QMessageBox::No);
        const int answer = question->exec();
        if (!question)
            return;
        delete question.data();
        if (answer != QMessageBox::Yes)
            return;
        runAction(kCancelPipeline, [project = project(), pipeline](const GitLabClient &client,
                                                                   GitLabClient::Done done) {
            client.cancelPipeline(project, pipeline, std::move(done));
        });
    }

    qint64 m_pipelineId = 0;
    QSet<QString> m_stages;
};

} // namespace

QWidget *createPipelineView(ItemContext &context, QWidget *parent)
{
    auto *view = new PipelineView(context, parent);
    view->follow(context.status());
    return view;
}

} // namespace mirante::modules::gitlab
