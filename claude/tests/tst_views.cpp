// The Claude session (local) view, with a fake context that records the
// programs it would start. No terminal starts. See README.md#view.
#include "../LocalSessionView.h"

#include <mirante/ItemContext.h>
#include <mirante/widgets/ActionRow.h>
#include <mirante/widgets/SummaryView.h>

#include <QHash>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTest>

#include <memory>

using mirante::widgets::ActionRow;
using mirante::widgets::SummaryView;

namespace claude = mirante::modules::claude;

namespace {

class FakeContext : public mirante::ItemContext
{
public:
    QJsonObject itemConfig;
    QHash<QString, QString> paths;      // folder id to local path
    QHash<QString, QString> pathsAfterMap; // what mapFolder() stores
    QJsonObject deviceSettings;
    QString device = QStringLiteral("Laptop");
    QString launchError;

    struct Launch
    {
        QString dir;
        QString program;
        QStringList arguments;
    };
    QList<Launch> launches;
    QStringList mapped;

    QString itemName() const override { return QStringLiteral("Item"); }
    QJsonObject config() const override { return itemConfig; }
    QJsonValue projectSetting(const QString &) const override { return {}; }
    QJsonObject moduleSettings(const QString &) const override { return {}; }
    QString folderPath(const QString &folderId) const override { return paths.value(folderId); }
    void mapFolder(const QString &folderId) override
    {
        mapped << folderId;
        if (pathsAfterMap.contains(folderId))
            paths.insert(folderId, pathsAfterMap.value(folderId));
    }
    QWidget *createWebView(const QUrl &, QWidget *) override { return nullptr; }
    QWebEngineProfile *profile() override { return nullptr; }
    QWidget *createMessageView(const QString &, QWidget *) override { return nullptr; }
    void updateConfig(const QJsonObject &) override {}
    QJsonValue deviceSetting(const QString &key) const override
    {
        // The registered default, as the core gives it.
        if (key == QLatin1String("program") && !deviceSettings.contains(key))
            return QStringLiteral("claude");
        return deviceSettings.value(key);
    }
    QString deviceName() const override { return device; }
    QString launchTerminal(const QString &dir, const QString &program, const QStringList &arguments) override
    {
        launches << Launch{dir, program, arguments};
        return launchError;
    }
    QString startProgram(const QString &, const QString &, const QStringList &) override
    {
        return QStringLiteral("startProgram is not expected here.");
    }
};

// A session on this device in the mapped main folder.
void useMapped(FakeContext &context)
{
    context.itemConfig = {{QStringLiteral("sessionId"), QStringLiteral("0a1b2c3d-4e5f-6789-abcd-ef0123456789")},
                          {QStringLiteral("device"), QStringLiteral("Laptop")}};
    context.paths.insert(QString(), QStringLiteral("C:/work/My Project"));
}

ActionRow *actionsOf(QWidget *view)
{
    return static_cast<SummaryView *>(view)->actions();
}

QPushButton *buttonOf(QWidget *view, const QString &id)
{
    return view->findChild<QPushButton *>(id);
}

} // namespace

class TestViews : public QObject
{
    Q_OBJECT

private slots:
    void resumesInTheFolder()
    {
        FakeContext context;
        useMapped(context);
        std::unique_ptr<QWidget> view(claude::createLocalSessionView(context, nullptr));

        QPushButton *resume = buttonOf(view.get(), QStringLiteral("resume"));
        QVERIFY(resume->isEnabled());
        QVERIFY(actionsOf(view.get())->error().isEmpty());
        QVERIFY(!actionsOf(view.get())->isActionVisible(QStringLiteral("chooseFolder")));

        resume->click();
        QCOMPARE(context.launches.size(), 1);
        QCOMPARE(context.launches.at(0).dir, QStringLiteral("C:/work/My Project"));
        QCOMPARE(context.launches.at(0).program, QStringLiteral("claude"));
        QCOMPARE(context.launches.at(0).arguments,
                 QStringList({QStringLiteral("--resume"), QStringLiteral("0a1b2c3d-4e5f-6789-abcd-ef0123456789")}));
    }

