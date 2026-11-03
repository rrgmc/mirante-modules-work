#include "GitLabIssueListView.h"

#include "GitLabIssueListLayout.h"
#include "GitLabListSupport.h"
#include "GitLabLists.h"
#include "GitLabSettings.h"

#include <mirante/ItemContext.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

namespace mirante::modules::gitlab {

IssueListModel::IssueListModel(ItemContext &context)
    : ListModel(context, QCoreApplication::translate("gitlab", "No issues"),
                listWebUrl(ListKind::Issues, baseUrlOf(context.moduleSettings(kGeneralScreen)), context.config(),
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

void IssueListModel::read(const QJsonArray &rows, const QUrl &baseUrl)
{
    m_issues.clear();
    for (int index = 0; index < rows.size(); ++index) {
        const QJsonObject row = rows.at(index).toObject();
        const NumberedRow read = readNumberedRow(row, index, baseUrl, ListKind::Issues, QLatin1Char('#'),
                                                 QStringLiteral("gitlab.issue.summary"));
        addRow(read.row, read.add);

        Issue issue;
        static_cast<ListRow &>(issue) = this->row(read.row.key);
        issue.project = read.project;
        issue.iid = read.iid;
        issue.ref = read.ref;
        issue.refSortKey = read.refSortKey;
        issue.title = row.value(QStringLiteral("title")).toString();
        for (const QJsonValue &user : row.value(QStringLiteral("assignees")).toArray()) {
            const QString name = user.toObject().value(QStringLiteral("name")).toString();
            if (!name.isEmpty())
                issue.assignees << name;
        }
        for (const QJsonValue &label : row.value(QStringLiteral("labels")).toArray())
            issue.labels << label.toString();
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

} // namespace mirante::modules::gitlab
