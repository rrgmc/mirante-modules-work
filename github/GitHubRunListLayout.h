// The interface of the extension point "runlistlayout": the native view of a
// list of GitHub Actions runs. GitHubRunListLayoutDefault.cpp implements it,
// and so does any extension that an app names for "runlistlayout" in its
// manifest. See README.md#list-layouts.
#pragma once

#include "GitHubListModel.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>

class QWidget;

namespace mirante::modules::github {

class RunListModel : public ListModel
{
public:
    struct Run : ListRow
    {
        qint64 id = 0;
        // "owner/repo": the row's own, else the item's.
        QString repository;
        // Its number in its workflow, or 0.
        qint64 number = 0;
        QString workflow;
        // Such as Good "Passed". Empty text when GitHub does not say.
        Pill state;
        QString branch;
        // Who started it.
        QString actor;
        // When it started, else when it was made. Invalid when GitHub does not
        // say.
        QDateTime started;
    };

    // Called by the module only.
    explicit RunListModel(ItemContext &context, ListSpan span = ListSpan::Scope);

    // In the order of GitHub's answer: newest first. Over several
    // repositories, newest first by created_at.
    QList<Run> runs() const;
    Run run(const QString &key) const { return m_runs.value(key); }

protected:
    void read(const QJsonArray &rows, const QUrl &webBase) override;

private:
    QString m_repository;
    QString m_workflow;
    QHash<QString, Run> m_runs;
};

// Makes the view of model, a child of parent. It draws the model now, and again
// at each changed().
QWidget *makeRunListLayout(RunListModel &model, QWidget *parent);

} // namespace mirante::modules::github
