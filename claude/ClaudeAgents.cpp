#include "ClaudeAgents.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStandardPaths>
#include <QTimeZone>
#include <QtGlobal>

#include <initializer_list>

namespace mirante::modules::claude {

namespace {

const QString kRows = QStringLiteral("rows");
const QString kErrors = QStringLiteral("errors");
const QString kAccounts = QStringLiteral("accounts");

// The main repository of a worktree, from the "gitdir:" line of its .git
// file, such as <main>/.git/worktrees/<name>. Empty when the file says
// something else.
QString mainOfWorktree(const QString &gitFile, const QString &top)
{
    QFile file(gitFile);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QString line = QString::fromUtf8(file.readLine()).trimmed();
    if (!line.startsWith(QLatin1String("gitdir:")))
        return {};
    QString gitDir = QDir::fromNativeSeparators(line.mid(7).trimmed());
    if (QDir::isRelativePath(gitDir))
        gitDir = QDir(top).absoluteFilePath(gitDir);
    gitDir = QDir::cleanPath(gitDir);
    const int at = gitDir.lastIndexOf(QLatin1String("/.git/worktrees/"));
    if (at < 0)
        return {};
    return gitDir.left(at);
}

} // namespace

std::optional<QList<Agent>> parseAgents(const QByteArray &output, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(output.trimmed(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (error)
            *error = QCoreApplication::translate("claude", "Claude Code gave an answer that is not a list of agents.");
        return std::nullopt;
    }

    QList<Agent> agents;
    for (const QJsonValue &value : document.array()) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        Agent agent;
        agent.sessionId = object.value(QStringLiteral("sessionId")).toString();
        if (agent.sessionId.isEmpty())
            agent.sessionId = object.value(QStringLiteral("id")).toString();
        agent.name = object.value(QStringLiteral("name")).toString();
        agent.cwd = object.value(QStringLiteral("cwd")).toString();
        agent.kind = object.value(QStringLiteral("kind")).toString();
        agent.activity = object.value(QStringLiteral("status")).toString();
        if (agent.activity.isEmpty())
            agent.activity = object.value(QStringLiteral("state")).toString();
        const QJsonValue started = object.value(QStringLiteral("startedAt"));
        if (started.isDouble())
            agent.startedAt = QDateTime::fromMSecsSinceEpoch(qint64(started.toDouble()), QTimeZone::UTC);
        agents << agent;
    }
    return agents;
}

Activity activityOf(const QString &value)
{
    if (value == QLatin1String("busy") || value == QLatin1String("working"))
        return Activity::Working;
    if (value == QLatin1String("waiting") || value == QLatin1String("blocked"))
        return Activity::NeedsInput;
    if (value == QLatin1String("idle"))
        return Activity::Idle;
    if (value == QLatin1String("done"))
        return Activity::Done;
    if (value == QLatin1String("failed"))
        return Activity::Failed;
    if (value == QLatin1String("stopped"))
        return Activity::Stopped;
    return Activity::Unknown;
}

QString activityText(Activity activity, const QString &value)
{
    switch (activity) {
    case Activity::Working: return QCoreApplication::translate("claude", "Working");
    case Activity::NeedsInput: return QCoreApplication::translate("claude", "Needs input");
    case Activity::Idle: return QCoreApplication::translate("claude", "Idle");
    case Activity::Done: return QCoreApplication::translate("claude", "Done");
    case Activity::Failed: return QCoreApplication::translate("claude", "Failed");
    case Activity::Stopped: return QCoreApplication::translate("claude", "Stopped");
    case Activity::Unknown: break;
    }
    return value.isEmpty() ? QStringLiteral("-") : value;
}

std::optional<Repository> repositoryOf(const QString &folder)
{
    if (folder.isEmpty())
        return std::nullopt;
    QDir dir(QDir::cleanPath(QDir::fromNativeSeparators(folder)));
    while (true) {
        const QString gitPath = dir.filePath(QStringLiteral(".git"));
        const QFileInfo git(gitPath);
        if (git.exists()) {
            Repository repository;
            repository.top = dir.absolutePath();
            QString main = repository.top;
            if (git.isFile()) {
                const QString worktreeMain = mainOfWorktree(gitPath, repository.top);
                if (!worktreeMain.isEmpty())
                    main = worktreeMain;
            }
            repository.name = QFileInfo(main).fileName();
            if (repository.name.isEmpty())
                repository.name = main;
            return repository;
        }
        if (!dir.cdUp())
            return std::nullopt;
    }
}

QString accountLabel(const QString &configFolder)
{
    if (configFolder.isEmpty())
        return QCoreApplication::translate("claude", "Default");
    // The setting is synced, so a folder may come from another platform:
    // both separators count everywhere.
    QString folder = configFolder;
    folder.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (folder.size() > 1 && folder.endsWith(QLatin1Char('/')))
        folder.chop(1);
    const QString name = folder.section(QLatin1Char('/'), -1);
    return name.isEmpty() ? configFolder : name;
}

QStringList accountsOf(const QJsonObject &config)
{
    QStringList accounts;
    for (const QJsonValue &value : config.value(kAccounts).toArray()) {
        const QString folder = value.toString().trimmed();
        if (!folder.isEmpty() && !accounts.contains(folder))
            accounts << folder;
    }
    return accounts;
}

QStringList agentArguments(const QJsonObject &config, const QString &folderPath)
{
    QStringList arguments{QStringLiteral("agents"), QStringLiteral("--json")};
    if (config.value(QStringLiteral("includeFinished")).toBool())
        arguments << QStringLiteral("--all");
    if (!folderPath.isEmpty())
        arguments << QStringLiteral("--cwd") << QDir::toNativeSeparators(folderPath);
    return arguments;
}

ItemStatus agentsStatus(const QList<AccountResult> &results, const std::function<QString(const QString &)> &headOf)
{
    ItemStatus status;
    QJsonArray rows;
    QJsonArray errors;
    QJsonArray accounts;
    int waiting = 0;
    int failedAccounts = 0;

    for (const AccountResult &result : results) {
        const QString account = accountLabel(result.configFolder);
        accounts.append(account);
        if (!result.error.isEmpty()) {
            ++failedAccounts;
            errors.append(QJsonObject{{QStringLiteral("account"), account}, {QStringLiteral("message"), result.error}});
            continue;
        }
        for (const Agent &agent : result.agents) {
            const Activity activity = activityOf(agent.activity);
            if (activity == Activity::NeedsInput)
                ++waiting;
            QJsonObject row;
            row.insert(QStringLiteral("key"), result.configFolder + QLatin1Char('|') + agent.sessionId);
            row.insert(QStringLiteral("sessionId"), agent.sessionId);
            row.insert(QStringLiteral("title"), agent.name);
            row.insert(QStringLiteral("activity"), agent.activity);
            row.insert(QStringLiteral("kind"), agent.kind);
            row.insert(QStringLiteral("cwd"), agent.cwd);
            row.insert(QStringLiteral("account"), account);
            if (agent.startedAt.isValid())
                row.insert(QStringLiteral("startedAt"), double(agent.startedAt.toMSecsSinceEpoch()));
            if (const std::optional<Repository> repository = repositoryOf(agent.cwd)) {
                row.insert(QStringLiteral("repository"), repository->name);
                if (headOf)
                    row.insert(QStringLiteral("branch"), headOf(repository->top));
            }
            rows.append(row);
        }
    }

    if (!results.isEmpty() && failedAccounts == results.size()) {
        status.state = ItemStatus::State::Error;
        QStringList messages;
        for (const QJsonValue &error : std::as_const(errors)) {
            const QJsonObject object = error.toObject();
            messages << (results.size() > 1 ? object.value(QStringLiteral("account")).toString() + QStringLiteral(": ")
                                            : QString())
                            + object.value(QStringLiteral("message")).toString();
        }
        status.message = messages.join(QLatin1Char('\n'));
        return status;
    }

    status.state = ItemStatus::State::Ok;
    status.data.insert(kRows, rows);
    status.data.insert(kErrors, errors);
    status.data.insert(kAccounts, accounts);
    const int count = int(rows.size());
    QStringList parts{count == 1 ? QCoreApplication::translate("claude", "1 agent")
                                 : QCoreApplication::translate("claude", "%1 agents").arg(count)};
    if (waiting > 0)
        parts << QCoreApplication::translate("claude", "%1 waiting for input").arg(waiting);
    if (failedAccounts == 1)
        parts << QCoreApplication::translate("claude", "1 account failed");
    else if (failedAccounts > 1)
        parts << QCoreApplication::translate("claude", "%1 accounts failed").arg(failedAccounts);
    status.summary = parts.join(QStringLiteral(" · "));
    status.updatedAt = QDateTime::currentDateTimeUtc();
    return status;
}

namespace {

// How long the bridge's files are kept.
constexpr qint64 kBridgeKeepSeconds = 7 * 24 * 60 * 60;

std::optional<QJsonObject> readObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    return document.object();
}

// A number at a path of keys, such as {"cost", "total_cost_usd"}, or null.
std::optional<double> numberAt(const QJsonObject &object, std::initializer_list<const char *> keys)
{
    QJsonValue value = object;
    for (const char *key : keys)
        value = value.toObject().value(QLatin1String(key));
    if (!value.isDouble())
        return std::nullopt;
    return value.toDouble();
}

// A rate-limit window's use, and its reset time in milliseconds since the
// epoch, added to limit as prefix + "Percent" and prefix + "ResetsAt". False
// when the window is missing or has reset before now.
bool addWindow(QJsonObject &limit, const QJsonObject &window, const QString &prefix, const QDateTime &now)
{
    const QJsonValue used = window.value(QStringLiteral("used_percentage"));
    if (!used.isDouble())
        return false;
    const QJsonValue resets = window.value(QStringLiteral("resets_at"));
    if (resets.isDouble()) {
        const qint64 resetsMs = qint64(resets.toDouble() * 1000);
        if (resetsMs <= now.toMSecsSinceEpoch())
            return false;
        limit.insert(prefix + QStringLiteral("ResetsAt"), double(resetsMs));
    }
    limit.insert(prefix + QStringLiteral("Percent"), used.toDouble());
    return true;
}

} // namespace

