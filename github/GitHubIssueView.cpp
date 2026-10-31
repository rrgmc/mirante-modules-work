#include "GitHubIssueView.h"

#include "GitHubUrls.h"
#include "GitHubViewSupport.h"

#include <mirante/ItemContext.h>
#include <mirante/widgets/FlowRow.h>

#include <QCoreApplication>
#include <QFont>
#include <QLabel>
#include <QLayout>
#include <QString>

#include <algorithm>

namespace mirante::modules::github {

namespace {

// The latest comments a summary shows.
constexpr qsizetype kShownComments = 5;

class IssueView : public GitHubSummary
{
public:
    IssueView(ItemContext &context, QWidget *parent)
        : GitHubSummary(context, parent)
    {
        addOpenPage();
    }

protected:
    void draw(const ItemStatus &status) override
    {
        const QJsonObject issue = status.data.value(QStringLiteral("issue")).toObject();

        setTitle(issue.value(QStringLiteral("title")).toString());
        auto *row = new QWidget;
        auto *flow = new widgets::FlowRow(row);
        flow->addWidget(statePill(issue));
        setStateRow(row);

        setPageAddress(webAddress(issue, issueUrl(webBase(), config(), defaultRepository())));

        setSection(QStringLiteral("details"), QCoreApplication::translate("github", "Details"), detailsSection(issue));
        setSection(QStringLiteral("labels"), QCoreApplication::translate("github", "Labels"),
                   labelChips(issue.value(QStringLiteral("labels")).toArray()));
        setSection(QStringLiteral("comments"), QCoreApplication::translate("github", "Comments"),
                   commentsSection(status.data, issue));

        applyField(QStringLiteral("details"), QStringLiteral("showDetails"));
        applyField(QStringLiteral("labels"), QStringLiteral("showLabels"));
        applyField(QStringLiteral("comments"), QStringLiteral("showComments"));
    }

private:
    static QWidget *detailsSection(const QJsonObject &issue)
    {
        QWidget *column = newColumn();
        const QString assignees = userNames(issue.value(QStringLiteral("assignees")).toArray());
        column->layout()->addWidget(
            plainLine(QCoreApplication::translate("github", "Assignees: %1")
                          .arg(assignees.isEmpty() ? QCoreApplication::translate("github", "none") : assignees)));
        const QString milestone =
            issue.value(QStringLiteral("milestone")).toObject().value(QStringLiteral("title")).toString();
        if (!milestone.isEmpty())
            column->layout()->addWidget(
                plainLine(QCoreApplication::translate("github", "Milestone: %1").arg(milestone)));
        return column;
    }

    // The status leaves the comments out both when there are none and when
    // they could not be read. The issue's own count tells the two apart.
    static QWidget *commentsSection(const QJsonObject &data, const QJsonObject &issue)
    {
        const QJsonArray comments = data.value(QStringLiteral("comments")).toArray();
        if (comments.isEmpty()) {
            if (data.contains(QStringLiteral("comments")) || issue.value(QStringLiteral("comments")).toInt() == 0)
                return plainLine(QCoreApplication::translate("github", "No comments"));
            return plainLine(QCoreApplication::translate("github", "The comments could not be read."));
        }

        QWidget *column = newColumn();
        const qsizetype shown = std::min(comments.size(), kShownComments);
        for (qsizetype i = 0; i < shown; ++i) {
            const QJsonObject comment = comments.at(i).toObject();
            QLabel *byline = plainLine(QStringLiteral("%1 · %2").arg(
                comment.value(QStringLiteral("user")).toObject().value(QStringLiteral("login")).toString(),
                localDateTime(comment.value(QStringLiteral("created_at")).toString())));
            QFont font = byline->font();
            font.setItalic(true);
            byline->setFont(font);
            column->layout()->addWidget(byline);
            column->layout()->addWidget(plainLine(comment.value(QStringLiteral("body")).toString()));
        }
        return column;
    }
};

} // namespace

QWidget *createIssueView(ItemContext &context, QWidget *parent)
{
    auto *view = new IssueView(context, parent);
    view->follow(context.status());
    return view;
}

} // namespace mirante::modules::github
