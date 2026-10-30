// The gitlab module: GitLab merge requests, pipelines and issues as items. See README.md.
#include "GitLabIssueListView.h"
#include "GitLabIssueView.h"
#include "GitLabMergeRequestListView.h"
#include "GitLabMergeRequestView.h"
#include "GitLabPipelineView.h"
#include "GitLabSettings.h"
#include "GitLabStatus.h"
#include "GitLabUrls.h"

#include <mirante/Fields.h>
#include <mirante/ItemContext.h>
#include <mirante/ItemType.h>
#include <mirante/Module.h>

#include <QCoreApplication>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace mirante::modules::gitlab {

namespace {

// Fetches one item's status, for ItemType::fetchStatus(). See GitLabStatus.h.
using FetchStatus = void (*)(StatusJob &job);
// Makes one placement's view, for ItemType::createView().
using CreateView = QWidget *(*)(ItemContext &context, QWidget *parent);

// A type with an id, a name, an icon and its configuration fields. All GitLab
// types talk to the server in the General Configuration of the item's
// profile. The "project" field offers the paths of that screen. A type with a
// fetch reports status, and one with a view factory shows that view.
class GitLabItemType : public ItemType
{
public:
    GitLabItemType(const QString &id, const QString &name, const QString &shortName, const QString &description,
                   std::vector<Field> fields, const ModuleSettings &settings, FetchStatus fetch = nullptr,
                   CreateView view = nullptr)
        : m_id(id)
        , m_name(name)
        , m_shortName(shortName)
        , m_description(description)
        , m_icon(QStringLiteral(":/modules/gitlab/icons/gitlab.svg"))
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
    std::vector<Field> fieldsForProfile(const QString &profileId) const override
    {
        return withProjectPaths(m_fields, m_settings, profileId);
    }
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

