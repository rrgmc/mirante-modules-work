// The default of the extension point "issuelayout": a SummaryView with the
// issue's state, Details, Labels and Comments. It is compiled only when the app
// names no extension for "issuelayout". See README.md#layouts.
#include "GitHubIssueLayout.h"
#include "GitHubViewSupport.h"

#include <mirante/widgets/FlowRow.h>
#include <mirante/widgets/SummaryView.h>

#include <QCoreApplication>
#include <QFont>
#include <QLabel>
#include <QLayout>

#include <algorithm>

namespace mirante::modules::github {

namespace {

// The latest comments the view shows.
constexpr qsizetype kShownComments = 5;

class IssueLayout : public widgets::SummaryView
{
public:
    IssueLayout(IssueModel &model, QWidget *parent)
        : widgets::SummaryView(parent)
        , m_model(model)
    {
        setObjectName(defaultLayoutName(QStringLiteral("issue")));
        followActions(this, model);
        addOpenPage(this, model);
        connect(&model, &SummaryModel::changed, this, [this] { draw(); });
        draw();
    }

private:
    void draw()
    {
        if (!m_model.hasData()) {
            setMessage(m_model.message());
            return;
        }
        setMessage({});
        const QJsonObject issue = m_model.issue();

        setTitle(issue.value(QStringLiteral("title")).toString());
        auto *row = new QWidget;
        auto *flow = new widgets::FlowRow(row);
        flow->addWidget(pillWidget(m_model.state()));
        setStateRow(row);
        showPageAddress(this, m_model);

        setSection(QStringLiteral("details"), QCoreApplication::translate("github", "Details"), detailsSection(issue));
        setSection(QStringLiteral("labels"), QCoreApplication::translate("github", "Labels"),
                   labelChips(issue.value(QStringLiteral("labels")).toArray()));
        setSection(QStringLiteral("comments"), QCoreApplication::translate("github", "Comments"), commentsSection());
        for (const QString &section : {QStringLiteral("details"), QStringLiteral("labels"), QStringLiteral("comments")})
            setSectionVisible(section, m_model.isSectionShown(section));
    }

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

    QWidget *commentsSection() const
    {
        switch (m_model.commentsState()) {
        case IssueModel::Comments::None:
            return plainLine(QCoreApplication::translate("github", "No comments"));
        case IssueModel::Comments::Unreadable:
            return plainLine(QCoreApplication::translate("github", "The comments could not be read."));
        case IssueModel::Comments::Shown:
            break;
        }

        const QJsonArray comments = m_model.comments();
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

    IssueModel &m_model;
};

} // namespace

QWidget *makeIssueLayout(IssueModel &model, QWidget *parent)
{
    return new IssueLayout(model, parent);
}

} // namespace mirante::modules::github
