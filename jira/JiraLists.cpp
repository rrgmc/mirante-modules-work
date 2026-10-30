#include "JiraLists.h"

#include "JiraClient.h"
#include "JiraSettings.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>

namespace mirante::modules::jira {

namespace {

// The rows a list asks for. A list that reaches this many may have more.
constexpr int kMaxRows = 100;

const QString kJqlPreset = QStringLiteral("jql");

// A text as a JQL string, so that nothing in it can end the string.
QString quotedJql(QString text)
{
    text.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    text.replace(QLatin1Char('"'), QLatin1String("\\\""));
    text.replace(QLatin1Char('\n'), QLatin1String("\\n"));
    text.replace(QLatin1Char('\r'), QLatin1String("\\r"));
    return QLatin1Char('"') + text + QLatin1Char('"');
}

QString listSummary(int total)
{
    if (total >= kMaxRows)
        return QCoreApplication::translate("jira", "%1+ issues").arg(kMaxRows);
    if (total == 1)
        return QCoreApplication::translate("jira", "1 issue");
    return QCoreApplication::translate("jira", "%1 issues").arg(total);
}

// No attention, and closed stays unset, so a list never counts towards a task
// being done. See decision 0067.
ItemStatus listStatus(const QJsonArray &rows)
{
    const int total = int(rows.size());
    ItemStatus status;
    status.state = ItemStatus::State::Ok;
    status.updatedAt = QDateTime::currentDateTimeUtc();
    status.summary = listSummary(total);
    status.data = QJsonObject{{QStringLiteral("rows"), rows}, {QStringLiteral("total"), total}};
    return status;
}

} // namespace

std::optional<QString> listJql(const QString &preset, const QString &jql, const QString &projectKey)
{
    if (preset == kJqlPreset) {
        const QString query = jql.trimmed();
        return query.isEmpty() ? std::nullopt : std::optional<QString>(query);
    }

    const bool sprint = preset == QLatin1String("currentSprint");
    if (sprint || preset == QLatin1String("recentlyUpdated")) {
        const QString key = projectKey.trimmed();
        if (key.isEmpty())
            return std::nullopt;
        const QString project = QStringLiteral("project = ") + quotedJql(key);
        return project
               + (sprint ? QStringLiteral(" AND sprint in openSprints() ORDER BY rank")
                         : QStringLiteral(" AND updated >= -7d ORDER BY updated DESC"));
    }

    // An absent preset is the first choice of the form.
    return QStringLiteral("assignee = currentUser() AND resolution = Unresolved ORDER BY updated DESC");
}

QUrl listWebUrl(const QUrl &baseUrl, const QString &jql)
{
    if (baseUrl.isEmpty())
        return {};

    // Keep a slash at the end of the base URL from doubling.
    QString path = baseUrl.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);

    QUrl url = baseUrl;
    url.setPath(path + QStringLiteral("/issues/"));
    url.setQuery(QStringLiteral("jql=") + QString::fromLatin1(QUrl::toPercentEncoding(jql)));
    url.setFragment(QString());
    return url;
}

void fetchListStatus(StatusJob &job)
{
    const QJsonObject config = job.config();
    const QString preset = config.value(QStringLiteral("preset")).toString();
    const std::optional<QString> jql = listJql(preset, config.value(QStringLiteral("jql")).toString(),
                                               job.projectSetting(QStringLiteral("projectKey")).toString());
    if (!jql) {
        ItemStatus status;
        status.message =
            preset == kJqlPreset
                ? QCoreApplication::translate("jira", "Set the JQL query in the item's settings.")
                : QCoreApplication::translate("jira", "Set the Jira project key in the project's settings.");
        job.finish(status);
        return;
    }

    SearchOptions options;
    options.fields = QStringList{QStringLiteral("summary"), QStringLiteral("status"), QStringLiteral("assignee"),
                                 QStringLiteral("updated")};
    options.maxRows = kMaxRows;

    const JiraClient client(job.server(), deploymentOf(job.moduleSettings(kGeneralScreen)));
    StatusJob *owner = &job;
    client.search(*jql, options, job.owner(), [owner](const ApiReply &reply) {
        owner->finish(reply.ok() ? listStatus(reply.json.object().value(QStringLiteral("issues")).toArray())
                                 : statusFromReply(reply));
    });
}

} // namespace mirante::modules::jira