    void showsTheSession()
    {
        FakeContext context;
        useMapped(context);
        std::unique_ptr<QWidget> view(claude::createLocalSessionView(context, nullptr));

        const QString folder = view->findChild<QLabel *>(QStringLiteral("folder"))->text();
        QVERIFY2(folder.startsWith(QStringLiteral("My Project")), qPrintable(folder));
        QVERIFY2(folder.contains(QStringLiteral("work")), qPrintable(folder));
        QCOMPARE(view->findChild<QLabel *>(QStringLiteral("device"))->text(), QStringLiteral("Device: Laptop"));
    }

    void usesTheProgramSettingAndTheItemsFolder()
    {
        FakeContext context;
        useMapped(context);
        context.itemConfig.insert(QStringLiteral("folder"), QStringLiteral("f2"));
        context.paths.insert(QStringLiteral("f2"), QStringLiteral("D:/other"));
        context.deviceSettings.insert(QStringLiteral("program"), QStringLiteral("C:/tools/claude.exe"));
        std::unique_ptr<QWidget> view(claude::createLocalSessionView(context, nullptr));

        buttonOf(view.get(), QStringLiteral("resume"))->click();
        QCOMPARE(context.launches.size(), 1);
        QCOMPARE(context.launches.at(0).dir, QStringLiteral("D:/other"));
        QCOMPARE(context.launches.at(0).program, QStringLiteral("C:/tools/claude.exe"));
    }

    void emptyProgramFallsBackToClaude()
    {
        FakeContext context;
        useMapped(context);
        context.deviceSettings.insert(QStringLiteral("program"), QStringLiteral("  "));
        std::unique_ptr<QWidget> view(claude::createLocalSessionView(context, nullptr));

        buttonOf(view.get(), QStringLiteral("resume"))->click();
        QCOMPARE(context.launches.size(), 1);
        QCOMPARE(context.launches.at(0).program, QStringLiteral("claude"));
    }

    void deviceComparesWithoutCaseOrSpaces()
    {
        FakeContext context;
        useMapped(context);
        context.itemConfig.insert(QStringLiteral("device"), QStringLiteral(" laptop "));
        context.device = QStringLiteral("LAPTOP");
        std::unique_ptr<QWidget> view(claude::createLocalSessionView(context, nullptr));
        QVERIFY(buttonOf(view.get(), QStringLiteral("resume"))->isEnabled());

        // No device means any device.
        context.itemConfig.remove(QStringLiteral("device"));
        std::unique_ptr<QWidget> anyDevice(claude::createLocalSessionView(context, nullptr));
        QVERIFY(buttonOf(anyDevice.get(), QStringLiteral("resume"))->isEnabled());
    }

    void invalidIdStartsNothing_data()
    {
        QTest::addColumn<QString>("id");
        QTest::addColumn<QString>("message");
        const QString invalid = QStringLiteral("The session id may hold only letters, digits and dashes.");
        QTest::newRow("empty") << QString() << QStringLiteral("Set the session id in the item's settings.");
        QTest::newRow("semicolon") << QStringLiteral("abc;calc") << invalid;
        QTest::newRow("space") << QStringLiteral("abc def") << invalid;
        QTest::newRow("quote") << QStringLiteral("abc\"") << invalid;
        QTest::newRow("option") << QStringLiteral("abc --dangerous") << invalid;
        QTest::newRow("underscore") << QStringLiteral("abc_def") << invalid;
    }

    void invalidIdStartsNothing()
    {
        QFETCH(QString, id);
        QFETCH(QString, message);
        FakeContext context;
        useMapped(context);
        context.itemConfig.insert(QStringLiteral("sessionId"), id);
        std::unique_ptr<QWidget> view(claude::createLocalSessionView(context, nullptr));

        QPushButton *resume = buttonOf(view.get(), QStringLiteral("resume"));
        QVERIFY(!resume->isEnabled());
        QCOMPARE(actionsOf(view.get())->error(), message);
        resume->click();
        QVERIFY(context.launches.isEmpty());
    }

    void otherDeviceDisables()
    {
        FakeContext context;
        useMapped(context);
        context.itemConfig.insert(QStringLiteral("device"), QStringLiteral("Desktop"));
        std::unique_ptr<QWidget> view(claude::createLocalSessionView(context, nullptr));

        QVERIFY(!buttonOf(view.get(), QStringLiteral("resume"))->isEnabled());
        QCOMPARE(actionsOf(view.get())->error(), QStringLiteral("This session lives on Desktop."));
    }

