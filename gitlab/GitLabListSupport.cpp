#include "GitLabListSupport.h"

#include "GitLabSettings.h"
#include "GitLabUrls.h"
#include "GitLabViewSupport.h"

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

namespace mirante::modules::gitlab {

namespace {

const QString kWebLink = QStringLiteral("web");
const QString kOpenLink = QStringLiteral("open");
const QString kAddLink = QStringLiteral("add");

} // namespace

QString rowProject(const QJsonObject &row, QChar separator, const QUrl &baseUrl)
{
    const QString full =
        row.value(QStringLiteral("references")).toObject().value(QStringLiteral("full")).toString();
    const qsizetype end = full.lastIndexOf(separator);
    if (end > 0)
        return full.left(end);
    const QUrl address(row.value(QStringLiteral("web_url")).toString());
    const std::optional<PageAddress> page = readPageAddress(address, {baseUrl});
    return page ? page->project : QString();
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

GitLabList::GitLabList(ItemContext &context, const QString &emptyText, const QUrl &page, QWidget *parent)
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

void GitLabList::follow(ItemStatusSource *source)
{
    if (!source) {
        showStatus({});
        return;
    }
    showStatus(source->current());
    source->subscribe(this, [this](const ItemStatus &status) { showStatus(status); });
}

void GitLabList::showEvent(QShowEvent *event)
{
    widgets::ItemListView::showEvent(event);
    m_shown = true;
    askForCells();
}

void GitLabList::hideEvent(QHideEvent *event)
{
    widgets::ItemListView::hideEvent(event);
    m_shown = false;
    stopAsking();
}

ListCell GitLabList::linksCell(const ListRowTarget &target) const
{
    ListCell cell;
    cell.sortKey = target.sortKey;
    if (!target.address.isEmpty())
        // "Open" is also a state; the link opens the row, so it is told apart.
        cell.links << ListLink{kWebLink, QCoreApplication::translate("gitlab", "Web")}
                   << ListLink{kOpenLink, QCoreApplication::translate("gitlab", "Open", "link that opens a row")};
    if (!target.addType.isEmpty())
        cell.links << ListLink{kAddLink, QCoreApplication::translate("gitlab", "Add")};
    return cell;
}

void GitLabList::showStatus(const ItemStatus &status)
{
    // Old data stays through an error. Problems show in the cell header.
    if (status.data.isEmpty()) {
        if (!m_drawn)
            setMessage(status.message.isEmpty() ? QCoreApplication::translate("gitlab", "Loading…") : status.message);
        return;
    }
    m_drawn = true;
    setMessage({});

    const QUrl base = baseUrlOf(m_context->moduleSettings(kGeneralScreen));
    const QJsonArray answers = status.data.value(QStringLiteral("rows")).toArray();
    QList<ListRow> rows;
    m_targets.clear();
    for (int index = 0; index < answers.size(); ++index) {
        const QJsonObject row = answers.at(index).toObject();
        const ListRowTarget target = rowTarget(row, index, base);
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

void GitLabList::showRowMenu(const QString &key, const QPoint &globalPos)
{
    const ListRowTarget target = m_targets.value(key);

    // It belongs to the view and does not wait, because the view can be made
    // again at any time.
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    QAction *web = menu->addAction(QCoreApplication::translate("gitlab", "Web"));
    web->setEnabled(!target.address.isEmpty());
    connect(web, &QAction::triggered, this, [this, key] { activateLink(key, kWebLink); });

    QAction *open = menu->addAction(QCoreApplication::translate("gitlab", "Open page"));
    open->setEnabled(!target.address.isEmpty());
    connect(open, &QAction::triggered, this, [this, key] { activateLink(key, kOpenLink); });

    QAction *add = menu->addAction(QCoreApplication::translate("gitlab", "Add to task as item"));
    add->setEnabled(!target.addType.isEmpty());
    connect(add, &QAction::triggered, this, [this, key] { activateLink(key, kAddLink); });

    QAction *copy = menu->addAction(QCoreApplication::translate("gitlab", "Copy link"));
    copy->setEnabled(!target.address.isEmpty());
    connect(copy, &QAction::triggered, this, [address = target.address] {
        QGuiApplication::clipboard()->setText(address.toString());
    });

    menu->popup(globalPos);
}

void GitLabList::activateLink(const QString &key, const QString &link)
{
    const ListRowTarget target = m_targets.value(key);
    if (link == kWebLink && !target.address.isEmpty())
        QDesktopServices::openUrl(target.address);
    else if (link == kOpenLink && !target.address.isEmpty())
        openPage(target.address);
    else if (link == kAddLink && !target.addType.isEmpty())
        m_context->addItemToTask(target.addType, target.addName, target.addConfig);
}

void GitLabList::openPage(const QUrl &address)
{
    if (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier)
        QDesktopServices::openUrl(address);
    else
        m_context->showPage(address);
}

NumberedList::NumberedList(ItemContext &context, const ListTraits &traits, QWidget *parent)
    : GitLabList(context, traits.emptyText,
                 listWebUrl(traits.kind, baseUrlOf(context.moduleSettings(kGeneralScreen)), context.config(),
                            context.projectSetting(kProjectSettingKey)),
                 parent)
    , m_traits(traits)
{
    setDescription(listDescription(traits.kind, context.config(), context.projectSetting(kProjectSettingKey)));
}

ListRowTarget NumberedList::rowTarget(const QJsonObject &row, int index, const QUrl &baseUrl) const
{
    ListRowTarget target;
    target.project = rowProject(row, m_traits.separator, baseUrl);
    target.iid = row.value(QStringLiteral("iid")).toInteger();
    const QString ref = target.project + m_traits.separator + QString::number(target.iid);
    target.key = target.complete() ? ref : QStringLiteral("row-%1").arg(index);
    target.sortKey
        = target.project + m_traits.separator + QStringLiteral("%1").arg(target.iid, 12, 10, QLatin1Char('0'));

    const QJsonObject config{{QStringLiteral("project"), target.project}, {QStringLiteral("iid"), target.iid}};
    const QUrl built = m_traits.kind == ListKind::MergeRequests ? mergeRequestUrl(baseUrl, config, {})
                                                                : issueUrl(baseUrl, config, {});
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
    const QString number = m_traits.separator + QString::number(target.iid);
    return {target.project.section(QLatin1Char('/'), -1) + number, target.sortKey, {}};
}

} // namespace mirante::modules::gitlab
