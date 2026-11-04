#include "GitLabListModel.h"

#include "GitLabLists.h"
#include "GitLabSettings.h"

#include <mirante/ItemContext.h>

#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QEvent>
#include <QGuiApplication>
#include <QJsonArray>
#include <QWidget>

namespace mirante::modules::gitlab {

ListModel::ListModel(ItemContext &context, const QString &emptyText, const QUrl &page, const QString &description,
                     ListSpan span)
    : m_context(&context)
    , m_span(span)
    , m_emptyText(emptyText)
    , m_pageAddress(page.scheme() == QLatin1String("https") || page.scheme() == QLatin1String("http") ? page : QUrl())
    , m_description(description)
    , m_message(QCoreApplication::translate("gitlab", "Loading…"))
{
    // The cell header's "Open in browser" button opens the list's page.
    m_context->setWebLink(m_pageAddress);
}

ListModel::~ListModel() = default;

QJsonObject ListModel::config() const
{
    return m_context->config();
}

int ListModel::pageSize() const
{
    return listPageSize(config());
}

QStringList ListModel::filterDefault(const QString &column) const
{
    return gitlab::filterDefault(config(), column);
}

void ListModel::keepFilter(const QString &column, const QStringList &values)
{
    const QJsonObject current = config();
    const QJsonObject next = withFilter(current, column, values);
    if (next != current)
        m_context->updateConfig(next);
}

bool ListModel::isColumnShown(const QString &column) const
{
    if (column.isEmpty())
        return true;
    const QString field = QStringLiteral("show") + column.at(0).toUpper() + column.mid(1);
    return config().value(field).toBool(true);
}

void ListModel::openWeb(const QString &key)
{
    const QUrl address = m_rows.value(key).address;
    if (!address.isEmpty())
        QDesktopServices::openUrl(address);
}

void ListModel::openPage(const QString &key)
{
    const QUrl address = m_rows.value(key).address;
    if (address.isEmpty())
        return;
    if (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier)
        QDesktopServices::openUrl(address);
    else
        m_context->showPage(address);
}

void ListModel::addToTask(const QString &key)
{
    const auto add = m_adds.constFind(key);
    if (add != m_adds.constEnd())
        m_context->addItemToTask(add->type, add->name, add->config);
}

void ListModel::copyLink(const QString &key)
{
    const QUrl address = m_rows.value(key).address;
    if (!address.isEmpty())
        QGuiApplication::clipboard()->setText(address.toString());
}

void ListModel::setVisibleRows(const QStringList &keys)
{
    Q_UNUSED(keys)
}

void ListModel::follow(ItemStatusSource *source)
{
    if (!source)
        return;
    showStatus(source->current());
    source->subscribe(this, [this](const ItemStatus &status) { showStatus(status); });
}

void ListModel::attach(QWidget *view)
{
    m_view = view;
    setParent(view);
    view->installEventFilter(this);
    m_viewVisible = view->isVisible();
    viewVisibilityChanged(m_viewVisible);
}

bool ListModel::eventFilter(QObject *watched, QEvent *event)
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

void ListModel::addRow(const ListRow &row, const AddTarget &add)
{
    m_keys << row.key;
    ListRow kept = row;
    kept.canAdd = !add.type.isEmpty();
    m_rows.insert(row.key, kept);
    if (kept.canAdd)
        m_adds.insert(row.key, add);
}

void ListModel::showStatus(const ItemStatus &status)
{
    // Old rows stay through an error. Problems show in the cell header.
    if (status.data.isEmpty()) {
        if (!m_hasData) {
            const QString message =
                status.message.isEmpty() ? QCoreApplication::translate("gitlab", "Loading…") : status.message;
            if (message != m_message) {
                m_message = message;
                emit changed();
            }
        }
        return;
    }
    m_hasData = true;
    m_message.clear();
    m_keys.clear();
    m_rows.clear();
    m_adds.clear();
    read(status.data.value(QStringLiteral("rows")).toArray(), baseUrlOf(m_context->moduleSettings(kGeneralScreen)));
    emit changed();
}

} // namespace mirante::modules::gitlab
