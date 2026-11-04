#include "GitLabServiceStatus.h"

#include <mirante/ItemContext.h>
#include <mirante/widgets/ActionRow.h>
#include <mirante/widgets/CompactDate.h>
#include <mirante/widgets/FlowRow.h>
#include <mirante/widgets/StatusPill.h>
#include <mirante/widgets/SummaryView.h>

#include <QCoreApplication>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QVBoxLayout>

#include <utility>

namespace mirante::modules::gitlab {

namespace {

constexpr int kTimeoutMs = 30000;
// The longest incident message the view shows.
constexpr int kMessageChars = 400;

// The id of the gitlab.com page at status.io.
constexpr char kPageId[] = "5b36dc6502d06804c08349f7";

// The level of a status.io status code, on the scale of the view.
QString levelOfCode(int code)
{
    switch (code) {
    case 200:
        return QStringLiteral("maintenance");
    case 300:
        return QStringLiteral("minor");
    case 400:
        return QStringLiteral("major");
    case 500:
    case 600:
        return QStringLiteral("critical");
    default:
        return QStringLiteral("none");
    }
}

// The text of a status.io status code, or the page's own text for another.
QString textOfCode(int code, const QString &fallback)
{
    switch (code) {
    case 100:
        return QCoreApplication::translate("gitlab", "Operational");
    case 200:
        return QCoreApplication::translate("gitlab", "Planned maintenance");
    case 300:
        return QCoreApplication::translate("gitlab", "Degraded performance");
    case 400:
        return QCoreApplication::translate("gitlab", "Partial service disruption");
    case 500:
        return QCoreApplication::translate("gitlab", "Service disruption");
    case 600:
        return QCoreApplication::translate("gitlab", "Security event");
    default:
        return fallback;
    }
}

// The state of an incident's message, as the view names it.
QString stateOfCode(int code)
{
    switch (code) {
    case 100:
        return QStringLiteral("investigating");
    case 200:
        return QStringLiteral("identified");
    case 300:
        return QStringLiteral("monitoring");
    default:
        return {};
    }
}

// The newest message of an incident or a maintenance.
QJsonObject latestMessage(const QJsonObject &entry)
{
    QJsonObject latest;
    QDateTime latestTime;
    for (const QJsonValue &value : entry.value(QStringLiteral("messages")).toArray()) {
        const QJsonObject message = value.toObject();
        const QDateTime time =
            QDateTime::fromString(message.value(QStringLiteral("datetime")).toString(), Qt::ISODateWithMs);
        if (latest.isEmpty() || (time.isValid() && (!latestTime.isValid() || time >= latestTime))) {
            latest = message;
            latestTime = time;
        }
    }
    return latest;
}

// The address of an incident or a maintenance on the page.
QString pageUrl(const QString &page, const QString &kind, const QString &id)
{
    return QStringLiteral("%1/pages/%2/%3/%4").arg(page, kind, QLatin1String(kPageId), id);
}

// The page's levels, from best to worst. "maintenance" ranks above "none" so
// that it shows, and below the outages.
int rankOf(const QString &level)
{
    static const QStringList levels{QStringLiteral("none"), QStringLiteral("maintenance"), QStringLiteral("minor"),
                                    QStringLiteral("major"), QStringLiteral("critical")};
    return qMax(0, levels.indexOf(level));
}

QString incidentStatusText(const QString &status)
{
    if (status == QLatin1String("investigating"))
        return QCoreApplication::translate("gitlab", "Investigating");
    if (status == QLatin1String("identified"))
        return QCoreApplication::translate("gitlab", "Identified");
    if (status == QLatin1String("monitoring"))
        return QCoreApplication::translate("gitlab", "Monitoring");
    if (status == QLatin1String("resolved"))
        return QCoreApplication::translate("gitlab", "Resolved");
    if (status == QLatin1String("scheduled"))
        return QCoreApplication::translate("gitlab", "Scheduled");
    if (status == QLatin1String("in_progress"))
        return QCoreApplication::translate("gitlab", "In progress");
    if (status == QLatin1String("verifying"))
        return QCoreApplication::translate("gitlab", "Verifying");
    if (status == QLatin1String("completed"))
        return QCoreApplication::translate("gitlab", "Completed");
    return status;
}

QString levelText(const QString &level)
{
    if (level == QLatin1String("maintenance"))
        return QCoreApplication::translate("gitlab", "Maintenance");
    if (level == QLatin1String("minor"))
        return QCoreApplication::translate("gitlab", "Minor");
    if (level == QLatin1String("major"))
        return QCoreApplication::translate("gitlab", "Major");
    if (level == QLatin1String("critical"))
        return QCoreApplication::translate("gitlab", "Critical");
    return QCoreApplication::translate("gitlab", "Operational");
}

widgets::StatusRole roleOf(const QString &level)
{
    if (level == QLatin1String("major") || level == QLatin1String("critical"))
        return widgets::StatusRole::Bad;
    if (level == QLatin1String("minor"))
        return widgets::StatusRole::Waiting;
    if (level == QLatin1String("maintenance"))
        return widgets::StatusRole::Neutral;
    return widgets::StatusRole::Good;
}

QNetworkAccessManager *network()
{
    static QPointer<QNetworkAccessManager> manager;
    if (!manager)
        manager = new QNetworkAccessManager(QCoreApplication::instance());
    return manager;
}

QDateTime timeOf(const QJsonObject &object, const QString &key)
{
    return QDateTime::fromString(object.value(key).toString(), Qt::ISODateWithMs);
}

QString elided(QString text)
{
    text = text.simplified();
    return text.size() > kMessageChars ? text.left(kMessageChars - 1) + QChar(0x2026) : text;
}

QLabel *wrappedLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    label->setOpenExternalLinks(true);
    return label;
}

QString linkTo(const QString &url, const QString &text)
{
    const QString escaped = text.toHtmlEscaped();
    return url.isEmpty() ? escaped : QStringLiteral("<a href=\"%1\">%2</a>").arg(url.toHtmlEscaped(), escaped);
}

// One incident or maintenance: its linked name, pills, then its message.
QWidget *entryWidget(const QJsonObject &entry, const QString &when, QWidget *parent)
{
    auto *box = new QWidget(parent);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 4);
    layout->setSpacing(2);
    auto *name = wrappedLabel(linkTo(entry.value(QStringLiteral("url")).toString(),
                                     entry.value(QStringLiteral("name")).toString()),
                              box);
    name->setObjectName(QStringLiteral("entryName"));
    layout->addWidget(name);

