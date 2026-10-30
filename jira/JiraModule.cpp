// The jira module: Jira issues and issue lists as items. See README.md.
#include "JiraQuickAdd.h"
#include "JiraIssueView.h"
#include "JiraListView.h"
#include "JiraLists.h"
#include "JiraServerType.h"
#include "JiraSettings.h"
#include "JiraStatus.h"
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

// Fetches one item's status, for ItemType::fetchStatus().
using FetchStatus = void (*)(StatusJob &job);
// Makes one placement's view, for ItemType::createView().
using CreateView = QWidget *(*)(ItemContext &context, QWidget *parent);

// A type with an id, a name, an icon and its configuration fields. All Jira
// types talk to the site in the General Configuration of the item's profile.
// A type with a fetch reports status, and one with a view factory shows that
// view.
class JiraItemType : public ItemType
{
public:
    JiraItemType(const QString &id, const QString &name, const QString &shortName, const QString &description,
                 std::vector<Field> fields, const ModuleSettings &settings, FetchStatus fetch = nullptr,
                 CreateView view = nullptr)
        : m_id(id)
        , m_name(name)
        , m_shortName(shortName)
        , m_description(description)
        , m_icon(QStringLiteral(":/modules/jira/icons/jira.svg"))
        , m_fields(std::move(fields))
        , m_settings(settings)
        , m_fetch(fetch)
        , m_view(view)
    {
    }

    QString id() const override { return m_id; }
    QString name() const override { return m_name; }
    QIcon icon() const override { return m_icon; }
    QString shortName() const override { return m_shortName; }
    QString description() const override { return m_description; }
    std::vector<Field> fields() const override { return m_fields; }
    Rendering rendering() const override { return Rendering::Native; }

    bool hasStatus() const override { return m_fetch != nullptr; }
    void fetchStatus(StatusJob &job) const override
    {
        if (m_fetch)
            m_fetch(job);
        else
            job.finish(ItemStatus());
    }

    QWidget *createView(ItemContext &context, QWidget *parent) const override
    {
        return m_view ? m_view(context, parent) : nullptr;
    }

    // An address on the profile's site, or on any site whose addresses look
    // like Jira's.
    std::optional<UrlMatch> fromUrl(const QUrl &url, const QString &profileId) const override
    {
        QList<QUrl> baseUrls;
        const QUrl site = baseUrlOf(m_settings.values(profileId, kGeneralScreen));
        if (!site.isEmpty())
            baseUrls << site;
        return matchUrl(m_id, url, baseUrls);
    }

private:
    QString m_id;
    QString m_name;
    QString m_shortName;
    QString m_description;
    QIcon m_icon;
    std::vector<Field> m_fields;
    const ModuleSettings &m_settings;
    FetchStatus m_fetch;
    CreateView m_view;
};

// The issue page: a web view of the issue on the Jira site of the item's
// profile, or a message when the site or the key is missing.
class PageItemType : public JiraItemType
{
public:
    using JiraItemType::JiraItemType;

    Rendering rendering() const override { return Rendering::Web; }

    QWidget *createView(ItemContext &context, QWidget *parent) const override
    {
        const QUrl site = baseUrlOf(context.moduleSettings(kGeneralScreen));
        if (site.isEmpty()) {
            return context.createMessageView(
                QCoreApplication::translate("jira", "Set the Jira base URL in this profile's Modules > Jira > "
                                                    "General Configuration, in the Profiles menu."),
                parent);
        }
        const QUrl url = issueUrl(site, context.config());
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

// A switch for one section of the summary, on by default. The view also takes
// it as on when the key is absent. See decision 0066.
Field sectionField(const QString &key, const QString &label)
{
    Field field;
    field.key = key;
    field.label = label;
    field.type = Field::Type::Bool;
    field.defaultValue = true;
    return field;
}

std::vector<Field> summaryFields()
{
    std::vector<Field> fields = issueFields();
    fields.push_back(sectionField(QStringLiteral("showDetails"),
                                  QCoreApplication::translate("jira", "Show the details")));
    fields.push_back(sectionField(QStringLiteral("showDescription"),
                                  QCoreApplication::translate("jira", "Show the description")));
    fields.push_back(sectionField(QStringLiteral("showComments"),
                                  QCoreApplication::translate("jira", "Show the comments")));
    fields.push_back(sectionField(QStringLiteral("showLinks"),
                                  QCoreApplication::translate("jira", "Show the sub-tasks and links")));
    return fields;
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

    Field limit;
    limit.key = QStringLiteral("limit");
    limit.label = QCoreApplication::translate("jira", "Rows to fetch");
    limit.type = Field::Type::Integer;
    limit.defaultValue = 100;

    Field pageSize;
    pageSize.key = QStringLiteral("pageSize");
    pageSize.label = QCoreApplication::translate("jira", "Rows per page");
    pageSize.type = Field::Type::Integer;
    pageSize.defaultValue = 50;
    return {preset, jql, limit, pageSize};
}

// The project's Jira project key, which the list presets of the project use.
Field projectKeyField()
{
    Field field;
    field.key = QStringLiteral("projectKey");
    field.label = QCoreApplication::translate("jira", "Jira project key");
    field.placeholder = QStringLiteral("PROJ");
    return field;
}

} // namespace

void registerModule(ModuleRegistrar &registrar)
{
    const ModuleSettings &settings = registrar.settings();
    registrar.setModuleName(QCoreApplication::translate("jira", "Jira"));
    registrar.setModuleDescription(QCoreApplication::translate(
        "jira", "Issues and lists of issues from a Jira site. Each profile sets its site and token in the module's "
                "General Configuration."));
    registrar.addSettingsScreen(makeGeneralScreen());
    registrar.setServerType(std::make_unique<JiraServerType>());
    registrar.addProjectSetting(projectKeyField());

    registrar.addItemType(std::make_unique<JiraItemType>(
        QStringLiteral("jira.issue.summary"), QCoreApplication::translate("jira", "Jira Issue (summary)"),
        QCoreApplication::translate("jira", "Jira"),
        QCoreApplication::translate("jira", "A summary of one issue: its status, people, sprint, description and "
                                            "latest comments."),
        summaryFields(), settings, &fetchIssueSummaryStatus, &createIssueView));
    registrar.addItemType(std::make_unique<PageItemType>(
        QStringLiteral("jira.issue.page"), QCoreApplication::translate("jira", "Jira Issue (page)"),
        QCoreApplication::translate("jira", "Jira"),
        QCoreApplication::translate("jira", "The web page of one issue, on the Jira site of the item's profile."),
        issueFields(), settings, &fetchIssueStatus));
    registrar.addItemType(std::make_unique<JiraItemType>(
        QStringLiteral("jira.list"), QCoreApplication::translate("jira", "Jira List"),
        QCoreApplication::translate("jira", "Jira"),
        QCoreApplication::translate("jira", "A table of issues from a preset, such as your open issues or the "
                                            "current sprint, or from a JQL query."),
        listFields(), settings, &fetchListStatus, &createListView));

    registrar.addQuickAddHandler(std::make_unique<JiraQuickAddHandler>());
    registrar.addRelatedItemFinder(std::make_unique<JiraRelatedItemFinder>());
}

} // namespace mirante::modules::jira
