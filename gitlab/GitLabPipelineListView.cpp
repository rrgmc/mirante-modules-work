#include "GitLabPipelineListView.h"

#include "GitLabListSupport.h"
#include "GitLabSettings.h"
#include "GitLabUrls.h"
#include "GitLabViewSupport.h"

#include <mirante/ItemContext.h>

#include <QJsonObject>
#include <QRegularExpression>

using mirante::widgets::ListCell;

namespace mirante::modules::gitlab {

namespace {

enum Column { IdColumn, StatusColumn, RefColumn, AuthorColumn, StartedColumn, LinksColumn };

// The number in the ref of a pipeline that ran for a merge request, not on a
// branch, or 0.
qint64 mergeRequestOfRef(const QString &ref)
{
    static const QRegularExpression pattern(QStringLiteral("^refs/merge-requests/(\\d+)/head$"));
    return pattern.match(ref).captured(1).toLongLong();
}

class PipelineListView : public GitLabList
{
public:
    PipelineListView(ItemContext &context, QWidget *parent)
        : GitLabList(context, tr("No pipelines"),
                     pipelinesUrl(baseUrlOf(context.moduleSettings(kGeneralScreen)), context.config(),
                                  context.projectSetting(kProjectSettingKey)),
                     parent)
        , m_project(projectPath(context.config(), context.projectSetting(kProjectSettingKey)))
    {
        setColumns({{tr("Id"), 2},
                    {tr("Status"), 1},
                    {tr("Ref"), 0, true},
                    {tr("Author"), 4},
                    {tr("Started"), 3},
                    {tr("Links"), 0}},
                   IdColumn);
        addFilter(StatusColumn, tr("Status"));
    }

protected:
    ListRowTarget rowTarget(const QJsonObject &row, int index, const QUrl &baseUrl) const override
    {
        const qint64 id = row.value(QStringLiteral("id")).toInteger();
        ListRowTarget target;
        target.key = id > 0 ? QString::number(id) : QStringLiteral("row-%1").arg(index);
        target.sortKey = id;

        QUrl built;
        if (id > 0) {
            built = pipelinesUrl(baseUrl, QJsonObject{{QStringLiteral("project"), m_project}}, {});
            if (!built.isEmpty())
                built.setPath(built.path() + QLatin1Char('/') + QString::number(id));
        }
        const QUrl address = webAddress(row, built);
        target.address = isWebAddress(address) ? address : QUrl();

        const QString ref = row.value(QStringLiteral("ref")).toString();
        if (ref.isEmpty() || m_project.isEmpty())
            return target;
        // The same item that a pasted address of its branch or merge request makes.
        target.addType = QStringLiteral("gitlab.pipeline");
        target.addConfig.insert(QStringLiteral("project"), m_project);
        const qint64 iid = mergeRequestOfRef(ref);
        if (iid > 0) {
            target.addName = m_project + QLatin1Char('!') + QString::number(iid) + QStringLiteral(" CI");
            target.addConfig.insert(QStringLiteral("branchSource"), QStringLiteral("mergeRequest"));
            target.addConfig.insert(QStringLiteral("iid"), iid);
        } else {
            target.addName = m_project + QLatin1Char(' ') + ref + QStringLiteral(" CI");
            target.addConfig.insert(QStringLiteral("branchSource"), QStringLiteral("fixed"));
            target.addConfig.insert(QStringLiteral("branch"), ref);
        }
        return target;
    }

    QList<ListCell> rowCells(const ListRowTarget &target, const QJsonObject &row) const override
    {
        const QString status = row.value(QStringLiteral("status")).toString();
        const QString author = row.value(QStringLiteral("user")).toObject().value(QStringLiteral("name")).toString();
        return {{row.value(QStringLiteral("id")).toVariant().toString(), target.sortKey, {}},
                status.isEmpty() ? dashCell() : ListCell{statusText(status), {}, statusRole(status)},
                {row.value(QStringLiteral("ref")).toString(), {}, {}},
                author.isEmpty() ? dashCell() : ListCell{author, {}, {}},
                dateCell(row.value(QStringLiteral("started_at"))),
                linksCell(target)};
    }

private:
    QString m_project;
};

} // namespace

QWidget *createPipelineListView(ItemContext &context, QWidget *parent)
{
    auto *view = new PipelineListView(context, parent);
    view->follow(context.status());
    return view;
}

} // namespace mirante::modules::gitlab
