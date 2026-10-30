#include "GitLabListSupport.h"

#include "GitLabSettings.h"
#include "GitLabUrls.h"
#include "GitLabViewSupport.h"

#include <mirante/ItemContext.h>

#include <QAbstractProxyModel>
#include <QAction>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QJsonArray>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QShowEvent>
#include <QTableView>

using mirante::widgets::ListCell;
using mirante::widgets::ListRow;

namespace mirante::modules::gitlab {

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

ListCell updatedCell(const QJsonObject &row)
{
    const QString text = row.value(QStringLiteral("updated_at")).toString();
    const QDateTime date = QDateTime::fromString(text, Qt::ISODateWithMs);
    return {localDateTime(text), date.isValid() ? QVariant(date) : QVariant(), {}};
}

GitLabList::GitLabList(ItemContext &context, const ListTraits &traits, QWidget *parent)
    : widgets::ItemListView(parent)
    , m_context(&context)
    , m_traits(traits)
    , m_emptyLine(plainLine(traits.emptyText))
{
    // setMessage() shows instead of the table, and this shows with it.
    m_emptyLine->setObjectName(QStringLiteral("emptyLine"));
    m_emptyLine->setAlignment(Qt::AlignCenter);
    layout()->addWidget(m_emptyLine);
    m_emptyLine->hide();

    const QUrl link = listWebUrl(traits.kind, baseUrlOf(context.moduleSettings(kGeneralScreen)), context.config(),
                                 context.projectSetting(kProjectSettingKey));
    setWebLink(isWebAddress(link) ? link : QUrl());

    connect(this, &ItemListView::rowSelected, this, [this](const QString &key) { showPreviewOf(key); });
    connect(this, &ItemListView::rowMenuRequested, this,
            [this](const QString &key, const QPoint &globalPos) { showRowMenu(key, globalPos); });
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
    if (m_previewOutdated)
        readPreviewAgain();
}

void GitLabList::hideEvent(QHideEvent *event)
{
    widgets::ItemListView::hideEvent(event);
    m_shown = false;
    stopAsking();
}

ListCell GitLabList::refCell(const ListRowTarget &target) const
{
    const QString number = m_traits.separator + QString::number(target.iid);
    const QString sortKey = target.project + m_traits.separator
        + QStringLiteral("%1").arg(target.iid, 12, 10, QLatin1Char('0'));
    return {target.project.section(QLatin1Char('/'), -1) + number, sortKey, {}};
}

QStringList GitLabList::keysInTableOrder() const
{
    // The table shows the rows through a sort proxy, whose source rows are in
    // the order given to setRows().
    const auto *table = findChild<QTableView *>();
    const auto *proxy = table ? qobject_cast<const QAbstractProxyModel *>(table->model()) : nullptr;
    if (!proxy)
        return m_keys;
    QStringList keys;
    for (int row = 0; row < proxy->rowCount(); ++row)
        keys << m_keys.value(proxy->mapToSource(proxy->index(row, 0)).row());
    return keys;
}

void GitLabList::showStatus(const ItemStatus &status)
{
    // Old data stays through an error. Problems show in the cell header.
    if (status.data.isEmpty()) {
        if (!m_drawn)
            setMessage(status.message.isEmpty() ? tr("Loading…") : status.message);
        return;
    }
    m_drawn = true;
    setMessage({});

    const QUrl base = baseUrlOf(m_context->moduleSettings(kGeneralScreen));
    const QJsonArray answers = status.data.value(QStringLiteral("rows")).toArray();
    QList<ListRow> rows;
    m_keys.clear();
    m_targets.clear();
    for (int index = 0; index < answers.size(); ++index) {
        const QJsonObject row = answers.at(index).toObject();
        const ListRowTarget target = readTarget(row, index, base);
        m_keys << target.key;
        m_targets.insert(target.key, target);
        rows.append(ListRow{target.key, rowCells(target, row)});
    }
    setRows(rows);
    m_emptyLine->setVisible(rows.isEmpty());

    if (m_shown)
        askForCells();
    // A preview whose row has gone stays as it is. See decision 0068.
    if (preview() && m_targets.contains(m_previewKey)) {
        if (m_shown)
            readPreviewAgain();
        else
            m_previewOutdated = true;
    }
}

ListRowTarget GitLabList::readTarget(const QJsonObject &row, int index, const QUrl &baseUrl) const
{
    ListRowTarget target;
    target.project = rowProject(row, m_traits.separator, baseUrl);
    target.iid = row.value(QStringLiteral("iid")).toInteger();
    target.key = target.complete() ? target.project + m_traits.separator + QString::number(target.iid)
                                   : QStringLiteral("row-%1").arg(index);

    const QJsonObject config{{QStringLiteral("project"), target.project}, {QStringLiteral("iid"), target.iid}};
    const QUrl built = m_traits.kind == ListKind::MergeRequests ? mergeRequestUrl(baseUrl, config, {})
                                                                : issueUrl(baseUrl, config, {});
    const QUrl address = webAddress(row, built);
    target.address = isWebAddress(address) ? address : QUrl();
    return target;
}

void GitLabList::showPreviewOf(const QString &key)
{
    // The row of a preview that stayed can come back and be clicked.
    if (key == m_previewKey && preview())
        return;
    const ListRowTarget target = m_targets.value(key);
    m_previewKey = key;
    m_previewOutdated = false;
    setPreview(target.complete() ? m_traits.createPreview(*m_context, target.project, target.iid, this) : nullptr);
}

void GitLabList::readPreviewAgain()
{
    m_previewOutdated = false;
    if (auto *summary = dynamic_cast<GitLabSummary *>(preview()))
        summary->readItself(m_traits.fetchPreview);
}

void GitLabList::showRowMenu(const QString &key, const QPoint &globalPos)
{
    const ListRowTarget target = m_targets.value(key);

    // It belongs to the view and does not wait, because the view can be made
    // again at any time.
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    QAction *add = menu->addAction(tr("Add to task as item"));
    add->setEnabled(target.complete());
    connect(add, &QAction::triggered, this, [this, target] {
        m_context->addItemToTask(m_traits.summaryType, target.project + m_traits.separator + QString::number(target.iid),
                                 QJsonObject{{QStringLiteral("project"), target.project},
                                             {QStringLiteral("iid"), target.iid}});
    });

    QAction *open = menu->addAction(tr("Open page"));
    open->setEnabled(!target.address.isEmpty());
    connect(open, &QAction::triggered, this, [this, address = target.address] { openPage(address); });

    QAction *copy = menu->addAction(tr("Copy link"));
    copy->setEnabled(!target.address.isEmpty());
    connect(copy, &QAction::triggered, this, [address = target.address] {
        QGuiApplication::clipboard()->setText(address.toString());
    });

    menu->popup(globalPos);
}

void GitLabList::openPage(const QUrl &address)
{
    if (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier)
        QDesktopServices::openUrl(address);
    else
        m_context->showPage(address);
}

} // namespace mirante::modules::gitlab
