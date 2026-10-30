#include "GitLabViewSupport.h"

#include "GitLabSettings.h"
#include "GitLabUrls.h"

#include <mirante/ItemContext.h>
#include <mirante/widgets/ActionRow.h>
#include <mirante/widgets/FlowRow.h>
#include <mirante/widgets/LinkStyle.h>

#include <QDate>
#include <QDateTime>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QLabel>
#include <QLocale>
#include <QObject>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

#include <cmath>

using mirante::widgets::StatusPill;
using mirante::widgets::StatusRole;

namespace mirante::modules::gitlab {

namespace {

const QString kOpenPage = QStringLiteral("open-page");

QString translated(const char *source)
{
    return QCoreApplication::translate("gitlab", source);
}

} // namespace

QString localDateTime(const QString &text)
{
    const QDateTime date = QDateTime::fromString(text, Qt::ISODateWithMs);
    return date.isValid() ? QLocale().toString(date.toLocalTime(), QLocale::ShortFormat) : text;
}

QString localDate(const QString &text)
{
    const QDate date = QDate::fromString(text, Qt::ISODate);
    return date.isValid() ? QLocale().toString(date, QLocale::ShortFormat) : text;
}

QString durationText(const QJsonValue &seconds)
{
    if (!seconds.isDouble())
        return {};
    const qint64 total = std::llround(seconds.toDouble());
    if (total < 60)
        return translated("%1 s").arg(total);
    if (total < 3600)
        return translated("%1 min %2 s").arg(total / 60).arg(total % 60);
    return translated("%1 h %2 min").arg(total / 3600).arg(total / 60 % 60);
}

QString statusText(const QString &status)
{
    if (status == QLatin1String("success"))
        return translated("Passed");
    QString words = status;
    words.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!words.isEmpty())
        words[0] = words[0].toUpper();
    return words;
}

StatusRole statusRole(const QString &status)
{
    if (status == QLatin1String("success"))
        return StatusRole::Good;
    if (status == QLatin1String("failed"))
        return StatusRole::Bad;
    if (status == QLatin1String("canceled") || status == QLatin1String("skipped")
        || status == QLatin1String("manual") || status == QLatin1String("created"))
        return StatusRole::Neutral;
    return StatusRole::Waiting;
}

bool canRetryPipeline(const QString &status)
{
    return status == QLatin1String("failed") || status == QLatin1String("canceled");
}

bool isWebAddress(const QUrl &url)
{
    const QString scheme = url.scheme();
    return scheme == QLatin1String("https") || scheme == QLatin1String("http");
}

QUrl webAddress(const QJsonObject &object, const QUrl &fallback)
{
    const QUrl url(object.value(QStringLiteral("web_url")).toString());
    return isWebAddress(url) ? url : fallback;
}

QString actionError(const ApiReply &reply)
{
    if (reply.error == ApiReply::Error::Forbidden || reply.httpStatus == 403)
        return translated("The token cannot do this. It needs the api scope.");
    return reply.message.isEmpty() ? translated("The action failed.") : reply.message;
}

QString userNames(const QJsonArray &users)
{
    QStringList names;
    for (const QJsonValue &user : users) {
        const QString name = user.toObject().value(QStringLiteral("name")).toString();
        if (!name.isEmpty())
            names << name;
    }
    return names.join(QStringLiteral(", "));
}

QLabel *plainLine(const QString &text)
{
    QLabel *label = plainWord(text);
    label->setWordWrap(true);
    return label;
}

QLabel *plainWord(const QString &text)
{
    auto *label = new QLabel(text);
    label->setTextFormat(Qt::PlainText);
    return label;
}

StatusPill *statePill(const QString &state)
{
    if (state == QLatin1String("opened"))
        return new StatusPill(translated("Open"), StatusRole::Good);
    if (state == QLatin1String("merged"))
        return new StatusPill(translated("Merged"), StatusRole::Good);
    if (state == QLatin1String("closed"))
        return new StatusPill(translated("Closed"), StatusRole::Neutral);
    if (state == QLatin1String("locked"))
        return new StatusPill(translated("Locked"), StatusRole::Neutral);
    return new StatusPill(statusText(state), StatusRole::Neutral);
}

