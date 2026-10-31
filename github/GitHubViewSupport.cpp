#include "GitHubViewSupport.h"

#include "GitHubSettings.h"
#include "GitHubUrls.h"

#include <mirante/ItemContext.h>
#include <mirante/widgets/ActionRow.h>
#include <mirante/widgets/FlowRow.h>
#include <mirante/widgets/LinkStyle.h>

#include <QCoreApplication>
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

namespace mirante::modules::github {

namespace {

const QString kOpenPage = QStringLiteral("open-page");

QDateTime gitHubDate(const QString &text)
{
    return QDateTime::fromString(text, Qt::ISODateWithMs);
}

// A status of a run that has not completed.
bool isWaitingStatus(const QString &status)
{
    return status == QLatin1String("queued") || status == QLatin1String("waiting")
        || status == QLatin1String("requested") || status == QLatin1String("pending");
}

} // namespace

QString localDateTime(const QString &text)
{
    const QDateTime date = gitHubDate(text);
    return date.isValid() ? QLocale().toString(date.toLocalTime(), QLocale::ShortFormat) : text;
}

QString localDate(const QString &text)
{
    // A milestone's due_on is a whole date and time; its day is what counts.
    const QDateTime dateTime = gitHubDate(text);
    if (dateTime.isValid())
        return QLocale().toString(dateTime.toLocalTime().date(), QLocale::ShortFormat);
    const QDate date = QDate::fromString(text, Qt::ISODate);
    return date.isValid() ? QLocale().toString(date, QLocale::ShortFormat) : text;
}

QString durationText(const QJsonValue &seconds)
{
    if (!seconds.isDouble())
        return {};
    const qint64 total = std::llround(seconds.toDouble());
    if (total < 0)
        return {};
    if (total < 60)
        return QCoreApplication::translate("github", "%1 s").arg(total);
    if (total < 3600)
        return QCoreApplication::translate("github", "%1 min %2 s").arg(total / 60).arg(total % 60);
    return QCoreApplication::translate("github", "%1 h %2 min").arg(total / 3600).arg(total / 60 % 60);
}

QString durationBetween(const QString &start, const QString &end)
{
    const QDateTime from = gitHubDate(start);
    const QDateTime to = gitHubDate(end);
    if (!from.isValid() || !to.isValid())
        return {};
    return durationText(QJsonValue(double(from.secsTo(to))));
}

QString statusText(const QString &status, const QString &conclusion)
{
    if (status == QLatin1String("in_progress"))
        return QCoreApplication::translate("github", "Running");
    if (status == QLatin1String("queued"))
        return QCoreApplication::translate("github", "Queued");
    if (status != QLatin1String("completed"))
        return QCoreApplication::translate("github", "Waiting");
    if (conclusion == QLatin1String("success"))
        return QCoreApplication::translate("github", "Passed");
    if (conclusion == QLatin1String("timed_out"))
        return QCoreApplication::translate("github", "Timed out");
    if (isFailedConclusion(conclusion))
        return QCoreApplication::translate("github", "Failed");
    if (conclusion == QLatin1String("cancelled"))
        return QCoreApplication::translate("github", "Cancelled");
    if (conclusion == QLatin1String("skipped"))
        return QCoreApplication::translate("github", "Skipped");
    if (conclusion == QLatin1String("neutral"))
        return QCoreApplication::translate("github", "Neutral");
    if (conclusion == QLatin1String("action_required"))
        return QCoreApplication::translate("github", "Action required");
    if (conclusion == QLatin1String("stale"))
        return QCoreApplication::translate("github", "Stale");
    QString words = conclusion;
    words.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!words.isEmpty())
        words[0] = words[0].toUpper();
    return words;
}

StatusRole statusRole(const QString &status, const QString &conclusion)
{
    if (status != QLatin1String("completed"))
        return StatusRole::Waiting;
    if (conclusion == QLatin1String("success"))
        return StatusRole::Good;
    if (isFailedConclusion(conclusion))
        return StatusRole::Bad;
    if (conclusion == QLatin1String("action_required"))
        return StatusRole::Waiting;
    return StatusRole::Neutral;
}

