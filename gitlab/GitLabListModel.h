// What the three GitLab lists share: the model that a list layout draws. A
// layout is the code of an extension point, so this header is an interface that
// apps build against. A change to it is noted in CHANGELOG.md. See
// README.md#list-layouts.
#pragma once

#include "GitLabLists.h"
#include "GitLabSummaryModel.h"

#include <mirante/Status.h>

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

class QWidget;

namespace mirante {
class ItemContext;
}

namespace mirante::modules::gitlab {

// What every row of a list has.
struct ListRow
{
    // Stable across statuses, such as "acme/backend!42" or a pipeline's id, so a
    // layout can keep a selection.
    QString key;
    // GitLab's row object: a merge request, an issue or a pipeline.
    QJsonObject data;
    // Its page on the site, or empty. Without one, openWeb(), openPage() and
    // copyLink() do nothing.
    QUrl address;
    // Whether addToTask() can add it as an item.
    bool canAdd = false;
};

// The model of a list: the item's status, read into rows, and what a row's
// links do. A subclass adds its typed rows. The module makes the model, then the
// view with the point's make function, which draws the model now and again at
// each changed(). The model lives as long as the view.
class ListModel : public QObject
{
    Q_OBJECT

public:
    // What addToTask() adds for a row: an item of this type, name and
    // configuration. An empty type adds nothing.
    struct AddTarget
    {
        QString type;
        QString name;
        QJsonObject config;
    };

    ~ListModel() override;

    ItemContext &context() const { return *m_context; }
    // The item's configuration.
    QJsonObject config() const;

    // False until the first rows come. The view then shows message() alone,
    // such as "Loading…". Old rows stay through an error.
    bool hasData() const { return m_hasData; }
    QString message() const { return m_message; }

    // What the list shows, such as "Assigned to me · acme/backend".
    QString description() const { return m_description; }
    // The list's page on the site, or empty.
    QUrl pageAddress() const { return m_pageAddress; }
    // What an empty list says, such as "No merge requests".
    QString emptyText() const { return m_emptyText; }
    // The rows a page shows, from the item's pageSize field.
    int pageSize() const;
    // Whether the item's settings show a column: the boolean field show<Column>,
    // on when absent. isColumnShown("pipeline") reads showPipeline.
    bool isColumnShown(const QString &column) const;
    // Whether the list is of a type over several projects, such as
    // gitlab.mr.multilist. Its layout then shows each row's project, with a
    // filter on it.
    bool spansProjects() const { return m_span == ListSpan::Projects; }
    // The values a view filter on a column chooses at the start: the item's
    // field <column>Filter. filterDefault("status") reads statusFilter. Empty
    // means "All".
    QStringList filterDefault(const QString &column) const;

    // The keys of the rows, in the order of GitLab's answer.
    QStringList keys() const { return m_keys; }
    // A row by its key, or an empty row.
    ListRow row(const QString &key) const { return m_rows.value(key); }

    // A row's page in the external browser.
    void openWeb(const QString &key);
    // A row's page in Mirante, or in the external browser while Shift is held.
    void openPage(const QString &key);
    // Adds the row to the current task as an item of its own.
    void addToTask(const QString &key);
    // Copies the address of a row's page.
    void copyLink(const QString &key);

    // The rows the layout shows now, such as the current page in its order. A
    // model that reads more about rows reads it for these. A layout calls it
    // after each changed() and whenever the shown rows change.
    virtual void setVisibleRows(const QStringList &keys);

    // Called by the module only. follow() reads the item's status now and at
    // each change; a null source stays at "Loading…". attach() gives the model
    // to the view, which then owns it.
    void follow(ItemStatusSource *source);
    void attach(QWidget *view);

signals:
    // New rows, or another message.
    void changed();
    // More about one row came, such as its pipeline.
    void rowChanged(const QString &key);

protected:
    ListModel(ItemContext &context, const QString &emptyText, const QUrl &page, const QString &description,
              ListSpan span = ListSpan::Scope);

    // Reads the rows of a new status. baseUrl is the server's address, for an
    // address a row does not give. It calls addRow() for each row, in order.
    virtual void read(const QJsonArray &rows, const QUrl &baseUrl) = 0;
    void addRow(const ListRow &row, const AddTarget &add = {});
    // When the view shows or hides.
    virtual void viewVisibilityChanged(bool visible) { Q_UNUSED(visible) }
    bool isViewVisible() const { return m_viewVisible; }

    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void showStatus(const ItemStatus &status);

    ItemContext *m_context;
    ListSpan m_span;
    QString m_emptyText;
    QUrl m_pageAddress;
    QString m_description;
    bool m_hasData = false;
    QString m_message;
    QStringList m_keys;
    QHash<QString, ListRow> m_rows;
    QHash<QString, AddTarget> m_adds;
    QWidget *m_view = nullptr;
    bool m_viewVisible = false;
};

} // namespace mirante::modules::gitlab
