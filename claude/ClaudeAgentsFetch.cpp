#include "ClaudeAgentsFetch.h"

#include "ClaudeAgents.h"

#include <mirante/Status.h>

#include <QCoreApplication>
#include <QDir>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>

namespace mirante::modules::claude {

namespace {

// How long one run of the command may take.
constexpr int kTimeoutMs = 30000;

// A config folder as Claude Code reads it: "~" at the start is the home
// folder.
QString expandHome(const QString &folder)
{
    if (folder == QLatin1String("~"))
        return QDir::homePath();
    if (folder.startsWith(QLatin1String("~/")) || folder.startsWith(QLatin1String("~\\")))
        return QDir::homePath() + folder.mid(1);
    return folder;
}

// Runs the command for each account in turn, then finishes the job. A child of
// the job's owner, so it goes when the job goes.
class AgentsFetch : public QObject
{
public:
    AgentsFetch(StatusJob &job, QString program, QStringList arguments, QStringList accounts)
        : QObject(job.owner())
        , m_job(job)
        , m_program(std::move(program))
        , m_arguments(std::move(arguments))
        , m_accounts(std::move(accounts))
    {
        if (m_accounts.isEmpty())
            m_accounts << QString();
    }

    void start() { runNext(); }

private:
    void runNext()
    {
        if (m_results.size() == m_accounts.size()) {
            const ItemStatus status = agentsStatus(m_results, [this](const QString &top) { return m_job.gitHead(top); });
            m_job.finish(status);
            deleteLater();
            return;
        }

        const QString account = m_accounts.at(m_results.size());
        auto *process = new QProcess(this);
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        if (!account.isEmpty())
            environment.insert(QStringLiteral("CLAUDE_CONFIG_DIR"), QDir::toNativeSeparators(expandHome(account)));
        process->setProcessEnvironment(environment);
        process->setProgram(m_program);
        process->setArguments(m_arguments);

        auto *timer = new QTimer(process);
        timer->setSingleShot(true);
        // Each process ends once: by finishing, by failing to start, or by
        // the timer.
        auto done = [this, process, timer, account](const QString &error) {
            if (process->property("done").toBool())
                return;
            process->setProperty("done", true);
            AccountResult result;
            result.configFolder = account;
            if (error.isEmpty()) {
                QString parseError;
                if (const auto agents = parseAgents(process->readAllStandardOutput(), &parseError))
                    result.agents = *agents;
                else
                    result.error = parseError;
            } else {
                result.error = error;
            }
            m_results << result;
            timer->stop();
            process->disconnect(this);
            if (process->state() != QProcess::NotRunning)
                process->kill();
            process->deleteLater();
            // Not inside the process's signal.
            QTimer::singleShot(0, this, [this] { runNext(); });
        };

        connect(timer, &QTimer::timeout, this, [done] {
            done(QCoreApplication::translate("claude", "Claude Code did not answer in time."));
        });
        connect(process, &QProcess::errorOccurred, this, [done](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
                done(QCoreApplication::translate("claude", "Claude Code could not be started."));
        });
        connect(process, &QProcess::finished, this, [done, process](int exitCode, QProcess::ExitStatus exitStatus) {
            if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                const QString detail = QString::fromUtf8(process->readAllStandardError()).trimmed().section(
                    QLatin1Char('\n'), 0, 0);
                done(detail.isEmpty()
                         ? QCoreApplication::translate("claude", "Claude Code failed, with exit code %1.").arg(exitCode)
                         : detail);
                return;
            }
            done(QString());
        });

        timer->start(kTimeoutMs);
        process->start();
    }

    StatusJob &m_job;
    QString m_program;
    QStringList m_arguments;
    QStringList m_accounts;
    QList<AccountResult> m_results;
};

ItemStatus errorStatus(const QString &message)
{
    ItemStatus status;
    status.state = ItemStatus::State::Error;
    status.message = message;
    return status;
}

} // namespace

void fetchAgents(StatusJob &job, const QString &program)
{
    const QJsonObject config = job.config();

    QString folderPath;
    const QString folderId = config.value(QStringLiteral("folder")).toString();
    if (!folderId.isEmpty()) {
        folderPath = job.folderPath(folderId);
        if (folderPath.isEmpty()) {
            job.finish(errorStatus(
                QCoreApplication::translate("claude", "The project folder is not mapped on this device.")));
            return;
        }
    }

    const QString found = program.isEmpty() ? findClaudeProgram() : program;
    if (found.isEmpty()) {
        job.finish(errorStatus(QCoreApplication::translate("claude", "Claude Code is not installed on this device.")));
        return;
    }

    auto *fetch = new AgentsFetch(job, found, agentArguments(config, folderPath), accountsOf(config));
    fetch->start();
}

} // namespace mirante::modules::claude
