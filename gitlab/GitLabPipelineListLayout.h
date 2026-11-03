// The interface of the extension point "pipelinelistlayout": the native view of
// a list of GitLab pipelines. GitLabPipelineListLayoutDefault.cpp implements
// it, and so does any extension that an app names for "pipelinelistlayout" in
// its manifest. See README.md#list-layouts.
#pragma once

#include "GitLabListModel.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>

class QWidget;

namespace mirante::modules::gitlab {

class PipelineListModel : public ListModel
{
public:
    struct Pipeline : ListRow
    {
        qint64 id = 0;
        // "group/project": the row's kRowProjectKey, else the item's.
        QString project;
        // Such as Bad "Failed". Empty text when GitLab does not say.
        Pill state;
        // The branch, tag or merge request ref it ran for.
        QString ref;
        // The name of who started it.
        QString author;
        // When it started, else when it was made. Invalid when GitLab does not
        // say.
        QDateTime started;
    };

    // Called by the module only.
    explicit PipelineListModel(ItemContext &context, ListSpan span = ListSpan::Scope);

    // In the order of GitLab's answer: newest first. Over several projects,
    // newest first by created_at.
    QList<Pipeline> pipelines() const;
    Pipeline pipeline(const QString &key) const { return m_pipelines.value(key); }

protected:
    void read(const QJsonArray &rows, const QUrl &baseUrl) override;

private:
    QString m_project;
    QHash<QString, Pipeline> m_pipelines;
};

// Makes the view of model, a child of parent. It draws the model now, and again
// at each changed().
QWidget *makePipelineListLayout(PipelineListModel &model, QWidget *parent);

} // namespace mirante::modules::gitlab
