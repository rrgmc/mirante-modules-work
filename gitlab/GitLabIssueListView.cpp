#include "GitLabIssueListView.h"

#include "GitLabListSupport.h"
#include "GitLabViewSupport.h"

#include <mirante/ItemContext.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

using mirante::widgets::ListCell;

namespace mirante::modules::gitlab {

namespace {

enum Column { RefColumn, TitleColumn, AssigneesColumn, LabelsColumn, UpdatedColumn, LinksColumn };

ListTraits issueTraits()
{
    return {ListKind::Issues,
            QLatin1Char('#'),
            QStringLiteral("gitlab.issue.summary"),
            QCoreApplication::translate("gitlab", "No issues")};
}

QString labelNames(const QJsonArray &labels)
{
    QStringList names;
    for (const QJsonValue &label : labels)
        names << label.toString();
    return names.join(QStringLiteral(", "));
}

class IssueListView : public NumberedList
{
public:
    IssueListView(ItemContext &context, QWidget *parent)
        : NumberedList(context, issueTraits(), parent)
    {
        setColumns({{QCoreApplication::translate("gitlab", "Ref"), 1},
                    {QCoreApplication::translate("gitlab", "Title"), 0, true},
                    {QCoreApplication::translate("gitlab", "Assignees"), 3},
                    {QCoreApplication::translate("gitlab", "Labels"), 4},
                    {QCoreApplication::translate("gitlab", "Updated"), 2},
                    {QCoreApplication::translate("gitlab", "Links"), 0}},
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

} // namespace mirante::modules::gitlab
