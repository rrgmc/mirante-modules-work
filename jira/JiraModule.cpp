// The jira module: Jira issues and issue lists as items. See README.md.
#include "JiraUrls.h"

#include <mirante/Fields.h>
#include <mirante/ItemContext.h>
#include <mirante/ItemType.h>
#include <mirante/Module.h>

#include <QCoreApplication>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace mirante::modules::jira {

namespace {

// A type with an id, a name, an icon and its configuration fields. All Jira
// types use a Jira Cloud connection, or else a Jira Data Center one. Native
// views arrive later.
class StubItemType : public ItemType
{
public:
    StubItemType(const QString &id, const QString &name, const QString &shortName, std::vector<Field> fields)
        : m_id(id)
        , m_name(name)
        , m_shortName(shortName)
        , m_icon(QStringLiteral(":/modules/jira/icons/jira.svg"))
        , m_fields(std::move(fields))
    {
    }

    QString id() const override { return m_id; }
    QString name() const override { return m_name; }
    QIcon icon() const override { return m_icon; }
    QString shortName() const override { return m_shortName; }
    std::vector<Field> fields() const override { return m_fields; }
    Rendering rendering() const override { return Rendering::Native; }

    QStringList connectionKinds() const override
    {
        return {QStringLiteral("jira.cloud"), QStringLiteral("jira.dc")};
    }

    std::optional<UrlMatch> fromUrl(const QUrl &url, const QList<QUrl> &baseUrls) const override
    {
        return matchUrl(m_id, url, baseUrls);
    }

private:
    QString m_id;
    QString m_name;
    QString m_shortName;
    QIcon m_icon;
    std::vector<Field> m_fields;
};

// The issue page: a web view of the issue on the item's Jira connection, or a
// message when the connection or the key is missing.
class PageItemType : public StubItemType
{
public:
    using StubItemType::StubItemType;

    Rendering rendering() const override { return Rendering::Web; }

    QWidget *createView(ItemContext &context, QWidget *parent) const override
    {
        const std::optional<ConnectionDetails> connection = context.connection(connectionKinds());
        if (!connection || !connection->baseUrl.isValid() || connection->baseUrl.host().isEmpty()) {
            return context.createMessageView(
                QCoreApplication::translate("jira", "No Jira connection in this group."), parent);
        }
        const QUrl url = issueUrl(connection->baseUrl, context.config());
        if (url.isEmpty()) {
            return context.createMessageView(
                QCoreApplication::translate("jira", "Set the issue key in the item's settings."), parent);
        }
        return context.createWebView(url, parent);
    }
};

std::vector<Field> issueFields()
{
    Field key;
    key.key = QStringLiteral("key");
    key.label = QCoreApplication::translate("jira", "Issue key");
    key.required = true;
    key.placeholder = QStringLiteral("PROJ-123");
    return {key};
}

// A preset, or "jql" for the query in the jql field.
std::vector<Field> listFields()
{
    Field preset;
    preset.key = QStringLiteral("preset");
    preset.label = QCoreApplication::translate("jira", "Show");
    preset.type = Field::Type::Choice;
    preset.required = true;
    preset.choices = {QStringLiteral("myOpen"), QStringLiteral("currentSprint"), QStringLiteral("recentlyUpdated"),
                      QStringLiteral("jql")};
    preset.choiceLabels = {QCoreApplication::translate("jira", "My open issues"),
                           QCoreApplication::translate("jira", "Current sprint of this project"),
                           QCoreApplication::translate("jira", "Recently updated in this project"),
                           QCoreApplication::translate("jira", "The JQL query below")};
    preset.defaultValue = preset.choices.first();

    Field jql;
    jql.key = QStringLiteral("jql");
    jql.label = QCoreApplication::translate("jira", "JQL query");
    jql.placeholder = QStringLiteral("assignee = currentUser() AND resolution = Unresolved");
    return {preset, jql};
}

} // namespace

void registerModule(ModuleRegistrar &registrar)
{
    registrar.addItemType(std::make_unique<StubItemType>(
        QStringLiteral("jira.issue.summary"), QCoreApplication::translate("jira", "Jira Issue (summary)"),
        QCoreApplication::translate("jira", "Jira"), issueFields()));
    registrar.addItemType(std::make_unique<PageItemType>(
        QStringLiteral("jira.issue.page"), QCoreApplication::translate("jira", "Jira Issue (page)"),
        QCoreApplication::translate("jira", "Jira"), issueFields()));
    registrar.addItemType(std::make_unique<StubItemType>(
        QStringLiteral("jira.list"), QCoreApplication::translate("jira", "Jira List"),
        QCoreApplication::translate("jira", "Jira"), listFields()));
}

} // namespace mirante::modules::jira
