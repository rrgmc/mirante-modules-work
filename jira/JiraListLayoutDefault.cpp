// The default of the extension point "listlayout": the core's table, with the
// columns Key, Summary, Status, Assignee, Updated, the extra fields and Links,
// and a filter on Status, which starts with the user's last choice. It is
// compiled only when the app names no extension for "listlayout". See
// README.md#layouts.
#include "JiraListLayout.h"

#include <mirante/widgets/CompactDate.h>
#include <mirante/widgets/ItemListView.h>

#include <QCoreApplication>
#include <QMenu>

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

widgets::ListRow drawRow(const ListModel::Issue &issue)
{
    widgets::ListRow row;
    row.key = issue.key;
    row.cells = {
        {issue.key, issue.keySortKey, std::nullopt},
        {issue.summary, {}, std::nullopt},
        {issue.status.text, {}, issue.status.role},
        {issue.assignee.isEmpty() ? QCoreApplication::translate("jira", "Unassigned") : issue.assignee, {},
         std::nullopt},
        {issue.updated.isValid() ? widgets::compactDate(issue.updated) : issue.updatedText,
         issue.updated.toMSecsSinceEpoch(), std::nullopt},
    };
    for (const ListModel::FieldValue &field : issue.fields)
        row.cells.append({field.text, field.sortKey, std::nullopt});
    // Sorted by the key, since links have no text to sort by.
    row.cells.append({QString(), issue.keySortKey, std::nullopt, rowLinks(!issue.address.isEmpty())});
    return row;
}

class ListLayout : public widgets::ItemListView
{
public:
    ListLayout(ListModel &model, QWidget *parent)
        : widgets::ItemListView(parent)
        , m_model(model)
    {
        setObjectName(QStringLiteral("jira-list-default-layout"));
        const QStringList extraFields = model.extraFields();
        QList<widgets::ListColumn> columns{{QCoreApplication::translate("jira", "Key"), 1, false},
                                           {QCoreApplication::translate("jira", "Summary"), 0, true},
                                           {QCoreApplication::translate("jira", "Status"), 2, false},
                                           {QCoreApplication::translate("jira", "Assignee"), 4, false},
                                           {QCoreApplication::translate("jira", "Updated"), 3, false}};
        for (int i = 0; i < extraFields.size(); ++i)
            columns.append({extraFields.at(i), kFirstExtraPriority + i, false});
        columns.append({QCoreApplication::translate("jira", "Links"), 0, false});
        setColumns(columns, UpdatedColumn);
        // They show once the status says the site has them.
        for (int i = 0; i < extraFields.size(); ++i)
            setColumnVisible(FirstExtraColumn + i, false);
        addFilter(StatusColumn, QCoreApplication::translate("jira", "Status"));
        setFilterDefault(StatusColumn, model.filterDefault(QStringLiteral("status")));
        setPageSize(model.pageSize());
        setDescription(model.description());

        connect(this, &widgets::ItemListView::rowLinkActivated, this,
                [this](const QString &key, const QString &link) { activateLink(key, link); });
        connect(this, &widgets::ItemListView::rowMenuRequested, this,
                [this](const QString &key, const QPoint &globalPos) { showRowMenu(key, globalPos); });
        connect(this, &widgets::ItemListView::filterChanged, this, [this](int column, const QStringList &values) {
            if (column == StatusColumn)
                m_model.keepFilter(QStringLiteral("status"), values);
        });
        connect(&model, &ListModel::changed, this, [this] { draw(); });
        draw();
    }

private:
    void draw()
    {
        if (!m_model.hasData()) {
            setMessage(m_model.message());
            return;
        }
        setMessage(QString());

        const QList<bool> found = m_model.extraFieldsFound();
        for (int i = 0; i < found.size(); ++i)
            setColumnVisible(FirstExtraColumn + i, found.at(i));

        QList<widgets::ListRow> rows;
        for (const ListModel::Issue &issue : m_model.issues())
            rows << drawRow(issue);
        setRows(rows);
    }

    void activateLink(const QString &key, const QString &link)
    {
        if (link == kWebLink)
            m_model.openWeb(key);
        else if (link == kOpenLink)
            m_model.openPage(key);
        else if (link == kAddLink)
            m_model.addToTask(key);
    }

    // Not exec(), whose event loop could outlive this view.
    void showRowMenu(const QString &key, const QPoint &globalPos)
    {
        auto *menu = new QMenu(this);
        menu->setObjectName(QStringLiteral("rowMenu"));
        menu->setAttribute(Qt::WA_DeleteOnClose);
        const bool hasAddress = !m_model.issue(key).address.isEmpty();

        QAction *web = menu->addAction(QCoreApplication::translate("jira", "Web"));
        web->setEnabled(hasAddress);
        connect(web, &QAction::triggered, this, [this, key] { m_model.openWeb(key); });
        QAction *open = menu->addAction(QCoreApplication::translate("jira", "Open page"));
        open->setEnabled(hasAddress);
        connect(open, &QAction::triggered, this, [this, key] { m_model.openPage(key); });
        connect(menu->addAction(QCoreApplication::translate("jira", "Add to task as item")), &QAction::triggered, this,
                [this, key] { m_model.addToTask(key); });
        QAction *copy = menu->addAction(QCoreApplication::translate("jira", "Copy link"));
        copy->setEnabled(hasAddress);
        connect(copy, &QAction::triggered, this, [this, key] { m_model.copyLink(key); });

        menu->popup(globalPos);
    }

    ListModel &m_model;
};

} // namespace

QWidget *makeListLayout(ListModel &model, QWidget *parent)
{
    return new ListLayout(model, parent);
}

} // namespace mirante::modules::jira
