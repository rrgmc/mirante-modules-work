#include "ClaudeAgentsView.h"

#include "ClaudeAgents.h"

#include <mirante/ItemContext.h>
#include <mirante/Status.h>
#include <mirante/widgets/CompactDate.h>
#include <mirante/widgets/ItemListView.h>

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QPair>
#include <QTimeZone>
#include <QTimer>

#include <optional>

namespace mirante::modules::claude {

namespace {

enum Column {
    TitleColumn,
    StatusColumn,
    ToolColumn,
    RepositoryColumn,
    ModelColumn,
    ContextColumn,
    CostColumn,
    AccountColumn,
    StartedColumn
};

// The bridge plugin's columns, each shown only when a row has its key.
const QList<QPair<int, QString>> kBridgeColumns{{ToolColumn, QStringLiteral("tool")},
                                                {ModelColumn, QStringLiteral("model")},
                                                {ContextColumn, QStringLiteral("contextPercent")},
                                                {CostColumn, QStringLiteral("costUsd")}};

// A change in the bridge's folder refreshes the list at most this often.
constexpr qint64 kBridgeRefreshMs = 5000;

widgets::StatusRole roleOf(Activity activity)
{
    switch (activity) {
    case Activity::Working: return widgets::StatusRole::Waiting;
    case Activity::NeedsInput:
    case Activity::Failed: return widgets::StatusRole::Bad;
    case Activity::Done: return widgets::StatusRole::Good;
    case Activity::Idle:
    case Activity::Stopped:
    case Activity::Unknown: break;
    }
    return widgets::StatusRole::Neutral;
}

// The repository and its branch, such as "mirante-core · fix-dialogs", or the
// session's folder when it is not in a Git working tree.
QString repositoryText(const QJsonObject &row)
{
    const QString repository = row.value(QStringLiteral("repository")).toString();
    if (repository.isEmpty())
        return row.value(QStringLiteral("cwd")).toString();
    const QString branch = row.value(QStringLiteral("branch")).toString();
    return branch.isEmpty() ? repository : repository + QStringLiteral(" · ") + branch;
}

// The model and its effort, such as "Opus 5.5 · high".
QString modelText(const QJsonObject &row)
{
    const QString model = row.value(QStringLiteral("model")).toString();
    const QString effort = row.value(QStringLiteral("effort")).toString();
    if (model.isEmpty() || effort.isEmpty())
        return model;
    return model + QStringLiteral(" · ") + effort;
}

// A number cell, such as "42 %" or "$1.25", sorted on the number. Empty when
// the row has no such key.
widgets::ListCell numberCell(const QJsonObject &row, const QString &key, const QString &format, int decimals)
{
    const QJsonValue value = row.value(key);
    if (!value.isDouble())
        return {QString(), -1.0, std::nullopt};
    return {format.arg(value.toDouble(), 0, 'f', decimals), value.toDouble(), std::nullopt};
}

// The rate limits above the table, such as "Limits: 5 h 31 % · week 12 %".
// With several accounts, each is named instead of "Limits".
QString limitsText(const QJsonArray &limits, bool named)
{
    QStringList parts;
    for (const QJsonValue &value : limits) {
        const QJsonObject limit = value.toObject();
        QStringList windows;
        const QJsonValue fiveHour = limit.value(QStringLiteral("fiveHourPercent"));
        if (fiveHour.isDouble())
            windows << QCoreApplication::translate("claude", "5 h %1 %").arg(qRound(fiveHour.toDouble()));
        const QJsonValue weekly = limit.value(QStringLiteral("weeklyPercent"));
        if (weekly.isDouble())
            windows << QCoreApplication::translate("claude", "week %1 %").arg(qRound(weekly.toDouble()));
        if (windows.isEmpty())
            continue;
        const QString label = named ? limit.value(QStringLiteral("account")).toString()
                                    : QCoreApplication::translate("claude", "Limits");
        parts << label + QStringLiteral(": ") + windows.join(QStringLiteral(" · "));
    }
    return parts.join(QStringLiteral(" · "));
}

widgets::ListRow drawRow(const QJsonObject &row)
{
    const QString activityValue = row.value(QStringLiteral("activity")).toString();
    const Activity activity = activityOf(activityValue);
    const QString statusText = row.value(QStringLiteral("waitingFor")).toString() == QLatin1String("permission")
                                   ? QCoreApplication::translate("claude", "Needs permission")
                                   : activityText(activity, activityValue);
    QString title = row.value(QStringLiteral("title")).toString();
    if (title.isEmpty())
        title = QCoreApplication::translate("claude", "Untitled session");
    const QJsonValue startedValue = row.value(QStringLiteral("startedAt"));
    const QDateTime started = startedValue.isDouble()
                                  ? QDateTime::fromMSecsSinceEpoch(qint64(startedValue.toDouble()), QTimeZone::UTC)
                                  : QDateTime();

    widgets::ListRow listRow;
    listRow.key = row.value(QStringLiteral("key")).toString();
    listRow.cells = {
        {title, {}, std::nullopt},
        {statusText, int(activity), roleOf(activity)},
        {row.value(QStringLiteral("tool")).toString(), {}, std::nullopt},
        {repositoryText(row), {}, std::nullopt},
        {modelText(row), {}, std::nullopt},
        numberCell(row, QStringLiteral("contextPercent"), QStringLiteral("%1 %"), 0),
        numberCell(row, QStringLiteral("costUsd"), QStringLiteral("$%1"), 2),
        {row.value(QStringLiteral("account")).toString(), {}, std::nullopt},
        {widgets::compactDate(started), started.isValid() ? started.toMSecsSinceEpoch() : 0, std::nullopt},
    };
    return listRow;
}

class AgentsView : public widgets::ItemListView
{
public:
    AgentsView(ItemContext &context, QWidget *parent)
        : widgets::ItemListView(parent)
        , m_context(context)
    {
        setObjectName(QStringLiteral("claude-agents-view"));
        setColumns({{QCoreApplication::translate("claude", "Title"), 0, true},
                    {QCoreApplication::translate("claude", "Status"), 1, false},
                    {QCoreApplication::translate("claude", "Tool"), 5, false},
                    {QCoreApplication::translate("claude", "Repository"), 2, false},
                    {QCoreApplication::translate("claude", "Model"), 6, false},
                    {QCoreApplication::translate("claude", "Context"), 7, false},
                    {QCoreApplication::translate("claude", "Cost"), 8, false},
                    {QCoreApplication::translate("claude", "Account"), 4, false},
                    {QCoreApplication::translate("claude", "Started"), 3, false}},
                   StartedColumn);
        setColumnVisible(AccountColumn, false);
        for (const auto &column : kBridgeColumns)
            setColumnVisible(column.first, false);
        addFilter(StatusColumn, QCoreApplication::translate("claude", "Status"));
        connect(this, &widgets::ItemListView::rowMenuRequested, this,
                [this](const QString &key, const QPoint &globalPos) { showRowMenu(key, globalPos); });

        ItemStatusSource *source = context.status();
        if (!source) {
            setMessage(QCoreApplication::translate("claude", "Loading…"));
            return;
        }
        source->subscribe(this, [this](const ItemStatus &status) { draw(status); });
        draw(source->current());
        watchBridge(source);
    }

private:
    void draw(const ItemStatus &status)
    {
        // An error keeps the old rows, and the cell header shows the error.
        if (!status.data.contains(QStringLiteral("rows"))) {
            if (m_rows.isEmpty())
                setMessage(status.message.isEmpty() ? QCoreApplication::translate("claude", "Loading…")
                                                    : status.message);
            return;
        }

        m_rows.clear();
        QList<widgets::ListRow> rows;
        QList<bool> bridgeShown(kBridgeColumns.size(), false);
        for (const QJsonValue &value : status.data.value(QStringLiteral("rows")).toArray()) {
            const QJsonObject row = value.toObject();
            m_rows.insert(row.value(QStringLiteral("key")).toString(), row);
            rows << drawRow(row);
            for (qsizetype i = 0; i < kBridgeColumns.size(); ++i)
                bridgeShown[i] = bridgeShown.at(i) || row.contains(kBridgeColumns.at(i).second);
        }
        const bool severalAccounts = status.data.value(QStringLiteral("accounts")).toArray().size() > 1;
        setColumnVisible(AccountColumn, severalAccounts);
        for (qsizetype i = 0; i < kBridgeColumns.size(); ++i)
            setColumnVisible(kBridgeColumns.at(i).first, bridgeShown.at(i));

        QStringList description;
        const QString limits = limitsText(status.data.value(QStringLiteral("limits")).toArray(), severalAccounts);
        if (!limits.isEmpty())
            description << limits;
        for (const QJsonValue &value : status.data.value(QStringLiteral("errors")).toArray()) {
            const QJsonObject error = value.toObject();
            description << error.value(QStringLiteral("account")).toString() + QStringLiteral(": ")
                               + error.value(QStringLiteral("message")).toString();
        }
        setDescription(description.join(QStringLiteral(" · ")));

        setRows(rows);
        setMessage(rows.isEmpty() ? QCoreApplication::translate("claude", "No Claude agents are running.")
                                  : QString());
    }

