#include "JiraListView.h"

#include "JiraFields.h"
#include "JiraIssueView.h"
#include "JiraLists.h"
#include "JiraSettings.h"
#include "JiraUrls.h"

#include <mirante/ItemContext.h>
#include <mirante/Status.h>
#include <mirante/widgets/CompactDate.h>
#include <mirante/widgets/ItemListView.h>

#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QRegularExpression>

#include <optional>

namespace mirante::modules::jira {

namespace {

// The extra fields' columns come after these, and then the links.
enum Column { KeyColumn, SummaryColumn, StatusColumn, AssigneeColumn, UpdatedColumn, FirstExtraColumn };

// The hide priority of the first extra field. They hide before all other columns,
// the last of them first.
constexpr int kFirstExtraPriority = 5;

const QString kWebLink = QStringLiteral("web");
const QString kOpenLink = QStringLiteral("open");
const QString kAddLink = QStringLiteral("add");

// Sorts OPS-9 before OPS-10.
QString keySortKey(const QString &key)
{
    static const QRegularExpression pattern(QStringLiteral("^(.*-)(\\d+)$"));
    const QRegularExpressionMatch match = pattern.match(key);
    if (!match.hasMatch())
        return key;
    return match.captured(1) + match.captured(2).rightJustified(12, QLatin1Char('0'));
}

// The links of a row, with the web page first. Without a site, only the one that
// needs none.
QList<widgets::ListLink> rowLinks(bool hasSite)
{
    QList<widgets::ListLink> links;
    if (hasSite)
        links << widgets::ListLink {kWebLink, QCoreApplication::translate("jira", "Web")}
              << widgets::ListLink {kOpenLink, QCoreApplication::translate("jira", "Open")};
    links << widgets::ListLink{kAddLink, QCoreApplication::translate("jira", "Add")};
    return links;
}

// ids holds the field id of each extra field, or an empty one if the site has no
// such field.
widgets::ListRow readRow(const QJsonObject &issue, const QStringList &ids, const QList<widgets::ListLink> &links)
{
    const QJsonObject fields = issue.value(QStringLiteral("fields")).toObject();
    const QJsonObject status = fields.value(QStringLiteral("status")).toObject();
    const QString category =
        status.value(QStringLiteral("statusCategory")).toObject().value(QStringLiteral("key")).toString();
    const QString assignee =
        fields.value(QStringLiteral("assignee")).toObject().value(QStringLiteral("displayName")).toString();
    const QString updatedText = fields.value(QStringLiteral("updated")).toString();
    const QDateTime updated = readJiraDate(updatedText);
    const QString key = issue.value(QStringLiteral("key")).toString();

    widgets::ListRow row;
    row.key = key;
    row.cells = {
        {key, keySortKey(key), std::nullopt},
        {fields.value(QStringLiteral("summary")).toString(), {}, std::nullopt},
        {status.value(QStringLiteral("name")).toString(), {}, statusCategoryRole(category)},
        {assignee.isEmpty() ? QCoreApplication::translate("jira", "Unassigned") : assignee, {}, std::nullopt},
        {updated.isValid() ? widgets::compactDate(updated) : updatedText, updated.toMSecsSinceEpoch(), std::nullopt},
    };
    for (const QString &id : ids) {
        const QJsonValue value = fields.value(id);
        row.cells.append({fieldText(value), value.isDouble() ? QVariant(value.toDouble()) : QVariant(), std::nullopt});
    }
    // Sorted by the key, since links have no text to sort by.
    row.cells.append({QString(), keySortKey(key), std::nullopt, links});
    return row;
}

// The table of a list, from the item's status. See decision 0071.
class ListView : public widgets::ItemListView
{
public:
    ListView(ItemContext &context, QWidget *parent)
        : widgets::ItemListView(parent)
        , m_context(context)
        , m_extraFields(extraFieldsOf(context.moduleSettings(kGeneralScreen)))
    {
        QList<widgets::ListColumn> columns{{QCoreApplication::translate("jira", "Key"), 1, false},
                                           {QCoreApplication::translate("jira", "Summary"), 0, true},
                                           {QCoreApplication::translate("jira", "Status"), 2, false},
                                           {QCoreApplication::translate("jira", "Assignee"), 4, false},
                                           {QCoreApplication::translate("jira", "Updated"), 3, false}};
        for (int i = 0; i < m_extraFields.size(); ++i)
            columns.append({m_extraFields.at(i), kFirstExtraPriority + i, false});
        columns.append({QCoreApplication::translate("jira", "Links"), 0, false});
        setColumns(columns, UpdatedColumn);
        // They show once the status says the site has them.
        for (int i = 0; i < m_extraFields.size(); ++i)
            setColumnVisible(FirstExtraColumn + i, false);
        addFilter(StatusColumn, QCoreApplication::translate("jira", "Status"));
        setPageSize(listPageSize(context.config()));
        const QJsonObject config = context.config();
        setDescription(listDescription(config.value(QStringLiteral("preset")).toString(),
                                       config.value(QStringLiteral("jql")).toString(),
                                       context.projectSetting(QStringLiteral("projectKey")).toString()));

        connect(this, &widgets::ItemListView::rowLinkActivated, this,
                [this](const QString &key, const QString &link) { activateLink(key, link); });
        connect(this, &widgets::ItemListView::rowMenuRequested, this,
                [this](const QString &key, const QPoint &globalPos) { showRowMenu(key, globalPos); });

        if (ItemStatusSource *source = context.status()) {
            source->subscribe(this, [this](const ItemStatus &status) { showStatus(status); });
            showStatus(source->current());
        } else {
            setMessage(QCoreApplication::translate("jira", "Loading…"));
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
            setMessage(status.message.isEmpty() ? QCoreApplication::translate("jira", "Loading…") : status.message);
            return;
        }
        setMessage(QString());

        const QJsonObject names = status.data.value(QStringLiteral("names")).toObject();
        QStringList ids;
        for (int i = 0; i < m_extraFields.size(); ++i) {
            ids << fieldIdByName(names, m_extraFields.at(i));
            setColumnVisible(FirstExtraColumn + i, !ids.last().isEmpty());
        }

        const QList<widgets::ListLink> links = rowLinks(!site().isEmpty());
        QList<widgets::ListRow> rows;
        const QJsonArray issues = status.data.value(QStringLiteral("rows")).toArray();
        for (const QJsonValue &issue : issues)
            rows << readRow(issue.toObject(), ids, links);
        setRows(rows);
    }

