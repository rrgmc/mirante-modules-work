// The interface of the extension point "listlayout": the native view of a Jira
// list. JiraListLayoutDefault.cpp implements it, and so does any extension that
// an app names for "listlayout" in its manifest. A layout is code of an app, so
// this header is an interface that apps build against. A change to it is noted
// in CHANGELOG.md. See README.md#layouts.
#pragma once

#include "JiraIssueLayout.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariant>

class QWidget;

namespace mirante::modules::jira {

// The model of a list: the item's status, read into rows, and what a row's
// links do. The module makes the model, then the view with makeListLayout(),
// which draws the model now and again at each changed(). The model lives as long
// as the view.
class ListModel : public QObject
{
    Q_OBJECT

public:
    // The value of an extra field in a row.
    struct FieldValue
    {
        QString text;
        // A number sorts as one; else null, and the text sorts.
        QVariant sortKey;
    };

    struct Issue
    {
        // Such as "OPS-7".
        QString key;
        // A key that sorts OPS-9 before OPS-10.
        QString keySortKey;
        // Jira's issue object, with its fields.
        QJsonObject data;
        QString summary;
        // The status, coloured by its category.
        Pill status;
        // The assignee's name, or empty when unassigned.
        QString assignee;
        // Invalid when Jira's text cannot be read; updatedText is that text.
        QDateTime updated;
        QString updatedText;
        // One per extraFields(), empty where the site has no such field.
        QList<FieldValue> fields;
        // Its page on the site, or empty without a site.
        QUrl address;
    };

    // Called by the module only.
    explicit ListModel(ItemContext &context);
    ~ListModel() override;

    ItemContext &context() const { return *m_context; }
    QJsonObject config() const;

    // False while the status has no rows. The view then shows message() alone,
    // such as "Loading…".
    bool hasData() const { return m_hasData; }
    QString message() const { return m_message; }

    // What the list shows, such as "My open issues".
    QString description() const;
    // The list's page on the site, or empty when the query cannot be built.
    QUrl pageAddress() const;
    // The rows a page shows, from the item's pageSize field.
    int pageSize() const;
    // The values a view filter on a column chooses at the start: the item's
    // key <column>Filter, which keepFilter() writes. filterDefault("status")
    // reads statusFilter. Empty means "All".
    QStringList filterDefault(const QString &column) const;
    // Keeps the values the user chose in a view filter on a column, so that
    // the next start chooses them again. Empty means "All". The key is one of
    // the type's view keys, so writing it does not make the view again.
    void keepFilter(const QString &column, const QStringList &values);
    // The names of the extra fields of the module's settings.
    QStringList extraFields() const { return m_extraFields; }
    // Whether the site has each extra field, in the order of extraFields().
    QList<bool> extraFieldsFound() const { return m_found; }

    // In the order of Jira's answer.
    QList<Issue> issues() const { return m_issues; }
    Issue issue(const QString &key) const;

    // An issue's page in the external browser.
    void openWeb(const QString &key);
    // An issue's page in Mirante, or in the external browser while Shift is held.
    void openPage(const QString &key);
    // Adds the issue to the current task as a Jira issue item.
    void addToTask(const QString &key);
    // Copies the address of an issue's page.
    void copyLink(const QString &key);

    // Called by the module only. follow() reads the item's status now and at
    // each change; a null source stays at "Loading…". attach() gives the model
    // to the view, which then owns it.
    void follow(ItemStatusSource *source);
    void attach(QWidget *view);

signals:
    // New rows, or another message.
    void changed();

private:
    void showStatus(const ItemStatus &status);
    QUrl site() const;
    QUrl issueAddress(const QString &key) const;

    ItemContext *m_context;
    QStringList m_extraFields;
    bool m_hasData = false;
    QString m_message;
    QList<Issue> m_issues;
    QList<bool> m_found;
};

// Makes the view of model, a child of parent. It draws the model now, and again
// at each changed().
QWidget *makeListLayout(ListModel &model, QWidget *parent);

} // namespace mirante::modules::jira
