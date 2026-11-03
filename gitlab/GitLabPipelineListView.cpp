#include "GitLabPipelineListView.h"

#include "GitLabLists.h"
#include "GitLabPipelineListLayout.h"
#include "GitLabSettings.h"
#include "GitLabUrls.h"
#include "GitLabViewSupport.h"

#include <mirante/ItemContext.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

namespace mirante::modules::gitlab {

namespace {

// The number in the ref of a pipeline that ran for a merge request, not on a
// branch, or 0.
qint64 mergeRequestOfRef(const QString &ref)
{
    static const QRegularExpression pattern(QStringLiteral("^refs/merge-requests/(\\d+)/(head|merge|train)$"));
    return pattern.match(ref).captured(1).toLongLong();
}

} // namespace

PipelineListModel::PipelineListModel(ItemContext &context)
    : ListModel(context, QCoreApplication::translate("gitlab", "No pipelines"),
                pipelinesUrl(baseUrlOf(context.moduleSettings(kGeneralScreen)), context.config(),
                             context.projectSetting(kProjectSettingKey)),
                pipelineListDescription(context.config(), context.projectSetting(kProjectSettingKey)))
    , m_project(projectPath(context.config(), context.projectSetting(kProjectSettingKey)))
{
}

QList<PipelineListModel::Pipeline> PipelineListModel::pipelines() const
{
    QList<Pipeline> pipelines;
    for (const QString &key : keys())
        pipelines << m_pipelines.value(key);
    return pipelines;
}

void PipelineListModel::read(const QJsonArray &rows, const QUrl &baseUrl)
{
    m_pipelines.clear();
    for (int index = 0; index < rows.size(); ++index) {
        const QJsonObject row = rows.at(index).toObject();
        const qint64 id = row.value(QStringLiteral("id")).toInteger();

        ListRow listRow;
        listRow.key = id > 0 ? QString::number(id) : QStringLiteral("row-%1").arg(index);
        listRow.data = row;
        QUrl built;
        if (id > 0) {
            built = pipelinesUrl(baseUrl, QJsonObject{{QStringLiteral("project"), m_project}}, {});
            if (!built.isEmpty())
                built.setPath(built.path() + QLatin1Char('/') + QString::number(id));
        }
        const QUrl address = webAddress(row, built);
        listRow.address = isWebAddress(address) ? address : QUrl();

        // The same item that a pasted address of its branch or merge request
        // makes.
        AddTarget add;
        const QString ref = row.value(QStringLiteral("ref")).toString();
        if (!ref.isEmpty() && !m_project.isEmpty()) {
            add.type = QStringLiteral("gitlab.pipeline");
            add.config.insert(QStringLiteral("project"), m_project);
            const qint64 iid = mergeRequestOfRef(ref);
            if (iid > 0) {
                add.name = m_project + QLatin1Char('!') + QString::number(iid) + QStringLiteral(" CI");
                add.config.insert(QStringLiteral("branchSource"), QStringLiteral("mergeRequest"));
                add.config.insert(QStringLiteral("iid"), iid);
            } else {
                add.name = m_project + QLatin1Char(' ') + ref + QStringLiteral(" CI");
                add.config.insert(QStringLiteral("branchSource"), QStringLiteral("fixed"));
                add.config.insert(QStringLiteral("branch"), ref);
            }
        }
        addRow(listRow, add);

        Pipeline pipeline;
        static_cast<ListRow &>(pipeline) = this->row(listRow.key);
        pipeline.id = id;
        const QString status = row.value(QStringLiteral("status")).toString();
        if (!status.isEmpty())
            pipeline.state = statusPillOf(status);
        pipeline.ref = ref;
        pipeline.author = row.value(QStringLiteral("user")).toObject().value(QStringLiteral("name")).toString();
        // GitLab's list answers usually have only the time it was created.
        const QJsonValue started = row.value(QStringLiteral("started_at"));
        pipeline.started = QDateTime::fromString(
            (started.isString() ? started : row.value(QStringLiteral("created_at"))).toString(), Qt::ISODateWithMs);
        m_pipelines.insert(pipeline.key, pipeline);
    }
}

QWidget *createPipelineListView(ItemContext &context, QWidget *parent)
{
    auto *model = new PipelineListModel(context);
    model->follow(context.status());
    QWidget *view = makePipelineListLayout(*model, parent);
    model->attach(view);
    return view;
}

} // namespace mirante::modules::gitlab
