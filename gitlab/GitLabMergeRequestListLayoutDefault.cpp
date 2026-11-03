// The default of the extension point "mergerequestlistlayout": the core's
// table, with the columns Ref, Title, Project, Author, Pipeline, Approvals,
// Updated and Links. Project, and a filter on it, show only in a list over
// several projects. It is compiled only when the app names no extension for
// "mergerequestlistlayout". See README.md#list-layouts.
#include "GitLabListSupport.h"
#include "GitLabMergeRequestListLayout.h"

#include <QCoreApplication>

using mirante::widgets::ListCell;

namespace mirante::modules::gitlab {

namespace {

enum Column {
    RefColumn,
    TitleColumn,
    ProjectColumn,
    AuthorColumn,
    PipelineColumn,
    ApprovalsColumn,
    UpdatedColumn,
    LinksColumn
};

// Such as "1/2", or just the count when none are required. Sorted by the
// approvals given, and a row without an answer comes last.
ListCell approvalsCell(const MergeRequestListModel::Approvals &approvals)
{
    if (!approvals.answered) {
        ListCell cell = dashCell();
        cell.sortKey = -1;
        return cell;
    }
    const QString text = approvals.required > 0 ? QStringLiteral("%1/%2").arg(approvals.given).arg(approvals.required)
                                                : QString::number(approvals.given);
    return {text, approvals.given, {}};
}

class MergeRequestListLayout : public DefaultListLayout
{
public:
    MergeRequestListLayout(MergeRequestListModel &model, QWidget *parent)
        : DefaultListLayout(model, QStringLiteral("merge-request-list"), parent)
        , m_model(model)
    {
        setColumns({{QCoreApplication::translate("gitlab", "Ref"), 1},
                    {QCoreApplication::translate("gitlab", "Title"), 0, true},
                    {QCoreApplication::translate("gitlab", "Project"), 6},
                    {QCoreApplication::translate("gitlab", "Author"), 5},
                    {QCoreApplication::translate("gitlab", "Pipeline"), 2},
                    {QCoreApplication::translate("gitlab", "Approvals"), 4},
                    {QCoreApplication::translate("gitlab", "Updated"), 3},
                    {QCoreApplication::translate("gitlab", "Links"), 0}},
                   UpdatedColumn);
        setColumnVisible(PipelineColumn, model.isColumnShown(QStringLiteral("pipeline")));
        setColumnVisible(ApprovalsColumn, model.isColumnShown(QStringLiteral("approvals")));
        setColumnVisible(ProjectColumn, model.spansProjects());
        if (model.spansProjects()) {
            addFilter(ProjectColumn, QCoreApplication::translate("gitlab", "Project"));
            setFilterDefault(ProjectColumn, model.filterDefault(QStringLiteral("project")));
        }
        start();
    }

protected:
    QList<ListCell> rowCells(const QString &key) const override
    {
        const MergeRequestListModel::MergeRequest mergeRequest = m_model.mergeRequest(key);
        return {{mergeRequest.ref, mergeRequest.refSortKey, {}},
                {mergeRequest.title, {}, {}},
                textCell(mergeRequest.project),
                {mergeRequest.author, {}, {}},
                pillCell(mergeRequest.pipeline),
                approvalsCell(mergeRequest.approvals),
                dateCell(mergeRequest.data.value(QStringLiteral("updated_at"))),
                linksCell(mergeRequest, mergeRequest.refSortKey)};
    }

private:
    MergeRequestListModel &m_model;
};

} // namespace

QWidget *makeMergeRequestListLayout(MergeRequestListModel &model, QWidget *parent)
{
    return new MergeRequestListLayout(model, parent);
}

} // namespace mirante::modules::gitlab
