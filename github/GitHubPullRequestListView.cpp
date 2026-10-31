#include "GitHubPullRequestListView.h"

#include "GitHubClient.h"
#include "GitHubListSupport.h"
#include "GitHubStatus.h"
#include "GitHubViewSupport.h"

#include <mirante/ItemContext.h>

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QScopedValueRollback>
#include <QSet>
#include <QStringList>

#include <memory>

using mirante::widgets::ListCell;
using mirante::widgets::StatusRole;

namespace mirante::modules::github {

namespace {

enum Column { RefColumn, TitleColumn, AuthorColumn, ChecksColumn, ReviewsColumn, UpdatedColumn, LinksColumn };

// The rows of the page that get the further requests. See decisions 0067 and 0071.
constexpr int kAskedRows = 25;

ListTraits pullRequestTraits()
{
    return {ListKind::PullRequests, QStringLiteral("github.pr.summary"),
            QCoreApplication::translate("github", "No pull requests")};
}

// A pill, short because the column says what it is about. No checks is a dash.
ListCell checksCell(ChecksState state)
{
    switch (state) {
    case ChecksState::Passed:
        return {QCoreApplication::translate("github", "Passed"), {}, StatusRole::Good};
    case ChecksState::Failed:
        return {QCoreApplication::translate("github", "Failed"), {}, StatusRole::Bad};
    case ChecksState::Running:
        return {QCoreApplication::translate("github", "Running"), {}, StatusRole::Waiting};
    case ChecksState::None:
        break;
    }
    return dashCell();
}

// Such as "2 approved" or "changes requested", from each reviewer's latest
// review, as in the status's summary. Sorted by the approvals, with changes
// requested below none, and a row without an answer last.
ListCell reviewsCell(const QJsonArray &reviews)
{
    const ReviewState state = reviewState(reviews);
    if (state.changesRequested)
        return {QCoreApplication::translate("github", "changes requested"), -1, {}};
    if (state.approvals > 0)
        return {QCoreApplication::translate("github", "%n approved", nullptr, state.approvals), state.approvals, {}};
    ListCell cell = dashCell();
    cell.sortKey = 0;
    return cell;
}

ListCell noReviewsCell()
{
    ListCell cell = dashCell();
    cell.sortKey = -2;
    return cell;
}

// The two answers about a head commit's checks, which come in any order.
struct ChecksAnswers
{
    int waiting = 2;
    bool anyOk = false;
    QJsonArray checkRuns;
    QJsonObject combinedStatus;
};

class PullRequestListView : public NumberedList
{
public:
    PullRequestListView(ItemContext &context, QWidget *parent)
        : NumberedList(context, pullRequestTraits(), parent)
        , m_showChecks(context.config().value(QStringLiteral("showChecks")).toBool(true))
        , m_showReviews(context.config().value(QStringLiteral("showReviews")).toBool(true))
    {
        setColumns({{QCoreApplication::translate("github", "Ref"), 1},
                    {QCoreApplication::translate("github", "Title"), 0, true},
                    {QCoreApplication::translate("github", "Author"), 5},
                    {QCoreApplication::translate("github", "Checks"), 2},
                    {QCoreApplication::translate("github", "Reviews"), 4},
                    {QCoreApplication::translate("github", "Updated"), 3},
                    {QCoreApplication::translate("github", "Links"), 0}},
                   UpdatedColumn);
        setColumnVisible(ChecksColumn, m_showChecks);
        setColumnVisible(ReviewsColumn, m_showReviews);
    }

protected:
    QList<ListCell> rowCells(const ListRowTarget &target, const QJsonObject &row) const override
    {
        const QString author = row.value(QStringLiteral("user")).toObject().value(QStringLiteral("login")).toString();
        return {refCell(target),
                {row.value(QStringLiteral("title")).toString(), {}, {}},
                author.isEmpty() ? dashCell() : ListCell{author, {}, {}},
                m_checks.value(target.key, dashCell()),
                m_reviews.value(target.key, noReviewsCell()),
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
        if (!server || (!m_showChecks && !m_showReviews))
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
        const GitHubClient client(server, round);
        if (m_showChecks) {
            client.pullRequest(row.repository, row.number, [this, key, client, repository = row.repository](
                                                                 const ApiReply &reply) {
                const QString sha = reply.json.object()
                                        .value(QStringLiteral("head"))
                                        .toObject()
                                        .value(QStringLiteral("sha"))
                                        .toString();
                if (!reply.ok() || sha.isEmpty()) {
                    answerChecks(key, dashCell());
                    return;
                }
                askChecks(client, key, repository, sha);
            });
        }
        if (m_showReviews) {
            client.pullRequestReviews(row.repository, row.number, [this, key](const ApiReply &reply) {
                const ListCell cell = reply.ok() ? reviewsCell(reply.json.array()) : noReviewsCell();
                m_reviews.insert(key, cell);
                setAnswer(key, ReviewsColumn, cell);
            });
        }
    }

    // The check runs and the combined status of the head commit, at once. A
    // failed one leaves its part out, and both failed is a dash.
    void askChecks(const GitHubClient &client, const QString &key, const QString &repository, const QString &sha)
    {
        auto answers = std::make_shared<ChecksAnswers>();
        const auto finish = [this, key, answers] {
            if (--answers->waiting > 0)
                return;
            answerChecks(key, answers->anyOk ? checksCell(checksState(answers->checkRuns, answers->combinedStatus))
                                             : dashCell());
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

    void answerChecks(const QString &key, const ListCell &cell)
    {
        m_checks.insert(key, cell);
        setAnswer(key, ChecksColumn, cell);
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
        forget(m_checks, ChecksColumn, dashCell());
        forget(m_reviews, ReviewsColumn, noReviewsCell());
    }

    bool m_showChecks;
    bool m_showReviews;
    // The requests of the rows asked for since the latest status, each row with a
    // receiver of its own, so they can be cancelled row by row. A row stays here
    // after its answers come.
    QHash<QString, QObject *> m_rounds;
    // The latest answers, by row.
    QHash<QString, ListCell> m_checks;
    QHash<QString, ListCell> m_reviews;
    // A cell is changing because of an answer.
    bool m_answering = false;
};

} // namespace

QWidget *createPullRequestListView(ItemContext &context, QWidget *parent)
{
    auto *view = new PullRequestListView(context, parent);
    view->follow(context.status());
    return view;
}

} // namespace mirante::modules::github
