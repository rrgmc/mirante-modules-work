#include "GitLabPipelineView.h"

#include "GitLabUrls.h"
#include "GitLabViewSupport.h"

#include <mirante/ItemContext.h>
#include <mirante/widgets/ActionRow.h>
#include <mirante/widgets/FlowRow.h>

#include <QCoreApplication>
#include <QDesktopServices>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLayout>
#include <QList>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <utility>

using mirante::widgets::StatusPill;

namespace mirante::modules::gitlab {

namespace {

const QString kRetryPipeline = QStringLiteral("retry-pipeline");
const QString kCancelPipeline = QStringLiteral("cancel-pipeline");
const QString kTriggerPipeline = QStringLiteral("trigger-pipeline");
const QString kMergeRequest = QStringLiteral("merge-request");
const QString kJobsSection = QStringLiteral("jobs");

// How often the view asks for a refresh while the shown pipeline is unfinished.
constexpr int kRefreshMs = 15000;

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

class PipelineView : public GitLabSummary
{
public:
    PipelineView(ItemContext &context, QWidget *parent)
        : GitLabSummary(context, parent)
    {
        actions()->addAction(kRetryPipeline, QCoreApplication::translate("gitlab", "Retry pipeline"),
                             QCoreApplication::translate("gitlab", "Retry"), [this] {
            runAction(kRetryPipeline, [project = project(), pipeline = m_pipelineId](const GitLabClient &client,
                                                                                     GitLabClient::Done done) {
                client.retryPipeline(project, pipeline, std::move(done));
            });
        });
        actions()->addAction(kCancelPipeline, QCoreApplication::translate("gitlab", "Cancel pipeline"),
                             QCoreApplication::translate("gitlab", "Cancel"), [this] { cancelPipeline(); });
        actions()->addAction(kTriggerPipeline, QCoreApplication::translate("gitlab", "Trigger pipeline"),
                             QCoreApplication::translate("gitlab", "Trigger"), [this] { triggerPipeline(); });
        actions()->addLinkAction(kMergeRequest, QCoreApplication::translate("gitlab", "Merge request"),
                                 QCoreApplication::translate("gitlab", "MR"),
                                 [this] { QDesktopServices::openUrl(m_mergeRequestAddress); });
        addOpenPage();
        actions()->setActionVisible(kTriggerPipeline, false);
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
        connect(m_previous, &QPushButton::clicked, this, [this] { showAt(shownIndex() + 1); });
        steps->addWidget(m_previous);
        m_next = new QPushButton(QCoreApplication::translate("gitlab", "Next"));
        m_next->setObjectName(QStringLiteral("next-pipeline"));
        connect(m_next, &QPushButton::clicked, this, [this] { showAt(shownIndex() - 1); });
        steps->addWidget(m_next);
        steps->addStretch(1);
        m_head->layout()->addWidget(m_steps);
        setStateRow(m_head);

        m_refreshTimer = new QTimer(this);
        m_refreshTimer->setObjectName(QStringLiteral("pipeline-refresh"));
        m_refreshTimer->setInterval(kRefreshMs);
        connect(m_refreshTimer, &QTimer::timeout, this, [this] { refreshStatus(); });
    }

protected:
    void draw(const ItemStatus &status) override
    {
        readPipelines(status.data);
        m_branch = status.data.value(QStringLiteral("branch")).toString();
        actions()->setActionVisible(kTriggerPipeline, !m_branch.isEmpty());
        setMergeRequest(status.data.value(QStringLiteral("mergeRequest")).toObject());

        if (m_pipelines.isEmpty()) {
            // A branch without pipelines: the status says so. A new one can
            // still be started.
            m_pipelineId = 0;
            m_selectedId = 0;
            setTitle(status.summary);
            m_head->hide();
            setSectionVisible(kJobsSection, false);
            actions()->setActionVisible(kRetryPipeline, false);
            actions()->setActionVisible(kCancelPipeline, false);
            setPageAddress({});
            updateRefresh();
            return;
        }

        m_head->show();
        drawShown();
    }

    void showEvent(QShowEvent *event) override
    {
        GitLabSummary::showEvent(event);
        m_shown = true;
        updateRefresh();
    }

