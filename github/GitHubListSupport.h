// What the GitHub lists share: drawing rows from a status, the links and menu of
// a row, the pages and the Web link. Private to the module. See
// README.md#list-views and decision 0071.
#pragma once

#include "GitHubLists.h"

#include <mirante/Status.h>
#include <mirante/widgets/ItemListView.h>

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

namespace mirante::modules::github {

// What a row points at, and what its links do.
struct ListRowTarget
{
    QString key;
    QVariant sortKey;       // of the column that identifies the row, and of the links
    QString repository;     // "owner/repo", empty if the row does not say
    qint64 number = 0;      // the pull request or issue number, or the run id, or 0
    QUrl address;           // its page on the site, or empty, and then Web and Open go
    QString addType;        // the type that "Add" adds, or empty, and then Add goes
    QString addName;
    QJsonObject addConfig;

    bool complete() const { return !repository.isEmpty() && number > 0; }
};

// The repository of a search row, "owner/repo", from its repository_url
// (".../repos/<owner>/<repo>"). Empty if it has none.
QString rowRepository(const QJsonObject &row);

// The cell of a value that is missing, or of a column whose answer has not come.
widgets::ListCell dashCell();
// A date from GitHub, in compact form and sorted by the date. A missing one is a
// dash, and sorts as the oldest.
widgets::ListCell dateCell(const QJsonValue &value);

// The base of the lists. A subclass sets its columns in its constructor, and
// implements rowTarget() and rowCells(). Requests use this view, or a child of it,
// as their receiver, so an answer that comes after the view is gone is dropped.
class GitHubList : public widgets::ItemListView
{
public:
    // Draws the item's status now and at each change. Null, where the core keeps
    // no status, stays at "Loading…".
    void follow(ItemStatusSource *source);

protected:
    // page is the Web link above the table, which hides without a web address.
    // The item's pageSize field sets the rows per page.
    GitHubList(ItemContext &context, const QString &emptyText, const QUrl &page, QWidget *parent);

    // webBase is the profile's web address, for an address the row does not give.
    virtual ListRowTarget rowTarget(const QJsonObject &row, int index, const QUrl &webBase) const = 0;
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
    QString summaryType;    // the type that "Add" adds
    QString emptyText;      // the line under a table with no rows
};

// A list of pull requests or issues, from the search API. A list can span
// repositories, so a row's repository comes from the row, never from the item's
// configuration. A row's key is "owner/repo#42".
class NumberedList : public GitHubList
{
protected:
    NumberedList(ItemContext &context, const ListTraits &traits, QWidget *parent);

    ListRowTarget rowTarget(const QJsonObject &row, int index, const QUrl &webBase) const override;
    // Such as "repo#42", sorted by the whole repository and then by the number.
    widgets::ListCell refCell(const ListRowTarget &target) const;

private:
    ListTraits m_traits;
};

} // namespace mirante::modules::github
