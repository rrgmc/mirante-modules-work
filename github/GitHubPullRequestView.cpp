#include "GitHubPullRequestView.h"

#include "GitHubStatus.h"
#include "GitHubUrls.h"
#include "GitHubViewSupport.h"

#include <mirante/ItemContext.h>
#include <mirante/widgets/ActionRow.h>
#include <mirante/widgets/FlowRow.h>

#include <QCoreApplication>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QSet>
#include <QString>

using mirante::widgets::StatusPill;
using mirante::widgets::StatusRole;

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

class PullRequestView : public GitHubSummary
{
public:
    PullRequestView(ItemContext &context, QWidget *parent)
        : GitHubSummary(context, parent)
    {
        addOpenPage();
    }

protected:
    void draw(const ItemStatus &status) override
    {
        const QJsonObject pullRequest = status.data.value(QStringLiteral("pullRequest")).toObject();

        setTitle(pullRequest.value(QStringLiteral("title")).toString());
        setStateRow(stateRow(pullRequest));
        setPageAddress(webAddress(pullRequest, pullRequestUrl(webBase(), config(), defaultRepository())));

        setSection(QStringLiteral("checks"), QCoreApplication::translate("github", "Checks"),
                   checksSection(status.data.value(QStringLiteral("checkRuns")).toArray(),
                                 status.data.value(QStringLiteral("combinedStatus")).toObject()));
        setSection(QStringLiteral("reviews"), QCoreApplication::translate("github", "Reviews"),
                   reviewsSection(status.data.value(QStringLiteral("reviews")).toArray(), pullRequest));
        setSection(QStringLiteral("people"), QCoreApplication::translate("github", "People"),
                   peopleSection(pullRequest));
        setSection(QStringLiteral("labels"), QCoreApplication::translate("github", "Labels"),
                   labelChips(pullRequest.value(QStringLiteral("labels")).toArray()));

        applyField(QStringLiteral("checks"), QStringLiteral("showChecks"));
        applyField(QStringLiteral("reviews"), QStringLiteral("showReviews"));
        applyField(QStringLiteral("people"), QStringLiteral("showPeople"));
        applyField(QStringLiteral("labels"), QStringLiteral("showLabels"));
    }

private:
    static QWidget *stateRow(const QJsonObject &pullRequest)
    {
        auto *row = new QWidget;
        auto *flow = new widgets::FlowRow(row);
        flow->addWidget(statePill(pullRequest));
        const bool open = pullRequest.value(QStringLiteral("state")).toString() == QLatin1String("open");
        if (open && pullRequest.value(QStringLiteral("draft")).toBool())
            flow->addWidget(new StatusPill(QCoreApplication::translate("github", "Draft"), StatusRole::Waiting));
        flow->addWidget(plainWord(QStringLiteral("%1 → %2").arg(
            pullRequest.value(QStringLiteral("head")).toObject().value(QStringLiteral("ref")).toString(),
            pullRequest.value(QStringLiteral("base")).toObject().value(QStringLiteral("ref")).toString())));
        return row;
    }

