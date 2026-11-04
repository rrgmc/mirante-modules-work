#include "GitHubListSupport.h"

#include "GitHubClient.h"
#include "GitHubUrls.h"
#include "GitHubViewSupport.h"

#include <mirante/widgets/CompactDate.h>

#include <QAction>
#include <QCoreApplication>
#include <QDateTime>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QScopedValueRollback>

using mirante::widgets::ListCell;
using mirante::widgets::ListLink;


namespace mirante::modules::github {

namespace {

const QString kWebLink = QStringLiteral("web");
const QString kOpenLink = QStringLiteral("open");
const QString kAddLink = QStringLiteral("add");

bool sameCell(const ListCell &a, const ListCell &b)
{
    if (a.text != b.text || a.sortKey != b.sortKey || a.role != b.role || a.links.size() != b.links.size())
        return false;
    for (qsizetype i = 0; i < a.links.size(); ++i) {
        if (a.links.at(i).id != b.links.at(i).id || a.links.at(i).label != b.links.at(i).label)
            return false;
    }
    return true;
}

} // namespace

QString rowRepository(const QJsonObject &row)
{
    return GitHubClient::repositoryOfApiUrl(row.value(QStringLiteral("repository_url")).toString());
}

NumberedRow readNumberedRow(const QJsonObject &row, int index, const QUrl &webBase, ListKind kind,
                            const QString &summaryType)
{
    NumberedRow read;
    read.repository = rowRepository(row);
    read.number = row.value(QStringLiteral("number")).toInteger();
    const bool complete = !read.repository.isEmpty() && read.number > 0;
    const QString ref = read.repository + QLatin1Char('#') + QString::number(read.number);
    read.row.key = complete ? ref : QStringLiteral("row-%1").arg(index);
    read.row.data = row;
    read.refSortKey = read.repository + QLatin1Char('#') + QStringLiteral("%1").arg(read.number, 12, 10, QLatin1Char('0'));
    read.ref = read.repository.section(QLatin1Char('/'), -1) + QLatin1Char('#') + QString::number(read.number);

    const QJsonObject config{{QStringLiteral("repository"), read.repository}, {QStringLiteral("number"), read.number}};
    const QUrl built =
        kind == ListKind::PullRequests ? pullRequestUrl(webBase, config, {}) : issueUrl(webBase, config, {});
    const QUrl address = webAddress(row, built);
    read.row.address = isWebAddress(address) ? address : QUrl();

    if (complete)
        read.add = {summaryType, ref, config};
    return read;
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

ListCell textCell(const QString &text)
{
    return text.isEmpty() ? dashCell() : ListCell{text, {}, {}};
}

ListCell pillCell(const Pill &pill)
{
    return pill.text.isEmpty() ? dashCell() : ListCell{pill.text, {}, pill.role};
}

DefaultListLayout::DefaultListLayout(ListModel &model, const QString &view, QWidget *parent)
    : widgets::ItemListView(parent)
    , m_model(model)
    , m_emptyLine(plainLine(model.emptyText()))
{
    setObjectName(defaultLayoutName(view));
    // setMessage() shows instead of the table, and this shows with it.
    m_emptyLine->setObjectName(QStringLiteral("emptyLine"));
    m_emptyLine->setAlignment(Qt::AlignCenter);
    layout()->addWidget(m_emptyLine);
    m_emptyLine->hide();

    setDescription(model.description());
    setPageSize(model.pageSize());
    connect(this, &ItemListView::filterChanged, this, [this](int column, const QStringList &values) {
        const auto name = m_keptFilters.constFind(column);
        if (name != m_keptFilters.constEnd())
            m_model.keepFilter(*name, values);
    });
}

DefaultListLayout::~DefaultListLayout() = default;

void DefaultListLayout::addKeptFilter(int column, const QString &label, const QString &name)
{
    addFilter(column, label);
    setFilterDefault(column, m_model.filterDefault(name));
    m_keptFilters.insert(column, name);
}

void DefaultListLayout::start()
{
    connect(this, &ItemListView::rowMenuRequested, this,
            [this](const QString &key, const QPoint &globalPos) { showRowMenu(key, globalPos); });
    connect(this, &ItemListView::rowLinkActivated, this,
            [this](const QString &key, const QString &link) { activateLink(key, link); });
    connect(this, &ItemListView::visibleKeysChanged, this, [this] {
        if (!m_drawing)
            m_model.setVisibleRows(visibleKeys());
    });
    connect(&m_model, &ListModel::changed, this, [this] { draw(); });
    connect(&m_model, &ListModel::rowChanged, this, [this](const QString &key) { drawRow(key); });
    draw();
}

ListCell DefaultListLayout::linksCell(const ListRow &row, const QVariant &sortKey) const
{
    ListCell cell;
    cell.sortKey = sortKey;
    if (!row.address.isEmpty())
        // "Open" is also a state; the link opens the row, so it is told apart.
        cell.links << ListLink{kWebLink, QCoreApplication::translate("github", "Web")}
                   << ListLink{kOpenLink, QCoreApplication::translate("github", "Open", "link that opens a row")};
    if (row.canAdd)
        cell.links << ListLink{kAddLink, QCoreApplication::translate("github", "Add")};
    return cell;
}

void DefaultListLayout::draw()
{
    if (!m_model.hasData()) {
        setMessage(m_model.message());
        return;
    }
    setMessage({});
    QList<widgets::ListRow> rows;
    m_drawn.clear();
    for (const QString &key : m_model.keys()) {
        const QList<ListCell> cells = rowCells(key);
        m_drawn.insert(key, cells);
        rows.append(widgets::ListRow{key, cells});
    }
    {
        const QScopedValueRollback drawing(m_drawing, true);
        setRows(rows);
    }
    m_emptyLine->setVisible(rows.isEmpty());
    m_model.setVisibleRows(visibleKeys());
}

// Only the cells that changed, since each one can sort the table again.
void DefaultListLayout::drawRow(const QString &key)
{
    const auto drawn = m_drawn.find(key);
    if (drawn == m_drawn.end())
        return;
    // A cell can sort the table again. The model keeps the new shown rows, but
    // does not ask for more while it tells of an answer.
    const QList<ListCell> cells = rowCells(key);
    for (int column = 0; column < cells.size(); ++column) {
        if (column < drawn->size() && sameCell(drawn->at(column), cells.at(column)))
            continue;
        setCell(key, column, cells.at(column));
    }
    *drawn = cells;
}

void DefaultListLayout::showRowMenu(const QString &key, const QPoint &globalPos)
{
    const ListRow row = m_model.row(key);
    const bool hasAddress = !row.address.isEmpty();
    const bool canAdd = row.canAdd;

    // It belongs to the view and does not wait, because the view can be made
    // again at any time.
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    QAction *web = menu->addAction(QCoreApplication::translate("github", "Web"));
    web->setEnabled(hasAddress);
    connect(web, &QAction::triggered, this, [this, key] { m_model.openWeb(key); });

    QAction *open = menu->addAction(QCoreApplication::translate("github", "Open page"));
    open->setEnabled(hasAddress);
    connect(open, &QAction::triggered, this, [this, key] { m_model.openPage(key); });

    QAction *add = menu->addAction(QCoreApplication::translate("github", "Add to task as item"));
    add->setEnabled(canAdd);
    connect(add, &QAction::triggered, this, [this, key] { m_model.addToTask(key); });

    QAction *copy = menu->addAction(QCoreApplication::translate("github", "Copy link"));
    copy->setEnabled(hasAddress);
    connect(copy, &QAction::triggered, this, [this, key] { m_model.copyLink(key); });

    menu->popup(globalPos);
}

void DefaultListLayout::activateLink(const QString &key, const QString &link)
{
    if (link == kWebLink)
        m_model.openWeb(key);
    else if (link == kOpenLink)
        m_model.openPage(key);
    else if (link == kAddLink)
        m_model.addToTask(key);
}

} // namespace mirante::modules::github
