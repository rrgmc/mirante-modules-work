#include "GitHubRunView.h"

#include "GitHubUrls.h"
#include "GitHubViewSupport.h"

#include <mirante/ItemContext.h>
#include <mirante/widgets/ActionRow.h>
#include <mirante/widgets/FlowRow.h>

#include <QCoreApplication>
#include <QDateTime>
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
#include <QString>
#include <QTimer>

#include <algorithm>
#include <functional>
#include <utility>

using mirante::widgets::StatusPill;

namespace mirante::modules::github {

namespace {

const QString kRerunFailed = QStringLiteral("rerun-failed");
const QString kRerunRun = QStringLiteral("rerun-run");
const QString kCancelRun = QStringLiteral("cancel-run");
const QString kPullRequest = QStringLiteral("pull-request");
const QString kJobsSection = QStringLiteral("jobs");

// How often the view asks for a refresh while the shown run has not completed.
constexpr int kRefreshMs = 15000;

bool isCompleted(const QString &status)
{
    return status == QLatin1String("completed");
}

// One run of the group as a pill that shows it on a click.
class RunPill : public StatusPill
{
public:
    RunPill(const QString &text, widgets::StatusRole role, std::function<void()> clicked)
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

class RunView : public GitHubSummary
{
public:
    RunView(ItemContext &context, QWidget *parent)
        : GitHubSummary(context, parent)
    {
        actions()->addAction(kRerunFailed, QCoreApplication::translate("github", "Re-run failed jobs"),
                             QCoreApplication::translate("github", "Re-run failed"), [this] {
            runAction(kRerunFailed, [repository = repository(), run = m_runId](const GitHubClient &client,
                                                                                GitHubClient::Done done) {
                client.rerunFailedJobs(repository, run, std::move(done));
            });
        });
        actions()->addAction(kRerunRun, QCoreApplication::translate("github", "Re-run"),
                             QCoreApplication::translate("github", "Re-run"), [this] {
            runAction(kRerunRun, [repository = repository(), run = m_runId](const GitHubClient &client,
                                                                             GitHubClient::Done done) {
                client.rerunRun(repository, run, std::move(done));
            });
        });
        actions()->addAction(kCancelRun, QCoreApplication::translate("github", "Cancel run"),
                             QCoreApplication::translate("github", "Cancel"), [this] { cancelRun(); });
        actions()->addLinkAction(kPullRequest, QCoreApplication::translate("github", "Pull request"),
                                 QCoreApplication::translate("github", "PR"),
                                 [this] { QDesktopServices::openUrl(m_pullRequestAddress); });
        addOpenPage();
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
        connect(m_previous, &QPushButton::clicked, this, [this] { showAt(shownIndex() + 1); });
        steps->addWidget(m_previous);
        m_next = new QPushButton(QCoreApplication::translate("github", "Next"));
        m_next->setObjectName(QStringLiteral("next-run"));
        connect(m_next, &QPushButton::clicked, this, [this] { showAt(shownIndex() - 1); });
        steps->addWidget(m_next);
        steps->addStretch(1);
        m_head->layout()->addWidget(m_steps);
        setStateRow(m_head);

        m_refreshTimer = new QTimer(this);
        m_refreshTimer->setObjectName(QStringLiteral("run-refresh"));
        m_refreshTimer->setInterval(kRefreshMs);
        connect(m_refreshTimer, &QTimer::timeout, this, [this] { refreshStatus(); });
    }

protected:
    void draw(const ItemStatus &status) override
    {
        readRuns(status.data);
        m_branch = status.data.value(QStringLiteral("branch")).toString();
        m_linkedPullRequest = status.data.value(QStringLiteral("pullRequest")).toObject();

        if (m_runs.isEmpty()) {
            // A branch without runs: the status says so.
            m_runId = 0;
            m_selectedId = 0;
            setTitle(status.summary);
            m_head->hide();
            setSectionVisible(kJobsSection, false);
            actions()->setActionVisible(kRerunFailed, false);
            actions()->setActionVisible(kRerunRun, false);
            actions()->setActionVisible(kCancelRun, false);
            setPullRequest({});
            setPageAddress({});
            updateRefresh();
            return;
        }

        m_head->show();
        drawShown();
    }