    static QWidget *checksSection(const QJsonArray &checkRuns, const QJsonObject &combinedStatus)
    {
        QWidget *column = newColumn();
        auto *top = new QWidget;
        auto *topFlow = new widgets::FlowRow(top);
        topFlow->addWidget(checksPill(checksState(checkRuns, combinedStatus)));
        column->layout()->addWidget(top);

        const QJsonArray statuses = combinedStatus.value(QStringLiteral("statuses")).toArray();
        int passed = 0;
        for (const QJsonValue &value : checkRuns) {
            const QJsonObject checkRun = value.toObject();
            if (checkRunPassed(checkRun)) {
                ++passed;
                continue;
            }
            QUrl details(checkRun.value(QStringLiteral("html_url")).toString());
            if (!isWebAddress(details))
                details = QUrl(checkRun.value(QStringLiteral("details_url")).toString());
            column->layout()->addWidget(
                checkRow(checkRun.value(QStringLiteral("name")).toString(), runPill(checkRun),
                         QStringLiteral("check-details-%1").arg(checkRun.value(QStringLiteral("id")).toInteger()),
                         details));
        }
        for (const QJsonValue &value : statuses) {
            const QJsonObject commitStatus = value.toObject();
            if (commitStatus.value(QStringLiteral("state")).toString() == QLatin1String("success")) {
                ++passed;
                continue;
            }
            column->layout()->addWidget(
                checkRow(commitStatus.value(QStringLiteral("context")).toString(), commitStatusPill(commitStatus),
                         QStringLiteral("status-details-%1").arg(commitStatus.value(QStringLiteral("id")).toInteger()),
                         QUrl(commitStatus.value(QStringLiteral("target_url")).toString())));
        }
        const int total = int(checkRuns.size() + statuses.size());
        if (total > 0 && passed == total)
            column->layout()->addWidget(
                plainLine(QCoreApplication::translate("github", "%n check(s) passed", nullptr, total)));
        return column;
    }

    // Its name, a pill and a link to its details.
    static QWidget *checkRow(const QString &name, StatusPill *pill, const QString &linkName, const QUrl &details)
    {
        auto *row = new QWidget;
        auto *flow = new widgets::FlowRow(row);
        flow->addWidget(plainWord(name));
        flow->addWidget(pill);
        if (QPushButton *button = webButton(QCoreApplication::translate("github", "Details"), linkName, details))
            flow->addWidget(button);
        return row;
    }

    static QWidget *reviewsSection(const QJsonArray &reviews, const QJsonObject &pullRequest)
    {
        QWidget *column = newColumn();
        QSet<QString> reviewed;
        for (const QJsonValue &value : latestReviews(reviews)) {
            const QJsonObject review = value.toObject();
            const QString login = loginOf(review.value(QStringLiteral("user")));
            reviewed.insert(login);
            column->layout()->addWidget(
                reviewRow(login, reviewPill(review.value(QStringLiteral("state")).toString())));
        }
        for (const QJsonValue &value : pullRequest.value(QStringLiteral("requested_reviewers")).toArray()) {
            const QString login = loginOf(value);
            if (!login.isEmpty() && !reviewed.contains(login))
                column->layout()->addWidget(reviewRow(login, reviewPill({})));
        }
        for (const QJsonValue &value : pullRequest.value(QStringLiteral("requested_teams")).toArray()) {
            const QString team = value.toObject().value(QStringLiteral("name")).toString();
            if (!team.isEmpty())
                column->layout()->addWidget(reviewRow(team, reviewPill({})));
        }
        if (column->layout()->count() == 0)
            column->layout()->addWidget(plainLine(QCoreApplication::translate("github", "No reviews")));
        return column;
    }

    static QWidget *reviewRow(const QString &name, StatusPill *pill)
    {
        auto *row = new QWidget;
        auto *flow = new widgets::FlowRow(row);
        flow->addWidget(plainWord(name));
        flow->addWidget(pill);
        return row;
    }

    static QWidget *peopleSection(const QJsonObject &pullRequest)
    {
        const QString assignees = userNames(pullRequest.value(QStringLiteral("assignees")).toArray());
        QWidget *column = newColumn();
        column->layout()->addWidget(plainLine(
            QCoreApplication::translate("github", "Author: %1").arg(loginOf(pullRequest.value(QStringLiteral("user"))))));
        column->layout()->addWidget(plainLine(
            QCoreApplication::translate("github", "Assignees: %1")
                .arg(assignees.isEmpty() ? QCoreApplication::translate("github", "none") : assignees)));
        return column;
    }
};

} // namespace

QWidget *createPullRequestView(ItemContext &context, QWidget *parent)
{
    auto *view = new PullRequestView(context, parent);
    view->follow(context.status());
    return view;
}

} // namespace mirante::modules::github
