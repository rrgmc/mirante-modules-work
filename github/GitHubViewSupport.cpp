// Stubs until task T5 of the M11 plan implements README.md#summary-views.
#include "GitHubViewSupport.h"

#include "GitHubSettings.h"
#include "GitHubUrls.h"

#include <mirante/ItemContext.h>

#include <QCoreApplication>
#include <QLabel>
#include <QPushButton>
#include <QWidget>

using mirante::widgets::StatusPill;
using mirante::widgets::StatusRole;

namespace mirante::modules::github {

QString localDateTime(const QString &text)
{
    return text;
}

QString localDate(const QString &text)
{
    return text;
}

QString durationText(const QJsonValue &seconds)
{
    Q_UNUSED(seconds)
    return {};
}

QString durationBetween(const QString &start, const QString &end)
{
    Q_UNUSED(start)
    Q_UNUSED(end)
    return {};
}

QString statusText(const QString &status, const QString &conclusion)
{
    return conclusion.isEmpty() ? status : conclusion;
}

StatusRole statusRole(const QString &status, const QString &conclusion)
{
    Q_UNUSED(status)
    Q_UNUSED(conclusion)
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
    return new StatusPill(status.value(QStringLiteral("state")).toString(), StatusRole::Neutral);
}

QString checksText(ChecksState state)
{
    Q_UNUSED(state)
    return {};
}

StatusRole checksRole(ChecksState state)
{
    Q_UNUSED(state)
    return StatusRole::Neutral;
}

StatusPill *checksPill(ChecksState state)
{
    return new StatusPill(checksText(state), checksRole(state));
}

StatusPill *reviewPill(const QString &state)
{
    return new StatusPill(state, StatusRole::Neutral);
}

bool canCancelRun(const QString &status)
{
    Q_UNUSED(status)
    return false;
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
    return reply.message;
}

QString userNames(const QJsonArray &users)
{
    Q_UNUSED(users)
    return {};
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
    return new StatusPill(object.value(QStringLiteral("state")).toString(), StatusRole::Neutral);
}

QPushButton *linkButton(const QString &text, const QString &name)
{
    auto *button = new QPushButton(text);
    button->setObjectName(name);
    return button;
}

QPushButton *webButton(const QString &text, const QString &name, const QUrl &url)
{
    return isWebAddress(url) ? linkButton(text, name) : nullptr;
}

QWidget *newColumn()
{
    return new QWidget;
}

QWidget *labelChips(const QJsonArray &labels)
{
    Q_UNUSED(labels)
    return new QWidget;
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
    if (status.data.isEmpty()) {
        if (!m_drawn)
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
}

void GitHubSummary::setPageAddress(const QUrl &url)
{
    m_pageAddress = isWebAddress(url) ? url : QUrl();
}

void GitHubSummary::openPage()
{
    m_context->showPage(m_pageAddress);
}

void GitHubSummary::refreshStatus()
{
    if (m_refresh)
        m_refresh();
}

void GitHubSummary::runAction(const QString &id, const Request &request)
{
    Q_UNUSED(id)
    run(request, [] {});
}

void GitHubSummary::run(const Request &request, const std::function<void()> &finished)
{
    ApiClient *server = m_context->server();
    if (!server) {
        finished();
        return;
    }
    request(GitHubClient(*server, this), [this, finished](const ApiReply &reply) {
        finished();
        if (reply.ok())
            refreshStatus();
    });
}

} // namespace mirante::modules::github
