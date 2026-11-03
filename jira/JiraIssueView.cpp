#include "JiraIssueView.h"

#include "JiraFields.h"
#include "JiraIssueLayout.h"
#include "JiraSettings.h"
#include "JiraStatus.h"
#include "JiraUrls.h"

#include <mirante/ItemContext.h>

#include <QCoreApplication>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QLocale>
#include <QWidget>

#include <algorithm>

namespace mirante::modules::jira {

namespace {

QString dotted(const QString &first, const QString &second)
{
    if (first.isEmpty() || second.isEmpty())
        return first + second;
    return first + QStringLiteral(" ") + QChar(0x00B7) + QStringLiteral(" ") + second;
}

// Only pages on the web open from here. An address can come from a server, and
// opening another scheme could start a program.
bool isWebAddress(const QUrl &url)
{
    return url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http");
}

Pill statusPill(const QJsonObject &fields)
{
    const QJsonObject status = fields.value(QStringLiteral("status")).toObject();
    const QString category =
        status.value(QStringLiteral("statusCategory")).toObject().value(QStringLiteral("key")).toString();
    return {status.value(QStringLiteral("name")).toString(), statusCategoryRole(category)};
}

} // namespace

IssueModel::IssueModel(ItemContext &context)
    : m_context(&context)
    , m_site(baseUrlOf(context.moduleSettings(kGeneralScreen)))
    , m_message(QCoreApplication::translate("jira", "Loading…"))
{
}

IssueModel::~IssueModel() = default;

QJsonObject IssueModel::config() const
{
    return m_context->config();
}

bool IssueModel::isSectionShown(const QString &section) const
{
    if (section.isEmpty())
        return true;
    const QString field = QStringLiteral("show") + section.at(0).toUpper() + section.mid(1);
    return config().value(field).toBool(true);
}

QString IssueModel::key() const
{
    return config().value(QStringLiteral("key")).toString().trimmed();
}

QString IssueModel::title() const
{
    return m_issue.value(QStringLiteral("key")).toString() + QLatin1Char(' ')
        + m_issue.value(QStringLiteral("fields")).toObject().value(QStringLiteral("summary")).toString();
}

Pill IssueModel::status() const
{
    return statusPill(m_issue.value(QStringLiteral("fields")).toObject());
}

QString IssueModel::type() const
{
    return m_issue.value(QStringLiteral("fields"))
        .toObject()
        .value(QStringLiteral("issuetype"))
        .toObject()
        .value(QStringLiteral("name"))
        .toString();
}

QList<IssueModel::Detail> IssueModel::details() const
{
    const QJsonObject fields = m_issue.value(QStringLiteral("fields")).toObject();
    const auto person = [&fields](const QString &field) {
        return fields.value(field).toObject().value(QStringLiteral("displayName")).toString();
    };
    const QString assignee = person(QStringLiteral("assignee"));

    QList<Detail> details;
    const auto add = [&details](const QString &label, const QString &value) {
        if (!value.isEmpty())
            details << Detail{label, value};
    };
    add(QCoreApplication::translate("jira", "Priority"),
        fields.value(QStringLiteral("priority")).toObject().value(QStringLiteral("name")).toString());
    add(QCoreApplication::translate("jira", "Assignee"),
        assignee.isEmpty() ? QCoreApplication::translate("jira", "Unassigned") : assignee);
    add(QCoreApplication::translate("jira", "Reporter"), person(QStringLiteral("reporter")));
    add(QCoreApplication::translate("jira", "Sprint"), sprintOf(m_issue));

    // A name the site does not have is left out quietly. See decision 0073.
    const QJsonObject names = m_issue.value(QStringLiteral("names")).toObject();
    for (const QString &name : extraFieldsOf(m_context->moduleSettings(kGeneralScreen)))
        add(name, fieldText(fields.value(fieldIdByName(names, name))));
    return details;
}

RichContent IssueModel::description() const
{
    RichContent content;
    content.html = m_issue.value(QStringLiteral("renderedFields"))
                       .toObject()
                       .value(QStringLiteral("description"))
                       .toString();
    const QJsonValue raw = m_issue.value(QStringLiteral("fields")).toObject().value(QStringLiteral("description"));
    if (raw.isString())
        content.text = raw.toString();
    return content;
}

QList<IssueModel::Comment> IssueModel::comments() const
{
    const QJsonArray rendered = m_issue.value(QStringLiteral("renderedFields"))
                                    .toObject()
                                    .value(QStringLiteral("comment"))
                                    .toObject()
                                    .value(QStringLiteral("comments"))
                                    .toArray();
    QHash<QString, QString> htmlById;
    for (const QJsonValue &value : rendered) {
        const QJsonObject comment = value.toObject();
        htmlById.insert(comment.value(QStringLiteral("id")).toString(), comment.value(QStringLiteral("body")).toString());
    }

    const QJsonArray all = m_issue.value(QStringLiteral("fields"))
                               .toObject()
                               .value(QStringLiteral("comment"))
                               .toObject()
                               .value(QStringLiteral("comments"))
                               .toArray();
    QList<QJsonObject> sorted;
    for (const QJsonValue &value : all)
        sorted << value.toObject();
    std::stable_sort(sorted.begin(), sorted.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return readJiraDate(a.value(QStringLiteral("created")).toString())
            > readJiraDate(b.value(QStringLiteral("created")).toString());
    });

