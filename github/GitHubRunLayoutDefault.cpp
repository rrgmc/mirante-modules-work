// The default of the extension point "runlayout": a SummaryView with the shown
// run's state, the pills of its group, Previous and Next, the actions and a
// table of jobs. It is compiled only when the app names no extension for
// "runlayout". See README.md#layouts.
#include "GitHubRunLayout.h"
#include "GitHubViewSupport.h"

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

namespace mirante::modules::github {

namespace {

const QString kPullRequest = QStringLiteral("pull-request");
const QString kJobsSection = QStringLiteral("jobs");

// One run of the group as a pill that shows it on a click.
class RunPill : public StatusPill
{
public:
    RunPill(const Pill &pill, std::function<void()> clicked)
        : StatusPill(pill.text, pill.role)
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

class RunLayout : public widgets::SummaryView
{
public:
    RunLayout(RunModel &model, QWidget *parent)
        : widgets::SummaryView(parent)
        , m_model(model)
    {
        setObjectName(defaultLayoutName(QStringLiteral("run")));
        followActions(this, model);
        actions()->addAction(RunModel::RerunFailedAction, QCoreApplication::translate("github", "Re-run failed jobs"),
                             QCoreApplication::translate("github", "Re-run failed"), [this] { m_model.rerunFailed(); });
        actions()->addAction(RunModel::RerunAction, QCoreApplication::translate("github", "Re-run"),
                             QCoreApplication::translate("github", "Re-run"), [this] { m_model.rerun(); });
        actions()->addAction(RunModel::CancelAction, QCoreApplication::translate("github", "Cancel run"),
                             QCoreApplication::translate("github", "Cancel"), [this] { cancelRun(); });
        actions()->addLinkAction(kPullRequest, QCoreApplication::translate("github", "Pull request"),
                                 QCoreApplication::translate("github", "PR"),
                                 [this] { QDesktopServices::openUrl(m_model.pullRequestAddress()); });
        addOpenPage(this, model);
        actions()->setActionVisible(kPullRequest, false);

        m_head = newColumn();
        m_summaryRow = new QWidget;
        m_head->layout()->addWidget(m_summaryRow);
        m_pills = new QWidget;
        m_head->layout()->addWidget(m_pills);

        // These stay through a new status, so they keep the keyboard's focus.
        m_steps = new QWidget;
        auto *steps = new QHBoxLayout(m_steps);
        steps->setContentsMargins(0, 0, 0, 0);
        m_previous = new QPushButton(QCoreApplication::translate("github", "Previous"));
        m_previous->setObjectName(QStringLiteral("previous-run"));
        connect(m_previous, &QPushButton::clicked, this, [this] { m_model.showRun(m_model.shownIndex() + 1); });
        steps->addWidget(m_previous);
        m_next = new QPushButton(QCoreApplication::translate("github", "Next"));
        m_next->setObjectName(QStringLiteral("next-run"));
        connect(m_next, &QPushButton::clicked, this, [this] { m_model.showRun(m_model.shownIndex() - 1); });
        steps->addWidget(m_next);
        steps->addStretch(1);
        m_head->layout()->addWidget(m_steps);
        setStateRow(m_head);

        connect(&model, &SummaryModel::changed, this, [this] { draw(); });
        // A job's Re-run is not in the action row, so it follows its own busy state.
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
        actions()->setActionVisible(RunModel::RerunFailedAction, m_model.canRerunFailed());
        actions()->setActionVisible(RunModel::RerunAction, m_model.canRerun());
        actions()->setActionVisible(RunModel::CancelAction, m_model.canCancel());
        actions()->setActionVisible(kPullRequest, !m_model.pullRequestAddress().isEmpty());
        showPageAddress(this, m_model);

        if (!m_model.hasRuns()) {
            m_head->hide();
            setSectionVisible(kJobsSection, false);
            return;
        }

        m_head->show();
        const qsizetype index = m_model.shownIndex();
        const RunModel::Run shown = m_model.shownRun();
        replaceInHead(m_summaryRow, summaryRow(shown));
        replaceInHead(m_pills, pillRow(shown.id));
        m_pills->setVisible(m_model.hasGroup());
        m_steps->setVisible(m_model.runs().size() > 1);
        m_previous->setEnabled(index + 1 < m_model.runs().size());
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

    static QWidget *summaryRow(const RunModel::Run &run)
    {
        auto *row = new QWidget;
        auto *flow = new widgets::FlowRow(row);
        flow->addWidget(pillWidget(run.state));
        flow->addWidget(plainWord(run.branch));
        if (!run.duration.isEmpty())
            flow->addWidget(plainWord(run.duration));
        if (!run.started.isEmpty())
            flow->addWidget(plainWord(run.started));
        return row;
    }

    // One pill per run of the group, named by its workflow.
    QWidget *pillRow(qint64 shownId)
    {
        auto *row = new QWidget;
        auto *flow = new widgets::FlowRow(row);
        const QList<RunModel::Run> runs = m_model.runs();
        for (qsizetype i = 0; i < runs.size(); ++i) {
            const RunModel::Run &run = runs.at(i);
            if (!run.inGroup)
                continue;
            auto *pill = new RunPill({run.workflow, run.state.role}, [this, i] { m_model.showRun(i); });
            pill->setObjectName(QStringLiteral("run-pill-%1").arg(run.id));
            pill->setToolTip(run.state.text);
            if (run.id == shownId) {
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
        setSection(kJobsSection, QCoreApplication::translate("github", "Jobs"), content);
        setSectionVisible(kJobsSection, true);
    }

    // One table, in the order of GitHub's answer.
    void drawJobs()
    {
        switch (m_model.jobsState()) {
        case RunModel::Jobs::Loading:
            setJobsContent(plainLine(QCoreApplication::translate("github", "Loading…")));
            return;
        case RunModel::Jobs::Unreadable:
            setJobsContent(plainLine(QCoreApplication::translate("github", "The jobs could not be read.")));
            return;
        case RunModel::Jobs::Shown:
            break;
        }
        const QList<RunModel::Job> jobs = m_model.jobs();
        if (jobs.isEmpty()) {
            setJobsContent(plainLine(QCoreApplication::translate("github", "No jobs")));
            return;
        }

        auto *table = new QWidget;
        auto *grid = new QGridLayout(table);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setColumnStretch(1, 1);
        int row = 0;
        for (const RunModel::Job &job : jobs)
            addJobRow(grid, row++, job);
        setJobsContent(table);
    }

    void addJobRow(QGridLayout *grid, int row, const RunModel::Job &job)
    {
        grid->addWidget(pillWidget(job.state), row, 0, Qt::AlignLeft | Qt::AlignVCenter);
        grid->addWidget(plainLine(job.name), row, 1);
        grid->addWidget(plainWord(job.duration), row, 2);

        auto *links = new QWidget;
        auto *layout = new QHBoxLayout(links);
        layout->setContentsMargins(0, 0, 0, 0);
        if (QPushButton *log =
                webButton(QCoreApplication::translate("github", "Log"), QStringLiteral("log-%1").arg(job.id), job.log))
            layout->addWidget(log);
        if (job.canRerun) {
            const QString action = RunModel::rerunJobAction(job.id);
            QPushButton *button = linkButton(QCoreApplication::translate("github", "Re-run"), action);
            button->setEnabled(!m_model.isBusy(action));
            const qint64 id = job.id;
            connect(button, &QPushButton::clicked, this, [this, id] { m_model.rerunJob(id); });
            layout->addWidget(button);
        }
        layout->addStretch(1);
        grid->addWidget(links, row, 3);
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

    void cancelRun()
    {
        if (!confirm(QCoreApplication::translate("github", "Cancel run"),
                     QCoreApplication::translate("github", "Cancel %1?").arg(m_model.title())))
            return;
        m_model.cancel();
    }

    RunModel &m_model;
    QWidget *m_head = nullptr;
    QWidget *m_summaryRow = nullptr;
    QWidget *m_pills = nullptr;
    QWidget *m_steps = nullptr;
    QPushButton *m_previous = nullptr;
    QPushButton *m_next = nullptr;
};

} // namespace

QWidget *makeRunLayout(RunModel &model, QWidget *parent)
{
    return new RunLayout(model, parent);
}

} // namespace mirante::modules::github