    // An address on the profile's server, or on any server whose addresses
    // look like GitLab's.
    std::optional<UrlMatch> fromUrl(const QUrl &url, const QString &profileId) const override
    {
        return matchUrl(m_id, url, {baseUrlOf(m_settings.values(profileId, kGeneralScreen))});
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

// Builds a page address from the server's base URL, the item's configuration
// and the project's "project" setting.
using PageUrl = QUrl (*)(const QUrl &baseUrl, const QJsonObject &config, const QJsonValue &defaultProject);

// A merge request or issue page: a web view of the page on the GitLab server
// of the item's profile, or a message when the server or the configuration is
// missing.
class PageItemType : public GitLabItemType
{
public:
    PageItemType(const QString &id, const QString &name, const QString &shortName, const QString &description,
                 std::vector<Field> fields, const ModuleSettings &settings, PageUrl pageUrl, FetchStatus fetch)
        : GitLabItemType(id, name, shortName, description, std::move(fields), settings, fetch)
        , m_pageUrl(pageUrl)
    {
    }

    Rendering rendering() const override { return Rendering::Web; }

    QWidget *createView(ItemContext &context, QWidget *parent) const override
    {
        const QUrl baseUrl = baseUrlOf(context.moduleSettings(kGeneralScreen));
        if (!baseUrl.isValid() || baseUrl.host().isEmpty()) {
            return context.createMessageView(
                QCoreApplication::translate("gitlab", "Set the GitLab base URL in this profile's Modules > GitLab > "
                                                      "General Configuration, in the Profiles menu."),
                parent);
        }
        const QUrl url = m_pageUrl(baseUrl, context.config(), context.projectSetting(kProjectSettingKey));
        if (url.isEmpty()) {
            return context.createMessageView(
                QCoreApplication::translate("gitlab", "Set the project and number in the item's settings."), parent);
        }
        return context.createWebView(url, parent);
    }

private:
    PageUrl m_pageUrl;
};

Field makeField(const QString &key, const QString &label, Field::Type type, bool required = false,
                const QString &placeholder = {})
{
    Field field;
    field.key = key;
    field.label = label;
    field.type = type;
    field.required = required;
    field.placeholder = placeholder;
    return field;
}

// A required choice whose default is the first choice.
Field choiceField(const QString &key, const QString &label, const QStringList &choices,
                  const QStringList &choiceLabels)
{
    Field field = makeField(key, label, Field::Type::Choice, true);
    field.choices = choices;
    field.choiceLabels = choiceLabels;
    field.defaultValue = choices.first();
    return field;
}

// A switch for one section of a native view, on by default. A view also takes
// it as on when the key is absent. See decision 0066.
Field sectionField(const QString &key, const QString &label)
{
    Field field = makeField(key, label, Field::Type::Bool);
    field.defaultValue = true;
    return field;
}

// base with the switches of a type's sections after it.
std::vector<Field> withSections(std::vector<Field> base, const std::vector<Field> &sections)
{
    base.insert(base.end(), sections.begin(), sections.end());
    return base;
}

// Empty means the project's gitlab.project setting. GitLabItemType::fieldsForProfile()
// adds the configured project paths as suggestions.
Field projectField()
{
    return makeField(QStringLiteral("project"), QCoreApplication::translate("gitlab", "Project path"),
                     Field::Type::Text, false, QStringLiteral("group/project"));
}

// A merge request or an issue: its project and its number in that project.
std::vector<Field> singleFields(const QString &numberLabel)
{
    return {projectField(), makeField(QStringLiteral("iid"), numberLabel, Field::Type::Integer, true)};
}

// A list: its scope, a preset and an optional custom filter.
std::vector<Field> listFields(const QStringList &presets, const QStringList &presetLabels)
{
    return {
        choiceField(QStringLiteral("scope"), QCoreApplication::translate("gitlab", "Scope"),
                    {QStringLiteral("project"), QStringLiteral("group"), QStringLiteral("all")},
                    {QCoreApplication::translate("gitlab", "One project"),
                     QCoreApplication::translate("gitlab", "One group"),
                     QCoreApplication::translate("gitlab", "All projects visible to me")}),
        projectField(),
        makeField(QStringLiteral("group"), QCoreApplication::translate("gitlab", "Group path"), Field::Type::Text,
                  false, QStringLiteral("group")),
        choiceField(QStringLiteral("preset"), QCoreApplication::translate("gitlab", "Show"), presets, presetLabels),
        makeField(QStringLiteral("filter"), QCoreApplication::translate("gitlab", "Custom filter"),
                  Field::Type::Text, false, QStringLiteral("labels=bug&target_branch=main")),
    };
}

std::vector<Field> pipelineFields()
{
    return {
        projectField(),
        choiceField(QStringLiteral("branchSource"), QCoreApplication::translate("gitlab", "Branch from"),
                    {QStringLiteral("fixed"), QStringLiteral("mainFolder"), QStringLiteral("mergeRequest")},
                    {QCoreApplication::translate("gitlab", "A fixed branch"),
                     QCoreApplication::translate("gitlab", "Current branch of the main folder"),
                     QCoreApplication::translate("gitlab", "Branch of a linked merge request")}),
        makeField(QStringLiteral("branch"), QCoreApplication::translate("gitlab", "Branch"), Field::Type::Text,
                  false, QStringLiteral("main")),
        makeField(QStringLiteral("iid"), QCoreApplication::translate("gitlab", "Merge request number"),
                  Field::Type::Integer),
    };
}

} // namespace

void registerModule(ModuleRegistrar &registrar)
{
    const std::vector<Field> mergeRequest =
        singleFields(QCoreApplication::translate("gitlab", "Merge request number"));
    const std::vector<Field> issue = singleFields(QCoreApplication::translate("gitlab", "Issue number"));
    const Field showPipeline =
        sectionField(QStringLiteral("showPipeline"), QCoreApplication::translate("gitlab", "Show the pipeline"));
    const Field showApprovals =
        sectionField(QStringLiteral("showApprovals"), QCoreApplication::translate("gitlab", "Show the approvals"));
    const Field showLabels =
        sectionField(QStringLiteral("showLabels"), QCoreApplication::translate("gitlab", "Show the labels"));
    const ModuleSettings &settings = registrar.settings();

    registrar.setModuleName(QStringLiteral("GitLab"));
    registrar.setModuleDescription(QCoreApplication::translate(
        "gitlab", "Merge requests, pipelines and issues from a GitLab server. Each profile sets its server and "
                  "token in the module's General Configuration."));
    registrar.addSettingsScreen(makeGeneralScreen());
    registrar.setServerType(std::make_unique<GitLabServerType>());
    registrar.addProjectSetting(makeProjectSetting());

    registrar.addItemType(std::make_unique<GitLabItemType>(
        QStringLiteral("gitlab.mr.summary"), QCoreApplication::translate("gitlab", "GitLab MR (summary)"),
        QCoreApplication::translate("gitlab", "MR"),
        QCoreApplication::translate("gitlab", "A summary of one merge request: its state, branches, people, "
                                              "pipeline and approvals."),
        withSections(mergeRequest,
                     {showPipeline, showApprovals,
                      sectionField(QStringLiteral("showPeople"),
                                   QCoreApplication::translate("gitlab", "Show the people")),
                      showLabels}),
        settings, &fetchMergeRequestSummaryStatus, &createMergeRequestView));
    registrar.addItemType(std::make_unique<PageItemType>(
        QStringLiteral("gitlab.mr.page"), QCoreApplication::translate("gitlab", "GitLab MR (page)"),
        QCoreApplication::translate("gitlab", "MR"),
        QCoreApplication::translate("gitlab", "The web page of one merge request, on the GitLab server of the "
                                              "item's profile."),
        mergeRequest, settings, &mergeRequestUrl, &fetchMergeRequestStatus));
    registrar.addItemType(std::make_unique<GitLabItemType>(
        QStringLiteral("gitlab.mr.list"), QCoreApplication::translate("gitlab", "GitLab MR List"),
        QCoreApplication::translate("gitlab", "MRs"),
        QCoreApplication::translate("gitlab", "A table of merge requests in a project, a group or everywhere, such "
                                              "as those that wait for your review."),
        withSections(listFields({QStringLiteral("createdByMe"), QStringLiteral("reviewRequested"),
                                 QStringLiteral("assignedToMe"), QStringLiteral("allOpen")},
                                {QCoreApplication::translate("gitlab", "Created by me"),
                                 QCoreApplication::translate("gitlab", "Review requested from me"),
                                 QCoreApplication::translate("gitlab", "Assigned to me"),
                                 QCoreApplication::translate("gitlab", "All open")}),
                     {showPipeline, showApprovals}),
        settings, nullptr, &createMergeRequestListView));
    registrar.addItemType(std::make_unique<GitLabItemType>(
        QStringLiteral("gitlab.pipeline"), QCoreApplication::translate("gitlab", "GitLab CI Pipeline"),
        QCoreApplication::translate("gitlab", "CI"),
        QCoreApplication::translate("gitlab", "The latest pipeline of a branch, with its stages and jobs. The branch "
                                              "is a fixed one, the main folder's current branch or a merge "
                                              "request's branch."),
        pipelineFields(), settings, &fetchPipelineStatus, &createPipelineView));
    registrar.addItemType(std::make_unique<GitLabItemType>(
        QStringLiteral("gitlab.issue.summary"), QCoreApplication::translate("gitlab", "GitLab Issue (summary)"),
        QCoreApplication::translate("gitlab", "Issue"),
        QCoreApplication::translate("gitlab", "A summary of one issue: its state, assignees, labels, milestone and "
                                              "latest comments."),
        withSections(issue,
                     {sectionField(QStringLiteral("showDetails"),
                                   QCoreApplication::translate("gitlab", "Show the details")),
                      showLabels,
                      sectionField(QStringLiteral("showComments"),
                                   QCoreApplication::translate("gitlab", "Show the comments"))}),
        settings, &fetchIssueSummaryStatus, &createIssueView));
    registrar.addItemType(std::make_unique<PageItemType>(
        QStringLiteral("gitlab.issue.page"), QCoreApplication::translate("gitlab", "GitLab Issue (page)"),
        QCoreApplication::translate("gitlab", "Issue"),
        QCoreApplication::translate("gitlab", "The web page of one issue, on the GitLab server of the item's "
                                              "profile."),
        issue, settings, &issueUrl, &fetchIssueStatus));
    registrar.addItemType(std::make_unique<GitLabItemType>(
        QStringLiteral("gitlab.issue.list"), QCoreApplication::translate("gitlab", "GitLab Issue List"),
        QCoreApplication::translate("gitlab", "Issues"),
        QCoreApplication::translate("gitlab", "A table of issues in a project, a group or everywhere, such as those "
                                              "assigned to you."),
        listFields({QStringLiteral("assignedToMe"), QStringLiteral("createdByMe"), QStringLiteral("allOpen")},
                   {QCoreApplication::translate("gitlab", "Assigned to me"),
                    QCoreApplication::translate("gitlab", "Created by me"),
                    QCoreApplication::translate("gitlab", "All open")}),
        settings, nullptr, &createIssueListView));
}

} // namespace mirante::modules::gitlab
