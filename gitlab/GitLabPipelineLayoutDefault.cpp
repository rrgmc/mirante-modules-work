// The default of the extension point "pipelinelayout": a SummaryView with the
// shown pipeline's state, the pills of its group, Previous and Next, the
// actions and a table of jobs by stage. It is compiled only when the app names
// no extension for "pipelinelayout". See README.md#layouts.
#include "GitLabPipelineLayout.h"
#include "GitLabViewSupport.h"

#include <mirante/widgets/ActionRow.h>
#include <mirante/widgets/FlowRow.h>
#include <mirante/widgets/SummaryView.h>

#include <QCoreApplication>
#include <QDesktopServices>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>

#include <functional>
#include <utility>

using mirante::widgets::StatusPill;

namespace mirante::modules::gitlab {

namespace {

const QString kMergeRequest = QStringLiteral("merge-request");
const QString kJobsSection = QStringLiteral("jobs");

// One pipeline of the group as a pill that shows it on a click.
class PipelinePill : public StatusPill
{
public:
    PipelinePill(const QString &text, widgets::StatusRole role, std::function<void()> clicked)
        : StatusPill(text, role)
        , m_clicked(std::move(clicked))
    {
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override { event->accept(); }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (rect().contains(event->position().toPoint()))
            m_clicked();
    }

private:
    std::function<void()> m_clicked;
};

class PipelineLayout : public widgets::SummaryView
{
public:
    PipelineLayout(PipelineModel &model, QWidget *parent)
        : widgets::SummaryView(parent)
        , m_model(model)
    {
        setObjectName(defaultLayoutName(QStringLiteral("pipeline")));
        followActions(this, model);
        actions()->addAction(PipelineModel::RetryAction, QCoreApplication::translate("gitlab", "Retry pipeline"),
                             QCoreApplication::translate("gitlab", "Retry"), [this] { m_model.retry(); });
        actions()->addAction(PipelineModel::CancelAction, QCoreApplication::translate("gitlab", "Cancel pipeline"),
                             QCoreApplication::translate("gitlab", "Cancel"), [this] { cancelPipeline(); });
        actions()->addAction(PipelineModel::TriggerAction, QCoreApplication::translate("gitlab", "Trigger pipeline"),
                             QCoreApplication::translate("gitlab", "Trigger"), [this] { triggerPipeline(); });
        actions()->addLinkAction(kMergeRequest, QCoreApplication::translate("gitlab", "Merge request"),
                                 QCoreApplication::translate("gitlab", "MR"),
                                 [this] { QDesktopServices::openUrl(m_model.mergeRequestAddress()); });
        addOpenPage(this, model);
        actions()->setActionVisible(PipelineModel::TriggerAction, false);
        actions()->setActionVisible(kMergeRequest, false);

        m_head = newColumn();
        m_summaryRow = new QWidget;
        m_head->layout()->addWidget(m_summaryRow);
        m_pills = new QWidget;
        m_head->layout()->addWidget(m_pills);

        // These stay through a new status, so they keep the keyboard's focus.
        m_steps = new QWidget;
        auto *steps = new QHBoxLayout(m_steps);
        steps->setContentsMargins(0, 0, 0, 0);
        m_previous = new QPushButton(QCoreApplication::translate("gitlab", "Previous"));
        m_previous->setObjectName(QStringLiteral("previous-pipeline"));
        connect(m_previous, &QPushButton::clicked, this,
                [this] { m_model.showPipeline(m_model.shownIndex() + 1); });
        steps->addWidget(m_previous);
        m_next = new QPushButton(QCoreApplication::translate("gitlab", "Next"));
        m_next->setObjectName(QStringLiteral("next-pipeline"));
        connect(m_next, &QPushButton::clicked, this, [this] { m_model.showPipeline(m_model.shownIndex() - 1); });
        steps->addWidget(m_next);
        steps->addStretch(1);
        m_head->layout()->addWidget(m_steps);
        setStateRow(m_head);

        connect(&model, &SummaryModel::changed, this, [this] { draw(); });
        // A job's buttons are not in the action row, so they follow their own
        // busy state.
        connect(&model, &SummaryModel::busyChanged, this, [this](const QString &action) {
            if (QPushButton *button = findChild<QPushButton *>(action))
                button->setEnabled(!m_model.isBusy(action));
        });
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
        setTitle(m_model.title());
        actions()->setActionVisible(PipelineModel::TriggerAction, m_model.canTrigger());
        actions()->setActionVisible(PipelineModel::RetryAction, m_model.canRetry());
        actions()->setActionVisible(PipelineModel::CancelAction, m_model.canCancel());
        actions()->setActionVisible(kMergeRequest, !m_model.mergeRequestAddress().isEmpty());
        showPageAddress(this, m_model);

        if (!m_model.hasPipelines()) {
            m_head->hide();
            setSectionVisible(kJobsSection, false);
            return;
        }

        m_head->show();
        const qsizetype index = m_model.shownIndex();
        const PipelineModel::Pipeline shown = m_model.shownPipeline();
        replaceInHead(m_summaryRow, summaryRow(shown));
        replaceInHead(m_pills, pillRow(shown.id));
        m_pills->setVisible(m_model.hasGroup());
        m_steps->setVisible(m_model.pipelines().size() > 1);
        m_previous->setEnabled(index + 1 < m_model.pipelines().size());
        m_next->setEnabled(index > 0);
        drawJobs();
    }

