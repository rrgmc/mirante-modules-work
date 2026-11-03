#include "GitHubPullRequestView.h"

#include "GitHubPullRequestLayout.h"
#include "GitHubStatus.h"
#include "GitHubUrls.h"
#include "GitHubViewSupport.h"

#include <mirante/ItemContext.h>

#include <QSet>

namespace mirante::modules::github {

namespace {

// A check run that did not keep the checks from passing.
bool checkRunPassed(const QJsonObject &checkRun)
{
    const QString conclusion = checkRun.value(QStringLiteral("conclusion")).toString();
    return checkRun.value(QStringLiteral("status")).toString() == QLatin1String("completed")
        && (conclusion == QLatin1String("success") || conclusion == QLatin1String("skipped")
            || conclusion == QLatin1String("neutral"));
}

QString loginOf(const QJsonValue &user)
{
    return user.toObject().value(QStringLiteral("login")).toString();
}

QList<PullRequestModel::Check> readChecks(const QJsonArray &checkRuns, const QJsonObject &combinedStatus)
{
    QList<PullRequestModel::Check> checks;
    for (const QJsonValue &value : checkRuns) {
        const QJsonObject checkRun = value.toObject();
        QUrl details(checkRun.value(QStringLiteral("html_url")).toString());
        if (!isWebAddress(details))
            details = QUrl(checkRun.value(QStringLiteral("details_url")).toString());
        checks << PullRequestModel::Check{
            checkRun.value(QStringLiteral("name")).toString(), runPillOf(checkRun), checkRunPassed(checkRun),
            isWebAddress(details) ? details : QUrl(),
            QStringLiteral("check-details-%1").arg(checkRun.value(QStringLiteral("id")).toInteger())};
    }
    for (const QJsonValue &value : combinedStatus.value(QStringLiteral("statuses")).toArray()) {
        const QJsonObject commitStatus = value.toObject();
        const QUrl details(commitStatus.value(QStringLiteral("target_url")).toString());
        checks << PullRequestModel::Check{
            commitStatus.value(QStringLiteral("context")).toString(), commitStatusPillOf(commitStatus),
            commitStatus.value(QStringLiteral("state")).toString() == QLatin1String("success"),
            isWebAddress(details) ? details : QUrl(),
            QStringLiteral("status-details-%1").arg(commitStatus.value(QStringLiteral("id")).toInteger())};
    }
    return checks;
}

QList<PullRequestModel::Reviewer> readReviewers(const QJsonArray &reviews, const QJsonObject &pullRequest)
{
    QList<PullRequestModel::Reviewer> reviewers;
    QSet<QString> reviewed;
    for (const QJsonValue &value : latestReviews(reviews)) {
        const QJsonObject review = value.toObject();
        const QString login = loginOf(review.value(QStringLiteral("user")));
        reviewed.insert(login);
        reviewers << PullRequestModel::Reviewer{login, reviewPillOf(review.value(QStringLiteral("state")).toString()),
                                                false};
    }
    for (const QJsonValue &value : pullRequest.value(QStringLiteral("requested_reviewers")).toArray()) {
        const QString login = loginOf(value);
        if (!login.isEmpty() && !reviewed.contains(login))
            reviewers << PullRequestModel::Reviewer{login, reviewPillOf({}), false};
    }
    for (const QJsonValue &value : pullRequest.value(QStringLiteral("requested_teams")).toArray()) {
        const QString team = value.toObject().value(QStringLiteral("name")).toString();
        if (!team.isEmpty())
            reviewers << PullRequestModel::Reviewer{team, reviewPillOf({}), true};
    }
    return reviewers;
}

} // namespace

PullRequestModel::PullRequestModel(ItemContext &context)
    : SummaryModel(context)
{
}

Pill PullRequestModel::state() const
{
    return statePillOf(m_pullRequest);
}

bool PullRequestModel::isDraft() const
{
    return m_pullRequest.value(QStringLiteral("state")).toString() == QLatin1String("open")
        && m_pullRequest.value(QStringLiteral("draft")).toBool();
}

void PullRequestModel::read(const ItemStatus &status)
{
    m_pullRequest = status.data.value(QStringLiteral("pullRequest")).toObject();
    const QJsonArray checkRuns = status.data.value(QStringLiteral("checkRuns")).toArray();
    const QJsonObject combinedStatus = status.data.value(QStringLiteral("combinedStatus")).toObject();
    m_checks = checksPillOf(checksState(checkRuns, combinedStatus));
    m_checkList = readChecks(checkRuns, combinedStatus);
    m_reviewers = readReviewers(status.data.value(QStringLiteral("reviews")).toArray(), m_pullRequest);
    setPageAddress(webAddress(m_pullRequest, pullRequestUrl(webBase(), config(), defaultRepository())));
}

QWidget *createPullRequestView(ItemContext &context, QWidget *parent)
{
    auto *model = new PullRequestModel(context);
    model->follow(context.status());
    QWidget *view = makePullRequestLayout(*model, parent);
    model->attach(view);
    return view;
}

} // namespace mirante::modules::github
