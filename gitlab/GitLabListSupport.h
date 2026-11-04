// What the GitLab lists share: reading a row's project, and the base
// of the default list layouts, which draw a model in the core's ItemListView.
// Private to the module: a layout of an app uses the models. See
// README.md#list-views and decision 0071.
#pragma once

#include "GitLabListModel.h"
#include "GitLabLists.h"

#include <mirante/widgets/ItemListView.h>

#include <QChar>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>

class QLabel;

namespace mirante::modules::gitlab {

// The project of a row: from references.full, else from web_url. project_id is
// no use here, because it is a number. Empty if neither gives one.
QString rowProject(const QJsonObject &row, QChar separator, const QUrl &baseUrl);

// A merge request or issue row. A list can span projects, so a row's project
// comes from the row, never from the item's configuration. Its key is
// "group/project!42" or "group/project#7", or "row-<index>" without one.
struct NumberedRow
{
    ListRow row;
    ListModel::AddTarget add;
    QString project;
    qint64 iid = 0;
    // Such as "backend!42", and a key that sorts by the whole path and then by
    // the number.
    QString ref;
    QString refSortKey;
};
// separator is '!' for merge requests and '#' for issues.
NumberedRow readNumberedRow(const QJsonObject &row, int index, const QUrl &baseUrl, ListKind kind, QChar separator,
                            const QString &summaryType);

// The cell of a value that is missing, or of a column whose answer has not come.
widgets::ListCell dashCell();
// A date from GitLab, in compact form and sorted by the date. A missing one is a
// dash, and sorts as the oldest.
widgets::ListCell dateCell(const QJsonValue &value);
// A text, or a dash when it is empty.
widgets::ListCell textCell(const QString &text);
// A state as a pill, or a dash when it has no text.
widgets::ListCell pillCell(const Pill &pill);

// The base of the default list layouts: a table of the model's rows, with the
// description, the Web link, pages, and the links and menu of each row. A
// subclass sets its columns in its constructor, then calls start().
class DefaultListLayout : public widgets::ItemListView
{
public:
    ~DefaultListLayout() override;

protected:
    // view names the layout, such as "pull-request-list", for its object name.
    DefaultListLayout(ListModel &model, const QString &view, QWidget *parent);

    // Draws the model now, and follows it.
    void start();
    // A filter on a column that starts with the user's last choice, kept in
    // the item's <name>Filter. See ListModel::keepFilter().
    void addKeptFilter(int column, const QString &label, const QString &name);
    // The cells of one row, in the order of the columns.
    virtual QList<widgets::ListCell> rowCells(const QString &key) const = 0;
    // "Web", "Open" and "Add", as far as the row allows, sorted by sortKey.
    widgets::ListCell linksCell(const ListRow &row, const QVariant &sortKey) const;

private:
    void draw();
    void drawRow(const QString &key);
    void showRowMenu(const QString &key, const QPoint &globalPos);
    void activateLink(const QString &key, const QString &link);

    ListModel &m_model;
    QLabel *m_emptyLine;
    // The cells of each row as last drawn.
    QHash<QString, QList<widgets::ListCell>> m_drawn;
    // The name of each column's kept filter, by column.
    QHash<int, QString> m_keptFilters;
    // setRows() also changes visibleKeys(), and draw() tells the model after it.
    bool m_drawing = false;
};

} // namespace mirante::modules::gitlab