    // Not setStateRow(): the head stays, and only these parts of it change.
    // Deleted later, since this can run in answer to a click on a pill.
    void replaceInHead(QWidget *&part, QWidget *next)
    {
        delete m_head->layout()->replaceWidget(part, next);
        part->hide();
        part->setParent(nullptr);
        part->deleteLater();
        part = next;
        next->show();
    }

    QWidget *summaryRow(const PipelineModel::Pipeline &pipeline) const
    {
        auto *row = new QWidget;
        auto *flow = new widgets::FlowRow(row);
        flow->addWidget(pillWidget(pipeline.state));
        flow->addWidget(plainWord(m_model.branch()));
        if (!pipeline.duration.isEmpty())
            flow->addWidget(plainWord(pipeline.duration));
        if (!pipeline.created.isEmpty())
            flow->addWidget(plainWord(pipeline.created));
        return row;
    }

    // One pill per pipeline of the group.
    QWidget *pillRow(qint64 shownId)
    {
        auto *row = new QWidget;
        auto *flow = new widgets::FlowRow(row);
        const QList<PipelineModel::Pipeline> pipelines = m_model.pipelines();
        for (qsizetype i = 0; i < pipelines.size(); ++i) {
            const PipelineModel::Pipeline &pipeline = pipelines.at(i);
            if (!pipeline.inGroup)
                continue;
            const QString text = QStringLiteral("#%1 %2").arg(pipeline.id).arg(pipeline.state.text);
            auto *pill = new PipelinePill(text, pipeline.state.role, [this, i] { m_model.showPipeline(i); });
            pill->setObjectName(QStringLiteral("pipeline-pill-%1").arg(pipeline.id));
            if (pipeline.id == shownId) {
                QFont font = pill->font();
                font.setBold(true);
                pill->setFont(font);
            }
            flow->addWidget(pill);
        }
        return row;
    }

    void setJobsContent(QWidget *content)
    {
        setSection(kJobsSection, QCoreApplication::translate("gitlab", "Jobs"), content);
        setSectionVisible(kJobsSection, true);
    }

