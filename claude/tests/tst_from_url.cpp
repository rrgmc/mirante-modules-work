// Reading a pasted claude.ai address as a Claude session (web).
#include "../ClaudeUrls.h"

#include "items/ItemTypeRegistry.h"

#include <mirante/ItemType.h>

#include <QJsonObject>
#include <QTest>

namespace mirante::modules::claude {
void registerModule(mirante::ModuleRegistrar &registrar);
}

using mirante::modules::claude::matchUrl;

class TestFromUrl : public QObject
{
    Q_OBJECT

private slots:
    void claudeAddress()
    {
        const QString address = QStringLiteral("https://claude.ai/code/session_011iWHHjw6i9LorMB3n5seA4");
        const auto match = matchUrl(QUrl(address));
        QVERIFY(match);
        QCOMPARE(match->config, (QJsonObject{{QStringLiteral("url"), address}}));
        QCOMPARE(match->name, QStringLiteral("Claude session"));
        QVERIFY(!match->generic);
        QVERIFY(match->baseUrl.isEmpty());
        QVERIFY(matchUrl(QUrl(QStringLiteral("https://www.claude.ai/chat/abc"))));
    }

    void refused()
    {
        QVERIFY(!matchUrl(QUrl()));
        QVERIFY(!matchUrl(QUrl(QStringLiteral("https://example.com/claude.ai"))));
        QVERIFY(!matchUrl(QUrl(QStringLiteral("https://notclaude.ai/chat/abc"))));
        QVERIFY(!matchUrl(QUrl(QStringLiteral("ftp://claude.ai/chat/abc"))));
    }

    // The registered web session type takes the link.
    void registeredTypes()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("claude"), &mirante::modules::claude::registerModule);
        const QUrl address(QStringLiteral("https://claude.ai/chat/abc"));
        QVERIFY(registry.type(QStringLiteral("claude.session.web")).fromUrl(address, {}));
    }
};

QTEST_MAIN(TestFromUrl)
#include "tst_from_url.moc"
