#include "GitHubIssueListView.h"

#include "GitHubListSupport.h"
#include "GitHubViewSupport.h"

#include <mirante/ItemContext.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

using mirante::widgets::ListCell;

namespace mirante::modules::github {

namespace {

enum Column { RefColumn, TitleColumn, AssigneesColumn, LabelsColumn, UpdatedColumn, LinksColumn };

ListTraits issueTraits()
{
    return {ListKind::Issues, QStringLiteral("github.issue.summary"), QCoreApplication::translate("github", "No issues")};
}

// GitHub's labels are objects with a name.
QString labelNames(const QJsonArray &labels)
{
    QStringList names;
    for (const QJsonValue &label : labels)
        names << label.toObject().value(QStringLiteral("name")).toString();
    names.removeAll(QString());
    return names.join(QStringLiteral(", "));
}

class IssueListView : public NumberedList
{
public:
    IssueListView(ItemContext &context, QWidget *parent)
        : NumberedList(context, issueTraits(), parent)
    {
        setColumns({{QCoreApplication::translate("github", "Ref"), 1},
                    {QCoreApplication::translate("github", "Title"), 0, true},
                    {QCoreApplication::translate("github", "Assignees"), 3},
                    {QCoreApplication::translate("github", "Labels"), 4},
                    {QCoreApplication::translate("github", "Updated"), 2},
                    {QCoreApplication::translate("github", "Links"), 0}},
                   UpdatedColumn);
    }

protected:
    QList<ListCell> rowCells(const ListRowTarget &target, const QJsonObject &row) const override
    {
        return {refCell(target),
                {row.value(QStringLiteral("title")).toString(), {}, {}},
                {userNames(row.value(QStringLiteral("assignees")).toArray()), {}, {}},
                {labelNames(row.value(QStringLiteral("labels")).toArray()), {}, {}},
                dateCell(row.value(QStringLiteral("updated_at"))),
                linksCell(target)};
    }
};

} // namespace

QWidget *createIssueListView(ItemContext &context, QWidget *parent)
{
    auto *view = new IssueListView(context, parent);
    view->follow(context.status());
    return view;
}

} // namespace mirante::modules::github
