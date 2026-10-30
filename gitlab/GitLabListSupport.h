// What the GitLab lists share: drawing rows from a status, the links and menu of
// a row, the pages and the Web link. Private to the module. See decision 0071.
#pragma once

#include "GitLabLists.h"

#include <mirante/Status.h>
#include <mirante/widgets/ItemListView.h>

#include <QChar>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QUrl>
#include <QVariant>

class QLabel;

namespace mirante {
class ItemContext;
}

namespace mirante::modules::gitlab {

// What a row points at, and what its links do.
struct ListRowTarget
{
    QString key;
    QVariant sortKey;       // of the column that identifies the row, and of the links
    QString project;        // empty if the row does not say
    qint64 iid = 0;         // the merge request or issue number, or 0
    QUrl address;           // its page on the site, or empty, and then Web and Open go
    QString addType;        // the type that "Add" adds, or empty, and then Add goes
    QString addName;
    QJsonObject addConfig;

    bool complete() const { return !project.isEmpty() && iid > 0; }
};

// The project of a row: from references.full, else from web_url. project_id is
// no use here, because it is a number. Empty if neither gives one.
QString rowProject(const QJsonObject &row, QChar separator, const QUrl &baseUrl);

// The cell of a value that is missing, or of a column whose answer has not come.
widgets::ListCell dashCell();
// A date from GitLab, in compact form and sorted by the date. A missing one is a
// dash, and sorts as the oldest.
widgets::ListCell dateCell(const QJsonValue &value);

// The base of the lists. A subclass sets its columns in its constructor, and
// implements rowTarget() and rowCells(). Requests use this view, or a child of it,
// as their receiver, so an answer that comes after the view is gone is dropped.
class GitLabList : public widgets::ItemListView
{
public:
    // Draws the item's status now and at each change. Null, where the core keeps
    // no status, stays at "Loading…".
    void follow(ItemStatusSource *source);

protected:
    // page is the Web link above the table, which hides without a web address.
    // The item's pageSize field sets the rows per page.
    GitLabList(ItemContext &context, const QString &emptyText, const QUrl &page, QWidget *parent);

    virtual ListRowTarget rowTarget(const QJsonObject &row, int index, const QUrl &baseUrl) const = 0;
    // The cells of one row, in the order of the columns.
    virtual QList<widgets::ListCell> rowCells(const ListRowTarget &target, const QJsonObject &row) const = 0;
    // Called while visible, after each new status and each time the view shows.
    virtual void askForCells() {}
    // Called while visible, when a new page, sort or filter changes visibleKeys().
    virtual void askForNewKeys() {}
    // Called when the view hides.
    virtual void stopAsking() {}

    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

    ItemContext &context() const { return *m_context; }
    // "Web", "Open" and "Add", as far as the target allows.
    widgets::ListCell linksCell(const ListRowTarget &target) const;
    ListRowTarget target(const QString &key) const { return m_targets.value(key); }

private:
    void showStatus(const ItemStatus &status);
    void showRowMenu(const QString &key, const QPoint &globalPos);
    void activateLink(const QString &key, const QString &link);
    void openPage(const QUrl &address);
    void addToTask(const ListRowTarget &target);

    ItemContext *m_context;
    QLabel *m_emptyLine;
    QHash<QString, ListRowTarget> m_targets;
    bool m_drawn = false;
    bool m_shown = false;
    // setRows() also changes visibleKeys(), and showStatus() asks after it.
    bool m_settingRows = false;
};

// What differs between the two lists of numbered objects.
struct ListTraits
{
    ListKind kind;
    QChar separator;        // between a project and a number: '!' or '#'
    QString summaryType;    // the type that "Add" adds
    QString emptyText;      // the line under a table with no rows
};

// A list of merge requests or issues. A list can span projects, so a row's project
// comes from the row, never from the item's configuration.
class NumberedList : public GitLabList
{
protected:
    NumberedList(ItemContext &context, const ListTraits &traits, QWidget *parent);

    ListRowTarget rowTarget(const QJsonObject &row, int index, const QUrl &baseUrl) const override;
    // Such as "backend!42", sorted by the whole path and then by the number.
    widgets::ListCell refCell(const ListRowTarget &target) const;

private:
    ListTraits m_traits;
};

} // namespace mirante::modules::gitlab
