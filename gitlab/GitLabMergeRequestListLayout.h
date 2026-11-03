// The interface of the extension point "mergerequestlistlayout": the native
// view of a list of GitLab merge requests. GitLabMergeRequestListLayoutDefault.cpp
// implements it, and so does any extension that an app names for
// "mergerequestlistlayout" in its manifest. See README.md#list-layouts.
#pragma once

#include "GitLabListModel.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class QWidget;

namespace mirante {
class ApiClient;
}

namespace mirante::modules::gitlab {

// The rows come from the status. Their head pipeline and approvals come later,
// from requests of their own, for the first 25 rows that the layout shows: see
// setVisibleRows(). Each answer says rowChanged().
class MergeRequestListModel : public ListModel
{
public:
    // The rows of setVisibleRows() that get the further requests.
    static constexpr int AskedRows = 25;

    struct Approvals
    {
        // False until the answer comes, and when it fails.
        bool answered = false;
        int given = 0;
        // 0 when the project requires none.
        int required = 0;
    };

    struct MergeRequest : ListRow
    {
        QString project;  // "group/project"
        qint64 iid = 0;
        // Such as "backend!42", and a key that sorts by the whole path and
        // then by the number.
        QString ref;
        QString refSortKey;
        QString title;
        // The author's name.
        QString author;
        // Invalid when GitLab does not say.
        QDateTime updated;
        // The head pipeline's state, such as Bad "Failed". Empty text until the
        // answer comes, when it fails, and without a pipeline.
        Pill pipeline;
        Approvals approvals;
    };

    // Called by the module only.
    explicit MergeRequestListModel(ItemContext &context);
    ~MergeRequestListModel() override;

    // In the order of GitLab's answer.
    QList<MergeRequest> mergeRequests() const;
    MergeRequest mergeRequest(const QString &key) const;

    // Asks for the pipeline and approvals of the first AskedRows of keys, while
    // the view is visible. A row that stays keeps what is on the way, or its
    // answer. A row that goes drops them. The fields showPipeline and
    // showApprovals turn each request off.
    void setVisibleRows(const QStringList &keys) override;

protected:
    void read(const QJsonArray &rows, const QUrl &baseUrl) override;
    void viewVisibilityChanged(bool visible) override;

private:
    void askForNewKeys();
    void stopAsking();
    void ask(ApiClient &server, const QString &key);
    void answer(const QString &key);
    void forgetOthers(const QStringList &asked);

    QHash<QString, MergeRequest> m_mergeRequests;
    QStringList m_visible;
    // The requests of the rows asked for since the latest status, each row with
    // a receiver of its own, so they can be cancelled row by row. A row stays
    // here after its answers come.
    QHash<QString, QObject *> m_rounds;
    // The latest answers, by row.
    QHash<QString, Pill> m_pipelines;
    QHash<QString, Approvals> m_approvals;
    // rowChanged() is on its way, and a layout that sorts may tell of new rows.
    bool m_answering = false;
};

// Makes the view of model, a child of parent. It draws the model now, and again
// at each changed() and rowChanged(), and tells the model which rows it shows.
QWidget *makeMergeRequestListLayout(MergeRequestListModel &model, QWidget *parent);

} // namespace mirante::modules::gitlab
