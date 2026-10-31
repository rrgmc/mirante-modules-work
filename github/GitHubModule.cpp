// The github module: GitHub pull requests, Actions runs and issues as items. See README.md.
#include "GitHubIssueListView.h"
#include "GitHubIssueView.h"
#include "GitHubLists.h"
#include "GitHubPullRequestListView.h"
#include "GitHubPullRequestView.h"
#include "GitHubQuickAdd.h"
#include "GitHubRunListView.h"
#include "GitHubRunView.h"
#include "GitHubSettings.h"
#include "GitHubStatus.h"
#include "GitHubUrls.h"

#include <mirante/Fields.h>
#include <mirante/ItemContext.h>
#include <mirante/ItemType.h>
#include <mirante/Module.h>

#include <QCoreApplication>
#include <QUrl>
#include <QUuid>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace mirante::modules::github {

namespace {

// Fetches one item's status, for ItemType::fetchStatus(). See GitHubStatus.h
// and GitHubLists.h.
using FetchStatus = void (*)(StatusJob &job);
// Makes one placement's view, for ItemType::createView().
using CreateView = QWidget *(*)(ItemContext &context, QWidget *parent);

// A type with an id, a name, an icon and its configuration fields. All GitHub
// types talk to the server in the General Configuration of the item's
// profile, report a status and take pasted links. The "repository" field
// offers the repositories of that screen.
class GitHubItemType : public ItemType
{
public:
    GitHubItemType(const QString &id, const QString &name, const QString &shortName, const QString &description,
                   std::vector<Field> fields, const ModuleSettings &settings, FetchStatus fetch,
                   CreateView view = nullptr)
        : m_id(id)
        , m_name(name)
        , m_shortName(shortName)
        , m_description(description)
        , m_icon(QStringLiteral(":/modules/github/icons/github.svg"))
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
        return withRepositories(m_fields, m_settings, profileId);
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

