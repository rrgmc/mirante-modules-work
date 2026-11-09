#include "GitLabViewSupport.h"

#include "GitLabSettings.h"
#include "GitLabUrls.h"

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

namespace mirante::modules::gitlab {

namespace {

const QString kOpenPage = QStringLiteral("open-page");

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
        return QCoreApplication::translate("gitlab", "%1 s").arg(total);
    if (total < 3600)
        return QCoreApplication::translate("gitlab", "%1 min %2 s").arg(total / 60).arg(total % 60);
    return QCoreApplication::translate("gitlab", "%1 h %2 min").arg(total / 3600).arg(total / 60 % 60);
}

QString statusText(const QString &status)
{
    if (status == QLatin1String("success"))
        return QCoreApplication::translate("gitlab", "Passed");
    QString words = status;
    words.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!words.isEmpty())
        words[0] = words[0].toUpper();
    return words;
}

QStringList pipelineStatusTexts()
{
    QStringList texts;
    for (const char *status : {"created", "waiting_for_resource", "preparing", "pending", "running", "success",
                               "failed", "canceled", "skipped", "manual", "scheduled"})
        texts << statusText(QString::fromLatin1(status));
    return texts;
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
        return QCoreApplication::translate("gitlab", "The token cannot do this. It needs the api scope.");
    return reply.message.isEmpty() ? QCoreApplication::translate("gitlab", "The action failed.") : reply.message;
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

Pill statusPillOf(const QString &status)
{
    return {statusText(status), statusRole(status)};
}

Pill statePillOf(const QString &state)
{
    if (state == QLatin1String("opened"))
        return {QCoreApplication::translate("gitlab", "Open"), StatusRole::Good};
    if (state == QLatin1String("merged"))
        return {QCoreApplication::translate("gitlab", "Merged"), StatusRole::Good};
    if (state == QLatin1String("closed"))
        return {QCoreApplication::translate("gitlab", "Closed"), StatusRole::Neutral};
    if (state == QLatin1String("locked"))
        return {QCoreApplication::translate("gitlab", "Locked"), StatusRole::Neutral};
    return {statusText(state), StatusRole::Neutral};
}

Pill jobPillOf(const QJsonObject &job)
{
    const QString status = job.value(QStringLiteral("status")).toString();
    if (status == QLatin1String("failed") && job.value(QStringLiteral("allow_failure")).toBool())
        return {QCoreApplication::translate("gitlab", "Failed, allowed"), StatusRole::Waiting};
    return statusPillOf(status);
}

StatusPill *pillWidget(const Pill &pill)
{
    return new StatusPill(pill.text, pill.role);
}

StatusPill *statePill(const QString &state)
{
    return pillWidget(statePillOf(state));
}

StatusPill *jobPill(const QJsonObject &job)
{
    return pillWidget(jobPillOf(job));
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
        return plainLine(QCoreApplication::translate("gitlab", "No labels"));
    auto *row = new QWidget;
    auto *flow = new widgets::FlowRow(row);
    for (const QJsonValue &label : labels)
        flow->addWidget(new StatusPill(label.toString(), StatusRole::Neutral));
    return row;
}

QString defaultLayoutName(const QString &view)
{
    return QStringLiteral("gitlab-%1-default-layout").arg(view);
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
    view->actions()->addLinkAction(kOpenPage, QCoreApplication::translate("gitlab", "Open page"),
                                   QCoreApplication::translate("gitlab", "Page"), [&model] { model.openPage(); });
}

void showPageAddress(widgets::SummaryView *view, const SummaryModel &model)
{
    view->actions()->setActionVisible(kOpenPage, !model.pageAddress().isEmpty());
}

} // namespace mirante::modules::gitlab
