#include "JiraListView.h"

#include "JiraFields.h"
#include "JiraIssueView.h"
#include "JiraListLayout.h"
#include "JiraLists.h"
#include "JiraSettings.h"
#include "JiraUrls.h"

#include <mirante/ItemContext.h>

#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QWidget>

#include <optional>

namespace mirante::modules::jira {

namespace {

// Sorts OPS-9 before OPS-10.
QString keySortKey(const QString &key)
{
    static const QRegularExpression pattern(QStringLiteral("^(.*-)(\\d+)$"));
    const QRegularExpressionMatch match = pattern.match(key);
    if (!match.hasMatch())
        return key;
    return match.captured(1) + match.captured(2).rightJustified(12, QLatin1Char('0'));
}

} // namespace

ListModel::ListModel(ItemContext &context)
    : m_context(&context)
    , m_extraFields(extraFieldsOf(context.moduleSettings(kGeneralScreen)))
    , m_message(QCoreApplication::translate("jira", "Loading…"))
{
    for (int i = 0; i < m_extraFields.size(); ++i)
        m_found << false;
}

ListModel::~ListModel() = default;

QJsonObject ListModel::config() const
{
    return m_context->config();
}

QString ListModel::description() const
{
    const QJsonObject config = this->config();
    return listDescription(config.value(QStringLiteral("preset")).toString(),
                           config.value(QStringLiteral("jql")).toString(),
                           m_context->projectSetting(QStringLiteral("projectKey")).toString());
}

QUrl ListModel::pageAddress() const
{
    const QJsonObject config = this->config();
    const std::optional<QString> jql =
        listJql(config.value(QStringLiteral("preset")).toString(), config.value(QStringLiteral("jql")).toString(),
                m_context->projectSetting(QStringLiteral("projectKey")).toString());
    return jql ? listWebUrl(site(), *jql) : QUrl();
}

int ListModel::pageSize() const
{
    return listPageSize(config());
}

QStringList ListModel::filterDefault(const QString &column) const
{
    QStringList values;
    for (const QJsonValue &value : config().value(column + QStringLiteral("Filter")).toArray()) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty())
            values << text;
    }
    return values;
}

void ListModel::keepFilter(const QString &column, const QStringList &values)
{
    const QJsonObject current = config();
    QJsonObject next = current;
    const QString key = column + QStringLiteral("Filter");
    if (values.isEmpty())
        next.remove(key);
    else
        next.insert(key, QJsonArray::fromStringList(values));
    if (next != current)
        m_context->updateConfig(next);
}

ListModel::Issue ListModel::issue(const QString &key) const
{
    for (const Issue &issue : m_issues) {
        if (issue.key == key)
            return issue;
    }
    return {};
}

void ListModel::openWeb(const QString &key)
{
    const QUrl url = issueAddress(key);
    if (!url.isEmpty())
        QDesktopServices::openUrl(url);
}

void ListModel::openPage(const QString &key)
{
    jira::openPage(*m_context, issueAddress(key));
}

void ListModel::addToTask(const QString &key)
{
    if (!key.isEmpty())
        m_context->addItemToTask(QStringLiteral("jira.issue.summary"), key, QJsonObject{{QStringLiteral("key"), key}});
}

void ListModel::copyLink(const QString &key)
{
    const QUrl url = issueAddress(key);
    if (!url.isEmpty())
        QGuiApplication::clipboard()->setText(url.toString());
}

void ListModel::follow(ItemStatusSource *source)
{
    if (!source)
        return;
    source->subscribe(this, [this](const ItemStatus &status) { showStatus(status); });
    showStatus(source->current());
}

void ListModel::attach(QWidget *view)
{
    setParent(view);
}

void ListModel::showStatus(const ItemStatus &status)
{
    // An error keeps the old rows, and the cell header shows the error.
    if (!status.data.contains(QStringLiteral("rows"))) {
        m_hasData = false;
        m_message = status.message.isEmpty() ? QCoreApplication::translate("jira", "Loading…") : status.message;
        emit changed();
        return;
    }
    m_hasData = true;
    m_message.clear();

    const QJsonObject names = status.data.value(QStringLiteral("names")).toObject();
    QStringList ids;
    m_found.clear();
    for (const QString &name : std::as_const(m_extraFields)) {
        ids << fieldIdByName(names, name);
        m_found << !ids.last().isEmpty();
    }

    m_issues.clear();
    for (const QJsonValue &value : status.data.value(QStringLiteral("rows")).toArray()) {
        const QJsonObject row = value.toObject();
        const QJsonObject fields = row.value(QStringLiteral("fields")).toObject();
        const QJsonObject statusObject = fields.value(QStringLiteral("status")).toObject();
        const QString category =
            statusObject.value(QStringLiteral("statusCategory")).toObject().value(QStringLiteral("key")).toString();

        Issue issue;
        issue.key = row.value(QStringLiteral("key")).toString();
        issue.keySortKey = keySortKey(issue.key);
        issue.data = row;
        issue.summary = fields.value(QStringLiteral("summary")).toString();
        issue.status = {statusObject.value(QStringLiteral("name")).toString(), statusCategoryRole(category)};
        issue.assignee =
            fields.value(QStringLiteral("assignee")).toObject().value(QStringLiteral("displayName")).toString();
        issue.updatedText = fields.value(QStringLiteral("updated")).toString();
        issue.updated = readJiraDate(issue.updatedText);
        for (const QString &id : std::as_const(ids)) {
            const QJsonValue field = fields.value(id);
            issue.fields << FieldValue{fieldText(field), field.isDouble() ? QVariant(field.toDouble()) : QVariant()};
        }
        issue.address = issueAddress(issue.key);
        m_issues << issue;
    }
    emit changed();
}

QUrl ListModel::site() const
{
    return baseUrlOf(m_context->moduleSettings(kGeneralScreen));
}

QUrl ListModel::issueAddress(const QString &key) const
{
    const QUrl base = site();
    return base.isEmpty() || key.isEmpty() ? QUrl() : issueUrl(base, QJsonObject{{QStringLiteral("key"), key}});
}

QWidget *createListView(ItemContext &context, QWidget *parent)
{
    auto *model = new ListModel(context);
    model->follow(context.status());
    QWidget *view = makeListLayout(*model, parent);
    model->attach(view);
    return view;
}

} // namespace mirante::modules::jira