    QList<Comment> comments;
    for (const QJsonObject &comment : std::as_const(sorted)) {
        Comment entry;
        entry.author =
            comment.value(QStringLiteral("author")).toObject().value(QStringLiteral("displayName")).toString();
        entry.date = shortJiraDate(comment.value(QStringLiteral("created")).toString());
        entry.body.html = htmlById.value(comment.value(QStringLiteral("id")).toString());
        const QJsonValue raw = comment.value(QStringLiteral("body"));
        if (raw.isString())
            entry.body.text = raw.toString();
        comments << entry;
    }
    return comments;
}

QList<IssueModel::LinkedIssue> IssueModel::linkedIssues() const
{
    const QJsonObject fields = m_issue.value(QStringLiteral("fields")).toObject();
    QList<LinkedIssue> issues;
    // An issue named in the sub-tasks or links: its summary and status, after
    // the relation it has to this one.
    const auto addRef = [this, &issues](const QJsonObject &issue, const QString &relation) {
        const QJsonObject refFields = issue.value(QStringLiteral("fields")).toObject();
        const QString status =
            refFields.value(QStringLiteral("status")).toObject().value(QStringLiteral("name")).toString();
        const QString summary = refFields.value(QStringLiteral("summary")).toString();
        const QString key = issue.value(QStringLiteral("key")).toString();
        issues << LinkedIssue{key, dotted(relation, dotted(summary, status)), {}, issueAddress(key), false};
    };

    for (const QJsonValue &subtask : fields.value(QStringLiteral("subtasks")).toArray())
        addRef(subtask.toObject(), QCoreApplication::translate("jira", "Sub-task"));
    for (const QJsonValue &value : fields.value(QStringLiteral("issuelinks")).toArray()) {
        const QJsonObject link = value.toObject();
        const QJsonObject type = link.value(QStringLiteral("type")).toObject();
        if (link.contains(QStringLiteral("outwardIssue")))
            addRef(link.value(QStringLiteral("outwardIssue")).toObject(), type.value(QStringLiteral("outward")).toString());
        else if (link.contains(QStringLiteral("inwardIssue")))
            addRef(link.value(QStringLiteral("inwardIssue")).toObject(), type.value(QStringLiteral("inward")).toString());
    }
    for (const QJsonValue &value : m_answer.value(QStringLiteral("children")).toArray()) {
        const QJsonObject child = value.toObject();
        const QJsonObject childFields = child.value(QStringLiteral("fields")).toObject();
        const QString key = child.value(QStringLiteral("key")).toString();
        issues << LinkedIssue{key, childFields.value(QStringLiteral("summary")).toString(), statusPill(childFields),
                              issueAddress(key), true};
    }
    return issues;
}

int IssueModel::moreChildren() const
{
    const int more = m_answer.value(QStringLiteral("childrenTotal")).toInt()
        - int(m_answer.value(QStringLiteral("children")).toArray().size());
    return std::max(more, 0);
}

QUrl IssueModel::pageAddress() const
{
    return issueAddress(key());
}

void IssueModel::openPage()
{
    jira::openPage(*m_context, pageAddress());
}

void IssueModel::follow(ItemStatusSource *source)
{
    if (!source)
        return;
    source->subscribe(this, [this](const ItemStatus &status) { showStatus(status); });
    showStatus(source->current());
}

void IssueModel::attach(QWidget *view)
{
    setParent(view);
}

void IssueModel::showStatus(const ItemStatus &status)
{
    const QJsonObject issue = status.data.value(QStringLiteral("issue")).toObject();
    if (issue.isEmpty()) {
        const QString message =
            status.message.isEmpty() ? QCoreApplication::translate("jira", "Loading…") : status.message;
        if (m_issue.isEmpty() && message == m_message)
            return;
        m_answer = {};
        m_issue = {};
        m_message = message;
        emit changed();
        return;
    }
    // An error keeps the old data, and the cell header shows the error. A poll
    // usually brings the same answer, and drawing would lose a selection in the
    // text.
    if (status.data == m_answer)
        return;
    m_answer = status.data;
    m_issue = issue;
    m_message.clear();
    emit changed();
}

QUrl IssueModel::issueAddress(const QString &key) const
{
    if (m_site.isEmpty() || key.isEmpty())
        return {};
    const QUrl url = issueUrl(m_site, QJsonObject{{QStringLiteral("key"), key}});
    return isWebAddress(url) ? url : QUrl();
}

QWidget *createIssueView(ItemContext &context, QWidget *parent)
{
    auto *model = new IssueModel(context);
    model->follow(context.status());
    QWidget *view = makeIssueLayout(*model, parent);
    model->attach(view);
    return view;
}

widgets::StatusRole statusCategoryRole(const QString &category)
{
    if (category == QLatin1String("done"))
        return widgets::StatusRole::Good;
    if (category == QLatin1String("indeterminate"))
        return widgets::StatusRole::Waiting;
    return widgets::StatusRole::Neutral;
}

QDateTime readJiraDate(const QString &text)
{
    return QDateTime::fromString(text, Qt::ISODateWithMs);
}

QString shortJiraDate(const QString &text)
{
    const QDateTime date = readJiraDate(text);
    return date.isValid() ? QLocale().toString(date.toLocalTime(), QLocale::ShortFormat) : text;
}

void openPage(ItemContext &context, const QUrl &url)
{
    if (url.isEmpty())
        return;
    if (QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier))
        QDesktopServices::openUrl(url);
    else
        context.showPage(url);
}

} // namespace mirante::modules::jira
