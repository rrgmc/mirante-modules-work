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

Pill runPillOf(const QJsonObject &object)
{
    const QString status = object.value(QStringLiteral("status")).toString();
    const QString conclusion = object.value(QStringLiteral("conclusion")).toString();
    return {statusText(status, conclusion), statusRole(status, conclusion)};
}

StatusPill *runPill(const QJsonObject &object)
{
    return pillWidget(runPillOf(object));
}

Pill commitStatusPillOf(const QJsonObject &status)
{
    const QString state = status.value(QStringLiteral("state")).toString();
    if (state == QLatin1String("success"))
        return {QCoreApplication::translate("github", "Passed"), StatusRole::Good};
    if (state == QLatin1String("failure"))
        return {QCoreApplication::translate("github", "Failed"), StatusRole::Bad};
    if (state == QLatin1String("error"))
        return {QCoreApplication::translate("github", "Error"), StatusRole::Bad};
    if (state == QLatin1String("pending"))
        return {QCoreApplication::translate("github", "Pending"), StatusRole::Waiting};
    return {state, StatusRole::Neutral};
}

StatusPill *commitStatusPill(const QJsonObject &status)
{
    return pillWidget(commitStatusPillOf(status));
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

Pill checksPillOf(ChecksState state)
{
    return {checksText(state), checksRole(state)};
}

StatusPill *checksPill(ChecksState state)
{
    return pillWidget(checksPillOf(state));
}

Pill reviewPillOf(const QString &state)
{
    if (state == QLatin1String("APPROVED"))
        return {QCoreApplication::translate("github", "Approved"), StatusRole::Good};
    if (state == QLatin1String("CHANGES_REQUESTED"))
        return {QCoreApplication::translate("github", "Changes requested"), StatusRole::Bad};
    if (state == QLatin1String("COMMENTED"))
        return {QCoreApplication::translate("github", "Commented"), StatusRole::Neutral};
    if (state.isEmpty())
        return {QCoreApplication::translate("github", "Waiting"), StatusRole::Waiting};
    QString words = state.toLower();
    words.replace(QLatin1Char('_'), QLatin1Char(' '));
    words[0] = words[0].toUpper();
    return {words, StatusRole::Neutral};
}

StatusPill *reviewPill(const QString &state)
{
    return pillWidget(reviewPillOf(state));
}

StatusPill *pillWidget(const Pill &pill)
{
    return new StatusPill(pill.text, pill.role);
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

Pill statePillOf(const QJsonObject &object)
{
    const bool merged = object.value(QStringLiteral("merged")).toBool()
        || !object.value(QStringLiteral("merged_at")).toString().isEmpty();
    if (merged)
        return {QCoreApplication::translate("github", "Merged"), StatusRole::Good};
    const QString state = object.value(QStringLiteral("state")).toString();
    if (state == QLatin1String("open"))
        return {QCoreApplication::translate("github", "Open"), StatusRole::Good};
    if (state == QLatin1String("closed"))
        return {QCoreApplication::translate("github", "Closed"), StatusRole::Neutral};
    return {state, StatusRole::Neutral};
}

StatusPill *statePill(const QJsonObject &object)
{
    return pillWidget(statePillOf(object));
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

QString defaultLayoutName(const QString &view)
{
    return QStringLiteral("github-%1-default-layout").arg(view);
}

void followActions(widgets::SummaryView *view, SummaryModel &model)
{
    QObject::connect(&model, &SummaryModel::busyChanged, view, [view, &model](const QString &action) {
        view->actions()->setBusy(action, model.isBusy(action));
    });
    QObject::connect(&model, &SummaryModel::errorChanged, view, [view, &model] {
        if (model.error().isEmpty())
            view->actions()->clearError();
        else
            view->actions()->showError(model.error());
    });
}

void addOpenPage(widgets::SummaryView *view, SummaryModel &model)
{
    view->actions()->addLinkAction(kOpenPage, QCoreApplication::translate("github", "Open page"),
                                   QCoreApplication::translate("github", "Page"), [&model] { model.openPage(); });
}

void showPageAddress(widgets::SummaryView *view, const SummaryModel &model)
{
    view->actions()->setWebLink(model.pageAddress());
    view->actions()->setActionVisible(kOpenPage, !model.pageAddress().isEmpty());
}

} // namespace mirante::modules::github