    void showEvent(QShowEvent *event) override
    {
        GitHubSummary::showEvent(event);
        m_shown = true;
        updateRefresh();
    }

    void hideEvent(QHideEvent *event) override
    {
        GitHubSummary::hideEvent(event);
        m_shown = false;
        updateRefresh();
    }

private:
    // One run that the view can show. jobs is undefined when the status holds
    // none, so the view asks for them when it is shown.
    struct Run
    {
        QJsonObject run;
        QJsonValue jobs;
        bool inGroup = false;

        qint64 id() const { return run.value(QStringLiteral("id")).toInteger(); }
        QString status() const { return run.value(QStringLiteral("status")).toString(); }
        QString conclusion() const { return run.value(QStringLiteral("conclusion")).toString(); }
        QDateTime created() const
        {
            return QDateTime::fromString(run.value(QStringLiteral("created_at")).toString(), Qt::ISODateWithMs);
        }
    };

    // All runs of the status, newest first, as the status sorts a group: by
    // created_at, then by id. A date that cannot be read counts as the oldest.
    void readRuns(const QJsonObject &answer)
    {
        m_runs.clear();
        const QJsonObject lead = answer.value(QStringLiteral("run")).toObject();
        if (lead.isEmpty()) {
            m_leadId = 0;
            return;
        }
        m_runs << Run{lead, answer.value(QStringLiteral("jobs")), true};
        m_leadId = m_runs.constFirst().id();
        for (const QJsonValue &value : answer.value(QStringLiteral("siblings")).toArray()) {
            const QJsonObject sibling = value.toObject();
            m_runs << Run{sibling.value(QStringLiteral("run")).toObject(), sibling.value(QStringLiteral("jobs")), true};
        }
        for (const QJsonValue &value : answer.value(QStringLiteral("older")).toArray())
            m_runs << Run{value.toObject(), QJsonValue(QJsonValue::Undefined), false};

        std::stable_sort(m_runs.begin(), m_runs.end(), [](const Run &a, const Run &b) {
            const QDateTime first = a.created();
            const QDateTime second = b.created();
            if (first.isValid() != second.isValid())
                return first.isValid();
            if (first != second)
                return first > second;
            return a.id() > b.id();
        });
    }

    // The lead, unless the user chose another that is still there.
    qsizetype shownIndex() const
    {
        const qint64 wanted = m_selectedId != 0 ? m_selectedId : m_leadId;
        for (qsizetype i = 0; i < m_runs.size(); ++i) {
            if (m_runs.at(i).id() == wanted)
                return i;
        }
        for (qsizetype i = 0; i < m_runs.size(); ++i) {
            if (m_runs.at(i).id() == m_leadId)
                return i;
        }
        return 0;
    }

    void showAt(qsizetype index)
    {
        if (index < 0 || index >= m_runs.size())
            return;
        // The lead is not held on to, so the view follows a newer one.
        const qint64 id = m_runs.at(index).id();
        m_selectedId = id == m_leadId ? 0 : id;
        // An error of the last action was about the run shown then.
        actions()->clearError();
        drawShown();
    }

    void drawShown()
    {
        const qsizetype index = shownIndex();
        const Run shown = m_runs.at(index);
        const QString state = shown.status();
        m_runId = shown.id();
        m_runName = runName(shown.run);

        setTitle(m_runName);
        replaceInHead(m_summaryRow, summaryRow(shown.run));
        replaceInHead(m_pills, pillRow(shown.id()));
        m_pills->setVisible(std::count_if(m_runs.cbegin(), m_runs.cend(), [](const Run &run) {
                                return run.inGroup;
                            }) > 1);
        m_steps->setVisible(m_runs.size() > 1);
        m_previous->setEnabled(index + 1 < m_runs.size());
        m_next->setEnabled(index > 0);

        actions()->setActionVisible(kRerunFailed, isCompleted(state) && isFailedConclusion(shown.conclusion()));
        actions()->setActionVisible(kRerunRun, isCompleted(state));
        actions()->setActionVisible(kCancelRun, canCancelRun(state));
        setPullRequest(shown.run);
        setPageAddress(webAddress(shown.run, {}));

        drawJobsOf(shown);
        updateRefresh();
    }

