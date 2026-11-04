#include "ClaudeAgentsView.h"

#include "ClaudeAgents.h"

#include <mirante/ItemContext.h>
#include <mirante/Status.h>
#include <mirante/widgets/CompactDate.h>
#include <mirante/widgets/ItemListView.h>

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QTimeZone>

#include <optional>

namespace mirante::modules::claude {

namespace {

enum Column { TitleColumn, StatusColumn, RepositoryColumn, AccountColumn, StartedColumn };

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

widgets::ListRow drawRow(const QJsonObject &row)
{
    const QString activityValue = row.value(QStringLiteral("activity")).toString();
    const Activity activity = activityOf(activityValue);
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
        {activityText(activity, activityValue), int(activity), roleOf(activity)},
        {repositoryText(row), {}, std::nullopt},
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
                    {QCoreApplication::translate("claude", "Repository"), 2, false},
                    {QCoreApplication::translate("claude", "Account"), 4, false},
                    {QCoreApplication::translate("claude", "Started"), 3, false}},
                   StartedColumn);
        setColumnVisible(AccountColumn, false);
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
        for (const QJsonValue &value : status.data.value(QStringLiteral("rows")).toArray()) {
            const QJsonObject row = value.toObject();
            m_rows.insert(row.value(QStringLiteral("key")).toString(), row);
            rows << drawRow(row);
        }
        setColumnVisible(AccountColumn, status.data.value(QStringLiteral("accounts")).toArray().size() > 1);

        QStringList errors;
        for (const QJsonValue &value : status.data.value(QStringLiteral("errors")).toArray()) {
            const QJsonObject error = value.toObject();
            errors << error.value(QStringLiteral("account")).toString() + QStringLiteral(": ")
                          + error.value(QStringLiteral("message")).toString();
        }
        setDescription(errors.join(QStringLiteral(" · ")));

        setRows(rows);
        setMessage(rows.isEmpty() ? QCoreApplication::translate("claude", "No Claude agents are running.")
                                  : QString());
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
};

} // namespace

QWidget *createAgentsView(ItemContext &context, QWidget *parent)
{
    return new AgentsView(context, parent);
}

} // namespace mirante::modules::claude
