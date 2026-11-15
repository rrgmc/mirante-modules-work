// The interface of the extension point "repositorylistlayout": the native view
// of the CI health list, an owner's repositories with the checks of each
// default branch. GitHubRepositoryListLayoutDefault.cpp implements it, and so
// does any extension that an app names for "repositorylistlayout" in its
// manifest. See README.md#list-layouts.
#pragma once

#include "GitHubListModel.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class QWidget;

namespace mirante::modules::github {

class RepositoryListModel : public ListModel
{
public:
    struct Repository : ListRow
    {
        QString repository;  // "owner/repo"
        // The default branch, or empty for a repository without commits.
        QString branch;
        // "Passed", "Failed", "Running" or "No checks", for the default
        // branch's newest commit. Empty text without a default branch.
        Pill checks;
        // The first line of that commit's message.
        QString commit;
        // When the repository was last pushed to. Invalid when GitHub does not
        // say.
        QDateTime pushed;
    };

    // The texts of every checks state that a row can have, for a filter.
    static QStringList checksTexts();

    // Called by the module only.
    explicit RepositoryListModel(ItemContext &context);

    // In the order of GitHub's answer: the most recently pushed first.
    QList<Repository> repositories() const;
    Repository repository(const QString &key) const { return m_repositories.value(key); }

protected:
    void read(const QJsonArray &rows, const QUrl &webBase) override;

private:
    QHash<QString, Repository> m_repositories;
};

// Makes the view of model, a child of parent. It draws the model now, and again
// at each changed().
QWidget *makeRepositoryListLayout(RepositoryListModel &model, QWidget *parent);

} // namespace mirante::modules::github
