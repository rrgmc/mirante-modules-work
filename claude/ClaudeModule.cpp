// The claude module: Claude sessions as items. See README.md.
#include "ClaudeAgentsFetch.h"
#include "ClaudeAgentsView.h"
#include "ClaudeUrls.h"

#include <mirante/Fields.h>
#include <mirante/ItemContext.h>
#include <mirante/ItemType.h>
#include <mirante/Module.h>
#include <mirante/Status.h>

#include <QCoreApplication>
#include <QUrl>
#include <QUuid>

#include <memory>
#include <utility>
#include <vector>

namespace mirante::modules::claude {

namespace {

// A type with an id, a name, an icon and its configuration fields. The
// session type below adds its view.
class ClaudeItemType : public ItemType
{
public:
    ClaudeItemType(const QString &id, const QString &name, const QString &shortName, const QString &description,
                 std::vector<Field> fields)
        : m_id(id)
        , m_name(name)
        , m_shortName(shortName)
        , m_description(description)
        , m_icon(QStringLiteral(":/modules/claude/icons/claude.svg"))
        , m_fields(std::move(fields))
    {
    }

    QString id() const override { return m_id; }
    QString name() const override { return m_name; }
    QIcon icon() const override { return m_icon; }
    QString shortName() const override { return m_shortName; }
    QString description() const override { return m_description; }
    std::vector<Field> fields() const override { return m_fields; }
    Rendering rendering() const override { return Rendering::Native; }

private:
    QString m_id;
    QString m_name;
    QString m_shortName;
    QString m_description;
    QIcon m_icon;
    std::vector<Field> m_fields;
};

// A session on claude.ai: a web view of its address, or a message when the
// address is missing or is not an http or https address.
class WebSessionItemType : public ClaudeItemType
{
public:
    using ClaudeItemType::ClaudeItemType;

    Rendering rendering() const override { return Rendering::Web; }

    std::optional<UrlMatch> fromUrl(const QUrl &url, const QString &profileId) const override
    {
        Q_UNUSED(profileId)
        return matchUrl(url);
    }

    QWidget *createView(ItemContext &context, QWidget *parent) const override
    {
        const QUrl url = sessionUrl(context.config());
        if (url.isEmpty()) {
            return context.createMessageView(
                QCoreApplication::translate("claude", "Set the address in the item's settings."), parent);
        }
        return context.createWebView(url, parent);
    }
};

// The Claude Code sessions on this device, from `claude agents --json`, in the
// core's table. See decision 0143 in mirante-core.
class AgentsItemType : public ClaudeItemType
{
public:
    using ClaudeItemType::ClaudeItemType;

    bool hasStatus() const override { return true; }
    void fetchStatus(StatusJob &job) const override { fetchAgents(job); }

    QWidget *createView(ItemContext &context, QWidget *parent) const override
    {
        return createAgentsView(context, parent);
    }
};

std::vector<Field> agentsFields()
{
    Field folder;
    folder.key = QStringLiteral("folder");
    folder.label = QCoreApplication::translate("claude", "Folder");
    folder.type = Field::Type::Folder;
    folder.help = QCoreApplication::translate("claude", "Only sessions in this project folder. Empty lists all.");

    Field finished;
    finished.key = QStringLiteral("includeFinished");
    finished.label = QCoreApplication::translate("claude", "Show finished background sessions");
    finished.type = Field::Type::Bool;
    finished.defaultValue = false;

    Field accounts;
    accounts.key = QStringLiteral("accounts");
    accounts.label = QCoreApplication::translate("claude", "Accounts");
    accounts.type = Field::Type::TextList;
    accounts.placeholder = QStringLiteral("~/.claude-work");
    accounts.help = QCoreApplication::translate("claude", "Claude Code config folders on this device, one per "
                                                          "account. Empty uses the default account.");
    return {folder, finished, accounts};
}

std::vector<Field> webFields()
{
    Field url;
    url.key = QStringLiteral("url");
    url.label = QCoreApplication::translate("claude", "Address");
    url.type = Field::Type::Url;
    url.required = true;
    url.placeholder = QStringLiteral("https://claude.ai/");
    return {url};
}

} // namespace

void registerModule(ModuleRegistrar &registrar)
{
    registrar.setModuleName(QCoreApplication::translate("claude", "Claude"));
    registrar.setModuleDescription(
        QCoreApplication::translate("claude", "Claude sessions on claude.ai, and the Claude agents on this device."));
    registrar.setModuleUuid(QUuid(QStringLiteral("00caf22e-8d0f-431f-8832-17a09c048ac2")));
    registrar.setModuleVersion(QStringLiteral("0.1.0"));
    registrar.setModuleAuthor(QStringLiteral("Rangel Reale"));
    registrar.setModuleHomepage(QUrl(QStringLiteral("https://github.com/rrgmc/mirante/tree/master/src/modules/claude")));
    registrar.addItemType(std::make_unique<WebSessionItemType>(
        QStringLiteral("claude.session.web"), QCoreApplication::translate("claude", "Claude session (web)"),
        QCoreApplication::translate("claude", "Claude"),
        QCoreApplication::translate("claude", "A Claude session on claude.ai, such as a Remote Control or cloud "
                                              "session, in a web view."),
        webFields()));
    registrar.addItemType(std::make_unique<AgentsItemType>(
        QStringLiteral("claude.agents"), QCoreApplication::translate("claude", "Claude agents"),
        QCoreApplication::translate("claude", "Agents"),
        QCoreApplication::translate("claude", "The Claude Code sessions on this device, with their title, status "
                                              "and repository. No session text."),
        agentsFields()));
}

} // namespace mirante::modules::claude