StatusPill *jobPill(const QJsonObject &job)
{
    const QString status = job.value(QStringLiteral("status")).toString();
    if (status == QLatin1String("failed") && job.value(QStringLiteral("allow_failure")).toBool())
        return new StatusPill(translated("Failed, allowed"), StatusRole::Waiting);
    return new StatusPill(statusText(status), statusRole(status));
}

QPushButton *linkButton(const QString &text, const QString &name)
{
    auto *button = new QPushButton(text);
    button->setObjectName(name);
    widgets::styleAsLink(button);
    return button;
}

QPushButton *webButton(const QString &text, const QString &name, const QUrl &url)
{
    if (!isWebAddress(url))
        return nullptr;
    QPushButton *button = linkButton(text, name);
    QObject::connect(button, &QPushButton::clicked, button, [url] { QDesktopServices::openUrl(url); });
    return button;
}

QWidget *newColumn()
{
    auto *column = new QWidget;
    auto *layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    return column;
}

QWidget *labelChips(const QJsonArray &labels)
{
    if (labels.isEmpty())
        return plainLine(translated("No labels"));
    auto *row = new QWidget;
    auto *flow = new widgets::FlowRow(row);
    for (const QJsonValue &label : labels)
        flow->addWidget(new StatusPill(label.toString(), StatusRole::Neutral));
    return row;
}

GitLabSummary::GitLabSummary(ItemContext &context, QWidget *parent)
    : widgets::SummaryView(parent)
    , m_context(&context)
{
}

void GitLabSummary::follow(ItemStatusSource *source)
{
    if (!source) {
        showStatus({});
        return;
    }
    m_refresh = [source] { source->refresh(); };
    showStatus(source->current());
    source->subscribe(this, [this](const ItemStatus &status) { showStatus(status); });
}

void GitLabSummary::showStatus(const ItemStatus &status)
{
    // Old data stays through an error. Problems show in the cell header.
    if (status.data.isEmpty()) {
        if (!m_drawn)
            setMessage(status.message.isEmpty() ? tr("Loading…") : status.message);
        return;
    }
    m_drawn = true;
    setMessage({});
    draw(status);
}

QJsonObject GitLabSummary::config() const
{
    return m_context->config();
}

QString GitLabSummary::project() const
{
    return projectPath(config(), defaultProject());
}

ApiClient *GitLabSummary::server() const
{
    return m_context->server();
}

QUrl GitLabSummary::baseUrl() const
{
    return baseUrlOf(m_context->moduleSettings(kGeneralScreen));
}

QJsonValue GitLabSummary::defaultProject() const
{
    return m_context->projectSetting(kProjectSettingKey);
}

void GitLabSummary::applyField(const QString &section, const QString &field)
{
    setSectionVisible(section, config().value(field).toBool(true));
}

void GitLabSummary::addOpenPage()
{
    actions()->addLinkAction(kOpenPage, tr("Open page"), tr("Page"), [this] { openPage(); });
}

void GitLabSummary::setPageAddress(const QUrl &url)
{
    m_pageAddress = isWebAddress(url) ? url : QUrl();
    actions()->setWebLink(m_pageAddress);
    actions()->setActionVisible(kOpenPage, !m_pageAddress.isEmpty());
}

void GitLabSummary::openPage()
{
    if (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier)
        QDesktopServices::openUrl(m_pageAddress);
    else
        m_context->showPage(m_pageAddress);
}

void GitLabSummary::refreshStatus()
{
    if (m_refresh)
        m_refresh();
}

void GitLabSummary::runAction(const QString &id, const Request &request)
{
    actions()->setBusy(id, true);
    run(request, [this, id] { actions()->setBusy(id, false); });
}

void GitLabSummary::run(const Request &request, const std::function<void()> &finished)
{
    ApiClient *server = m_context->server();
    if (!server) {
        finished();
        actions()->showError(tr("This item has no server."));
        return;
    }
    request(GitLabClient(*server, this), [this, finished](const ApiReply &reply) {
        finished();
        if (!reply.ok())
            actions()->showError(actionError(reply));
        else
            refreshStatus();
    });
}

} // namespace mirante::modules::gitlab
