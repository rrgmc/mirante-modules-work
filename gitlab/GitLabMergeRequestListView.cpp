#include "GitLabMergeRequestListView.h"

#include "GitLabClient.h"
#include "GitLabListSupport.h"
#include "GitLabViewSupport.h"

#include <mirante/ItemContext.h>

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QScopedValueRollback>
#include <QSet>
#include <QStringList>

using mirante::widgets::ListCell;

namespace mirante::modules::gitlab {

namespace {

enum Column { RefColumn, TitleColumn, AuthorColumn, PipelineColumn, ApprovalsColumn, UpdatedColumn, LinksColumn };

// The rows of the page that get the further requests. See decisions 0067 and 0071.
constexpr int kAskedRows = 25;

ListTraits mergeRequestTraits()
{
    return {ListKind::MergeRequests,
            QLatin1Char('!'),
            QStringLiteral("gitlab.mr.summary"),
            QCoreApplication::translate("gitlab", "No merge requests")};
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

class MergeRequestListView : public NumberedList
{
public:
    MergeRequestListView(ItemContext &context, QWidget *parent)
        : NumberedList(context, mergeRequestTraits(), parent)
        , m_showPipeline(context.config().value(QStringLiteral("showPipeline")).toBool(true))
        , m_showApprovals(context.config().value(QStringLiteral("showApprovals")).toBool(true))
    {
        setColumns({{QCoreApplication::translate("gitlab", "Ref"), 1},
                    {QCoreApplication::translate("gitlab", "Title"), 0, true},
                    {QCoreApplication::translate("gitlab", "Author"), 5},
                    {QCoreApplication::translate("gitlab", "Pipeline"), 2},
                    {QCoreApplication::translate("gitlab", "Approvals"), 4},
                    {QCoreApplication::translate("gitlab", "Updated"), 3},
                    {QCoreApplication::translate("gitlab", "Links"), 0}},
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
                dateCell(row.value(QStringLiteral("updated_at"))),
                linksCell(target)};
    }

    // A new status asks again for every row.
    void askForCells() override
    {
        stopAsking();
        askForNewKeys();
    }

    // A row that stays keeps what is on the way, or the answer it has. An answer
    // can move its row when the table sorts on that column. That does not ask again
    // until the next change, or rows would keep trading places with those they push
    // out of the first 25.
    void askForNewKeys() override
    {
        if (m_answering)
            return;
        forgetOthers(askedKeys());

        ApiClient *server = context().server();
        if (!server || (!m_showPipeline && !m_showApprovals))
            return;
        for (const QString &key : askedKeys()) {
            if (!m_rounds.contains(key))
                ask(*server, key);
        }
    }

    void stopAsking() override
    {
        // The answers to their requests are dropped with them.
        qDeleteAll(m_rounds);
        m_rounds.clear();
    }

private:
    QStringList askedKeys() const { return visibleKeys().mid(0, kAskedRows); }

    void ask(ApiClient &server, const QString &key)
    {
        const ListRowTarget row = target(key);
        if (!row.complete())
            return;
        auto *round = new QObject(this);
        m_rounds.insert(key, round);
        const GitLabClient client(server, round);
        if (m_showPipeline) {
            client.mergeRequest(row.project, row.iid, [this, key](const ApiReply &reply) {
                const ListCell cell = reply.ok() ? pipelineCell(reply.json.object()) : dashCell();
                m_pipelines.insert(key, cell);
                setAnswer(key, PipelineColumn, cell);
            });
        }
        if (m_showApprovals) {
            client.approvals(row.project, row.iid, [this, key](const ApiReply &reply) {
                const ListCell cell = reply.ok() ? approvalsCell(reply.json.object()) : noApprovalsCell();
                m_approvals.insert(key, cell);
                setAnswer(key, ApprovalsColumn, cell);
            });
        }
    }

    void setAnswer(const QString &key, int column, const ListCell &cell)
    {
        const QScopedValueRollback answering(m_answering, true);
        setCell(key, column, cell);
    }

    // Rows that are not asked for drop what is on the way and go back to a dash.
    // The others keep their last answer until the new one comes, so the cells do
    // not flicker at each poll.
    void forgetOthers(const QStringList &asked)
    {
        const QSet<QString> kept(asked.cbegin(), asked.cend());
        for (auto it = m_rounds.begin(); it != m_rounds.end();) {
            if (kept.contains(it.key())) {
                ++it;
            } else {
                delete it.value();
                it = m_rounds.erase(it);
            }
        }
        const auto forget = [this, &kept](QHash<QString, ListCell> &answers, int column, const ListCell &empty) {
            for (auto it = answers.begin(); it != answers.end();) {
                if (kept.contains(it.key())) {
                    ++it;
                } else {
                    setAnswer(it.key(), column, empty);
                    it = answers.erase(it);
                }
            }
        };
        forget(m_pipelines, PipelineColumn, dashCell());
        forget(m_approvals, ApprovalsColumn, noApprovalsCell());
    }

    bool m_showPipeline;
    bool m_showApprovals;
    // The requests of the rows asked for since the latest status, each row with a
    // receiver of its own, so they can be cancelled row by row. A row stays here
    // after its answers come.
    QHash<QString, QObject *> m_rounds;
    // The latest answers, by row.
    QHash<QString, ListCell> m_pipelines;
    QHash<QString, ListCell> m_approvals;
    // A cell is changing because of an answer.
    bool m_answering = false;
};

} // namespace

QWidget *createMergeRequestListView(ItemContext &context, QWidget *parent)
{
    auto *view = new MergeRequestListView(context, parent);
    view->follow(context.status());
    return view;
}

} // namespace mirante::modules::gitlab