    // The bridge plugin replaces its files by renaming, which changes the
    // folder. A change refreshes the list at once or, within kBridgeRefreshMs
    // of the last refresh, when that time is up. The folder is made, so that
    // it can be watched before the plugin's first write.
    void watchBridge(ItemStatusSource *source)
    {
        const QString folder = bridgeFolder();
        if (!QDir().mkpath(folder))
            return;
        auto *watcher = new QFileSystemWatcher({folder}, this);
        auto *later = new QTimer(this);
        later->setSingleShot(true);
        auto refresh = [this, source, later] {
            later->stop();
            m_lastRefresh.start();
            source->refresh();
        };
        connect(later, &QTimer::timeout, this, refresh);
        connect(watcher, &QFileSystemWatcher::directoryChanged, this, [this, later, refresh] {
            if (later->isActive())
                return;
            const qint64 since = m_lastRefresh.isValid() ? m_lastRefresh.elapsed() : kBridgeRefreshMs;
            if (since >= kBridgeRefreshMs)
                refresh();
            else
                later->start(int(kBridgeRefreshMs - since));
        });
    }

    // Not exec(), whose event loop could outlive this view.
    void showRowMenu(const QString &key, const QPoint &globalPos)
    {
        const QJsonObject row = m_rows.value(key);
        if (row.isEmpty())
            return;
        const QString cwd = row.value(QStringLiteral("cwd")).toString();
        const QString sessionId = row.value(QStringLiteral("sessionId")).toString();

        auto *menu = new QMenu(this);
        menu->setObjectName(QStringLiteral("rowMenu"));
        menu->setAttribute(Qt::WA_DeleteOnClose);
        QAction *folder = menu->addAction(QCoreApplication::translate("claude", "Show folder"));
        folder->setEnabled(!cwd.isEmpty());
        connect(folder, &QAction::triggered, this, [this, cwd] { m_context.showFolder(cwd); });
        QAction *copy = menu->addAction(QCoreApplication::translate("claude", "Copy session ID"));
        copy->setEnabled(!sessionId.isEmpty());
        connect(copy, &QAction::triggered, this,
                [sessionId] { QGuiApplication::clipboard()->setText(sessionId); });
        menu->popup(globalPos);
    }

    ItemContext &m_context;
    QHash<QString, QJsonObject> m_rows;
    QElapsedTimer m_lastRefresh;
};

} // namespace

QWidget *createAgentsView(ItemContext &context, QWidget *parent)
{
    return new AgentsView(context, parent);
}

} // namespace mirante::modules::claude