    // Such as "CI #812".
    static QString runName(const QJsonObject &run)
    {
        return QCoreApplication::translate("github", "%1 #%2")
            .arg(run.value(QStringLiteral("name")).toString())
            .arg(run.value(QStringLiteral("run_number")).toInteger());
    }

    // The linked pull request for a pull request's branch; else the shown
    // run's first pull request, if it has one.
    void setPullRequest(const QJsonObject &run)
    {
        QUrl address;
        const bool linked = config().value(QStringLiteral("branchSource")).toString() == QLatin1String("pullRequest");
        if (linked) {
            address = webAddress(m_linkedPullRequest, pullRequestUrl(webBase(), config(), defaultRepository()));
        } else {
            const QJsonArray pullRequests = run.value(QStringLiteral("pull_requests")).toArray();
            const qint64 number = pullRequests.isEmpty()
                ? 0
                : pullRequests.first().toObject().value(QStringLiteral("number")).toInteger();
            const QString repository = this->repository();
            if (number > 0 && !repository.isEmpty())
                address = webPageUrl(webBase(), QStringLiteral("%1/pull/%2").arg(repository).arg(number));
        }
        m_pullRequestAddress = isWebAddress(address) ? address : QUrl();
        actions()->setActionVisible(kPullRequest, !m_pullRequestAddress.isEmpty());
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

    QWidget *summaryRow(const QJsonObject &run) const
    {
        auto *row = new QWidget;
        auto *flow = new widgets::FlowRow(row);
        flow->addWidget(runPill(run));
        const QString branch = m_branch.isEmpty() ? run.value(QStringLiteral("head_branch")).toString() : m_branch;
        flow->addWidget(plainWord(branch));
        const QString started = run.value(QStringLiteral("run_started_at")).toString().isEmpty()
            ? run.value(QStringLiteral("created_at")).toString()
            : run.value(QStringLiteral("run_started_at")).toString();
        if (isCompleted(run.value(QStringLiteral("status")).toString())) {
            const QString time = durationBetween(started, run.value(QStringLiteral("updated_at")).toString());
            if (!time.isEmpty())
                flow->addWidget(plainWord(time));
        }
        if (!started.isEmpty())
            flow->addWidget(plainWord(localDateTime(started)));
        return row;
    }

    // One pill per run of the group, named by its workflow.
    QWidget *pillRow(qint64 shownId)
    {
        auto *row = new QWidget;
        auto *flow = new widgets::FlowRow(row);
        for (qsizetype i = 0; i < m_runs.size(); ++i) {
            const Run &run = m_runs.at(i);
            if (!run.inGroup)
                continue;
            auto *pill = new RunPill(run.run.value(QStringLiteral("name")).toString(),
                                     statusRole(run.status(), run.conclusion()), [this, i] { showAt(i); });
            pill->setObjectName(QStringLiteral("run-pill-%1").arg(run.id()));
            pill->setToolTip(statusText(run.status(), run.conclusion()));
            if (run.id() == shownId) {
                QFont font = pill->font();
                font.setBold(true);
                pill->setFont(font);
            }
            flow->addWidget(pill);
        }
        return row;
    }

    void drawJobsOf(const Run &shown)
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
            setJobsContent(plainLine(QCoreApplication::translate("github", "Loading…")));
        ApiClient *client = server();
        if (!client) {
            setJobsContent(plainLine(QCoreApplication::translate("github", "The jobs could not be read.")));
            return;
        }
        GitHubClient(*client, this).runJobs(repository(), id, [this, read, id](const ApiReply &reply) {
            if (read != m_jobReads)
                return;
            if (reply.ok()) {
                const QJsonArray jobs = reply.json.object().value(QStringLiteral("jobs")).toArray();
                m_readJobs.insert(id, jobs);
                drawJobs(jobs);
            } else if (!m_readJobs.contains(id)) {
                setJobsContent(plainLine(QCoreApplication::translate("github", "The jobs could not be read.")));
            }
        });
    }

