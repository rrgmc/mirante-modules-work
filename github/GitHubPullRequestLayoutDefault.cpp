// The default of the extension point "pullrequestlayout": a SummaryView with the
// pull request's state, Checks, Reviews, People and Labels. It is compiled only
// when the app names no extension for "pullrequestlayout". See
// README.md#layouts.
#include "GitHubPullRequestLayout.h"
#include "GitHubViewSupport.h"

#include <mirante/widgets/FlowRow.h>
#include <mirante/widgets/SummaryView.h>

#include <QCoreApplication>
#include <QLabel>
#include <QLayout>
#include <QPushButton>

using mirante::widgets::StatusPill;
using mirante::widgets::StatusRole;

namespace mirante::modules::github {

namespace {

const QStringList &sections()
{
    static const QStringList keys{QStringLiteral("checks"), QStringLiteral("reviews"), QStringLiteral("people"),
                                  QStringLiteral("labels")};
    return keys;
}

// A name and a pill on one row.
QWidget *nameRow(const QString &name, StatusPill *pill)
{
    auto *row = new QWidget;
    auto *flow = new widgets::FlowRow(row);
    flow->addWidget(plainWord(name));
    flow->addWidget(pill);
    return row;
}

class PullRequestLayout : public widgets::SummaryView
{
public:
    PullRequestLayout(PullRequestModel &model, QWidget *parent)
        : widgets::SummaryView(parent)
        , m_model(model)
    {
        setObjectName(defaultLayoutName(QStringLiteral("pull-request")));
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
        const QJsonObject pullRequest = m_model.pullRequest();

        setTitle(pullRequest.value(QStringLiteral("title")).toString());
        setStateRow(stateRow(pullRequest));
        showPageAddress(this, m_model);

        setSection(QStringLiteral("checks"), QCoreApplication::translate("github", "Checks"), checksSection());
        setSection(QStringLiteral("reviews"), QCoreApplication::translate("github", "Reviews"), reviewsSection());
        setSection(QStringLiteral("people"), QCoreApplication::translate("github", "People"),
                   peopleSection(pullRequest));
        setSection(QStringLiteral("labels"), QCoreApplication::translate("github", "Labels"),
                   labelChips(pullRequest.value(QStringLiteral("labels")).toArray()));
        for (const QString &section : sections())
            setSectionVisible(section, m_model.isSectionShown(section));
    }

    QWidget *stateRow(const QJsonObject &pullRequest) const
    {
        auto *row = new QWidget;
        auto *flow = new widgets::FlowRow(row);
        flow->addWidget(pillWidget(m_model.state()));
        if (m_model.isDraft())
            flow->addWidget(new StatusPill(QCoreApplication::translate("github", "Draft"), StatusRole::Waiting));
        flow->addWidget(plainWord(QStringLiteral("%1 → %2").arg(
            pullRequest.value(QStringLiteral("head")).toObject().value(QStringLiteral("ref")).toString(),
            pullRequest.value(QStringLiteral("base")).toObject().value(QStringLiteral("ref")).toString())));
        return row;
    }

    // The overall pill, then each check that did not pass, with a link to its
    // details, or the count when every one passed.
    QWidget *checksSection() const
    {
        QWidget *column = newColumn();
        auto *top = new QWidget;
        auto *topFlow = new widgets::FlowRow(top);
        topFlow->addWidget(pillWidget(m_model.checks()));
        column->layout()->addWidget(top);

        const QList<PullRequestModel::Check> checks = m_model.checkList();
        int passed = 0;
        for (const PullRequestModel::Check &check : checks) {
            if (check.passed) {
                ++passed;
                continue;
            }
            QWidget *row = nameRow(check.name, pillWidget(check.state));
            if (QPushButton *button = webButton(QCoreApplication::translate("github", "Details"), check.id, check.details))
                row->layout()->addWidget(button);
            column->layout()->addWidget(row);
        }
        const int total = int(checks.size());
        if (total > 0 && passed == total)
            column->layout()->addWidget(
                plainLine(QCoreApplication::translate("github", "%n check(s) passed", nullptr, total)));
        return column;
    }

    QWidget *reviewsSection() const
    {
        QWidget *column = newColumn();
        for (const PullRequestModel::Reviewer &reviewer : m_model.reviewers())
            column->layout()->addWidget(nameRow(reviewer.name, pillWidget(reviewer.state)));
        if (column->layout()->count() == 0)
            column->layout()->addWidget(plainLine(QCoreApplication::translate("github", "No reviews")));
        return column;
    }

    static QWidget *peopleSection(const QJsonObject &pullRequest)
    {
        const QString assignees = userNames(pullRequest.value(QStringLiteral("assignees")).toArray());
        const QString author = pullRequest.value(QStringLiteral("user")).toObject().value(QStringLiteral("login")).toString();
        QWidget *column = newColumn();
        column->layout()->addWidget(plainLine(QCoreApplication::translate("github", "Author: %1").arg(author)));
        column->layout()->addWidget(plainLine(
            QCoreApplication::translate("github", "Assignees: %1")
                .arg(assignees.isEmpty() ? QCoreApplication::translate("github", "none") : assignees)));
        return column;
    }

    PullRequestModel &m_model;
};

} // namespace

QWidget *makePullRequestLayout(PullRequestModel &model, QWidget *parent)
{
    return new PullRequestLayout(model, parent);
}

} // namespace mirante::modules::github
