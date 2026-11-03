#include "GitLabSummaryModel.h"

#include "GitLabClient.h"
#include "GitLabSettings.h"
#include "GitLabUrls.h"
#include "GitLabViewSupport.h"

#include <mirante/ItemContext.h>

#include <QCoreApplication>
#include <QDesktopServices>
#include <QEvent>
#include <QGuiApplication>
#include <QWidget>

namespace mirante::modules::gitlab {

SummaryModel::SummaryModel(ItemContext &context)
    : m_context(&context)
{
}

SummaryModel::~SummaryModel() = default;

QJsonObject SummaryModel::config() const
{
    return m_context->config();
}

bool SummaryModel::isSectionShown(const QString &section) const
{
    if (section.isEmpty())
        return true;
    const QString field = QStringLiteral("show") + section.at(0).toUpper() + section.mid(1);
    return config().value(field).toBool(true);
}

void SummaryModel::openPage()
{
    if (m_pageAddress.isEmpty())
        return;
    if (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier)
        QDesktopServices::openUrl(m_pageAddress);
    else
        m_context->showPage(m_pageAddress);
}

void SummaryModel::clearError()
{
    setError({});
}

void SummaryModel::refresh()
{
    if (m_refresh)
        m_refresh();
}

void SummaryModel::follow(ItemStatusSource *source)
{
    if (!source) {
        showStatus({});
        return;
    }
    m_refresh = [source] { source->refresh(); };
    showStatus(source->current());
    source->subscribe(this, [this](const ItemStatus &status) { showStatus(status); });
}

void SummaryModel::attach(QWidget *view)
{
    m_view = view;
    setParent(view);
    view->installEventFilter(this);
    m_viewVisible = view->isVisible();
    viewVisibilityChanged(m_viewVisible);
}

bool SummaryModel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_view && (event->type() == QEvent::Show || event->type() == QEvent::Hide)) {
        const bool visible = event->type() == QEvent::Show;
        if (visible != m_viewVisible) {
            m_viewVisible = visible;
            viewVisibilityChanged(visible);
        }
    }
    return QObject::eventFilter(watched, event);
}

void SummaryModel::showStatus(const ItemStatus &status)
{
    // Old data stays through an error. Problems show in the cell header. A
    // message without an error, such as a main folder that left its branch,
    // says the item has nothing to show now, so it replaces the old data.
    if (status.data.isEmpty()) {
        if (!m_hasData || (status.state == ItemStatus::State::None && !status.message.isEmpty())) {
            m_hasData = false;
            m_message = status.message.isEmpty() ? QCoreApplication::translate("gitlab", "Loading…") : status.message;
            emit changed();
        }
        return;
    }
    m_hasData = true;
    m_message.clear();
    read(status);
    emit changed();
}

QString SummaryModel::project() const
{
    return projectPath(config(), defaultProject());
}

ApiClient *SummaryModel::server() const
{
    return m_context->server();
}

QUrl SummaryModel::baseUrl() const
{
    return baseUrlOf(m_context->moduleSettings(kGeneralScreen));
}

QJsonValue SummaryModel::defaultProject() const
{
    return m_context->projectSetting(kProjectSettingKey);
}

void SummaryModel::setPageAddress(const QUrl &url)
{
    m_pageAddress = isWebAddress(url) ? url : QUrl();
}

void SummaryModel::runAction(const QString &id, const Request &request)
{
    clearError();
    ApiClient *client = m_context->server();
    if (!client) {
        setError(QCoreApplication::translate("gitlab", "This item has no server."));
        return;
    }
    setBusy(id, true);
    request(GitLabClient(*client, this), [this, id](const ApiReply &reply) {
        setBusy(id, false);
        if (!reply.ok())
            setError(actionError(reply));
        else
            refresh();
    });
}

void SummaryModel::setBusy(const QString &action, bool busy)
{
    const bool was = m_busy.contains(action);
    if (was == busy)
        return;
    if (busy)
        m_busy.insert(action);
    else
        m_busy.remove(action);
    emit busyChanged(action);
}

void SummaryModel::setError(const QString &text)
{
    if (m_error == text)
        return;
    m_error = text;
    emit errorChanged();
}

} // namespace mirante::modules::gitlab