QString bridgeFolder()
{
    const QString folder = qEnvironmentVariable("MIRANTE_CLAUDE_DIR");
    if (!folder.isEmpty())
        return QDir::cleanPath(QDir::fromNativeSeparators(folder));
    return QDir::homePath() + QStringLiteral("/.mirante/claude/sessions");
}

QHash<QString, BridgeSession> readBridge(const QString &folder, const QDateTime &now)
{
    QHash<QString, BridgeSession> sessions;
    const QString hookSuffix = QStringLiteral(".hook.json");
    const QString statusSuffix = QStringLiteral(".status.json");
    QDirIterator it(folder, {QStringLiteral("*.json")}, QDir::Files);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        const QString name = info.fileName();
        const bool isHook = name.endsWith(hookSuffix);
        if (!isHook && !name.endsWith(statusSuffix))
            continue;
        const QDateTime modified = info.lastModified().toUTC();
        if (modified.secsTo(now) > kBridgeKeepSeconds) {
            QFile::remove(info.filePath());
            continue;
        }
        const QString session = name.chopped(isHook ? hookSuffix.size() : statusSuffix.size());
        if (session.isEmpty())
            continue;
        const std::optional<QJsonObject> object = readObject(info.filePath());
        if (!object)
            continue;
        BridgeSession &entry = sessions[session];
        if (isHook) {
            entry.hook = *object;
            entry.hookAt = modified;
        } else {
            entry.status = *object;
            entry.statusAt = modified;
        }
    }
    return sessions;
}

