// The default of the extension point "runlistlayout": the core's table, with
// the columns Run, Workflow, Status, Branch, Actor, Started and Links, and
// filters on Status and Workflow. It is compiled only when the app names no
// extension for "runlistlayout". See README.md#list-layouts.
#include "GitHubListSupport.h"
#include "GitHubRunListLayout.h"

#include <QCoreApplication>

using mirante::widgets::ListCell;

namespace mirante::modules::github {

namespace {

enum Column { RunColumn, WorkflowColumn, StatusColumn, BranchColumn, ActorColumn, StartedColumn, LinksColumn };

class RunListLayout : public DefaultListLayout
{
public:
    RunListLayout(RunListModel &model, QWidget *parent)
        : DefaultListLayout(model, QStringLiteral("run-list"), parent)
        , m_model(model)
    {
        setColumns({{QCoreApplication::translate("github", "Run"), 2},
                    {QCoreApplication::translate("github", "Workflow"), 3},
                    {QCoreApplication::translate("github", "Status"), 1},
                    {QCoreApplication::translate("github", "Branch"), 0, true},
                    {QCoreApplication::translate("github", "Actor"), 5},
                    {QCoreApplication::translate("github", "Started"), 4},
                    {QCoreApplication::translate("github", "Links"), 0}},
                   RunColumn);
        addFilter(StatusColumn, QCoreApplication::translate("github", "Status"));
        addFilter(WorkflowColumn, QCoreApplication::translate("github", "Workflow"));
        start();
    }

protected:
    QList<ListCell> rowCells(const QString &key) const override
    {
        const RunListModel::Run run = m_model.run(key);
        // Run numbers count per workflow, and ids grow across them, so the id
        // sorts the runs by age.
        const QJsonValue started = run.data.value(QStringLiteral("run_started_at"));
        return {{run.number > 0 ? QStringLiteral("#%1").arg(run.number) : dashCell().text, run.id, {}},
                textCell(run.workflow),
                pillCell(run.state),
                textCell(run.branch),
                textCell(run.actor),
                dateCell(started.isString() ? started : run.data.value(QStringLiteral("created_at"))),
                linksCell(run, run.id)};
    }

private:
    RunListModel &m_model;
};

} // namespace

QWidget *makeRunListLayout(RunListModel &model, QWidget *parent)
{
    return new RunListLayout(model, parent);
}

} // namespace mirante::modules::github
