#include "GitHubRunListView.h"

#include "GitHubListSupport.h"
#include "GitHubSettings.h"
#include "GitHubUrls.h"
#include "GitHubViewSupport.h"

#include <mirante/ItemContext.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

using mirante::widgets::ListCell;

namespace mirante::modules::github {

namespace {

enum Column { RunColumn, WorkflowColumn, StatusColumn, BranchColumn, ActorColumn, StartedColumn, LinksColumn };

// The pull request a run ran for: its first entry in pull_requests, for a run
// of the pull_request event. Else 0.
qint64 pullRequestOfRun(const QJsonObject &run)
{
    if (run.value(QStringLiteral("event")).toString() != QLatin1String("pull_request"))
        return 0;
    const QJsonArray pullRequests = run.value(QStringLiteral("pull_requests")).toArray();
    if (pullRequests.isEmpty())
        return 0;
    return pullRequests.first().toObject().value(QStringLiteral("number")).toInteger();
}

class RunListView : public GitHubList
{
public:
    RunListView(ItemContext &context, QWidget *parent)
        : GitHubList(context, QCoreApplication::translate("github", "No runs"),
                     runListWebUrl(webBaseOf(context.moduleSettings(kGeneralScreen)), context.config(),
                                   context.projectSetting(kProjectSettingKey)),
                     parent)
        , m_repository(repositoryOf(context.config(), context.projectSetting(kProjectSettingKey)))
        , m_workflow(configuredWorkflow(context.config()))
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
        setDescription(runListDescription(context.config(), context.projectSetting(kProjectSettingKey)));
    }

protected:
    ListRowTarget rowTarget(const QJsonObject &row, int index, const QUrl &webBase) const override
    {
        const qint64 id = row.value(QStringLiteral("id")).toInteger();
        ListRowTarget target;
        target.key = id > 0 ? QString::number(id) : QStringLiteral("row-%1").arg(index);
        // Run numbers count per workflow, and ids grow across them, so the id
        // sorts the runs by age.
        target.sortKey = id;
        target.repository = m_repository;
        target.number = id;

        QUrl built;
        if (id > 0 && !m_repository.isEmpty())
            built = webPageUrl(webBase, m_repository + QStringLiteral("/actions/runs/") + QString::number(id));
        const QUrl address = webAddress(row, built);
        target.address = isWebAddress(address) ? address : QUrl();

        if (m_repository.isEmpty())
            return target;
        // The same item that a pasted address of its branch or pull request makes.
        QJsonObject config{{QStringLiteral("repository"), m_repository}};
        const qint64 number = pullRequestOfRun(row);
        const QString branch = row.value(QStringLiteral("head_branch")).toString();
        if (number > 0) {
            target.addName = m_repository + QLatin1Char('#') + QString::number(number) + QStringLiteral(" CI");
            config.insert(QStringLiteral("branchSource"), QStringLiteral("pullRequest"));
            config.insert(QStringLiteral("number"), number);
        } else if (!branch.isEmpty()) {
            target.addName = m_repository + QLatin1Char(' ') + branch + QStringLiteral(" CI");
            config.insert(QStringLiteral("branchSource"), QStringLiteral("fixed"));
            config.insert(QStringLiteral("branch"), branch);
        } else {
            return target;
        }
        if (!m_workflow.isEmpty())
            config.insert(QStringLiteral("workflow"), m_workflow);
        target.addType = QStringLiteral("github.run");
        target.addConfig = config;
        return target;
    }

    QList<ListCell> rowCells(const ListRowTarget &target, const QJsonObject &row) const override
    {
        const QString status = row.value(QStringLiteral("status")).toString();
        const QString conclusion = row.value(QStringLiteral("conclusion")).toString();
        const qint64 number = row.value(QStringLiteral("run_number")).toInteger();
        const QString workflow = row.value(QStringLiteral("name")).toString();
        const QString branch = row.value(QStringLiteral("head_branch")).toString();
        const QString actor = row.value(QStringLiteral("actor")).toObject().value(QStringLiteral("login")).toString();
        const QJsonValue started = row.value(QStringLiteral("run_started_at"));
        return {{number > 0 ? QStringLiteral("#%1").arg(number) : dashCell().text, target.sortKey, {}},
                workflow.isEmpty() ? dashCell() : ListCell{workflow, {}, {}},
                status.isEmpty() ? dashCell()
                                 : ListCell{statusText(status, conclusion), {}, statusRole(status, conclusion)},
                branch.isEmpty() ? dashCell() : ListCell{branch, {}, {}},
                actor.isEmpty() ? dashCell() : ListCell{actor, {}, {}},
                dateCell(started.isString() ? started : row.value(QStringLiteral("created_at"))),
                linksCell(target)};
    }

private:
    QString m_repository;
    QString m_workflow;
};

} // namespace

QWidget *createRunListView(ItemContext &context, QWidget *parent)
{
    auto *view = new RunListView(context, parent);
    view->follow(context.status());
    return view;
}

} // namespace mirante::modules::github
