#include "GitHubListSupport.h"

#include "GitHubClient.h"
#include "GitHubSettings.h"
#include "GitHubUrls.h"
#include "GitHubViewSupport.h"

#include <mirante/ItemContext.h>
#include <mirante/widgets/CompactDate.h>

#include <QAction>
#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QJsonArray>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QShowEvent>

using mirante::widgets::ListCell;
using mirante::widgets::ListLink;
using mirante::widgets::ListRow;

namespace mirante::modules::github {

namespace {

const QString kWebLink = QStringLiteral("web");
const QString kOpenLink = QStringLiteral("open");
const QString kAddLink = QStringLiteral("add");

} // namespace

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
    const QString text = value.toString();
    const QDateTime date = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!date.isValid())
        return {text.isEmpty() ? dashCell().text : text, QDateTime(), {}};
    return {widgets::compactDate(date), date, {}};
}

GitHubList::GitHubList(ItemContext &context, const QString &emptyText, const QUrl &page, QWidget *parent)
    : widgets::ItemListView(parent)
    , m_context(&context)
    , m_emptyLine(plainLine(emptyText))
{
    // setMessage() shows instead of the table, and this shows with it.
    m_emptyLine->setObjectName(QStringLiteral("emptyLine"));
    m_emptyLine->setAlignment(Qt::AlignCenter);
    layout()->addWidget(m_emptyLine);
    m_emptyLine->hide();

    setWebLink(isWebAddress(page) ? page : QUrl());
    setPageSize(listPageSize(context.config()));

    connect(this, &ItemListView::rowMenuRequested, this,
            [this](const QString &key, const QPoint &globalPos) { showRowMenu(key, globalPos); });
    connect(this, &ItemListView::rowLinkActivated, this,
            [this](const QString &key, const QString &link) { activateLink(key, link); });
    connect(this, &ItemListView::visibleKeysChanged, this, [this] {
        if (m_shown && !m_settingRows)
            askForNewKeys();
    });
}

void GitHubList::follow(ItemStatusSource *source)
{
    if (!source) {
        showStatus({});
        return;
    }
    showStatus(source->current());
    source->subscribe(this, [this](const ItemStatus &status) { showStatus(status); });
}

void GitHubList::showEvent(QShowEvent *event)
{
    widgets::ItemListView::showEvent(event);
    m_shown = true;
    askForCells();
}

void GitHubList::hideEvent(QHideEvent *event)
{
    widgets::ItemListView::hideEvent(event);
    m_shown = false;
    stopAsking();
}

ListCell GitHubList::linksCell(const ListRowTarget &target) const
{
    ListCell cell;
    cell.sortKey = target.sortKey;
    if (!target.address.isEmpty())
        // "Open" is also a state; the link opens the row, so it is told apart.
        cell.links << ListLink{kWebLink, QCoreApplication::translate("github", "Web")}
                   << ListLink{kOpenLink, QCoreApplication::translate("github", "Open", "link that opens a row")};
    if (!target.addType.isEmpty())
        cell.links << ListLink{kAddLink, QCoreApplication::translate("github", "Add")};
    return cell;
}

void GitHubList::showStatus(const ItemStatus &status)
{
    // Old data stays through an error. Problems show in the cell header.
    if (status.data.isEmpty()) {
        if (!m_drawn)
            setMessage(status.message.isEmpty() ? QCoreApplication::translate("github", "Loading…") : status.message);
        return;
    }
    m_drawn = true;
    setMessage({});

    const QUrl webBase = webBaseOf(m_context->moduleSettings(kGeneralScreen));
    const QJsonArray answers = status.data.value(QStringLiteral("rows")).toArray();
    QList<ListRow> rows;
    m_targets.clear();
    for (int index = 0; index < answers.size(); ++index) {
        const QJsonObject row = answers.at(index).toObject();
        const ListRowTarget target = rowTarget(row, index, webBase);
        m_targets.insert(target.key, target);
        rows.append(ListRow{target.key, rowCells(target, row)});
    }
    m_settingRows = true;
    setRows(rows);
    m_settingRows = false;
    m_emptyLine->setVisible(rows.isEmpty());

    if (m_shown)
        askForCells();
}