StatusPill *runPill(const QJsonObject &object)
{
    const QString status = object.value(QStringLiteral("status")).toString();
    const QString conclusion = object.value(QStringLiteral("conclusion")).toString();
    return new StatusPill(statusText(status, conclusion), statusRole(status, conclusion));
}

StatusPill *commitStatusPill(const QJsonObject &status)
{
    const QString state = status.value(QStringLiteral("state")).toString();
    if (state == QLatin1String("success"))
        return new StatusPill(QCoreApplication::translate("github", "Passed"), StatusRole::Good);
    if (state == QLatin1String("failure"))
        return new StatusPill(QCoreApplication::translate("github", "Failed"), StatusRole::Bad);
    if (state == QLatin1String("error"))
        return new StatusPill(QCoreApplication::translate("github", "Error"), StatusRole::Bad);
    if (state == QLatin1String("pending"))
        return new StatusPill(QCoreApplication::translate("github", "Pending"), StatusRole::Waiting);
    return new StatusPill(state, StatusRole::Neutral);
}

QString checksText(ChecksState state)
{
    switch (state) {
    case ChecksState::Passed:
        return QCoreApplication::translate("github", "Checks passed");
    case ChecksState::Failed:
        return QCoreApplication::translate("github", "Checks failed");
    case ChecksState::Running:
        return QCoreApplication::translate("github", "Checks running");
    case ChecksState::None:
        break;
    }
    return QCoreApplication::translate("github", "No checks");
}

StatusRole checksRole(ChecksState state)
{
    switch (state) {
    case ChecksState::Passed:
        return StatusRole::Good;
    case ChecksState::Failed:
        return StatusRole::Bad;
    case ChecksState::Running:
        return StatusRole::Waiting;
    case ChecksState::None:
        break;
    }
    return StatusRole::Neutral;
}

StatusPill *checksPill(ChecksState state)
{
    return new StatusPill(checksText(state), checksRole(state));
}

StatusPill *reviewPill(const QString &state)
{
    if (state == QLatin1String("APPROVED"))
        return new StatusPill(QCoreApplication::translate("github", "Approved"), StatusRole::Good);
    if (state == QLatin1String("CHANGES_REQUESTED"))
        return new StatusPill(QCoreApplication::translate("github", "Changes requested"), StatusRole::Bad);
    if (state == QLatin1String("COMMENTED"))
        return new StatusPill(QCoreApplication::translate("github", "Commented"), StatusRole::Neutral);
    if (state.isEmpty())
        return new StatusPill(QCoreApplication::translate("github", "Waiting"), StatusRole::Waiting);
    QString words = state.toLower();
    words.replace(QLatin1Char('_'), QLatin1Char(' '));
    words[0] = words[0].toUpper();
    return new StatusPill(words, StatusRole::Neutral);
}

bool canCancelRun(const QString &status)
{
    return status == QLatin1String("in_progress") || isWaitingStatus(status);
}

bool isWebAddress(const QUrl &url)
{
    const QString scheme = url.scheme();
    return scheme == QLatin1String("https") || scheme == QLatin1String("http");
}

QUrl webAddress(const QJsonObject &object, const QUrl &fallback)
{
    const QUrl url(object.value(QStringLiteral("html_url")).toString());
    return isWebAddress(url) ? url : fallback;
}

QString actionError(const ApiReply &reply)
{
    if (reply.error == ApiReply::Error::Forbidden || reply.httpStatus == 403)
        return QCoreApplication::translate("github", "The token cannot do this. It needs write access to Actions.");
    return reply.message.isEmpty() ? QCoreApplication::translate("github", "The action failed.") : reply.message;
}

