#include "GitHubIssueListView.h"

#include "GitHubIssueListLayout.h"
#include "GitHubListSupport.h"
#include "GitHubLists.h"
#include "GitHubSettings.h"

#include <mirante/ItemContext.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

namespace mirante::modules::github {

namespace {

QStringList names(const QJsonArray &objects, const QString &field)
{
    QStringList names;
    for (const QJsonValue &object : objects) {
        const QString name = object.toObject().value(field).toString();
        if (!name.isEmpty())
            names << name;
    }
    return names;
}

} // namespace

IssueListModel::IssueListModel(ItemContext &context)
    : ListModel(context, QCoreApplication::translate("github", "No issues"),
                listWebUrl(ListKind::Issues, webBaseOf(context.moduleSettings(kGeneralScreen)), context.config(),
                           context.projectSetting(kProjectSettingKey)),
                listDescription(ListKind::Issues, context.config(), context.projectSetting(kProjectSettingKey)))
{
}

QList<IssueListModel::Issue> IssueListModel::issues() const
{
    QList<Issue> issues;
    for (const QString &key : keys())
        issues << m_issues.value(key);
    return issues;
}

void IssueListModel::read(const QJsonArray &rows, const QUrl &webBase)
{
    m_issues.clear();
    for (int index = 0; index < rows.size(); ++index) {
        const QJsonObject row = rows.at(index).toObject();
        const NumberedRow read =
            readNumberedRow(row, index, webBase, ListKind::Issues, QStringLiteral("github.issue.summary"));
        addRow(read.row, read.add);

        Issue issue;
        static_cast<ListRow &>(issue) = this->row(read.row.key);
        issue.repository = read.repository;
        issue.number = read.number;
        issue.ref = read.ref;
        issue.refSortKey = read.refSortKey;
        issue.title = row.value(QStringLiteral("title")).toString();
        issue.assignees = names(row.value(QStringLiteral("assignees")).toArray(), QStringLiteral("login"));
        issue.labels = names(row.value(QStringLiteral("labels")).toArray(), QStringLiteral("name"));
        issue.updated = QDateTime::fromString(row.value(QStringLiteral("updated_at")).toString(), Qt::ISODateWithMs);
        m_issues.insert(issue.key, issue);
    }
}

QWidget *createIssueListView(ItemContext &context, QWidget *parent)
{
    auto *model = new IssueListModel(context);
    model->follow(context.status());
    QWidget *view = makeIssueListLayout(*model, parent);
    model->attach(view);
    return view;
}

} // namespace mirante::modules::github
