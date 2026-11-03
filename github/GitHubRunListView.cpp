#include "GitHubRunListView.h"

#include "GitHubLists.h"
#include "GitHubRunListLayout.h"
#include "GitHubSettings.h"
#include "GitHubUrls.h"
#include "GitHubViewSupport.h"

#include <mirante/ItemContext.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

namespace mirante::modules::github {

namespace {

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

} // namespace

RunListModel::RunListModel(ItemContext &context, ListSpan span)
    : ListModel(context, QCoreApplication::translate("github", "No runs"),
                runListWebUrl(webBaseOf(context.moduleSettings(kGeneralScreen)), context.config(),
                              context.projectSetting(kProjectSettingKey), span),
                runListDescription(context.config(), context.projectSetting(kProjectSettingKey), span), span)
    // A list over several repositories reads each row's repository instead.
    , m_repository(span == ListSpan::Repositories
                       ? QString()
                       : repositoryOf(context.config(), context.projectSetting(kProjectSettingKey)))
    , m_workflow(span == ListSpan::Repositories ? QString() : configuredWorkflow(context.config()))
{
}

QList<RunListModel::Run> RunListModel::runs() const
{
    QList<Run> runs;
    for (const QString &key : keys())
        runs << m_runs.value(key);
    return runs;
}

void RunListModel::read(const QJsonArray &rows, const QUrl &webBase)
{
    m_runs.clear();
    for (int index = 0; index < rows.size(); ++index) {
        const QJsonObject row = rows.at(index).toObject();
        const qint64 id = row.value(QStringLiteral("id")).toInteger();
        const QString ofRow =
            row.value(QStringLiteral("repository")).toObject().value(QStringLiteral("full_name")).toString();
        const QString repository = ofRow.isEmpty() ? m_repository : ofRow;

        ListRow listRow;
        listRow.key = id > 0 ? QString::number(id) : QStringLiteral("row-%1").arg(index);
        listRow.data = row;
        QUrl built;
        if (id > 0 && !repository.isEmpty())
            built = webPageUrl(webBase, repository + QStringLiteral("/actions/runs/") + QString::number(id));
        const QUrl address = webAddress(row, built);
        listRow.address = isWebAddress(address) ? address : QUrl();

        // The same item that a pasted address of its branch or pull request
        // makes.
        AddTarget add;
        const qint64 number = pullRequestOfRun(row);
        const QString branch = row.value(QStringLiteral("head_branch")).toString();
        if (!repository.isEmpty() && (number > 0 || !branch.isEmpty())) {
            QJsonObject config{{QStringLiteral("repository"), repository}};
            if (number > 0) {
                add.name = repository + QLatin1Char('#') + QString::number(number) + QStringLiteral(" CI");
                config.insert(QStringLiteral("branchSource"), QStringLiteral("pullRequest"));
                config.insert(QStringLiteral("number"), number);
            } else {
                add.name = repository + QLatin1Char(' ') + branch + QStringLiteral(" CI");
                config.insert(QStringLiteral("branchSource"), QStringLiteral("fixed"));
                config.insert(QStringLiteral("branch"), branch);
            }
            if (!m_workflow.isEmpty())
                config.insert(QStringLiteral("workflow"), m_workflow);
            add.type = QStringLiteral("github.run");
            add.config = config;
        }
        addRow(listRow, add);

        Run run;
        static_cast<ListRow &>(run) = this->row(listRow.key);
        run.id = id;
        run.repository = repository;
        run.number = row.value(QStringLiteral("run_number")).toInteger();
        run.workflow = row.value(QStringLiteral("name")).toString();
        const QString status = row.value(QStringLiteral("status")).toString();
        if (!status.isEmpty())
            run.state = runPillOf(row);
        run.branch = branch;
        run.actor = row.value(QStringLiteral("actor")).toObject().value(QStringLiteral("login")).toString();
        const QJsonValue started = row.value(QStringLiteral("run_started_at"));
        run.started = QDateTime::fromString(
            (started.isString() ? started : row.value(QStringLiteral("created_at"))).toString(), Qt::ISODateWithMs);
        m_runs.insert(run.key, run);
    }
}

namespace {

QWidget *createView(ItemContext &context, QWidget *parent, ListSpan span)
{
    auto *model = new RunListModel(context, span);
    model->follow(context.status());
    QWidget *view = makeRunListLayout(*model, parent);
    model->attach(view);
    return view;
}

} // namespace

QWidget *createRunListView(ItemContext &context, QWidget *parent)
{
    return createView(context, parent, ListSpan::Scope);
}

QWidget *createRunMultiListView(ItemContext &context, QWidget *parent)
{
    return createView(context, parent, ListSpan::Repositories);
}

} // namespace mirante::modules::github