    void unmappedFolderDisablesAndChoosesAFolder()
    {
        FakeContext context;
        useMapped(context);
        context.paths.clear();
        context.pathsAfterMap.insert(QString(), QStringLiteral("C:/work/Mapped"));
        std::unique_ptr<QWidget> view(claude::createLocalSessionView(context, nullptr));

        QPushButton *resume = buttonOf(view.get(), QStringLiteral("resume"));
        QVERIFY(!resume->isEnabled());
        QCOMPARE(actionsOf(view.get())->error(), QStringLiteral("Set the folder's path on this device."));
        QVERIFY(actionsOf(view.get())->isActionVisible(QStringLiteral("chooseFolder")));
        QCOMPARE(view->findChild<QLabel *>(QStringLiteral("folder"))->text(),
                 QStringLiteral("Folder not set on this device"));

        // The view reads the path again after the picker.
        buttonOf(view.get(), QStringLiteral("chooseFolder"))->click();
        QCOMPARE(context.mapped, QStringList({QString()}));
        QVERIFY(resume->isEnabled());
        QVERIFY(actionsOf(view.get())->error().isEmpty());
        QVERIFY(!actionsOf(view.get())->isActionVisible(QStringLiteral("chooseFolder")));
        QVERIFY(view->findChild<QLabel *>(QStringLiteral("folder"))->text().startsWith(QStringLiteral("Mapped")));

        resume->click();
        QCOMPARE(context.launches.size(), 1);
        QCOMPARE(context.launches.at(0).dir, QStringLiteral("C:/work/Mapped"));
    }

    void cancelledPickerKeepsTheReason()
    {
        FakeContext context;
        useMapped(context);
        context.paths.clear();
        std::unique_ptr<QWidget> view(claude::createLocalSessionView(context, nullptr));

        buttonOf(view.get(), QStringLiteral("chooseFolder"))->click();
        QCOMPARE(context.mapped.size(), 1);
        QVERIFY(!buttonOf(view.get(), QStringLiteral("resume"))->isEnabled());
        QCOMPARE(actionsOf(view.get())->error(), QStringLiteral("Set the folder's path on this device."));
    }

    // An invalid id comes first, then another device, then the folder.
    void reasonsInOrder()
    {
        FakeContext context;
        useMapped(context);
        context.paths.clear();
        context.itemConfig.insert(QStringLiteral("device"), QStringLiteral("Desktop"));
        context.itemConfig.insert(QStringLiteral("sessionId"), QStringLiteral("bad id"));
        std::unique_ptr<QWidget> invalid(claude::createLocalSessionView(context, nullptr));
        QCOMPARE(actionsOf(invalid.get())->error(),
                 QStringLiteral("The session id may hold only letters, digits and dashes."));

        context.itemConfig.insert(QStringLiteral("sessionId"), QStringLiteral("abc"));
        std::unique_ptr<QWidget> otherDevice(claude::createLocalSessionView(context, nullptr));
        QCOMPARE(actionsOf(otherDevice.get())->error(), QStringLiteral("This session lives on Desktop."));
        // Mapping the folder here would not help.
        QVERIFY(!actionsOf(otherDevice.get())->isActionVisible(QStringLiteral("chooseFolder")));
    }

    void launchFailureShowsItsMessage()
    {
        FakeContext context;
        useMapped(context);
        context.launchError = QStringLiteral("The terminal could not be started.");
        std::unique_ptr<QWidget> view(claude::createLocalSessionView(context, nullptr));

        buttonOf(view.get(), QStringLiteral("resume"))->click();
        QCOMPARE(context.launches.size(), 1);
        QCOMPARE(actionsOf(view.get())->error(), QStringLiteral("The terminal could not be started."));

        // A later success clears it.
        context.launchError.clear();
        buttonOf(view.get(), QStringLiteral("resume"))->click();
        QVERIFY(actionsOf(view.get())->error().isEmpty());
    }
};

QTEST_MAIN(TestViews)
#include "tst_views.moc"
