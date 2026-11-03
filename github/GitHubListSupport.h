// What the GitHub lists share: reading a search row's repository, and the base
// of the default list layouts, which draw a model in the core's ItemListView.
// Private to the module: a layout of an app uses the models. See
// README.md#list-views and decision 0071.
#pragma once

#include "GitHubListModel.h"
#include "GitHubLists.h"

#include <mirante/widgets/ItemListView.h>

#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>

class QLabel;

namespace mirante::modules::github {

// The repository of a search row, "owner/repo", from its repository_url
// (".../repos/<owner>/<repo>"). Empty if it has none.
QString rowRepository(const QJsonObject &row);

// A pull request or issue row of the search API. A list can span repositories,
// so a row's repository comes from the row, never from the item's
// configuration. Its key is "owner/repo#42", or "row-<index>" without one.
struct NumberedRow
{
    ListRow row;
    ListModel::AddTarget add;
    QString repository;
    qint64 number = 0;
    // Such as "widgets#42", and a key that sorts by the whole repository and
    // then by the number.
    QString ref;
    QString refSortKey;
};
NumberedRow readNumberedRow(const QJsonObject &row, int index, const QUrl &webBase, ListKind kind,
                            const QString &summaryType);

// The cell of a value that is missing, or of a column whose answer has not come.
widgets::ListCell dashCell();
// A date from GitHub, in compact form and sorted by the date. A missing one is a
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
    // setRows() and setCell() also change visibleKeys(), and draw() tells the
    // model after them.
    bool m_drawing = false;
};

} // namespace mirante::modules::github
