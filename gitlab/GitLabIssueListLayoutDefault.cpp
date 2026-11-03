// The default of the extension point "issuelistlayout": the core's table, with
// the columns Ref, Title, Assignees, Labels, Updated and Links. It is compiled
// only when the app names no extension for "issuelistlayout". See
// README.md#list-layouts.
#include "GitLabIssueListLayout.h"
#include "GitLabListSupport.h"

#include <QCoreApplication>

using mirante::widgets::ListCell;

namespace mirante::modules::gitlab {

namespace {

enum Column { RefColumn, TitleColumn, AssigneesColumn, LabelsColumn, UpdatedColumn, LinksColumn };

class IssueListLayout : public DefaultListLayout
{
public:
    IssueListLayout(IssueListModel &model, QWidget *parent)
        : DefaultListLayout(model, QStringLiteral("issue-list"), parent)
        , m_model(model)
    {
        setColumns({{QCoreApplication::translate("gitlab", "Ref"), 1},
                    {QCoreApplication::translate("gitlab", "Title"), 0, true},
                    {QCoreApplication::translate("gitlab", "Assignees"), 3},
                    {QCoreApplication::translate("gitlab", "Labels"), 4},
                    {QCoreApplication::translate("gitlab", "Updated"), 2},
                    {QCoreApplication::translate("gitlab", "Links"), 0}},
                   UpdatedColumn);
        start();
    }

protected:
    QList<ListCell> rowCells(const QString &key) const override
    {
        const IssueListModel::Issue issue = m_model.issue(key);
        return {{issue.ref, issue.refSortKey, {}},
                {issue.title, {}, {}},
                {issue.assignees.join(QStringLiteral(", ")), {}, {}},
                {issue.labels.join(QStringLiteral(", ")), {}, {}},
                dateCell(issue.data.value(QStringLiteral("updated_at"))),
                linksCell(issue, issue.refSortKey)};
    }

private:
    IssueListModel &m_model;
};

} // namespace

QWidget *makeIssueListLayout(IssueListModel &model, QWidget *parent)
{
    return new IssueListLayout(model, parent);
}

} // namespace mirante::modules::gitlab
