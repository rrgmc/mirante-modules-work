#include "GitLabMergeRequestListView.h"

#include "GitLabClient.h"
#include "GitLabListSupport.h"
#include "GitLabLists.h"
#include "GitLabMergeRequestListLayout.h"
#include "GitLabSettings.h"
#include "GitLabViewSupport.h"

#include <mirante/ItemContext.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QScopedValueRollback>
#include <QSet>

namespace mirante::modules::gitlab {

namespace {

Pill pipelineOf(const QJsonObject &mergeRequest)
{
    const QString status =
        mergeRequest.value(QStringLiteral("head_pipeline")).toObject().value(QStringLiteral("status")).toString();
    return status.isEmpty() ? Pill() : statusPillOf(status);
}

} // namespace

MergeRequestListModel::MergeRequestListModel(ItemContext &context)
    : ListModel(context, QCoreApplication::translate("gitlab", "No merge requests"),
                listWebUrl(ListKind::MergeRequests, baseUrlOf(context.moduleSettings(kGeneralScreen)),
                           context.config(), context.projectSetting(kProjectSettingKey)),
                listDescription(ListKind::MergeRequests, context.config(), context.projectSetting(kProjectSettingKey)))
{
}

MergeRequestListModel::~MergeRequestListModel() = default;

QList<MergeRequestListModel::MergeRequest> MergeRequestListModel::mergeRequests() const
{
    QList<MergeRequest> mergeRequests;
    for (const QString &key : keys())
        mergeRequests << mergeRequest(key);
    return mergeRequests;
}

MergeRequestListModel::MergeRequest MergeRequestListModel::mergeRequest(const QString &key) const
{
    MergeRequest mergeRequest = m_mergeRequests.value(key);
    mergeRequest.pipeline = m_pipelines.value(key);
    mergeRequest.approvals = m_approvals.value(key);
    return mergeRequest;
}

void MergeRequestListModel::setVisibleRows(const QStringList &keys)
{
    m_visible = keys;
    // An answer can move its row when the table sorts on that column. That does
    // not ask again until the next change, or rows would keep trading places
    // with those they push out of the first 25.
    if (!m_answering && isViewVisible())
        askForNewKeys();
}

void MergeRequestListModel::read(const QJsonArray &rows, const QUrl &baseUrl)
{
    m_mergeRequests.clear();
    for (int index = 0; index < rows.size(); ++index) {
        const QJsonObject row = rows.at(index).toObject();
        const NumberedRow read = readNumberedRow(row, index, baseUrl, ListKind::MergeRequests, QLatin1Char('!'),
                                                 QStringLiteral("gitlab.mr.summary"));
        addRow(read.row, read.add);

        MergeRequest mergeRequest;
        static_cast<ListRow &>(mergeRequest) = this->row(read.row.key);
        mergeRequest.project = read.project;
        mergeRequest.iid = read.iid;
        mergeRequest.ref = read.ref;
        mergeRequest.refSortKey = read.refSortKey;
        mergeRequest.title = row.value(QStringLiteral("title")).toString();
        mergeRequest.author = row.value(QStringLiteral("author")).toObject().value(QStringLiteral("name")).toString();
        mergeRequest.updated =
            QDateTime::fromString(row.value(QStringLiteral("updated_at")).toString(), Qt::ISODateWithMs);
        m_mergeRequests.insert(mergeRequest.key, mergeRequest);
    }
    // A new status asks again for every row, once the layout says which show.
    stopAsking();
}

void MergeRequestListModel::viewVisibilityChanged(bool visible)
{
    stopAsking();
    if (visible)
        askForNewKeys();
}

void MergeRequestListModel::askForNewKeys()
{
    const QStringList asked = m_visible.mid(0, AskedRows);
    forgetOthers(asked);

    ApiClient *server = context().server();
    if (!server || (!isColumnShown(QStringLiteral("pipeline")) && !isColumnShown(QStringLiteral("approvals"))))
        return;
    for (const QString &key : asked) {
        if (!m_rounds.contains(key))
            ask(*server, key);
    }
}

void MergeRequestListModel::stopAsking()
{
    // The answers to their requests are dropped with them.
    qDeleteAll(m_rounds);
    m_rounds.clear();
}

void MergeRequestListModel::ask(ApiClient &server, const QString &key)
{
    const MergeRequest row = m_mergeRequests.value(key);
    if (row.project.isEmpty() || row.iid <= 0)
        return;
    auto *round = new QObject(this);
    m_rounds.insert(key, round);
    const GitLabClient client(server, round);
    if (isColumnShown(QStringLiteral("pipeline"))) {
        client.mergeRequest(row.project, row.iid, [this, key](const ApiReply &reply) {
            m_pipelines.insert(key, reply.ok() ? pipelineOf(reply.json.object()) : Pill());
            answer(key);
        });
    }
    if (isColumnShown(QStringLiteral("approvals"))) {
        client.approvals(row.project, row.iid, [this, key](const ApiReply &reply) {
            Approvals approvals;
            if (reply.ok()) {
                const QJsonObject answer = reply.json.object();
                approvals = {true, int(answer.value(QStringLiteral("approved_by")).toArray().size()),
                             answer.value(QStringLiteral("approvals_required")).toInt()};
            }
            m_approvals.insert(key, approvals);
            this->answer(key);
        });
    }
}

void MergeRequestListModel::answer(const QString &key)
{
    const QScopedValueRollback answering(m_answering, true);
    emit rowChanged(key);
}

// Rows that are not asked for drop what is on the way and their answers. The
// others keep their last answer until the new one comes, so the cells do not
// flicker at each poll.
void MergeRequestListModel::forgetOthers(const QStringList &asked)
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
    QSet<QString> forgotten;
    for (auto it = m_pipelines.begin(); it != m_pipelines.end();) {
        if (kept.contains(it.key())) {
            ++it;
        } else {
            forgotten.insert(it.key());
            it = m_pipelines.erase(it);
        }
    }
    for (auto it = m_approvals.begin(); it != m_approvals.end();) {
        if (kept.contains(it.key())) {
            ++it;
        } else {
            forgotten.insert(it.key());
            it = m_approvals.erase(it);
        }
    }
    for (const QString &key : std::as_const(forgotten))
        answer(key);
}

QWidget *createMergeRequestListView(ItemContext &context, QWidget *parent)
{
    auto *model = new MergeRequestListModel(context);
    model->follow(context.status());
    QWidget *view = makeMergeRequestListLayout(*model, parent);
    model->attach(view);
    return view;
}

} // namespace mirante::modules::gitlab
