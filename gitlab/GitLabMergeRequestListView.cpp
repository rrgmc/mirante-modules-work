#include "GitLabMergeRequestListView.h"

#include "GitLabClient.h"
#include "GitLabListSupport.h"
#include "GitLabMergeRequestView.h"
#include "GitLabStatus.h"
#include "GitLabViewSupport.h"

#include <mirante/ItemContext.h>

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QStringList>

using mirante::widgets::ListCell;
using mirante::widgets::ListColumn;

namespace mirante::modules::gitlab {

namespace {

enum Column { RefColumn, TitleColumn, AuthorColumn, PipelineColumn, ApprovalsColumn, UpdatedColumn };

// The rows that get the further requests. See decision 0067.
constexpr int kAskedRows = 25;

ListTraits mergeRequestTraits()
{
    return {ListKind::MergeRequests,
            QLatin1Char('!'),
            QStringLiteral("gitlab.mr.summary"),
            QCoreApplication::translate("gitlab", "No merge requests"),
            &createMergeRequestPreview,
            &fetchMergeRequestSummaryStatus};
}

ListCell pipelineCell(const QJsonObject &mergeRequest)
{
    const QString status =
        mergeRequest.value(QStringLiteral("head_pipeline")).toObject().value(QStringLiteral("status")).toString();
    if (status.isEmpty())
        return dashCell();
    return {statusText(status), {}, statusRole(status)};
}

// Such as "1/2", or just the count when none are required, as in the status's
// summary. Sorted by the approvals given, and a row without an answer comes last.
ListCell approvalsCell(const QJsonObject &approvals)
{
    const int given = int(approvals.value(QStringLiteral("approved_by")).toArray().size());
    const int required = approvals.value(QStringLiteral("approvals_required")).toInt();
    const QString text = required > 0 ? QStringLiteral("%1/%2").arg(given).arg(required) : QString::number(given);
    return {text, given, {}};
}

ListCell noApprovalsCell()
{
    ListCell cell = dashCell();
    cell.sortKey = -1;
    return cell;
}

class MergeRequestListView : public GitLabList
{
public:
    MergeRequestListView(ItemContext &context, QWidget *parent)
        : GitLabList(context, mergeRequestTraits(), parent)
        , m_showPipeline(context.config().value(QStringLiteral("showPipeline")).toBool(true))
        , m_showApprovals(context.config().value(QStringLiteral("showApprovals")).toBool(true))
    {
        setColumns({{tr("Ref"), 1},
                    {tr("Title"), 0, true},
                    {tr("Author"), 5},
                    {tr("Pipeline"), 2},
                    {tr("Approvals"), 4},
                    {tr("Updated"), 3}},
                   UpdatedColumn);
        setColumnVisible(PipelineColumn, m_showPipeline);
        setColumnVisible(ApprovalsColumn, m_showApprovals);
    }

protected:
    QList<ListCell> rowCells(const ListRowTarget &target, const QJsonObject &row) const override
    {
        return {refCell(target),
                {row.value(QStringLiteral("title")).toString(), {}, {}},
                {row.value(QStringLiteral("author")).toObject().value(QStringLiteral("name")).toString(), {}, {}},
                m_pipelines.value(target.key, dashCell()),
                m_approvals.value(target.key, noApprovalsCell()),
                updatedCell(row)};
    }

    void askForCells() override
    {
        stopAsking();
        const QStringList asked = keysInTableOrder().mid(0, kAskedRows);
        forgetOthers(asked);

        ApiClient *server = context().server();
        if (!server || (!m_showPipeline && !m_showApprovals))
            return;
        m_round = new QObject(this);
        const GitLabClient client(*server, m_round);
        for (const QString &key : asked) {
            const ListRowTarget row = target(key);
            if (!row.complete())
                continue;
            if (m_showPipeline) {
                client.mergeRequest(row.project, row.iid, [this, key](const ApiReply &reply) {
                    const ListCell cell = reply.ok() ? pipelineCell(reply.json.object()) : dashCell();
                    m_pipelines.insert(key, cell);
                    setCell(key, PipelineColumn, cell);
                });
            }
            if (m_showApprovals) {
                client.approvals(row.project, row.iid, [this, key](const ApiReply &reply) {
                    const ListCell cell = reply.ok() ? approvalsCell(reply.json.object()) : noApprovalsCell();
                    m_approvals.insert(key, cell);
                    setCell(key, ApprovalsColumn, cell);
                });
            }
        }
    }

    void stopAsking() override
    {
        // The answers to its requests are dropped with it.
        delete m_round;
        m_round = nullptr;
    }

private:
    // Rows that are not asked for go back to a dash. The others keep their last
    // answer until the new one comes, so the cells do not flicker at each poll.
    void forgetOthers(const QStringList &asked)
    {
        const QSet<QString> kept(asked.cbegin(), asked.cend());
        const auto forget = [this, &kept](QHash<QString, ListCell> &answers, int column, const ListCell &empty) {
            for (auto it = answers.begin(); it != answers.end();) {
                if (kept.contains(it.key())) {
                    ++it;
                } else {
                    setCell(it.key(), column, empty);
                    it = answers.erase(it);
                }
            }
        };
        forget(m_pipelines, PipelineColumn, dashCell());
        forget(m_approvals, ApprovalsColumn, noApprovalsCell());
    }

    bool m_showPipeline;
    bool m_showApprovals;
    // The requests of the latest status. Null when none are running.
    QObject *m_round = nullptr;
    // The latest answers, by row.
    QHash<QString, ListCell> m_pipelines;
    QHash<QString, ListCell> m_approvals;
};

} // namespace

QWidget *createMergeRequestListView(ItemContext &context, QWidget *parent)
{
    auto *view = new MergeRequestListView(context, parent);
    view->follow(context.status());
    return view;
}

} // namespace mirante::modules::gitlab
