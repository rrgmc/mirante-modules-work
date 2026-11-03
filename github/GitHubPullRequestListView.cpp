#include "GitHubPullRequestListView.h"

#include "GitHubClient.h"
#include "GitHubListSupport.h"
#include "GitHubLists.h"
#include "GitHubPullRequestListLayout.h"
#include "GitHubSettings.h"
#include "GitHubStatus.h"
#include "GitHubViewSupport.h"

#include <mirante/ItemContext.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QScopedValueRollback>
#include <QSet>

#include <memory>

namespace mirante::modules::github {

namespace {

// A pill, short because the column says what it is about. No checks is empty.
Pill shortChecksPill(ChecksState state)
{
    switch (state) {
    case ChecksState::Passed:
        return {QCoreApplication::translate("github", "Passed"), widgets::StatusRole::Good};
    case ChecksState::Failed:
        return {QCoreApplication::translate("github", "Failed"), widgets::StatusRole::Bad};
    case ChecksState::Running:
        return {QCoreApplication::translate("github", "Running"), widgets::StatusRole::Waiting};
    case ChecksState::None:
        break;
    }
    return {};
}

// The two answers about a head commit's checks, which come in any order.
struct ChecksAnswers
{
    int waiting = 2;
    bool anyOk = false;
    QJsonArray checkRuns;
    QJsonObject combinedStatus;
};

} // namespace

PullRequestListModel::PullRequestListModel(ItemContext &context, ListSpan span)
    : ListModel(context, QCoreApplication::translate("github", "No pull requests"),
                listWebUrl(ListKind::PullRequests, webBaseOf(context.moduleSettings(kGeneralScreen)), context.config(),
                           context.projectSetting(kProjectSettingKey), span),
                listDescription(ListKind::PullRequests, context.config(), context.projectSetting(kProjectSettingKey),
                                span),
                span)
{
}

PullRequestListModel::~PullRequestListModel() = default;

QList<PullRequestListModel::PullRequest> PullRequestListModel::pullRequests() const
{
    QList<PullRequest> pullRequests;
    for (const QString &key : keys())
        pullRequests << pullRequest(key);
    return pullRequests;
}

PullRequestListModel::PullRequest PullRequestListModel::pullRequest(const QString &key) const
{
    PullRequest pullRequest = m_pullRequests.value(key);
    pullRequest.checks = m_checks.value(key);
    pullRequest.reviews = m_reviews.value(key);
    return pullRequest;
}

void PullRequestListModel::setVisibleRows(const QStringList &keys)
{
    m_visible = keys;
    // An answer can move its row when the table sorts on that column. That does
    // not ask again until the next change, or rows would keep trading places
    // with those they push out of the first 25.
    if (!m_answering && isViewVisible())
        askForNewKeys();
}

void PullRequestListModel::read(const QJsonArray &rows, const QUrl &webBase)
{
    m_pullRequests.clear();
    for (int index = 0; index < rows.size(); ++index) {
        const QJsonObject row = rows.at(index).toObject();
        const NumberedRow read =
            readNumberedRow(row, index, webBase, ListKind::PullRequests, QStringLiteral("github.pr.summary"));
        addRow(read.row, read.add);

        PullRequest pullRequest;
        static_cast<ListRow &>(pullRequest) = this->row(read.row.key);
        pullRequest.repository = read.repository;
        pullRequest.number = read.number;
        pullRequest.ref = read.ref;
        pullRequest.refSortKey = read.refSortKey;
        pullRequest.title = row.value(QStringLiteral("title")).toString();
        pullRequest.author = row.value(QStringLiteral("user")).toObject().value(QStringLiteral("login")).toString();
        pullRequest.updated =
            QDateTime::fromString(row.value(QStringLiteral("updated_at")).toString(), Qt::ISODateWithMs);
        m_pullRequests.insert(pullRequest.key, pullRequest);
    }
    // A new status asks again for every row, once the layout says which show.
    stopAsking();
}

void PullRequestListModel::viewVisibilityChanged(bool visible)
{
    stopAsking();
    if (visible)
        askForNewKeys();
}

void PullRequestListModel::askForNewKeys()
{
    const QStringList asked = m_visible.mid(0, AskedRows);
    forgetOthers(asked);

    ApiClient *server = context().server();
    if (!server || (!isColumnShown(QStringLiteral("checks")) && !isColumnShown(QStringLiteral("reviews"))))
        return;
    for (const QString &key : asked) {
        if (!m_rounds.contains(key))
            ask(*server, key);
    }
}

void PullRequestListModel::stopAsking()
{
    // The answers to their requests are dropped with them.
    qDeleteAll(m_rounds);
    m_rounds.clear();
}

void PullRequestListModel::ask(ApiClient &server, const QString &key)
{
    const PullRequest row = m_pullRequests.value(key);
    if (row.repository.isEmpty() || row.number <= 0)
        return;
    auto *round = new QObject(this);
    m_rounds.insert(key, round);
    const GitHubClient client(server, round);
    if (isColumnShown(QStringLiteral("checks"))) {
        client.pullRequest(row.repository, row.number,
                           [this, key, client, repository = row.repository](const ApiReply &reply) {
            const QString sha =
                reply.json.object().value(QStringLiteral("head")).toObject().value(QStringLiteral("sha")).toString();
            if (!reply.ok() || sha.isEmpty()) {
                m_checks.insert(key, {});
                answer(key);
                return;
            }
            askChecks(client, key, repository, sha);
        });
    }
    if (isColumnShown(QStringLiteral("reviews"))) {
        client.pullRequestReviews(row.repository, row.number, [this, key](const ApiReply &reply) {
            Reviews reviews;
            if (reply.ok()) {
                const ReviewState state = reviewState(reply.json.array());
                reviews = {true, state.approvals, state.changesRequested};
            }
            m_reviews.insert(key, reviews);
            answer(key);
        });
    }
}

// The check runs and the combined status of the head commit, at once. A failed
// one leaves its part out, and both failed is no checks.
void PullRequestListModel::askChecks(const GitHubClient &client, const QString &key, const QString &repository,
                                     const QString &sha)
{
    auto answers = std::make_shared<ChecksAnswers>();
    const auto finish = [this, key, answers] {
        if (--answers->waiting > 0)
            return;
        m_checks.insert(key, answers->anyOk ? shortChecksPill(checksState(answers->checkRuns, answers->combinedStatus))
                                            : Pill());
        answer(key);
    };
    client.checkRuns(repository, sha, [answers, finish](const ApiReply &reply) {
        if (reply.ok()) {
            answers->anyOk = true;
            answers->checkRuns = reply.json.object().value(QStringLiteral("check_runs")).toArray();
        }
        finish();
    });
    client.combinedStatus(repository, sha, [answers, finish](const ApiReply &reply) {
        if (reply.ok()) {
            answers->anyOk = true;
            answers->combinedStatus = reply.json.object();
        }
        finish();
    });
}

void PullRequestListModel::answer(const QString &key)
{
    const QScopedValueRollback answering(m_answering, true);
    emit rowChanged(key);
}

// Rows that are not asked for drop what is on the way and their answers. The
// others keep their last answer until the new one comes, so the cells do not
// flicker at each poll.
void PullRequestListModel::forgetOthers(const QStringList &asked)
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
    for (auto it = m_checks.begin(); it != m_checks.end();) {
        if (kept.contains(it.key())) {
            ++it;
        } else {
            forgotten.insert(it.key());
            it = m_checks.erase(it);
        }
    }
    for (auto it = m_reviews.begin(); it != m_reviews.end();) {
        if (kept.contains(it.key())) {
            ++it;
        } else {
            forgotten.insert(it.key());
            it = m_reviews.erase(it);
        }
    }
    for (const QString &key : std::as_const(forgotten))
        answer(key);
}

namespace {

QWidget *createView(ItemContext &context, QWidget *parent, ListSpan span)
{
    auto *model = new PullRequestListModel(context, span);
    model->follow(context.status());
    QWidget *view = makePullRequestListLayout(*model, parent);
    model->attach(view);
    return view;
}

} // namespace

QWidget *createPullRequestListView(ItemContext &context, QWidget *parent)
{
    return createView(context, parent, ListSpan::Scope);
}

QWidget *createPullRequestMultiListView(ItemContext &context, QWidget *parent)
{
    return createView(context, parent, ListSpan::Repositories);
}

} // namespace mirante::modules::github
