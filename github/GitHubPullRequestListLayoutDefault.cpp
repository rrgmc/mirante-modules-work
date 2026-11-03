// The default of the extension point "pullrequestlistlayout": the core's table,
// with the columns Ref, Title, Repository, Author, Checks, Reviews, Updated and
// Links. Repository, and a filter on it, show only in a list over several
// repositories. It is compiled only when the app names no extension for
// "pullrequestlistlayout".
// See README.md#list-layouts.
#include "GitHubListSupport.h"
#include "GitHubPullRequestListLayout.h"

#include <QCoreApplication>

using mirante::widgets::ListCell;

namespace mirante::modules::github {

namespace {

enum Column {
    RefColumn,
    TitleColumn,
    RepositoryColumn,
    AuthorColumn,
    ChecksColumn,
    ReviewsColumn,
    UpdatedColumn,
    LinksColumn
};

// Such as "2 approved" or "changes requested". Sorted by the approvals, with
// changes requested below none, and a row without an answer last.
ListCell reviewsCell(const PullRequestListModel::Reviews &reviews)
{
    if (!reviews.answered) {
        ListCell cell = dashCell();
        cell.sortKey = -2;
        return cell;
    }
    if (reviews.changesRequested)
        return {QCoreApplication::translate("github", "changes requested"), -1, {}};
    if (reviews.approvals > 0)
        return {QCoreApplication::translate("github", "%n approved", nullptr, reviews.approvals), reviews.approvals, {}};
    ListCell cell = dashCell();
    cell.sortKey = 0;
    return cell;
}

class PullRequestListLayout : public DefaultListLayout
{
public:
    PullRequestListLayout(PullRequestListModel &model, QWidget *parent)
        : DefaultListLayout(model, QStringLiteral("pull-request-list"), parent)
        , m_model(model)
    {
        setColumns({{QCoreApplication::translate("github", "Ref"), 1},
                    {QCoreApplication::translate("github", "Title"), 0, true},
                    {QCoreApplication::translate("github", "Repository"), 6},
                    {QCoreApplication::translate("github", "Author"), 5},
                    {QCoreApplication::translate("github", "Checks"), 2},
                    {QCoreApplication::translate("github", "Reviews"), 4},
                    {QCoreApplication::translate("github", "Updated"), 3},
                    {QCoreApplication::translate("github", "Links"), 0}},
                   UpdatedColumn);
        setColumnVisible(ChecksColumn, model.isColumnShown(QStringLiteral("checks")));
        setColumnVisible(ReviewsColumn, model.isColumnShown(QStringLiteral("reviews")));
        setColumnVisible(RepositoryColumn, model.spansRepositories());
        if (model.spansRepositories()) {
            addFilter(RepositoryColumn, QCoreApplication::translate("github", "Repository"));
            setFilterDefault(RepositoryColumn, model.filterDefault(QStringLiteral("repository")));
        }
        start();
    }

protected:
    QList<ListCell> rowCells(const QString &key) const override
    {
        const PullRequestListModel::PullRequest pullRequest = m_model.pullRequest(key);
        return {{pullRequest.ref, pullRequest.refSortKey, {}},
                {pullRequest.title, {}, {}},
                textCell(pullRequest.repository),
                textCell(pullRequest.author),
                pillCell(pullRequest.checks),
                reviewsCell(pullRequest.reviews),
                dateCell(pullRequest.data.value(QStringLiteral("updated_at"))),
                linksCell(pullRequest, pullRequest.refSortKey)};
    }

private:
    PullRequestListModel &m_model;
};

} // namespace

QWidget *makePullRequestListLayout(PullRequestListModel &model, QWidget *parent)
{
    return new PullRequestListLayout(model, parent);
}

} // namespace mirante::modules::github
