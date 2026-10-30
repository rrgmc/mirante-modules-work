#include "GitLabIssueView.h"

#include "GitLabStatus.h"
#include "GitLabUrls.h"
#include "GitLabViewSupport.h"

#include <mirante/ItemContext.h>
#include <mirante/widgets/ActionRow.h>
#include <mirante/widgets/FlowRow.h>

#include <QCoreApplication>
#include <QFont>
#include <QLabel>
#include <QLayout>
#include <QString>

#include <algorithm>

namespace mirante::modules::gitlab {

namespace {

// The latest comments a summary shows.
constexpr qsizetype kShownComments = 5;

class IssueView : public GitLabSummary
{
public:
    IssueView(ItemContext &context, QWidget *parent)
        : GitLabSummary(context, parent)
    {
        addOpenPage();
    }

protected:
    void draw(const ItemStatus &status) override
    {
        const QJsonObject issue = status.data.value(QStringLiteral("issue")).toObject();
        const QString state = issue.value(QStringLiteral("state")).toString();

        setTitle(issue.value(QStringLiteral("title")).toString());
        auto *row = new QWidget;
        auto *flow = new widgets::FlowRow(row);
        flow->addWidget(statePill(state));
        setStateRow(row);

        setPageAddress(webAddress(issue, issueUrl(baseUrl(), config(), defaultProject())));

        setSection(QStringLiteral("details"), QCoreApplication::translate("gitlab", "Details"), detailsSection(issue));
        setSection(QStringLiteral("labels"), QCoreApplication::translate("gitlab", "Labels"),
                   labelChips(issue.value(QStringLiteral("labels")).toArray()));
        setSection(QStringLiteral("comments"), QCoreApplication::translate("gitlab", "Comments"),
                   commentsSection(status.data));

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
            plainLine(QCoreApplication::translate("gitlab", "Assignees: %1")
                          .arg(assignees.isEmpty() ? QCoreApplication::translate("gitlab", "none") : assignees)));
        const QString milestone =
            issue.value(QStringLiteral("milestone")).toObject().value(QStringLiteral("title")).toString();
        if (!milestone.isEmpty())
            column->layout()->addWidget(
                plainLine(QCoreApplication::translate("gitlab", "Milestone: %1").arg(milestone)));
        const QString due = issue.value(QStringLiteral("due_date")).toString();
        if (!due.isEmpty())
            column->layout()->addWidget(
                plainLine(QCoreApplication::translate("gitlab", "Due: %1").arg(localDate(due))));
        return column;
    }

    // An empty list means no comments. A missing one means the request failed.
    static QWidget *commentsSection(const QJsonObject &data)
    {
        const QJsonValue notes = data.value(QStringLiteral("notes"));
        if (!notes.isArray())
            return plainLine(QCoreApplication::translate("gitlab", "The comments could not be read."));
        const QJsonArray list = notes.toArray();
        if (list.isEmpty())
            return plainLine(QCoreApplication::translate("gitlab", "No comments"));

        QWidget *column = newColumn();
        const qsizetype shown = std::min(list.size(), kShownComments);
        for (qsizetype i = 0; i < shown; ++i) {
            const QJsonObject note = list.at(i).toObject();
            QLabel *byline = plainLine(QStringLiteral("%1 · %2").arg(
                note.value(QStringLiteral("author")).toObject().value(QStringLiteral("name")).toString(),
                localDateTime(note.value(QStringLiteral("created_at")).toString())));
            QFont font = byline->font();
            font.setItalic(true);
            byline->setFont(font);
            column->layout()->addWidget(byline);
            column->layout()->addWidget(plainLine(note.value(QStringLiteral("body")).toString()));
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

} // namespace mirante::modules::gitlab
