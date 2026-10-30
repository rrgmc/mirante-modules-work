#include "LocalSessionView.h"

#include <mirante/ItemContext.h>
#include <mirante/widgets/ActionRow.h>
#include <mirante/widgets/SummaryView.h>

#include <QCoreApplication>
#include <QDir>
#include <QJsonObject>
#include <QLabel>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace mirante::modules::claude {

namespace {

const QString kResume = QStringLiteral("resume");
const QString kChooseFolder = QStringLiteral("chooseFolder");

QString trClaude(const char *text)
{
    return QCoreApplication::translate("claude", text);
}

// A label for text from the configuration, which wraps and is never read as
// markup.
QLabel *plainLabel(const QString &objectName, QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setObjectName(objectName);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

// The id is passed to a program as one argument, but it comes from synced
// data, so only the characters of a real id are let through.
bool isValidSessionId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9-]+$"));
    return pattern.match(id).hasMatch();
}

// The session id as the title, the folder and the device under it, and the
// button that resumes the session. The reason the button is disabled, or the
// error of the last launch, is the line under it.
class LocalSessionView : public widgets::SummaryView
{
public:
    LocalSessionView(ItemContext &context, QWidget *parent)
        : widgets::SummaryView(parent)
        , m_context(context)
    {
        auto *state = new QWidget;
        auto *layout = new QVBoxLayout(state);
        layout->setContentsMargins(0, 0, 0, 0);
        m_folder = plainLabel(QStringLiteral("folder"), state);
        m_device = plainLabel(QStringLiteral("device"), state);
        layout->addWidget(m_folder);
        layout->addWidget(m_device);
        setStateRow(state);

        actions()->addAction(kResume, trClaude("Resume in terminal"), trClaude("Resume"), [this] { resume(); });
        actions()->addAction(kChooseFolder, trClaude("Choose folder…"), trClaude("Folder…"), [this] {
            // A modal picker. The core does not make the view again after it,
            // so the view reads the path again itself.
            m_context.mapFolder(folderId());
            draw();
        });
        draw();
    }

private:
    QString sessionId() const { return m_context.config().value(QStringLiteral("sessionId")).toString().trimmed(); }
    QString folderId() const { return m_context.config().value(QStringLiteral("folder")).toString(); }
    QString device() const { return m_context.config().value(QStringLiteral("device")).toString().trimmed(); }

    bool onOtherDevice() const
    {
        const QString itemDevice = device();
        return !itemDevice.isEmpty()
               && itemDevice.compare(m_context.deviceName().trimmed(), Qt::CaseInsensitive) != 0;
    }

    // Why the session cannot be resumed here, or empty if it can.
    QString disabledReason(const QString &path) const
    {
        const QString id = sessionId();
        if (id.isEmpty())
            return trClaude("Set the session id in the item's settings.");
        if (!isValidSessionId(id))
            return trClaude("The session id may hold only letters, digits and dashes.");
        if (onOtherDevice())
            return trClaude("This session lives on %1.").arg(device());
        if (path.isEmpty())
            return trClaude("Set the folder's path on this device.");
        return {};
    }

    void draw()
    {
        const QString id = sessionId();
        const QString path = m_context.folderPath(folderId());
        setTitle(id.isEmpty() ? trClaude("No session id") : id);

        if (path.isEmpty()) {
            m_folder->setText(trClaude("Folder not set on this device"));
        } else {
            // Module views cannot read the project's folders, so the name is
            // the path's last part. A drive's root has none.
            const QString name = QDir(path).dirName();
            const QString shown = QDir::toNativeSeparators(path);
            m_folder->setText(name.isEmpty() ? shown
                                             : name + QStringLiteral(" ") + QChar(0x00B7) + QStringLiteral(" ") + shown);
        }
        m_device->setText(device().isEmpty() ? QString() : trClaude("Device: %1").arg(device()));
        m_device->setVisible(!device().isEmpty());

        const QString reason = disabledReason(path);
        // The button is disabled for as long as the reason holds, not only
        // while a request runs.
        actions()->setBusy(kResume, !reason.isEmpty());
        actions()->setActionVisible(kChooseFolder, path.isEmpty() && !onOtherDevice());
        actions()->showError(reason);
    }

    void resume()
    {
        const QString path = m_context.folderPath(folderId());
        if (!disabledReason(path).isEmpty()) {
            draw();
            return;
        }
        QString program = m_context.deviceSetting(QStringLiteral("program")).toString().trimmed();
        if (program.isEmpty())
            program = QStringLiteral("claude");
        const QString error =
            m_context.launchTerminal(path, program, {QStringLiteral("--resume"), sessionId()});
        actions()->showError(error);
    }

    ItemContext &m_context;
    QLabel *m_folder = nullptr;
    QLabel *m_device = nullptr;
};

} // namespace

QWidget *createLocalSessionView(ItemContext &context, QWidget *parent)
{
    return new LocalSessionView(context, parent);
}

} // namespace mirante::modules::claude
