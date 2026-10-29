// The claude module: Claude sessions as items. See README.md.
#include <mirante/Fields.h>
#include <mirante/ItemType.h>
#include <mirante/Module.h>

#include <QCoreApplication>

#include <memory>
#include <utility>
#include <vector>

namespace mirante::modules::claude {

namespace {

// A type with an id, a name, an icon and its configuration fields. Views arrive later.
class StubItemType : public ItemType
{
public:
    StubItemType(const QString &id, const QString &name, std::vector<Field> fields)
        : m_id(id)
        , m_name(name)
        , m_icon(QStringLiteral(":/modules/claude/icons/claude.svg"))
        , m_fields(std::move(fields))
    {
    }

    QString id() const override { return m_id; }
    QString name() const override { return m_name; }
    QIcon icon() const override { return m_icon; }
    std::vector<Field> fields() const override { return m_fields; }

private:
    QString m_id;
    QString m_name;
    QIcon m_icon;
    std::vector<Field> m_fields;
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
    registrar.addItemType(std::make_unique<StubItemType>(
        QStringLiteral("claude.session.web"), QCoreApplication::translate("claude", "Claude session (web)"),
        webFields()));
    registrar.addItemType(std::make_unique<StubItemType>(
        QStringLiteral("claude.session.local"), QCoreApplication::translate("claude", "Claude session (local)"),
        localFields()));
}

} // namespace mirante::modules::claude