    void activateLink(const QString &key, const QString &link)
    {
        if (link == kWebLink)
            openWeb(key);
        else if (link == kOpenLink)
            openPage(m_context, issuePageUrl(key));
        else if (link == kAddLink)
            addToTask(key);
    }

    void openWeb(const QString &key)
    {
        const QUrl url = issuePageUrl(key);
        if (!url.isEmpty())
            QDesktopServices::openUrl(url);
    }

    void addToTask(const QString &key)
    {
        m_context.addItemToTask(QStringLiteral("jira.issue.summary"), key, QJsonObject{{QStringLiteral("key"), key}});
    }

    // Not exec(), whose event loop could outlive this view.
    void showRowMenu(const QString &key, const QPoint &globalPos)
    {
        auto *menu = new QMenu(this);
        menu->setObjectName(QStringLiteral("rowMenu"));
        menu->setAttribute(Qt::WA_DeleteOnClose);
        const QUrl url = issuePageUrl(key);

        QAction *web = menu->addAction(QCoreApplication::translate("jira", "Web"));
        web->setEnabled(!url.isEmpty());
        connect(web, &QAction::triggered, this, [this, key] { openWeb(key); });
        QAction *open = menu->addAction(QCoreApplication::translate("jira", "Open page"));
        open->setEnabled(!url.isEmpty());
        connect(open, &QAction::triggered, this, [this, url] { openPage(m_context, url); });
        connect(menu->addAction(QCoreApplication::translate("jira", "Add to task as item")), &QAction::triggered, this,
                [this, key] { addToTask(key); });
        QAction *copy = menu->addAction(QCoreApplication::translate("jira", "Copy link"));
        copy->setEnabled(!url.isEmpty());
        connect(copy, &QAction::triggered, this,
                [url] { QGuiApplication::clipboard()->setText(url.toString()); });

        menu->popup(globalPos);
    }

    ItemContext &m_context;
    QStringList m_extraFields;
};

} // namespace

QWidget *createListView(ItemContext &context, QWidget *parent)
{
    return new ListView(context, parent);
}

} // namespace mirante::modules::jira
