// The claude module: Claude sessions as items. See README.md.
#include "ClaudeUrls.h"
#include "LocalSessionView.h"

#include <mirante/Fields.h>
#include <mirante/ItemContext.h>
#include <mirante/ItemType.h>
#include <mirante/Module.h>

#include <QCoreApplication>

#include <memory>
#include <utility>
#include <vector>

namespace mirante::modules::claude {

namespace {

// A type with an id, a name, an icon and its configuration fields. Native
// views arrive later.
class StubItemType : public ItemType
{
public:
    StubItemType(const QString &id, const QString &name, const QString &shortName, const QString &description,
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
class WebSessionItemType : public StubItemType
{
public:
    using StubItemType::StubItemType;

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

// A session on one device, resumed in a terminal.
class LocalSessionItemType : public StubItemType
{
public:
    using StubItemType::StubItemType;

    QWidget *createView(ItemContext &context, QWidget *parent) const override
    {
        return createLocalSessionView(context, parent);
    }
};

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

std::vector<Field> localFields()
{
    Field session;
    session.key = QStringLiteral("sessionId");
    session.label = QCoreApplication::translate("claude", "Session id");
    session.required = true;

    // Empty means the project's main folder.
    Field folder;
    folder.key = QStringLiteral("folder");
    folder.label = QCoreApplication::translate("claude", "Folder");
    folder.type = Field::Type::Folder;

    Field device;
    device.key = QStringLiteral("device");
    device.label = QCoreApplication::translate("claude", "Device");
    device.placeholder = QCoreApplication::translate("claude", "The device the session lives on");
    return {session, folder, device};
}

} // namespace

void registerModule(ModuleRegistrar &registrar)
{
    registrar.setModuleName(QStringLiteral("Claude"));
    registrar.setModuleDescription(
        QCoreApplication::translate("claude", "Claude sessions, on claude.ai or on one of your devices."));
    registrar.addItemType(std::make_unique<WebSessionItemType>(
        QStringLiteral("claude.session.web"), QCoreApplication::translate("claude", "Claude session (web)"),
        QCoreApplication::translate("claude", "Claude"),
        QCoreApplication::translate("claude", "A Claude session on claude.ai, such as a Remote Control or cloud "
                                              "session, in a web view."),
        webFields()));
    registrar.addItemType(std::make_unique<LocalSessionItemType>(
        QStringLiteral("claude.session.local"), QCoreApplication::translate("claude", "Claude session (local)"),
        QCoreApplication::translate("claude", "Claude"),
        QCoreApplication::translate("claude", "A Claude Code session on one device, with a button that resumes it "
                                              "in a terminal in the project's folder."),
        localFields()));

    // The program that runs Claude Code, kept on each device. See decision 0079.
    Field program;
    program.key = QStringLiteral("program");
    program.label = QCoreApplication::translate("claude", "Claude Code program");
    program.defaultValue = QStringLiteral("claude");
    program.placeholder = QStringLiteral("claude");
    registrar.addDeviceSetting(program);
}

} // namespace mirante::modules::claude