    void hideEvent(QHideEvent *event) override
    {
        GitLabSummary::hideEvent(event);
        m_shown = false;
        updateRefresh();
    }

private:
    // One pipeline that the view can show. jobs is undefined when the status
    // holds none, so the view asks for them when it is shown.
    struct Pipeline
    {
        QJsonObject pipeline;
        QJsonValue jobs;
        bool inGroup = false;

        qint64 id() const { return pipeline.value(QStringLiteral("id")).toInteger(); }
        QString state() const { return pipeline.value(QStringLiteral("status")).toString(); }
    };

    // Newest first: the latest, the rest of its group, then the older ones.
    void readPipelines(const QJsonObject &answer)
    {
        m_pipelines.clear();
        const QJsonObject latest = answer.value(QStringLiteral("pipeline")).toObject();
        if (latest.isEmpty())
            return;
        m_pipelines << Pipeline{latest, answer.value(QStringLiteral("jobs")), true};
        for (const QJsonValue &value : answer.value(QStringLiteral("siblings")).toArray()) {
            const QJsonObject sibling = value.toObject();
            m_pipelines << Pipeline{sibling.value(QStringLiteral("pipeline")).toObject(),
                                    sibling.value(QStringLiteral("jobs")), true};
        }
        for (const QJsonValue &value : answer.value(QStringLiteral("older")).toArray())
            m_pipelines << Pipeline{value.toObject(), QJsonValue(QJsonValue::Undefined), false};
    }

    // From the configuration for a merge request's branch, else the open merge
    // request of the branch, if the status found one.
    void setMergeRequest(const QJsonObject &mergeRequest)
    {
        const bool linked = config().value(QStringLiteral("branchSource")).toString() == QLatin1String("mergeRequest");
        const QUrl built = linked ? mergeRequestUrl(baseUrl(), config(), defaultProject()) : QUrl();
        const QUrl address = webAddress(mergeRequest, built);
        m_mergeRequestAddress = isWebAddress(address) ? address : QUrl();
        actions()->setActionVisible(kMergeRequest, !m_mergeRequestAddress.isEmpty());
    }

    // The latest, unless the user chose another that is still there.
    qsizetype shownIndex() const
    {
        for (qsizetype i = 0; i < m_pipelines.size(); ++i) {
            if (m_pipelines.at(i).id() == m_selectedId)
                return i;
        }
        return 0;
    }

    void showAt(qsizetype index)
    {
        if (index < 0 || index >= m_pipelines.size())
            return;
        // The latest is not held on to, so the view follows a newer one.
        m_selectedId = index == 0 ? 0 : m_pipelines.at(index).id();
        // An error of the last action was about the pipeline shown then.
        actions()->clearError();
        drawShown();
    }