    auto *pills = new widgets::FlowRow;
    const QString level = entry.value(QStringLiteral("impact")).toString();
    if (!level.isEmpty())
        pills->addWidget(new widgets::StatusPill(levelText(level), roleOf(level), box));
    const QString status = entry.value(QStringLiteral("status")).toString();
    if (!status.isEmpty())
        pills->addWidget(new QLabel(incidentStatusText(status), box));
    if (!when.isEmpty())
        pills->addWidget(new QLabel(when, box));
    layout->addLayout(pills);

    const QString message = entry.value(QStringLiteral("message")).toString();
    if (!message.isEmpty()) {
        auto *text = new QLabel(message, box);
        text->setWordWrap(true);
        text->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(text);
    }
    return box;
}

QWidget *entriesWidget(const QJsonArray &entries, bool maintenance, QWidget *parent)
{
    auto *box = new QWidget(parent);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    for (const QJsonValue &value : entries) {
        const QJsonObject entry = value.toObject();
        QString when;
        if (maintenance) {
            const QString start = widgets::compactDate(timeOf(entry, QStringLiteral("start")));
            const QString end = widgets::compactDate(timeOf(entry, QStringLiteral("end")));
            when = end.isEmpty() ? start : QCoreApplication::translate("gitlab", "%1 to %2").arg(start, end);
        } else {
            when = QCoreApplication::translate("gitlab", "Updated %1")
                       .arg(widgets::compactDate(timeOf(entry, QStringLiteral("updated"))));
        }
        layout->addWidget(entryWidget(entry, when, box));
    }
    if (entries.isEmpty()) {
        auto *none = new QLabel(QCoreApplication::translate("gitlab", "No open incidents."), box);
        none->setEnabled(false);
        layout->addWidget(none);
    }
    return box;
}

QWidget *componentsWidget(const QJsonArray &components, bool all, QWidget *parent)
{
    auto *box = new QWidget(parent);
    auto *grid = new QGridLayout(box);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setColumnStretch(0, 1);
    int row = 0;
    for (const QJsonValue &value : components) {
        const QJsonObject component = value.toObject();
        const QString level = component.value(QStringLiteral("level")).toString();
        if (!all && level == QLatin1String("none"))
            continue;
        auto *name = new QLabel(component.value(QStringLiteral("name")).toString(), box);
        name->setWordWrap(true);
        grid->addWidget(name, row, 0);
        grid->addWidget(new widgets::StatusPill(component.value(QStringLiteral("text")).toString(), roleOf(level), box),
                        row, 1, Qt::AlignRight);
        ++row;
    }
    if (row == 0) {
        auto *none = new QLabel(QCoreApplication::translate("gitlab", "All components are operational."), box);
        none->setEnabled(false);
        grid->addWidget(none, 0, 0, 1, 2);
    }
    return box;
}

