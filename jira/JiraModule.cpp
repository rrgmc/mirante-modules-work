// The jira module: Jira issues and issue lists as items. See README.md.
#include <mirante/ItemType.h>
#include <mirante/Module.h>

#include <QCoreApplication>

#include <memory>

namespace mirante::modules::jira {

namespace {

// Placeholder type for M2: only an id, a name and an icon. Views arrive later.
class StubItemType : public ItemType
{
public:
    StubItemType(const QString &id, const QString &name)
        : m_id(id)
        , m_name(name)
        , m_icon(QStringLiteral(":/modules/jira/icons/jira.svg"))
    {
    }

    QString id() const override { return m_id; }
    QString name() const override { return m_name; }
    QIcon icon() const override { return m_icon; }

private:
    QString m_id;
    QString m_name;
    QIcon m_icon;
};

} // namespace

void registerModule(ModuleRegistrar &registrar)
{
    registrar.addItemType(std::make_unique<StubItemType>(
        QStringLiteral("jira.issue.summary"), QCoreApplication::translate("jira", "Jira Issue (summary)")));
    registrar.addItemType(std::make_unique<StubItemType>(
        QStringLiteral("jira.issue.page"), QCoreApplication::translate("jira", "Jira Issue (page)")));
    registrar.addItemType(std::make_unique<StubItemType>(
        QStringLiteral("jira.list"), QCoreApplication::translate("jira", "Jira List")));
}

} // namespace mirante::modules::jira
