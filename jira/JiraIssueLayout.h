// The interface of the extension point "issuelayout": the native view of a Jira
// issue. JiraIssueLayoutDefault.cpp implements it, and so does any extension
// that an app names for "issuelayout" in its manifest. A layout is code of an
// app, so this header is an interface that apps build against. A change to it
// is noted in CHANGELOG.md. See README.md#layouts.
#pragma once

#include <mirante/Status.h>
#include <mirante/widgets/StatusPill.h>

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

class QWidget;

namespace mirante {
class ItemContext;
}

namespace mirante::modules::jira {

// The text and colour of a state, such as Good "Done", as Jira names it. A
// layout draws it as it likes, such as with widgets::StatusPill.
struct Pill
{
    QString text;
    widgets::StatusRole role = widgets::StatusRole::Neutral;
};

// Text that anyone with access to the issue wrote: Jira's rendered HTML, or
// else its plain text. Both are empty when there is none. HTML must be shown so
// that it loads nothing and opens only web links; the default layout's
// RichText does. A relative link is below siteAddress().
struct RichContent
{
    QString html;
    QString text;

    bool isEmpty() const { return html.trimmed().isEmpty() && text.isEmpty(); }
};

// The model of an issue: the item's status, read into what a layout shows. The
// module makes the model, then the view with makeIssueLayout(), which draws the
// model now and again at each changed(). The model lives as long as the view.
class IssueModel : public QObject
{
    Q_OBJECT

public:
    // A row of the details, such as "Priority" and "High".
    struct Detail
    {
        QString label;
        QString value;
    };

    struct Comment
    {
        QString author;
        // A short local date and time.
        QString date;
        RichContent body;
    };

    // A sub-task, a linked issue, or a child of an epic.
    struct LinkedIssue
    {
        QString key;
        // For a sub-task or a link, the relation, the summary and the status,
        // such as "blocks · Fix the login · Done". For a child, its summary.
        QString text;
        // A child's status, or empty text for a sub-task or a link.
        Pill state;
        // Its page on the site, or empty.
        QUrl address;
        bool child = false;
    };

    // Called by the module only.
    explicit IssueModel(ItemContext &context);
    ~IssueModel() override;

    ItemContext &context() const { return *m_context; }
    QJsonObject config() const;

    // False while the status has no issue. The view then shows message()
    // alone, such as "Loading…".
    bool hasData() const { return !m_issue.isEmpty(); }
    QString message() const { return m_message; }

    // Whether the item's settings show a section: the boolean field
    // show<Section>, on when absent. The sections are "details",
    // "description", "comments" and "links".
    bool isSectionShown(const QString &section) const;

    // Jira's issue object, with its fields, names and renderedFields.
    QJsonObject issue() const { return m_issue; }
    // The item's key, from its settings, such as "OPS-7".
    QString key() const;
    // The key and the summary, such as "OPS-7 Fix the login".
    QString title() const;
    // The status, coloured by its category, or empty text.
    Pill status() const;
    // The issue type's name, or empty.
    QString type() const;
    // Priority, assignee ("Unassigned" without one), reporter, sprint, then the
    // extra fields of the module's settings, each only with a value.
    QList<Detail> details() const;
    RichContent description() const;
    // All comments, newest first.
    QList<Comment> comments() const;
    // The sub-tasks, then the links, then the children of an epic.
    QList<LinkedIssue> linkedIssues() const;
    // How many children of an epic the status left out.
    int moreChildren() const;

    // The site's address, the base of relative links in HTML.
    QUrl siteAddress() const { return m_site; }
    // The issue's page on the site, or empty when there is none.
    QUrl pageAddress() const;
    // Shows the page in Mirante, or in the external browser while Shift is held.
    void openPage();

    // Called by the module only. follow() reads the item's status now and at
    // each change; a null source stays at "Loading…". attach() gives the model
    // to the view, which then owns it.
    void follow(ItemStatusSource *source);
    void attach(QWidget *view);

signals:
    // A new issue or another message. The same answer again says nothing, so a
    // selection in the text stays.
    void changed();

private:
    void showStatus(const ItemStatus &status);
    QUrl issueAddress(const QString &key) const;

    ItemContext *m_context;
    QUrl m_site;
    QJsonObject m_answer;
    QJsonObject m_issue;
    QString m_message;
};

// Makes the view of model, a child of parent. It draws the model now, and again
// at each changed().
QWidget *makeIssueLayout(IssueModel &model, QWidget *parent);

} // namespace mirante::modules::jira