    void setJobsContent(QWidget *content)
    {
        setSection(kJobsSection, QCoreApplication::translate("github", "Jobs"), content);
        setSectionVisible(kJobsSection, true);
    }

    // One table, in the order of GitHub's answer.
    void drawJobs(const QJsonArray &jobs)
    {
        if (jobs.isEmpty()) {
            setJobsContent(plainLine(QCoreApplication::translate("github", "No jobs")));
            return;
        }

        auto *table = new QWidget;
        auto *grid = new QGridLayout(table);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setColumnStretch(1, 1);
        int row = 0;
        for (const QJsonValue &value : jobs)
            addJobRow(grid, row++, value.toObject());
        setJobsContent(table);
    }

    void addJobRow(QGridLayout *grid, int row, const QJsonObject &job)
    {
        const qint64 id = job.value(QStringLiteral("id")).toInteger();
        const QString started = job.value(QStringLiteral("started_at")).toString();
        grid->addWidget(runPill(job), row, 0, Qt::AlignLeft | Qt::AlignVCenter);
        grid->addWidget(plainLine(job.value(QStringLiteral("name")).toString()), row, 1);
        grid->addWidget(plainWord(durationBetween(started, job.value(QStringLiteral("completed_at")).toString())), row,
                        2);

        auto *links = new QWidget;
        auto *layout = new QHBoxLayout(links);
        layout->setContentsMargins(0, 0, 0, 0);
        if (!started.isEmpty()) {
            if (QPushButton *log = webButton(QCoreApplication::translate("github", "Log"),
                                             QStringLiteral("log-%1").arg(id),
                                             QUrl(job.value(QStringLiteral("html_url")).toString())))
                layout->addWidget(log);
        }
        if (isCompleted(job.value(QStringLiteral("status")).toString())) {
            layout->addWidget(jobAction(QCoreApplication::translate("github", "Re-run"),
                                        QStringLiteral("rerun-job-%1").arg(id),
                                        [repository = repository(), id](const GitHubClient &client,
                                                                        GitHubClient::Done done) {
                                            client.rerunJob(repository, id, std::move(done));
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

    void cancelRun()
    {
        const qint64 run = m_runId;
        if (!confirm(QCoreApplication::translate("github", "Cancel run"),
                     QCoreApplication::translate("github", "Cancel %1?").arg(m_runName)))
            return;
        runAction(kCancelRun, [repository = repository(), run](const GitHubClient &client, GitHubClient::Done done) {
            client.cancelRun(repository, run, std::move(done));
        });
    }

    void updateRefresh()
    {
        const QString state = m_runs.isEmpty() ? QString() : m_runs.at(shownIndex()).status();
        const bool running = !state.isEmpty() && !isCompleted(state);
        if (!m_shown || !running)
            m_refreshTimer->stop();
        else if (!m_refreshTimer->isActive())
            m_refreshTimer->start();
    }

    QList<Run> m_runs;
    QHash<qint64, QJsonArray> m_readJobs;
    QString m_branch;
    QJsonObject m_linkedPullRequest;
    QUrl m_pullRequestAddress;
    qint64 m_runId = 0;
    QString m_runName;
    qint64 m_leadId = 0;
    // The run the user chose, or 0 for the lead.
    qint64 m_selectedId = 0;
    // Only the answer to the latest read of a run's jobs counts.
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

QWidget *createRunView(ItemContext &context, QWidget *parent)
{
    auto *view = new RunView(context, parent);
    view->follow(context.status());
    return view;
}

} // namespace mirante::modules::github