QString userNames(const QJsonArray &users)
{
    QStringList names;
    for (const QJsonValue &user : users) {
        const QString login = user.toObject().value(QStringLiteral("login")).toString();
        if (!login.isEmpty())
            names << login;
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

StatusPill *statePill(const QJsonObject &object)
{
    const bool merged = object.value(QStringLiteral("merged")).toBool()
        || !object.value(QStringLiteral("merged_at")).toString().isEmpty();
    if (merged)
        return new StatusPill(QCoreApplication::translate("github", "Merged"), StatusRole::Good);
    const QString state = object.value(QStringLiteral("state")).toString();
    if (state == QLatin1String("open"))
        return new StatusPill(QCoreApplication::translate("github", "Open"), StatusRole::Good);
    if (state == QLatin1String("closed"))
        return new StatusPill(QCoreApplication::translate("github", "Closed"), StatusRole::Neutral);
    return new StatusPill(state, StatusRole::Neutral);
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
        return plainLine(QCoreApplication::translate("github", "No labels"));
    auto *row = new QWidget;
    auto *flow = new widgets::FlowRow(row);
    for (const QJsonValue &label : labels)
        flow->addWidget(new StatusPill(label.toObject().value(QStringLiteral("name")).toString(), StatusRole::Neutral));
    return row;
}

GitHubSummary::GitHubSummary(ItemContext &context, QWidget *parent)
    : widgets::SummaryView(parent)
    , m_context(&context)
{
}

void GitHubSummary::follow(ItemStatusSource *source)
{
    if (!source) {
        showStatus({});
        return;
    }
    m_refresh = [source] { source->refresh(); };
    showStatus(source->current());
    source->subscribe(this, [this](const ItemStatus &status) { showStatus(status); });
}

void GitHubSummary::showStatus(const ItemStatus &status)
{
    // Old data stays through an error. Problems show in the cell header. A
    // message without an error, such as a main folder that left its branch,
    // says the item has nothing to show now, so it replaces the old data.
    if (status.data.isEmpty()) {
        if (!m_drawn || (status.state == ItemStatus::State::None && !status.message.isEmpty()))
            setMessage(status.message.isEmpty() ? QCoreApplication::translate("github", "Loading…") : status.message);
        return;
    }
    m_drawn = true;
    setMessage({});
    draw(status);
}

QJsonObject GitHubSummary::config() const
{
    return m_context->config();
}

QString GitHubSummary::repository() const
{
    return repositoryOf(config(), defaultRepository());
}

ApiClient *GitHubSummary::server() const
{
    return m_context->server();
}

QUrl GitHubSummary::webBase() const
{
    return webBaseOf(m_context->moduleSettings(kGeneralScreen));
}

QJsonValue GitHubSummary::defaultRepository() const
{
    return m_context->projectSetting(kProjectSettingKey);
}

void GitHubSummary::applyField(const QString &section, const QString &field)
{
    setSectionVisible(section, config().value(field).toBool(true));
}

void GitHubSummary::addOpenPage()
{
    actions()->addLinkAction(kOpenPage, QCoreApplication::translate("github", "Open page"),
                             QCoreApplication::translate("github", "Page"), [this] { openPage(); });
}

void GitHubSummary::setPageAddress(const QUrl &url)
{
    m_pageAddress = isWebAddress(url) ? url : QUrl();
    actions()->setWebLink(m_pageAddress);
    actions()->setActionVisible(kOpenPage, !m_pageAddress.isEmpty());
}

void GitHubSummary::openPage()
{
    if (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier)
        QDesktopServices::openUrl(m_pageAddress);
    else
        m_context->showPage(m_pageAddress);
}

void GitHubSummary::refreshStatus()
{
    if (m_refresh)
        m_refresh();
}

void GitHubSummary::runAction(const QString &id, const Request &request)
{
    actions()->setBusy(id, true);
    run(request, [this, id] { actions()->setBusy(id, false); });
}

void GitHubSummary::run(const Request &request, const std::function<void()> &finished)
{
    ApiClient *server = m_context->server();
    if (!server) {
        finished();
        actions()->showError(QCoreApplication::translate("github", "This item has no server."));
        return;
    }
    request(GitHubClient(*server, this), [this, finished](const ApiReply &reply) {
        finished();
        if (!reply.ok())
            actions()->showError(actionError(reply));
        else
            refreshStatus();
    });
}

} // namespace mirante::modules::github
