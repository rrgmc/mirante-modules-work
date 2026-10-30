#include "JiraListView.h"

#include "JiraIssueView.h"
#include "JiraLists.h"
#include "JiraSettings.h"
#include "JiraUrls.h"

#include <mirante/ItemContext.h>
#include <mirante/Status.h>
#include <mirante/widgets/ItemListView.h>

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QRegularExpression>

#include <optional>

namespace mirante::modules::jira {

namespace {

enum Column { KeyColumn, SummaryColumn, StatusColumn, AssigneeColumn, UpdatedColumn };

QString trJira(const char *text)
{
    return QCoreApplication::translate("jira", text);
}

// Sorts OPS-9 before OPS-10.
QString keySortKey(const QString &key)
{
    static const QRegularExpression pattern(QStringLiteral("^(.*-)(\\d+)$"));
    const QRegularExpressionMatch match = pattern.match(key);
    if (!match.hasMatch())
        return key;
    return match.captured(1) + match.captured(2).rightJustified(12, QLatin1Char('0'));
}

widgets::ListRow readRow(const QJsonObject &issue)
{
    const QJsonObject fields = issue.value(QStringLiteral("fields")).toObject();
    const QJsonObject status = fields.value(QStringLiteral("status")).toObject();
    const QString category =
        status.value(QStringLiteral("statusCategory")).toObject().value(QStringLiteral("key")).toString();
    const QString assignee =
        fields.value(QStringLiteral("assignee")).toObject().value(QStringLiteral("displayName")).toString();
    const QString updated = fields.value(QStringLiteral("updated")).toString();
    const QString key = issue.value(QStringLiteral("key")).toString();

    widgets::ListRow row;
    row.key = key;
    row.cells = {
        {key, keySortKey(key), std::nullopt},
        {fields.value(QStringLiteral("summary")).toString(), {}, std::nullopt},
        {status.value(QStringLiteral("name")).toString(), {}, statusCategoryRole(category)},
        {assignee.isEmpty() ? trJira("Unassigned") : assignee, {}, std::nullopt},
        {shortJiraDate(updated), readJiraDate(updated).toMSecsSinceEpoch(), std::nullopt},
    };
    return row;
}

// The table of a list, from the item's status. A row's preview reads its issue
// itself, and reads it again when the list has a new status. See decision 0068.
class ListView : public widgets::ItemListView
{
public:
    ListView(ItemContext &context, QWidget *parent)
        : widgets::ItemListView(parent)
        , m_context(context)
    {
        setColumns({{trJira("Key"), 1, false},
                    {trJira("Summary"), 0, true},
                    {trJira("Status"), 2, false},
                    {trJira("Assignee"), 4, false},
                    {trJira("Updated"), 3, false}},
                   UpdatedColumn);

        connect(this, &widgets::ItemListView::rowSelected, this,
                [this](const QString &key) { setPreview(createIssuePreview(m_context, key, this)); });
        connect(this, &widgets::ItemListView::rowMenuRequested, this,
                [this](const QString &key, const QPoint &globalPos) { showRowMenu(key, globalPos); });

        if (ItemStatusSource *source = context.status()) {
            source->subscribe(this, [this](const ItemStatus &status) { showStatus(status); });
            showStatus(source->current());
        } else {
            setMessage(trJira("Loading…"));
        }
    }

private:
    QUrl site() const { return baseUrlOf(m_context.moduleSettings(kGeneralScreen)); }

    QUrl issuePageUrl(const QString &key) const
    {
        const QUrl base = site();
        return base.isEmpty() ? QUrl() : issueUrl(base, QJsonObject{{QStringLiteral("key"), key}});
    }

    QUrl listPageUrl() const
    {
        const QJsonObject config = m_context.config();
        const std::optional<QString> jql =
            listJql(config.value(QStringLiteral("preset")).toString(), config.value(QStringLiteral("jql")).toString(),
                    m_context.projectSetting(QStringLiteral("projectKey")).toString());
        return jql ? listWebUrl(site(), *jql) : QUrl();
    }

    void showStatus(const ItemStatus &status)
    {
        setWebLink(listPageUrl());

        // An error keeps the old rows, and the cell header shows the error.
        if (!status.data.contains(QStringLiteral("rows"))) {
            setMessage(status.message.isEmpty() ? trJira("Loading…") : status.message);
            return;
        }
        setMessage(QString());

        QList<widgets::ListRow> rows;
        const QJsonArray issues = status.data.value(QStringLiteral("rows")).toArray();
        for (const QJsonValue &issue : issues)
            rows << readRow(issue.toObject());
        setRows(rows);

        // If its row went, the preview stays until the user chooses another
        // row or closes it, since the issue itself may still be of interest.
        if (preview())
            reloadIssuePreview(preview());
    }

    // Not exec(), whose event loop could outlive this view.
    void showRowMenu(const QString &key, const QPoint &globalPos)
    {
        auto *menu = new QMenu(this);
        menu->setObjectName(QStringLiteral("rowMenu"));
        menu->setAttribute(Qt::WA_DeleteOnClose);
        const QUrl url = issuePageUrl(key);

        connect(menu->addAction(trJira("Add to task as item")), &QAction::triggered, this, [this, key] {
            m_context.addItemToTask(QStringLiteral("jira.issue.summary"), key,
                                    QJsonObject{{QStringLiteral("key"), key}});
        });
        QAction *open = menu->addAction(trJira("Open page"));
        open->setEnabled(!url.isEmpty());
        connect(open, &QAction::triggered, this, [this, url] { openPage(m_context, url); });
        QAction *copy = menu->addAction(trJira("Copy link"));
        copy->setEnabled(!url.isEmpty());
        connect(copy, &QAction::triggered, this,
                [url] { QGuiApplication::clipboard()->setText(url.toString()); });

        menu->popup(globalPos);
    }

    ItemContext &m_context;
};

} // namespace

QWidget *createListView(ItemContext &context, QWidget *parent)
{
    return new ListView(context, parent);
}

} // namespace mirante::modules::jira