void showStatus(widgets::SummaryView *view, ItemContext &context, const ItemStatus &status)
{
    const QJsonObject data = status.data;
    if (data.isEmpty()) {
        // Old data stays through an error, which the cell header shows.
        if (!view->property("hasData").toBool())
            view->setMessage(status.message.isEmpty() ? QCoreApplication::translate("gitlab", "Loading…")
                                                      : status.message);
        return;
    }
    view->setProperty("hasData", true);
    view->setMessage({});
    const QJsonObject config = context.config();
    const QString indicator = data.value(QStringLiteral("indicator")).toString();
    view->setTitle(data.value(QStringLiteral("description")).toString());

    auto *state = new QWidget;
    auto *row = new widgets::FlowRow(state);
    auto *pill = new widgets::StatusPill(levelText(indicator), roleOf(indicator), state);
    pill->setObjectName(QStringLiteral("indicator"));
    row->addWidget(pill);
    if (status.updatedAt.isValid())
        row->addWidget(new QLabel(QCoreApplication::translate("gitlab", "Checked %1")
                                      .arg(widgets::compactDate(status.updatedAt.toLocalTime())),
                                  state));
    view->setStateRow(state);

    const QUrl page(data.value(QStringLiteral("page")).toString());
    view->actions()->setWebLink(page);

    view->setSection(QStringLiteral("incidents"), QCoreApplication::translate("gitlab", "Incidents"),
                     entriesWidget(data.value(QStringLiteral("incidents")).toArray(), false, nullptr));
    const QJsonArray maintenances = data.value(QStringLiteral("maintenances")).toArray();
    view->setSection(QStringLiteral("maintenance"), QCoreApplication::translate("gitlab", "Planned maintenance"),
                     entriesWidget(maintenances, true, nullptr));
    view->setSectionVisible(QStringLiteral("maintenance"),
                            !maintenances.isEmpty() && config.value(QStringLiteral("showMaintenance")).toBool(true));
    view->setSection(QStringLiteral("components"), QCoreApplication::translate("gitlab", "Components"),
                     componentsWidget(data.value(QStringLiteral("components")).toArray(),
                                      config.value(QStringLiteral("showAllComponents")).toBool(false), nullptr));
}

} // namespace

QUrl serviceSummaryUrl()
{
    return QUrl(QStringLiteral("https://api.status.io/1.0/status/") + QLatin1String(kPageId));
}