void GitHubList::showRowMenu(const QString &key, const QPoint &globalPos)
{
    const ListRowTarget target = m_targets.value(key);

    // It belongs to the view and does not wait, because the view can be made
    // again at any time.
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    QAction *web = menu->addAction(QCoreApplication::translate("github", "Web"));
    web->setEnabled(!target.address.isEmpty());
    connect(web, &QAction::triggered, this, [this, key] { activateLink(key, kWebLink); });

    QAction *open = menu->addAction(QCoreApplication::translate("github", "Open page"));
    open->setEnabled(!target.address.isEmpty());
    connect(open, &QAction::triggered, this, [this, key] { activateLink(key, kOpenLink); });

    QAction *add = menu->addAction(QCoreApplication::translate("github", "Add to task as item"));
    add->setEnabled(!target.addType.isEmpty());
    connect(add, &QAction::triggered, this, [this, key] { activateLink(key, kAddLink); });

    QAction *copy = menu->addAction(QCoreApplication::translate("github", "Copy link"));
    copy->setEnabled(!target.address.isEmpty());
    connect(copy, &QAction::triggered, this, [address = target.address] {
        QGuiApplication::clipboard()->setText(address.toString());
    });

    menu->popup(globalPos);
}

void GitHubList::activateLink(const QString &key, const QString &link)
{
    const ListRowTarget target = m_targets.value(key);
    if (link == kWebLink && !target.address.isEmpty())
        QDesktopServices::openUrl(target.address);
    else if (link == kOpenLink && !target.address.isEmpty())
        openPage(target.address);
    else if (link == kAddLink && !target.addType.isEmpty())
        addToTask(target);
}

void GitHubList::openPage(const QUrl &address)
{
    if (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier)
        QDesktopServices::openUrl(address);
    else
        m_context->showPage(address);
}

void GitHubList::addToTask(const ListRowTarget &target)
{
    m_context->addItemToTask(target.addType, target.addName, target.addConfig);
}

NumberedList::NumberedList(ItemContext &context, const ListTraits &traits, QWidget *parent)
    : GitHubList(context, traits.emptyText,
                 listWebUrl(traits.kind, webBaseOf(context.moduleSettings(kGeneralScreen)), context.config(),
                            context.projectSetting(kProjectSettingKey)),
                 parent)
    , m_traits(traits)
{
    setDescription(listDescription(traits.kind, context.config(), context.projectSetting(kProjectSettingKey)));
}

ListRowTarget NumberedList::rowTarget(const QJsonObject &row, int index, const QUrl &webBase) const
{
    ListRowTarget target;
    target.repository = rowRepository(row);
    target.number = row.value(QStringLiteral("number")).toInteger();
    const QString ref = target.repository + QLatin1Char('#') + QString::number(target.number);
    target.key = target.complete() ? ref : QStringLiteral("row-%1").arg(index);
    target.sortKey
        = target.repository + QLatin1Char('#') + QStringLiteral("%1").arg(target.number, 12, 10, QLatin1Char('0'));

    const QJsonObject config{{QStringLiteral("repository"), target.repository},
                             {QStringLiteral("number"), target.number}};
    const QUrl built = m_traits.kind == ListKind::PullRequests ? pullRequestUrl(webBase, config, {})
                                                               : issueUrl(webBase, config, {});
    const QUrl address = webAddress(row, built);
    target.address = isWebAddress(address) ? address : QUrl();

    if (target.complete()) {
        target.addType = m_traits.summaryType;
        target.addName = ref;
        target.addConfig = config;
    }
    return target;
}

ListCell NumberedList::refCell(const ListRowTarget &target) const
{
    return {target.repository.section(QLatin1Char('/'), -1) + QLatin1Char('#') + QString::number(target.number),
            target.sortKey,
            {}};
}

} // namespace mirante::modules::github
