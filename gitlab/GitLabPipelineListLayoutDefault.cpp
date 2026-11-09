// The default of the extension point "pipelinelistlayout": the core's table,
// with the columns Id, Project, Status, Ref, Author, Started and Links, and a
// filter on Status, which keeps the user's last choice in the item's
// statusFilter. The Status filter lists every status, not only those of the
// current pipelines. Project, and a
// filter on it, show only in a list over several projects. It is compiled only
// when the app names no extension for "pipelinelistlayout". See README.md#list-layouts.
#include "GitLabListSupport.h"
#include "GitLabPipelineListLayout.h"
#include "GitLabViewSupport.h"

#include <QCoreApplication>

using mirante::widgets::ListCell;

namespace mirante::modules::gitlab {

namespace {

enum Column { IdColumn, ProjectColumn, StatusColumn, RefColumn, AuthorColumn, StartedColumn, LinksColumn };

class PipelineListLayout : public DefaultListLayout
{
public:
    PipelineListLayout(PipelineListModel &model, QWidget *parent)
        : DefaultListLayout(model, QStringLiteral("pipeline-list"), parent)
        , m_model(model)
    {
        setColumns({{QCoreApplication::translate("gitlab", "Id"), 2},
                    {QCoreApplication::translate("gitlab", "Project"), 5},
                    {QCoreApplication::translate("gitlab", "Status"), 1},
                    {QCoreApplication::translate("gitlab", "Ref"), 0, true},
                    {QCoreApplication::translate("gitlab", "Author"), 4},
                    {QCoreApplication::translate("gitlab", "Started"), 3},
                    {QCoreApplication::translate("gitlab", "Links"), 0}},
                   IdColumn);
        setColumnVisible(ProjectColumn, model.spansProjects());
        if (model.spansProjects()) {
            addKeptFilter(ProjectColumn, QCoreApplication::translate("gitlab", "Project"), QStringLiteral("project"));
        }
        addKeptFilter(StatusColumn, QCoreApplication::translate("gitlab", "Status"), QStringLiteral("status"));
        // Every status, so that a chosen one stays chosen while no pipeline has it.
        setFilterValues(StatusColumn, pipelineStatusTexts());
        start();
    }

protected:
    QList<ListCell> rowCells(const QString &key) const override
    {
        const PipelineListModel::Pipeline pipeline = m_model.pipeline(key);
        const QJsonValue started = pipeline.data.value(QStringLiteral("started_at"));
        return {{pipeline.data.value(QStringLiteral("id")).toVariant().toString(), pipeline.id, {}},
                textCell(pipeline.project),
                pillCell(pipeline.state),
                {pipeline.ref, {}, {}},
                textCell(pipeline.author),
                dateCell(started.isString() ? started : pipeline.data.value(QStringLiteral("created_at"))),
                linksCell(pipeline, pipeline.id)};
    }

private:
    PipelineListModel &m_model;
};

} // namespace

QWidget *makePipelineListLayout(PipelineListModel &model, QWidget *parent)
{
    return new PipelineListLayout(model, parent);
}

} // namespace mirante::modules::gitlab
