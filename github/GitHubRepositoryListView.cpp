#include "GitHubRepositoryListView.h"

#include "GitHubLists.h"
#include "GitHubRepositoryListLayout.h"
#include "GitHubSettings.h"
#include "GitHubStatus.h"
#include "GitHubUrls.h"
#include "GitHubViewSupport.h"

#include <mirante/ItemContext.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

namespace mirante::modules::github {

namespace {

// A pill, short because the column says what it is about.
Pill checksCellPill(ChecksState state)
{
    switch (state) {
    case ChecksState::Passed:
        return {QCoreApplication::translate("github", "Passed"), widgets::StatusRole::Good};
    case ChecksState::Failed:
        return {QCoreApplication::translate("github", "Failed"), widgets::StatusRole::Bad};
    case ChecksState::Running:
        return {QCoreApplication::translate("github", "Running"), widgets::StatusRole::Waiting};
    case ChecksState::None:
        break;
    }
    return {QCoreApplication::translate("github", "No checks"), widgets::StatusRole::Neutral};
}

} // namespace

QStringList RepositoryListModel::checksTexts()
{
    QStringList texts;
    for (const ChecksState state :
         {ChecksState::Failed, ChecksState::Running, ChecksState::Passed, ChecksState::None})
        texts << checksCellPill(state).text;
    return texts;
}

RepositoryListModel::RepositoryListModel(ItemContext &context)
    : ListModel(context, QCoreApplication::translate("github", "No repositories"),
                ciHealthWebUrl(webBaseOf(context.moduleSettings(kGeneralScreen)), context.config()),
                ciHealthDescription(context.config()))
{
}

QList<RepositoryListModel::Repository> RepositoryListModel::repositories() const
{
    QList<Repository> repositories;
    for (const QString &key : keys())
        repositories << m_repositories.value(key);
    return repositories;
}

void RepositoryListModel::read(const QJsonArray &rows, const QUrl &webBase)
{
    m_repositories.clear();
    for (int index = 0; index < rows.size(); ++index) {
        const QJsonObject row = rows.at(index).toObject();
        const QString name = row.value(QStringLiteral("nameWithOwner")).toString();
        const QJsonObject branch = row.value(QStringLiteral("defaultBranchRef")).toObject();
        const QJsonObject commit = branch.value(QStringLiteral("target")).toObject();

        ListRow listRow;
        listRow.key = name.isEmpty() ? QStringLiteral("row-%1").arg(index) : name;
        listRow.data = row;
        // The newest commit's page, which shows its checks; else the
        // repository's, as GitHub gives it or built from the web address.
        QUrl address(commit.value(QStringLiteral("url")).toString());
        if (!isWebAddress(address))
            address = QUrl(row.value(QStringLiteral("url")).toString());
        if (!isWebAddress(address) && !name.isEmpty())
            address = webPageUrl(webBase, name);
        listRow.address = isWebAddress(address) ? address : QUrl();

        // A run of the default branch, as an empty branch follows it.
        AddTarget add;
        const QString branchName = branch.value(QStringLiteral("name")).toString();
        if (!name.isEmpty()) {
            add.type = QStringLiteral("github.run");
            add.name = (branchName.isEmpty() ? name : name + QLatin1Char(' ') + branchName) + QStringLiteral(" CI");
            add.config = {{QStringLiteral("repository"), name}, {QStringLiteral("branchSource"), QStringLiteral("fixed")}};
        }
        addRow(listRow, add);

        Repository repository;
        static_cast<ListRow &>(repository) = this->row(listRow.key);
        repository.repository = name;
        repository.branch = branchName;
        if (!branch.isEmpty())
            repository.checks = checksCellPill(repositoryChecks(row));
        repository.commit = commit.value(QStringLiteral("messageHeadline")).toString();
        repository.pushed =
            QDateTime::fromString(row.value(QStringLiteral("pushedAt")).toString(), Qt::ISODateWithMs);
        m_repositories.insert(repository.key, repository);
    }
}

QWidget *createCiHealthView(ItemContext &context, QWidget *parent)
{
    auto *model = new RepositoryListModel(context);
    model->follow(context.status());
    QWidget *view = makeRepositoryListLayout(*model, parent);
    model->attach(view);
    return view;
}

} // namespace mirante::modules::github