    // An address below the web base of the profile, or on github.com.
    std::optional<UrlMatch> fromUrl(const QUrl &url, const QString &profileId) const override
    {
        return matchUrl(m_id, url, {webBaseOf(m_settings.values(profileId, kGeneralScreen))});
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

// Builds a page address from the web base, the item's configuration and the
// project's "repository" setting.
using PageUrl = QUrl (*)(const QUrl &webBase, const QJsonObject &config, const QJsonValue &defaultRepository);

// A pull request or issue page: a web view of the page on the GitHub server
// of the item's profile, or a message when the server or the configuration is
// missing.
class PageItemType : public GitHubItemType
{
public:
    PageItemType(const QString &id, const QString &name, const QString &shortName, const QString &description,
                 std::vector<Field> fields, const ModuleSettings &settings, PageUrl pageUrl, FetchStatus fetch)
        : GitHubItemType(id, name, shortName, description, std::move(fields), settings, fetch)
        , m_pageUrl(pageUrl)
    {
    }

    Rendering rendering() const override { return Rendering::Web; }

    QWidget *createView(ItemContext &context, QWidget *parent) const override
    {
        const QUrl webBase = webBaseOf(context.moduleSettings(kGeneralScreen));
        if (!webBase.isValid() || webBase.host().isEmpty()) {
            return context.createMessageView(
                QCoreApplication::translate("github", "Set the GitHub base URL in this profile's Modules > GitHub > "
                                                      "General Configuration, in the Profiles menu."),
                parent);
        }
        const QUrl url = m_pageUrl(webBase, context.config(), context.projectSetting(kProjectSettingKey));
        if (url.isEmpty()) {
            return context.createMessageView(
                QCoreApplication::translate("github", "Set the repository and number in the item's settings."),
                parent);
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

// base with more fields after it.
std::vector<Field> withSections(std::vector<Field> base, const std::vector<Field> &sections)
{
    base.insert(base.end(), sections.begin(), sections.end());
    return base;
}

// Empty means the project's github.repository setting.
// GitHubItemType::fieldsForProfile() adds the configured repositories as
// suggestions.
Field repositoryField()
{
    return makeField(QStringLiteral("repository"), QCoreApplication::translate("github", "Repository"),
                     Field::Type::Text, false, QStringLiteral("owner/repo"));
}

// A pull request or an issue: its repository and its number in that repository.
std::vector<Field> singleFields(const QString &numberLabel)
{
    return {repositoryField(), makeField(QStringLiteral("number"), numberLabel, Field::Type::Integer, true)};
}

// The linked pull request of a run or a run list, used by some of their
// choices only, so not required.
Field linkedPullRequestField()
{
    return makeField(QStringLiteral("number"), QCoreApplication::translate("github", "Pull request number"),
                     Field::Type::Integer);
}

// How many rows a list fetches, and how many its view shows at a time. A view and
// a status also take the default when the key is absent. See decision 0071.
std::vector<Field> listSizeFields()
{
    Field limit = makeField(QStringLiteral("limit"), QCoreApplication::translate("github", "Rows to fetch"),
                            Field::Type::Integer);
    limit.defaultValue = kDefaultListLimit;
    Field pageSize = makeField(QStringLiteral("pageSize"), QCoreApplication::translate("github", "Rows per page"),
                               Field::Type::Integer);
    pageSize.defaultValue = kDefaultListPageSize;
    return {limit, pageSize};
}

// A pull request or issue list: its scope, a preset, an optional filter of
// search qualifiers and its sizes.
std::vector<Field> listFields(const QStringList &presets, const QStringList &presetLabels)
{
    std::vector<Field> fields{
        choiceField(QStringLiteral("scope"), QCoreApplication::translate("github", "Scope"),
                    {QStringLiteral("repository"), QStringLiteral("owner"), QStringLiteral("all")},
                    {QCoreApplication::translate("github", "One repository"),
                     QCoreApplication::translate("github", "One owner"),
                     QCoreApplication::translate("github", "All")}),
        repositoryField(),
        makeField(QStringLiteral("owner"), QCoreApplication::translate("github", "Owner"), Field::Type::Text, false,
                  QStringLiteral("owner")),
        choiceField(QStringLiteral("preset"), QCoreApplication::translate("github", "Show"), presets, presetLabels),
        makeField(QStringLiteral("filter"), QCoreApplication::translate("github", "Custom filter"),
                  Field::Type::Text, false, QStringLiteral("label:bug base:main")),
    };
    return withSections(std::move(fields), listSizeFields());
}

// Empty means the repository's default branch, which the placeholder says.
Field branchField()
{
    return makeField(QStringLiteral("branch"), QCoreApplication::translate("github", "Branch"), Field::Type::Text,
                     false, QCoreApplication::translate("github", "Default branch"));
}

// Empty means all workflows.
Field workflowField()
{
    return makeField(QStringLiteral("workflow"), QCoreApplication::translate("github", "Workflow file"),
                     Field::Type::Text, false, QStringLiteral("ci.yml"));
}

std::vector<Field> runFields()
{
    return {
        repositoryField(),
        choiceField(QStringLiteral("branchSource"), QCoreApplication::translate("github", "Branch from"),
                    {QStringLiteral("fixed"), QStringLiteral("mainFolder"), QStringLiteral("pullRequest")},
                    {QCoreApplication::translate("github", "Fixed branch"),
                     QCoreApplication::translate("github", "Current branch of the main folder"),
                     QCoreApplication::translate("github", "Branch of a linked pull request")}),
        branchField(),
        linkedPullRequestField(),
        workflowField(),
    };
}

std::vector<Field> runListFields()
{
    std::vector<Field> fields{
        repositoryField(),
        choiceField(QStringLiteral("source"), QCoreApplication::translate("github", "Show"),
                    {QStringLiteral("all"), QStringLiteral("mine"), QStringLiteral("branch"),
                     QStringLiteral("pullRequest")},
                    {QCoreApplication::translate("github", "All runs"),
                     QCoreApplication::translate("github", "Started by me"),
                     QCoreApplication::translate("github", "Of a branch"),
                     QCoreApplication::translate("github", "Of a pull request")}),
        branchField(),
        linkedPullRequestField(),
        workflowField(),
    };
    return withSections(std::move(fields), listSizeFields());
}

} // namespace

void registerModule(ModuleRegistrar &registrar)
{
    const std::vector<Field> pullRequest =
        singleFields(QCoreApplication::translate("github", "Pull request number"));
    const std::vector<Field> issue = singleFields(QCoreApplication::translate("github", "Issue number"));
    const Field showChecks =
        sectionField(QStringLiteral("showChecks"), QCoreApplication::translate("github", "Show the checks"));
    const Field showReviews =
        sectionField(QStringLiteral("showReviews"), QCoreApplication::translate("github", "Show the reviews"));
    const Field showLabels =
        sectionField(QStringLiteral("showLabels"), QCoreApplication::translate("github", "Show the labels"));
    const ModuleSettings &settings = registrar.settings();

    registrar.setModuleName(QCoreApplication::translate("github", "GitHub"));
    registrar.setModuleDescription(QCoreApplication::translate(
        "github", "Pull requests, Actions runs and issues from GitHub or GitHub Enterprise Server. Each profile sets "
                  "its server and token in the module's General Configuration."));
    registrar.setModuleUuid(QUuid(QStringLiteral("6be6a6eb-abdb-41f6-addb-14dd5f6cb935")));
    registrar.setModuleVersion(QStringLiteral("0.1.0"));
    registrar.setModuleAuthor(QStringLiteral("Rangel Reale"));
    registrar.setModuleHomepage(QUrl(QStringLiteral("https://github.com/rrgmc/mirante/tree/master/src/modules/github")));
    registrar.addSettingsScreen(makeGeneralScreen());
    registrar.setServerType(std::make_unique<GitHubServerType>());
    registrar.addProjectSetting(makeProjectSetting());

    registrar.addItemType(std::make_unique<GitHubItemType>(
        QStringLiteral("github.pr.summary"), QCoreApplication::translate("github", "GitHub PR (summary)"),
        QCoreApplication::translate("github", "PR"),
        QCoreApplication::translate("github", "A summary of one pull request: its state, branches, checks, reviews "
                                              "and people."),
        withSections(pullRequest,
                     {showChecks, showReviews,
                      sectionField(QStringLiteral("showPeople"),
                                   QCoreApplication::translate("github", "Show the people")),
                      showLabels}),
        settings, &fetchPullRequestStatus, &createPullRequestView));
    registrar.addItemType(std::make_unique<PageItemType>(
        QStringLiteral("github.pr.page"), QCoreApplication::translate("github", "GitHub PR (page)"),
        QCoreApplication::translate("github", "PR"),
        QCoreApplication::translate("github", "The web page of one pull request, on the GitHub server of the item's "
                                              "profile."),
        pullRequest, settings, &pullRequestUrl, &fetchPullRequestStatus));
    registrar.addItemType(std::make_unique<GitHubItemType>(
        QStringLiteral("github.pr.list"), QCoreApplication::translate("github", "GitHub PR List"),
        QCoreApplication::translate("github", "PRs"),
        QCoreApplication::translate("github", "A table of pull requests in a repository, an owner or everywhere, "
                                              "such as those that wait for your review."),
        withSections(listFields({QStringLiteral("createdByMe"), QStringLiteral("reviewRequested"),
                                 QStringLiteral("assignedToMe"), QStringLiteral("allOpen")},
                                {QCoreApplication::translate("github", "Created by me"),
                                 QCoreApplication::translate("github", "Review requested from me"),
                                 QCoreApplication::translate("github", "Assigned to me"),
                                 QCoreApplication::translate("github", "All open")}),
                     {showChecks, showReviews}),
        settings, &fetchPullRequestListStatus, &createPullRequestListView));
    registrar.addItemType(std::make_unique<GitHubItemType>(
        QStringLiteral("github.run"), QCoreApplication::translate("github", "GitHub Actions Run"),
        QCoreApplication::translate("github", "Run"),
        QCoreApplication::translate("github", "The Actions runs of a branch's newest commit, one per workflow, with "
                                              "their jobs. The branch is a fixed one, the main folder's current "
                                              "branch or a pull request's branch."),
        runFields(), settings, &fetchRunStatus, &createRunView));
    registrar.addItemType(std::make_unique<GitHubItemType>(
        QStringLiteral("github.run.list"), QCoreApplication::translate("github", "GitHub Actions Run List"),
        QCoreApplication::translate("github", "Runs"),
        QCoreApplication::translate("github", "A table of a repository's Actions runs: all of them, those you "
                                              "started, or those of a branch or a pull request."),
        runListFields(), settings, &fetchRunListStatus, &createRunListView));
    registrar.addItemType(std::make_unique<GitHubItemType>(
        QStringLiteral("github.issue.summary"), QCoreApplication::translate("github", "GitHub Issue (summary)"),
        QCoreApplication::translate("github", "Issue"),
        QCoreApplication::translate("github", "A summary of one issue: its state, assignees, labels, milestone and "
                                              "latest comments."),
        withSections(issue,
                     {sectionField(QStringLiteral("showDetails"),
                                   QCoreApplication::translate("github", "Show the details")),
                      showLabels,
                      sectionField(QStringLiteral("showComments"),
                                   QCoreApplication::translate("github", "Show the comments"))}),
        settings, &fetchIssueSummaryStatus, &createIssueView));
    registrar.addItemType(std::make_unique<PageItemType>(
        QStringLiteral("github.issue.page"), QCoreApplication::translate("github", "GitHub Issue (page)"),
        QCoreApplication::translate("github", "Issue"),
        QCoreApplication::translate("github", "The web page of one issue, on the GitHub server of the item's "
                                              "profile."),
        issue, settings, &issueUrl, &fetchIssueStatus));
    registrar.addItemType(std::make_unique<GitHubItemType>(
        QStringLiteral("github.issue.list"), QCoreApplication::translate("github", "GitHub Issue List"),
        QCoreApplication::translate("github", "Issues"),
        QCoreApplication::translate("github", "A table of issues in a repository, an owner or everywhere, such as "
                                              "those assigned to you."),
        listFields({QStringLiteral("assignedToMe"), QStringLiteral("createdByMe"), QStringLiteral("allOpen")},
                   {QCoreApplication::translate("github", "Assigned to me"),
                    QCoreApplication::translate("github", "Created by me"),
                    QCoreApplication::translate("github", "All open")}),
        settings, &fetchIssueListStatus, &createIssueListView));

    registrar.addQuickAddHandler(std::make_unique<GitHubQuickAddHandler>());
    registrar.addRelatedItemFinder(std::make_unique<GitHubRelatedItemFinder>());
}

} // namespace mirante::modules::github
