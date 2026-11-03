// The interface of the extension point "issuelistlayout": the native view of a
// list of GitHub issues. GitHubIssueListLayoutDefault.cpp implements it, and so
// does any extension that an app names for "issuelistlayout" in its manifest.
// See README.md#list-layouts.
#pragma once

#include "GitHubListModel.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

class QWidget;

namespace mirante::modules::github {

class IssueListModel : public ListModel
{
public:
    struct Issue : ListRow
    {
        QString repository;  // "owner/repo"
        qint64 number = 0;
        // Such as "widgets#7", and a key that sorts by the whole repository and
        // then by the number.
        QString ref;
        QString refSortKey;
        QString title;
        QStringList assignees;
        QStringList labels;
        // Invalid when GitHub does not say.
        QDateTime updated;
    };

    // Called by the module only.
    explicit IssueListModel(ItemContext &context);

    // In the order of GitHub's answer: newest update first.
    QList<Issue> issues() const;
    Issue issue(const QString &key) const { return m_issues.value(key); }

protected:
    void read(const QJsonArray &rows, const QUrl &webBase) override;

private:
    QHash<QString, Issue> m_issues;
};

// Makes the view of model, a child of parent. It draws the model now, and again
// at each changed().
QWidget *makeIssueListLayout(IssueListModel &model, QWidget *parent);

} // namespace mirante::modules::github
