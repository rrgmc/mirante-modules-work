// What the two GitLab lists share: drawing rows from a status, the preview of
// the selected row, the row menu and the Web link. Private to the module. See
// docs/plans/m6-native-item-types.md.
#pragma once

#include "GitLabLists.h"

#include <mirante/Status.h>
#include <mirante/widgets/ItemListView.h>

#include <QChar>
#include <QCoreApplication>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

class QLabel;

namespace mirante {
class ItemContext;
}

namespace mirante::modules::gitlab {

// What differs between the two lists.
struct ListTraits
{
    ListKind kind;
    QChar separator;        // between a project and a number: '!' or '#'
    QString summaryType;    // the type that "Add to task as item" adds
    QString emptyText;      // the line under a table with no rows
    QWidget *(*createPreview)(ItemContext &, const QString &project, qint64 iid, QWidget *parent);
    void (*fetchPreview)(StatusJob &);
};

// The merge request or issue of a row. A list can span projects, so the project
// comes from the row, never from the item's configuration.
struct ListRowTarget
{
    QString key;
    QString project;        // empty if the row does not say
    qint64 iid = 0;
    QUrl address;           // its page on the site, or empty

    bool complete() const { return !project.isEmpty() && iid > 0; }
};

// The project of a row: from references.full, else from web_url. project_id is
// no use here, because it is a number. Empty if neither gives one.
QString rowProject(const QJsonObject &row, QChar separator, const QUrl &baseUrl);

// The cell of a column whose answer has not come.
widgets::ListCell dashCell();
// The date of the last update, short and local, sorted by the date.
widgets::ListCell updatedCell(const QJsonObject &row);

// The base of the two lists. A subclass sets its columns in its constructor, and
// implements rowCells(). Requests use this view, or a child of it, as their
// receiver, so an answer that comes after the view is gone is dropped.
class GitLabList : public widgets::ItemListView
{
    Q_DECLARE_TR_FUNCTIONS(gitlab)

public:
    // Draws the item's status now and at each change. Null, where the core keeps
    // no status, stays at "Loading…".
    void follow(ItemStatusSource *source);

protected:
    GitLabList(ItemContext &context, const ListTraits &traits, QWidget *parent);

    // The cells of one row, in the order of the columns.
    virtual QList<widgets::ListCell> rowCells(const ListRowTarget &target, const QJsonObject &row) const = 0;
    // Called while visible, after each new status and each time the view shows.
    virtual void askForCells() {}
    // Called when the view hides.
    virtual void stopAsking() {}

    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

    ItemContext &context() const { return *m_context; }
    // Such as "backend!42", sorted by the whole path and then by the number.
    widgets::ListCell refCell(const ListRowTarget &target) const;
    ListRowTarget target(const QString &key) const { return m_targets.value(key); }
    // The keys of the rows from the top of the table, as it is sorted now.
    QStringList keysInTableOrder() const;

private:
    void showStatus(const ItemStatus &status);
    ListRowTarget readTarget(const QJsonObject &row, int index, const QUrl &baseUrl) const;
    void showPreviewOf(const QString &key);
    void readPreviewAgain();
    void showRowMenu(const QString &key, const QPoint &globalPos);
    void openPage(const QUrl &address);

    ItemContext *m_context;
    ListTraits m_traits;
    QLabel *m_emptyLine;
    // In the order given to setRows().
    QStringList m_keys;
    QHash<QString, ListRowTarget> m_targets;
    QString m_previewKey;
    // The list's status changed while the view was hidden.
    bool m_previewOutdated = false;
    bool m_drawn = false;
    bool m_shown = false;
};

} // namespace mirante::modules::gitlab