ItemStatus readServiceSummary(const QJsonObject &answer)
{
    const QString page = QStringLiteral("https://status.gitlab.com");
    const QJsonObject result = answer.value(QStringLiteral("result")).toObject();
    const QJsonObject overall = result.value(QStringLiteral("status_overall")).toObject();
    const int overallCode = overall.value(QStringLiteral("status_code")).toInt(100);
    const QString indicator = levelOfCode(overallCode);

    QJsonArray components;
    for (const QJsonValue &value : result.value(QStringLiteral("status")).toArray()) {
        const QJsonObject component = value.toObject();
        const int code = component.value(QStringLiteral("status_code")).toInt(100);
        components.append(QJsonObject{
            {QStringLiteral("name"), component.value(QStringLiteral("name")).toString()},
            {QStringLiteral("text"), textOfCode(code, component.value(QStringLiteral("status")).toString())},
            {QStringLiteral("level"), levelOfCode(code)},
        });
    }

    ItemStatus status;
    status.state = ItemStatus::State::Ok;
    status.updatedAt = QDateTime::currentDateTimeUtc();

    QJsonArray incidents;
    for (const QJsonValue &value : result.value(QStringLiteral("incidents")).toArray()) {
        const QJsonObject incident = value.toObject();
        const QString id = incident.value(QStringLiteral("_id")).toString();
        const QString name = incident.value(QStringLiteral("name")).toString();
        const QJsonObject latest = latestMessage(incident);
        QString impact = levelOfCode(latest.value(QStringLiteral("status")).toInt(overallCode));
        if (rankOf(impact) < rankOf(QStringLiteral("minor")))
            impact = QStringLiteral("minor");
        QString updated = latest.value(QStringLiteral("datetime")).toString();
        if (updated.isEmpty())
            updated = incident.value(QStringLiteral("datetime_open")).toString();
        incidents.append(QJsonObject{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), name},
            {QStringLiteral("status"), stateOfCode(latest.value(QStringLiteral("state")).toInt())},
            {QStringLiteral("impact"), impact},
            {QStringLiteral("updated"), updated},
            {QStringLiteral("url"), id.isEmpty() ? page : pageUrl(page, QStringLiteral("incident"), id)},
            {QStringLiteral("message"), elided(latest.value(QStringLiteral("details")).toString())},
        });
        AttentionReason reason;
        reason.key = QStringLiteral("incident:") + id;
        reason.text = QCoreApplication::translate("gitlab", "GitLab: %1").arg(name);
        reason.notify = rankOf(impact) >= rankOf(QStringLiteral("major"));
        status.attention.append(reason);
    }

    // Maintenance that runs now, then maintenance to come.
    QJsonArray maintenances;
    const QJsonObject maintenance = result.value(QStringLiteral("maintenance")).toObject();
    const std::pair<QString, QString> lists[] = {{QStringLiteral("active"), QStringLiteral("in_progress")},
                                                 {QStringLiteral("upcoming"), QStringLiteral("scheduled")}};
    for (const auto &[key, maintenanceStatus] : lists) {
        for (const QJsonValue &value : maintenance.value(key).toArray()) {
            const QJsonObject entry = value.toObject();
            const QString id = entry.value(QStringLiteral("_id")).toString();
            maintenances.append(QJsonObject{
                {QStringLiteral("id"), id},
                {QStringLiteral("name"), entry.value(QStringLiteral("name")).toString()},
                {QStringLiteral("status"), maintenanceStatus},
                {QStringLiteral("start"), entry.value(QStringLiteral("datetime_planned_start")).toString()},
                {QStringLiteral("end"), entry.value(QStringLiteral("datetime_planned_end")).toString()},
                {QStringLiteral("url"), id.isEmpty() ? page : pageUrl(page, QStringLiteral("maintenance"), id)},
                {QStringLiteral("message"),
                 elided(latestMessage(entry).value(QStringLiteral("details")).toString())},
            });
        }
    }

    const QString description = overallCode == 100
        ? QCoreApplication::translate("gitlab", "All systems operational")
        : textOfCode(overallCode, overall.value(QStringLiteral("status")).toString());

    // A degraded page without an open incident still needs attention.
    if (status.attention.isEmpty() && rankOf(indicator) >= rankOf(QStringLiteral("minor"))) {
        AttentionReason reason;
        reason.key = QStringLiteral("indicator:") + indicator;
        reason.text = QCoreApplication::translate("gitlab", "GitLab: %1").arg(description);
        reason.notify = rankOf(indicator) >= rankOf(QStringLiteral("major"));
        status.attention.append(reason);
    }

    status.summary = description;
    status.data = QJsonObject{
        {QStringLiteral("page"), page},
        {QStringLiteral("indicator"), indicator},
        {QStringLiteral("description"), description},
        {QStringLiteral("components"), components},
        {QStringLiteral("incidents"), incidents},
        {QStringLiteral("maintenances"), maintenances},
    };
    return status;
}

void fetchServiceStatus(StatusJob &job)
{
    QNetworkRequest request(serviceSummaryUrl());
    request.setTransferTimeout(kTimeoutMs);
    request.setRawHeader("Accept", "application/json");
    QNetworkReply *reply = network()->get(request);
    // A job that goes first stops the request; its answer then reaches no one.
    QObject::connect(job.owner(), &QObject::destroyed, reply, [reply] { reply->abort(); });
    // Deletes the reply in every case, also when the job has gone.
    QObject::connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    QObject::connect(reply, &QNetworkReply::finished, job.owner(), [&job, reply] {
        ItemStatus status;
        if (reply->error() != QNetworkReply::NoError) {
            status.state = ItemStatus::State::Error;
            status.message =
                QCoreApplication::translate("gitlab", "The GitLab status page did not answer: %1").arg(reply->errorString());
            job.finish(status);
            return;
        }
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            status.state = ItemStatus::State::Error;
            status.message = QCoreApplication::translate("gitlab", "The GitLab status page gave an answer that "
                                                                   "cannot be read.");
            job.finish(status);
            return;
        }
        job.finish(readServiceSummary(document.object()));
    });
}

QWidget *createServiceStatusView(ItemContext &context, QWidget *parent)
{
    auto *view = new widgets::SummaryView(parent);
    view->setObjectName(QStringLiteral("serviceStatus"));
    ItemStatusSource *source = context.status();
    if (!source) {
        view->setMessage(QCoreApplication::translate("gitlab", "Loading…"));
        return view;
    }
    view->actions()->addAction(QStringLiteral("refresh"), QCoreApplication::translate("gitlab", "Refresh"),
                               QCoreApplication::translate("gitlab", "Refresh"), [source] { source->refresh(); });
    showStatus(view, context, source->current());
    source->subscribe(view, [view, &context](const ItemStatus &status) { showStatus(view, context, status); });
    return view;
}

} // namespace mirante::modules::gitlab
