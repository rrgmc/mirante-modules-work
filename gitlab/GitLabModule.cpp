// The gitlab module: GitLab merge requests, pipelines and issues as items. See README.md.
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

// A type with an id, a name, an icon and its configuration fields. All GitLab
// types use a gitlab connection. Native views arrive later.
class StubItemType : public ItemType
{
public:
    StubItemType(const QString &id, const QString &name, const QString &shortName, std::vector<Field> fields)
        : m_id(id)
        , m_name(name)
        , m_shortName(shortName)
        , m_icon(QStringLiteral(":/modules/gitlab/icons/gitlab.svg"))
        , m_fields(std::move(fields))
    {
    }

    QString id() const override { return m_id; }
    QString name() const override { return m_name; }
    QIcon icon() const override { return m_icon; }
    QString shortName() const override { return m_shortName; }
    std::vector<Field> fields() const override { return m_fields; }
    Rendering rendering() const override { return Rendering::Native; }
    QStringList connectionKinds() const override { return {QStringLiteral("gitlab")}; }

private:
    QString m_id;
    QString m_name;
    QString m_shortName;
    QIcon m_icon;
    std::vector<Field> m_fields;
};

// Builds a page address from a connection's base URL, the item's configuration
// and the project's "project" setting.
using PageUrl = QUrl (*)(const QUrl &baseUrl, const QJsonObject &config, const QJsonValue &defaultProject);

// A merge request or issue page: a web view of the page on the item's GitLab
// connection, or a message when the connection or the configuration is missing.
class PageItemType : public StubItemType
{
public:
    PageItemType(const QString &id, const QString &name, const QString &shortName, std::vector<Field> fields,
                 PageUrl pageUrl)
        : StubItemType(id, name, shortName, std::move(fields))
        , m_pageUrl(pageUrl)
    {
    }

    Rendering rendering() const override { return Rendering::Web; }

    QWidget *createView(ItemContext &context, QWidget *parent) const override
    {
        const std::optional<ConnectionDetails> connection = context.connection(connectionKinds());
        if (!connection || !connection->baseUrl.isValid() || connection->baseUrl.host().isEmpty()) {
            return context.createMessageView(
                QCoreApplication::translate("gitlab", "No GitLab connection in this group."), parent);
        }
        const QUrl url = m_pageUrl(connection->baseUrl, context.config(),
                                   context.projectSetting(QStringLiteral("project")));
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

// Empty means the project's gitlab.project setting.
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

    registrar.addItemType(std::make_unique<StubItemType>(
        QStringLiteral("gitlab.mr.summary"), QCoreApplication::translate("gitlab", "GitLab MR (summary)"),
        QCoreApplication::translate("gitlab", "MR"), mergeRequest));
    registrar.addItemType(std::make_unique<PageItemType>(
        QStringLiteral("gitlab.mr.page"), QCoreApplication::translate("gitlab", "GitLab MR (page)"),
        QCoreApplication::translate("gitlab", "MR"), mergeRequest, &mergeRequestUrl));
    registrar.addItemType(std::make_unique<StubItemType>(
        QStringLiteral("gitlab.mr.list"), QCoreApplication::translate("gitlab", "GitLab MR List"),
        QCoreApplication::translate("gitlab", "MRs"),
        listFields({QStringLiteral("createdByMe"), QStringLiteral("reviewRequested"),
                    QStringLiteral("assignedToMe"), QStringLiteral("allOpen")},
                   {QCoreApplication::translate("gitlab", "Created by me"),
                    QCoreApplication::translate("gitlab", "Review requested from me"),
                    QCoreApplication::translate("gitlab", "Assigned to me"),
                    QCoreApplication::translate("gitlab", "All open")})));
    registrar.addItemType(std::make_unique<StubItemType>(
        QStringLiteral("gitlab.pipeline"), QCoreApplication::translate("gitlab", "GitLab CI Pipeline"),
        QCoreApplication::translate("gitlab", "CI"),
        pipelineFields()));
    registrar.addItemType(std::make_unique<StubItemType>(
        QStringLiteral("gitlab.issue.summary"), QCoreApplication::translate("gitlab", "GitLab Issue (summary)"),
        QCoreApplication::translate("gitlab", "Issue"), issue));
    registrar.addItemType(std::make_unique<PageItemType>(
        QStringLiteral("gitlab.issue.page"), QCoreApplication::translate("gitlab", "GitLab Issue (page)"),
        QCoreApplication::translate("gitlab", "Issue"), issue, &issueUrl));
    registrar.addItemType(std::make_unique<StubItemType>(
        QStringLiteral("gitlab.issue.list"), QCoreApplication::translate("gitlab", "GitLab Issue List"),
        QCoreApplication::translate("gitlab", "Issues"),
        listFields({QStringLiteral("assignedToMe"), QStringLiteral("createdByMe"), QStringLiteral("allOpen")},
                   {QCoreApplication::translate("gitlab", "Assigned to me"),
                    QCoreApplication::translate("gitlab", "Created by me"),
                    QCoreApplication::translate("gitlab", "All open")})));
}

} // namespace mirante::modules::gitlab