void mergeBridge(ItemStatus &status, const QHash<QString, BridgeSession> &sessions, const QDateTime &now)
{
    if (status.state != ItemStatus::State::Ok)
        return;

    // The newest status file with rate limits, of each account.
    struct Limits
    {
        QJsonObject rateLimits;
        QDateTime at;
    };
    QHash<QString, Limits> limitsOf;
    QStringList accountOrder;

    QJsonArray rows = status.data.value(kRows).toArray();
    for (qsizetype i = 0; i < rows.size(); ++i) {
        QJsonObject row = rows.at(i).toObject();
        const auto found = sessions.constFind(row.value(QStringLiteral("sessionId")).toString());
        if (found == sessions.constEnd())
            continue;
        const BridgeSession &session = *found;
        const Activity activity = activityOf(row.value(QStringLiteral("activity")).toString());

        // The hooks run in the background, so a file may describe an older
        // event than the latest. The command's state decides which part
        // counts. A second of slack covers file times without milliseconds.
        const QJsonValue started = row.value(QStringLiteral("startedAt"));
        const bool hookCounts = session.hookAt.isValid()
                                && (!started.isDouble()
                                    || session.hookAt.toMSecsSinceEpoch() >= qint64(started.toDouble()) - 1000);
        if (hookCounts) {
            const QString event = session.hook.value(QStringLiteral("event")).toString();
            const QString tool = session.hook.value(QStringLiteral("tool")).toString();
            if (activity == Activity::Working && event == QLatin1String("PreToolUse") && !tool.isEmpty())
                row.insert(QStringLiteral("tool"), tool);
            const bool permission = event == QLatin1String("PermissionRequest")
                                    || (event == QLatin1String("Notification")
                                        && session.hook.value(QStringLiteral("notification")).toString()
                                               == QLatin1String("permission_prompt"));
            if (activity == Activity::NeedsInput && permission)
                row.insert(QStringLiteral("waitingFor"), QStringLiteral("permission"));
        }

        if (session.statusAt.isValid()) {
            const QJsonObject &input = session.status;
            const QJsonObject model = input.value(QStringLiteral("model")).toObject();
            QString modelName = model.value(QStringLiteral("display_name")).toString();
            if (modelName.isEmpty())
                modelName = model.value(QStringLiteral("id")).toString();
            if (!modelName.isEmpty())
                row.insert(QStringLiteral("model"), modelName);
            const QJsonValue effort = input.value(QStringLiteral("effort"));
            const QString effortLevel = effort.isObject() ? effort.toObject().value(QStringLiteral("level")).toString()
                                                          : effort.toString();
            if (!effortLevel.isEmpty())
                row.insert(QStringLiteral("effort"), effortLevel);
            if (const auto context = numberAt(input, {"context_window", "used_percentage"}))
                row.insert(QStringLiteral("contextPercent"), *context);
            if (const auto cost = numberAt(input, {"cost", "total_cost_usd"}))
                row.insert(QStringLiteral("costUsd"), *cost);

            const QJsonValue rateLimits = input.value(QStringLiteral("rate_limits"));
            if (rateLimits.isObject()) {
                const QString account = row.value(QStringLiteral("account")).toString();
                if (!limitsOf.contains(account))
                    accountOrder << account;
                Limits &limits = limitsOf[account];
                if (!limits.at.isValid() || session.statusAt > limits.at)
                    limits = {rateLimits.toObject(), session.statusAt};
            }
        }
        rows[i] = row;
    }
    status.data.insert(kRows, rows);

    QJsonArray limits;
    for (const QString &account : std::as_const(accountOrder)) {
        const QJsonObject rateLimits = limitsOf.value(account).rateLimits;
        QJsonObject limit{{QStringLiteral("account"), account}};
        const bool fiveHour = addWindow(limit, rateLimits.value(QStringLiteral("five_hour")).toObject(),
                                        QStringLiteral("fiveHour"), now);
        const bool weekly = addWindow(limit, rateLimits.value(QStringLiteral("seven_day")).toObject(),
                                      QStringLiteral("weekly"), now);
        if (fiveHour || weekly)
            limits.append(limit);
    }
    status.data.insert(QStringLiteral("limits"), limits);
}

QString findClaudeProgram()
{
    const QString name = QStringLiteral("claude");
    const QString onPath = QStandardPaths::findExecutable(name);
    if (!onPath.isEmpty())
        return onPath;
    // An app started from the desktop may not get the user's PATH.
    const QString home = QDir::homePath();
    const QStringList folders{home + QStringLiteral("/.local/bin"), home + QStringLiteral("/.claude/local"),
                              QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin")};
    return QStandardPaths::findExecutable(name, folders);
}

} // namespace mirante::modules::claude
