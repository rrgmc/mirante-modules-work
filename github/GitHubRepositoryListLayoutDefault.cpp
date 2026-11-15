// The default of the extension point "repositorylistlayout": the core's table,
// with the columns Repository, Checks, Branch, Commit, Pushed and Links, and a
// filter on Checks, which keeps the user's last choice in the item's
// statusFilter. The filter lists every checks state, not only those of the
// current rows. It is compiled only when the app names no extension for
// "repositorylistlayout". See README.md#list-layouts.
#include "GitHubListSupport.h"
#include "GitHubRepositoryListLayout.h"

#include <QCoreApplication>

using mirante::widgets::ListCell;

namespace mirante::modules::github {

namespace {

enum Column { RepositoryColumn, ChecksColumn, BranchColumn, CommitColumn, PushedColumn, LinksColumn };

class RepositoryListLayout : public DefaultListLayout
{
public:
    RepositoryListLayout(RepositoryListModel &model, QWidget *parent)
        : DefaultListLayout(model, QStringLiteral("repository-list"), parent)
        , m_model(model)
    {
        // In a narrow list, Branch hides first, then Pushed, then Commit.
        setColumns({{QCoreApplication::translate("github", "Repository"), 0},
                    {QCoreApplication::translate("github", "Checks"), 0},
                    {QCoreApplication::translate("github", "Branch"), 4},
                    {QCoreApplication::translate("github", "Commit"), 2, true},
                    {QCoreApplication::translate("github", "Pushed"), 3},
                    {QCoreApplication::translate("github", "Links"), 0}},
                   PushedColumn);
        addKeptFilter(ChecksColumn, QCoreApplication::translate("github", "Checks"), QStringLiteral("status"));
        // Every state, so that a chosen one stays chosen while no row has it.
        setFilterValues(ChecksColumn, RepositoryListModel::checksTexts());
        start();
    }

protected:
    QList<ListCell> rowCells(const QString &key) const override
    {
        const RepositoryListModel::Repository repository = m_model.repository(key);
        return {textCell(repository.repository),
                pillCell(repository.checks),
                textCell(repository.branch),
                textCell(repository.commit),
                dateCell(repository.data.value(QStringLiteral("pushedAt"))),
                linksCell(repository, repository.repository)};
    }

private:
    RepositoryListModel &m_model;
};

} // namespace

QWidget *makeRepositoryListLayout(RepositoryListModel &model, QWidget *parent)
{
    return new RepositoryListLayout(model, parent);
}

} // namespace mirante::modules::github
