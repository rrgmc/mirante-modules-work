// The Claude agents list: reading `claude agents --json`, finding each
// session's repository, and building the item's status from the rows. Pure
// functions, so they are tested without a process. See README.md and decision
// 0143 in mirante-core.
#pragma once

#include <mirante/Status.h>

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace mirante::modules::claude {

// One session, as the command prints it.
struct Agent
{
    QString sessionId;      // or the short id of a background session
    QString name;           // the session's title; may be empty
    QString cwd;
    QString kind;           // "interactive" or "background"
    QString activity;       // "status" or, for a background session, "state"
    QDateTime startedAt;    // invalid when missing
};

// The sessions in the command's output, or null with a message when it is not
// a JSON array. Entries that are not objects are skipped, and unknown fields
// are ignored.
std::optional<QList<Agent>> parseAgents(const QByteArray &output, QString *error);

// What a session's activity means, for its pill.
enum class Activity { Working, NeedsInput, Idle, Done, Failed, Stopped, Unknown };
Activity activityOf(const QString &value);
// The text the user sees, such as "Needs input". An unknown value is shown as
// it came.
QString activityText(Activity activity, const QString &value);

// The Git working tree that holds a folder.
struct Repository
{
    QString top;    // the folder that holds .git, a directory or a worktree's file
    QString name;   // the main repository's folder name, also for a worktree
};
// Walks up from folder. Null when no folder up to the root holds .git. Reads
// only the file system.
std::optional<Repository> repositoryOf(const QString &folder);

// One account: a Claude Code config folder, or the user's default when empty.
struct AccountResult
{
    QString configFolder;
    // The sessions, or empty with error set when the command failed.
    QList<Agent> agents;
    QString error;
};

// The account's label: the config folder's name, or "Default".
QString accountLabel(const QString &configFolder);

// The config folders of the item's "accounts" key, trimmed and without empty
// lines. Empty means one run with the default.
QStringList accountsOf(const QJsonObject &config);

// The arguments of the command, from the item's configuration and the local
// path of its folder filter, which is empty for none.
QStringList agentArguments(const QJsonObject &config, const QString &folderPath);

// The status of the list: data.rows, data.errors and data.accounts, and a
// summary such as "3 agents · 1 needs input". Fails only when every account
// failed. headOf gives the branch of a working tree's top folder.
ItemStatus agentsStatus(const QList<AccountResult> &results, const std::function<QString(const QString &)> &headOf);

// The program, found on the PATH or where Claude Code's installers put it.
// Empty when it is not installed.
QString findClaudeProgram();

} // namespace mirante::modules::claude
