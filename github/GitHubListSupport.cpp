// Stubs until task T6 of the M11 plan implements README.md#list-views.
#include "GitHubListSupport.h"

#include "GitHubClient.h"
#include "GitHubViewSupport.h"

#include <mirante/ItemContext.h>

#include <QLabel>
#include <QShowEvent>

using mirante::widgets::ListCell;

namespace mirante::modules::github {

QString rowRepository(const QJsonObject &row)
{
    return GitHubClient::repositoryOfApiUrl(row.value(QStringLiteral("repository_url")).toString());
}

ListCell dashCell()
{
    return {QStringLiteral("–"), {}, {}};
}

ListCell dateCell(const QJsonValue &value)
{
    Q_UNUSED(value)
    return dashCell();
}

GitHubList::GitHubList(ItemContext &context, const QString &emptyText, const QUrl &page, QWidget *parent)
    : widgets::ItemListView(parent)
    , m_context(&context)
    , m_emptyLine(plainLine(emptyText))
{
    Q_UNUSED(page)
}

void GitHubList::follow(ItemStatusSource *source)
{
    if (source)
        showStatus(source->current());
}

void GitHubList::showEvent(QShowEvent *event)
{
    widgets::ItemListView::showEvent(event);
    m_shown = true;
}

void GitHubList::hideEvent(QHideEvent *event)
{
    widgets::ItemListView::hideEvent(event);
    m_shown = false;
}

ListCell GitHubList::linksCell(const ListRowTarget &target) const
{
    Q_UNUSED(target)
    return dashCell();
}

void GitHubList::showStatus(const ItemStatus &status)
{
    Q_UNUSED(status)
}

void GitHubList::showRowMenu(const QString &key, const QPoint &globalPos)
{
    Q_UNUSED(key)
    Q_UNUSED(globalPos)
}

void GitHubList::activateLink(const QString &key, const QString &link)
{
    Q_UNUSED(key)
    Q_UNUSED(link)
}

void GitHubList::openPage(const QUrl &address)
{
    Q_UNUSED(address)
}

void GitHubList::addToTask(const ListRowTarget &target)
{
    Q_UNUSED(target)
}

NumberedList::NumberedList(ItemContext &context, const ListTraits &traits, QWidget *parent)
    : GitHubList(context, traits.emptyText, QUrl(), parent)
    , m_traits(traits)
{
}

ListRowTarget NumberedList::rowTarget(const QJsonObject &row, int index, const QUrl &webBase) const
{
    Q_UNUSED(webBase)
    ListRowTarget target;
    target.repository = rowRepository(row);
    target.number = row.value(QStringLiteral("number")).toInteger();
    target.key = target.complete() ? target.repository + QLatin1Char('#') + QString::number(target.number)
                                   : QStringLiteral("row-%1").arg(index);
    return target;
}

ListCell NumberedList::refCell(const ListRowTarget &target) const
{
    return {target.repository.section(QLatin1Char('/'), -1) + QLatin1Char('#') + QString::number(target.number),
            target.sortKey,
            {}};
}

} // namespace mirante::modules::github