    // One table, so its columns line up across the stages, which head their
    // rows in the order the jobs were created.
    void drawJobs()
    {
        switch (m_model.jobsState()) {
        case PipelineModel::Jobs::Loading:
            setJobsContent(plainLine(QCoreApplication::translate("gitlab", "Loading…")));
            return;
        case PipelineModel::Jobs::Unreadable:
            setJobsContent(plainLine(QCoreApplication::translate("gitlab", "The jobs could not be read.")));
            return;
        case PipelineModel::Jobs::Shown:
            break;
        }
        const QList<PipelineModel::Job> jobs = m_model.jobs();
        if (jobs.isEmpty()) {
            setJobsContent(plainLine(QCoreApplication::translate("gitlab", "No jobs")));
            return;
        }

        auto *table = new QWidget;
        auto *grid = new QGridLayout(table);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setColumnStretch(1, 1);
        int row = 0;
        for (const QString &stage : m_model.stages()) {
            QLabel *heading = plainWord(stage);
            QFont font = heading->font();
            font.setItalic(true);
            heading->setFont(font);
            grid->addWidget(heading, row++, 0, 1, 4);
            for (const PipelineModel::Job &job : jobs) {
                if (job.stage == stage)
                    addJobRow(grid, row++, job);
            }
        }
        setJobsContent(table);
    }

    void addJobRow(QGridLayout *grid, int row, const PipelineModel::Job &job)
    {
        grid->addWidget(pillWidget(job.state), row, 0, Qt::AlignLeft | Qt::AlignVCenter);
        grid->addWidget(plainLine(job.name), row, 1);
        grid->addWidget(plainWord(job.duration), row, 2);

        auto *links = new QWidget;
        auto *layout = new QHBoxLayout(links);
        layout->setContentsMargins(0, 0, 0, 0);
        if (QPushButton *log =
                webButton(QCoreApplication::translate("gitlab", "Log"), QStringLiteral("log-%1").arg(job.id), job.log))
            layout->addWidget(log);
        const qint64 id = job.id;
        if (job.canRetry)
            layout->addWidget(jobButton(QCoreApplication::translate("gitlab", "Retry"),
                                        PipelineModel::retryJobAction(id), [this, id] { m_model.retryJob(id); }));
        if (job.canPlay)
            layout->addWidget(jobButton(QCoreApplication::translate("gitlab", "Play"),
                                        PipelineModel::playJobAction(id), [this, id] { m_model.playJob(id); }));
        layout->addStretch(1);
        grid->addWidget(links, row, 3);
    }

    QPushButton *jobButton(const QString &text, const QString &action, const std::function<void()> &clicked)
    {
        QPushButton *button = linkButton(text, action);
        button->setEnabled(!m_model.isBusy(action));
        connect(button, &QPushButton::clicked, this, clicked);
        return button;
    }

    // Asks first, and is false if the answer is no or the view is gone. Nothing
    // may touch the view after a false. On the heap, because the view can be
    // made again while the question is open, and it then deletes the question.
    bool confirm(const QString &title, const QString &text)
    {
        const QPointer<QMessageBox> question =
            new QMessageBox(QMessageBox::Question, title, text, QMessageBox::Yes | QMessageBox::No, this);
        question->setTextFormat(Qt::PlainText);
        question->setDefaultButton(QMessageBox::No);
        const int answer = question->exec();
        if (!question)
            return false;
        delete question.data();
        return answer == QMessageBox::Yes;
    }

    void cancelPipeline()
    {
        if (!confirm(QCoreApplication::translate("gitlab", "Cancel pipeline"),
                     QCoreApplication::translate("gitlab", "Cancel pipeline #%1?").arg(m_model.shownPipeline().id)))
            return;
        m_model.cancel();
    }

    // It starts work on the branch, so it asks first.
    void triggerPipeline()
    {
        if (!confirm(QCoreApplication::translate("gitlab", "Trigger pipeline"),
                     QCoreApplication::translate("gitlab", "Start a new pipeline on %1?").arg(m_model.branch())))
            return;
        m_model.trigger();
    }

    PipelineModel &m_model;
    QWidget *m_head = nullptr;
    QWidget *m_summaryRow = nullptr;
    QWidget *m_pills = nullptr;
    QWidget *m_steps = nullptr;
    QPushButton *m_previous = nullptr;
    QPushButton *m_next = nullptr;
};

} // namespace

QWidget *makePipelineLayout(PipelineModel &model, QWidget *parent)
{
    return new PipelineLayout(model, parent);
}

} // namespace mirante::modules::gitlab