    void drawShown()
    {
        const qsizetype index = shownIndex();
        const Pipeline shown = m_pipelines.at(index);
        const QString state = shown.state();
        m_pipelineId = shown.id();

        setTitle(QCoreApplication::translate("gitlab", "Pipeline #%1").arg(m_pipelineId));
        replaceInHead(m_summaryRow, summaryRow(shown.pipeline));
        replaceInHead(m_pills, pillRow(shown.id()));
        m_pills->setVisible(std::count_if(m_pipelines.cbegin(), m_pipelines.cend(), [](const Pipeline &pipeline) {
                                return pipeline.inGroup;
                            }) > 1);
        m_steps->setVisible(m_pipelines.size() > 1);
        m_previous->setEnabled(index + 1 < m_pipelines.size());
        m_next->setEnabled(index > 0);

        actions()->setActionVisible(kRetryPipeline, canRetryPipeline(state));
        actions()->setActionVisible(kCancelPipeline, canCancel(state));
        setPageAddress(webAddress(shown.pipeline, {}));

        drawJobsOf(shown);
        updateRefresh();
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

    QWidget *summaryRow(const QJsonObject &pipeline) const
    {
        const QString state = pipeline.value(QStringLiteral("status")).toString();
        auto *row = new QWidget;
        auto *flow = new widgets::FlowRow(row);
        flow->addWidget(new StatusPill(statusText(state), statusRole(state)));
        flow->addWidget(plainWord(m_branch));
        const QString time = durationText(pipeline.value(QStringLiteral("duration")));
        if (!time.isEmpty())
            flow->addWidget(plainWord(time));
        const QString created = pipeline.value(QStringLiteral("created_at")).toString();
        if (!created.isEmpty())
            flow->addWidget(plainWord(localDateTime(created)));
        return row;
    }

    // One pill per pipeline of the group.
    QWidget *pillRow(qint64 shownId)
    {
        auto *row = new QWidget;
        auto *flow = new widgets::FlowRow(row);
        for (qsizetype i = 0; i < m_pipelines.size(); ++i) {
            const Pipeline &pipeline = m_pipelines.at(i);
            if (!pipeline.inGroup)
                continue;
            const QString text = QStringLiteral("#%1 %2").arg(pipeline.id()).arg(statusText(pipeline.state()));
            auto *pill = new PipelinePill(text, statusRole(pipeline.state()), [this, i] { showAt(i); });
            pill->setObjectName(QStringLiteral("pipeline-pill-%1").arg(pipeline.id()));
            if (pipeline.id() == shownId) {
                QFont font = pill->font();
                font.setBold(true);
                pill->setFont(font);
            }
            flow->addWidget(pill);
        }
        return row;
    }

    void drawJobsOf(const Pipeline &shown)
    {
        // An answer on its way is for what was shown before.
        const int read = ++m_jobReads;
        if (shown.jobs.isArray()) {
            drawJobs(shown.jobs.toArray());
            return;
        }

        const qint64 id = shown.id();
        if (m_readJobs.contains(id))
            drawJobs(m_readJobs.value(id));
        else
            setJobsContent(plainLine(QCoreApplication::translate("gitlab", "Loading…")));
        ApiClient *client = server();
        if (!client) {
            setJobsContent(plainLine(QCoreApplication::translate("gitlab", "The jobs could not be read.")));
            return;
        }
        GitLabClient(*client, this).pipelineJobs(project(), id, GetOptions().maxRows, [this, read, id](const ApiReply &reply) {
            if (read != m_jobReads)
                return;
            if (reply.ok()) {
                m_readJobs.insert(id, reply.json.array());
                drawJobs(reply.json.array());
            } else if (!m_readJobs.contains(id)) {
                setJobsContent(plainLine(QCoreApplication::translate("gitlab", "The jobs could not be read.")));
            }
        });
    }

    void setJobsContent(QWidget *content)
    {
        setSection(kJobsSection, QCoreApplication::translate("gitlab", "Jobs"), content);
        setSectionVisible(kJobsSection, true);
    }

    // One table, so its columns line up across the stages, which head their
    // rows in the order the jobs were created. That order does not depend on
    // how GitLab sorts its answer.
    void drawJobs(const QJsonArray &jobs)
    {
        if (jobs.isEmpty()) {
            setJobsContent(plainLine(QCoreApplication::translate("gitlab", "No jobs")));
            return;
        }

        QList<QJsonObject> sorted;
        for (const QJsonValue &value : jobs)
            sorted << value.toObject();
        std::stable_sort(sorted.begin(), sorted.end(), [](const QJsonObject &a, const QJsonObject &b) {
            return a.value(QStringLiteral("id")).toInteger() < b.value(QStringLiteral("id")).toInteger();
        });
        QStringList stages;
        for (const QJsonObject &job : std::as_const(sorted)) {
            const QString stage = job.value(QStringLiteral("stage")).toString();
            if (!stages.contains(stage))
                stages << stage;
        }

        auto *table = new QWidget;
        auto *grid = new QGridLayout(table);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setColumnStretch(1, 1);
        int row = 0;
        for (const QString &stage : std::as_const(stages)) {
            QLabel *heading = plainWord(stage);
            QFont font = heading->font();
            font.setItalic(true);
            heading->setFont(font);
            grid->addWidget(heading, row++, 0, 1, 4);
            for (const QJsonObject &job : std::as_const(sorted)) {
                if (job.value(QStringLiteral("stage")).toString() == stage)
                    addJobRow(grid, row++, job);
            }
        }
        setJobsContent(table);
    }

    void addJobRow(QGridLayout *grid, int row, const QJsonObject &job)
    {
        const QString status = job.value(QStringLiteral("status")).toString();
        const qint64 id = job.value(QStringLiteral("id")).toInteger();
        grid->addWidget(jobPill(job), row, 0, Qt::AlignLeft | Qt::AlignVCenter);
        grid->addWidget(plainLine(job.value(QStringLiteral("name")).toString()), row, 1);
        grid->addWidget(plainWord(durationText(job.value(QStringLiteral("duration")))), row, 2);

        auto *links = new QWidget;
        auto *layout = new QHBoxLayout(links);
        layout->setContentsMargins(0, 0, 0, 0);
        if (hasRun(job)) {
            if (QPushButton *log = webButton(QCoreApplication::translate("gitlab", "Log"),
                                             QStringLiteral("log-%1").arg(id),
                                             QUrl(job.value(QStringLiteral("web_url")).toString())))
                layout->addWidget(log);
        }
        if (isFinishedJob(status)) {
            layout->addWidget(jobAction(QCoreApplication::translate("gitlab", "Retry"),
                                        QStringLiteral("retry-job-%1").arg(id),
                                        [project = project(), id](const GitLabClient &client, GitLabClient::Done done) {
                                            client.retryJob(project, id, std::move(done));
                                        }));
        }
        if (status == QLatin1String("manual")) {
            layout->addWidget(jobAction(QCoreApplication::translate("gitlab", "Play"),
                                        QStringLiteral("play-job-%1").arg(id),
                                        [project = project(), id](const GitLabClient &client, GitLabClient::Done done) {
                                            client.playJob(project, id, std::move(done));
                                        }));
        }
        layout->addStretch(1);
        grid->addWidget(links, row, 3);
    }

    QPushButton *jobAction(const QString &text, const QString &name, const Request &request)
    {
        QPushButton *button = linkButton(text, name);
        connect(button, &QPushButton::clicked, this, [this, button, request] {
            // Not in the action row, so it clears the last error itself.
            actions()->clearError();
            button->setEnabled(false);
            const QPointer<QPushButton> guard(button);
            run(request, [guard] {
                // A new status may have replaced the button by now.
                if (guard)
                    guard->setEnabled(true);
            });
        });
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
        const qint64 pipeline = m_pipelineId;
        if (!confirm(QCoreApplication::translate("gitlab", "Cancel pipeline"),
                     QCoreApplication::translate("gitlab", "Cancel pipeline #%1?").arg(pipeline)))
            return;
        runAction(kCancelPipeline, [project = project(), pipeline](const GitLabClient &client,
                                                                   GitLabClient::Done done) {
            client.cancelPipeline(project, pipeline, std::move(done));
        });
    }

    // It starts work on the branch, so it asks first.
    void triggerPipeline()
    {
        const QString branch = m_branch;
        if (!confirm(QCoreApplication::translate("gitlab", "Trigger pipeline"),
                     QCoreApplication::translate("gitlab", "Start a new pipeline on %1?").arg(branch)))
            return;
        // The new pipeline will be the latest, so show that when it comes.
        m_selectedId = 0;
        runAction(kTriggerPipeline, [project = project(), branch](const GitLabClient &client, GitLabClient::Done done) {
            client.createPipeline(project, branch, std::move(done));
        });
    }

    void updateRefresh()
    {
        const bool unfinished = !m_pipelines.isEmpty() && isUnfinished(m_pipelines.at(shownIndex()).state());
        if (!m_shown || !unfinished)
            m_refreshTimer->stop();
        else if (!m_refreshTimer->isActive())
            m_refreshTimer->start();
    }

    QList<Pipeline> m_pipelines;
    QHash<qint64, QJsonArray> m_readJobs;
    QString m_branch;
    QUrl m_mergeRequestAddress;
    qint64 m_pipelineId = 0;
    // The pipeline the user chose, or 0 for the latest.
    qint64 m_selectedId = 0;
    // Only the answer to the latest read of a pipeline's jobs counts.
    int m_jobReads = 0;
    bool m_shown = false;
    QWidget *m_head = nullptr;
    QWidget *m_summaryRow = nullptr;
    QWidget *m_pills = nullptr;
    QWidget *m_steps = nullptr;
    QPushButton *m_previous = nullptr;
    QPushButton *m_next = nullptr;
    QTimer *m_refreshTimer = nullptr;
};

} // namespace

QWidget *createPipelineView(ItemContext &context, QWidget *parent)
{
    auto *view = new PipelineView(context, parent);
    view->follow(context.status());
    return view;
}

} // namespace mirante::modules::gitlab
