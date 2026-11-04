#include "GitHubServiceStatus.h"

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

namespace mirante::modules::github {

namespace {

constexpr int kTimeoutMs = 30000;
// The longest incident message the view shows.
constexpr int kMessageChars = 400;

// The page's levels, from best to worst. "maintenance" ranks above "none" so
// that it shows, and below the outages.
int rankOf(const QString &level)
{
    static const QStringList levels{QStringLiteral("none"), QStringLiteral("maintenance"), QStringLiteral("minor"),
                                    QStringLiteral("major"), QStringLiteral("critical")};
    return qMax(0, levels.indexOf(level));
}

// The level of a component's status, on the scale of the page's indicator.
QString componentLevel(const QString &status)
{
    if (status == QLatin1String("degraded_performance"))
        return QStringLiteral("minor");
    if (status == QLatin1String("partial_outage"))
        return QStringLiteral("major");
    if (status == QLatin1String("major_outage"))
        return QStringLiteral("critical");
    if (status == QLatin1String("under_maintenance"))
        return QStringLiteral("maintenance");
    return QStringLiteral("none");
}

QString componentText(const QString &status)
{
    if (status == QLatin1String("operational"))
        return QCoreApplication::translate("github", "Operational");
    if (status == QLatin1String("degraded_performance"))
        return QCoreApplication::translate("github", "Degraded performance");
    if (status == QLatin1String("partial_outage"))
        return QCoreApplication::translate("github", "Partial outage");
    if (status == QLatin1String("major_outage"))
        return QCoreApplication::translate("github", "Major outage");
    if (status == QLatin1String("under_maintenance"))
        return QCoreApplication::translate("github", "Under maintenance");
    return status;
}

QString incidentStatusText(const QString &status)
{
    if (status == QLatin1String("investigating"))
        return QCoreApplication::translate("github", "Investigating");
    if (status == QLatin1String("identified"))
        return QCoreApplication::translate("github", "Identified");
    if (status == QLatin1String("monitoring"))
        return QCoreApplication::translate("github", "Monitoring");
    if (status == QLatin1String("resolved"))
        return QCoreApplication::translate("github", "Resolved");
    if (status == QLatin1String("scheduled"))
        return QCoreApplication::translate("github", "Scheduled");
    if (status == QLatin1String("in_progress"))
        return QCoreApplication::translate("github", "In progress");
    if (status == QLatin1String("verifying"))
        return QCoreApplication::translate("github", "Verifying");
    if (status == QLatin1String("completed"))
        return QCoreApplication::translate("github", "Completed");
    return status;
}

QString levelText(const QString &level)
{
    if (level == QLatin1String("maintenance"))
        return QCoreApplication::translate("github", "Maintenance");
    if (level == QLatin1String("minor"))
        return QCoreApplication::translate("github", "Minor");
    if (level == QLatin1String("major"))
        return QCoreApplication::translate("github", "Major");
    if (level == QLatin1String("critical"))
        return QCoreApplication::translate("github", "Critical");
    return QCoreApplication::translate("github", "Operational");
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

// The newest update of an incident, which the summary lists first.
QJsonObject latestUpdate(const QJsonObject &incident)
{
    const QJsonArray updates = incident.value(QStringLiteral("incident_updates")).toArray();
    return updates.isEmpty() ? QJsonObject() : updates.first().toObject();
}

QString urlOf(const QJsonObject &object, const QString &page)
{
    const QString shortlink = object.value(QStringLiteral("shortlink")).toString();
    if (shortlink.startsWith(QLatin1String("https://")))
        return shortlink;
    const QString id = object.value(QStringLiteral("id")).toString();
    return id.isEmpty() || page.isEmpty() ? page : page + QStringLiteral("/incidents/") + id;
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
            when = end.isEmpty() ? start : QCoreApplication::translate("github", "%1 to %2").arg(start, end);
        } else {
            when = QCoreApplication::translate("github", "Updated %1")
                       .arg(widgets::compactDate(timeOf(entry, QStringLiteral("updated"))));
        }
        layout->addWidget(entryWidget(entry, when, box));
    }
    if (entries.isEmpty()) {
        auto *none = new QLabel(QCoreApplication::translate("github", "No open incidents."), box);
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
        auto *none = new QLabel(QCoreApplication::translate("github", "All components are operational."), box);
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
            view->setMessage(status.message.isEmpty() ? QCoreApplication::translate("github", "Loading…")
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
        row->addWidget(new QLabel(QCoreApplication::translate("github", "Checked %1")
                                      .arg(widgets::compactDate(status.updatedAt.toLocalTime())),
                                  state));
    view->setStateRow(state);

    const QUrl page(data.value(QStringLiteral("page")).toString());
    view->actions()->setWebLink(page);

    view->setSection(QStringLiteral("incidents"), QCoreApplication::translate("github", "Incidents"),
                     entriesWidget(data.value(QStringLiteral("incidents")).toArray(), false, nullptr));
    const QJsonArray maintenances = data.value(QStringLiteral("maintenances")).toArray();
    view->setSection(QStringLiteral("maintenance"), QCoreApplication::translate("github", "Planned maintenance"),
                     entriesWidget(maintenances, true, nullptr));
    view->setSectionVisible(QStringLiteral("maintenance"),
                            !maintenances.isEmpty() && config.value(QStringLiteral("showMaintenance")).toBool(true));
    view->setSection(QStringLiteral("components"), QCoreApplication::translate("github", "Components"),
                     componentsWidget(data.value(QStringLiteral("components")).toArray(),
                                      config.value(QStringLiteral("showAllComponents")).toBool(false), nullptr));
}

} // namespace

QUrl serviceSummaryUrl()
{
    return QUrl(QStringLiteral("https://www.githubstatus.com/api/v2/summary.json"));
}

ItemStatus readServiceSummary(const QJsonObject &summary)
{
    const QJsonObject pageObject = summary.value(QStringLiteral("page")).toObject();
    QString page = pageObject.value(QStringLiteral("url")).toString();
    if (!page.startsWith(QLatin1String("https://")))
        page = QStringLiteral("https://www.githubstatus.com");
    const QJsonObject overall = summary.value(QStringLiteral("status")).toObject();
    QString indicator = overall.value(QStringLiteral("indicator")).toString(QStringLiteral("none"));
    if (rankOf(indicator) == 0 && indicator != QLatin1String("none"))
        indicator = QStringLiteral("none");

    QJsonArray components;
    for (const QJsonValue &value : summary.value(QStringLiteral("components")).toArray()) {
        const QJsonObject component = value.toObject();
        // A group only gathers other components, which are listed too.
        if (component.value(QStringLiteral("group")).toBool())
            continue;
        const QString status = component.value(QStringLiteral("status")).toString();
        const QString level = componentLevel(status);
        if (component.value(QStringLiteral("only_show_if_degraded")).toBool() && level == QLatin1String("none"))
            continue;
        components.append(QJsonObject{
            {QStringLiteral("name"), component.value(QStringLiteral("name")).toString()},
            {QStringLiteral("text"), componentText(status)},
            {QStringLiteral("level"), level},
        });
    }

    ItemStatus status;
    status.state = ItemStatus::State::Ok;
    status.updatedAt = QDateTime::currentDateTimeUtc();

    QJsonArray incidents;
    for (const QJsonValue &value : summary.value(QStringLiteral("incidents")).toArray()) {
        const QJsonObject incident = value.toObject();
        const QString incidentStatus = incident.value(QStringLiteral("status")).toString();
        if (incidentStatus == QLatin1String("resolved") || incidentStatus == QLatin1String("postmortem"))
            continue;
        const QString id = incident.value(QStringLiteral("id")).toString();
        const QString name = incident.value(QStringLiteral("name")).toString();
        QString impact = incident.value(QStringLiteral("impact")).toString(QStringLiteral("none"));
        if (rankOf(impact) == 0)
            impact = QStringLiteral("minor");
        incidents.append(QJsonObject{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), name},
            {QStringLiteral("status"), incidentStatus},
            {QStringLiteral("impact"), impact},
            {QStringLiteral("updated"), incident.value(QStringLiteral("updated_at")).toString()},
            {QStringLiteral("url"), urlOf(incident, page)},
            {QStringLiteral("message"), elided(latestUpdate(incident).value(QStringLiteral("body")).toString())},
        });
        AttentionReason reason;
        reason.key = QStringLiteral("incident:") + id;
        reason.text = QCoreApplication::translate("github", "GitHub: %1").arg(name);
        reason.notify = rankOf(impact) >= rankOf(QStringLiteral("major"));
        status.attention.append(reason);
    }

    QJsonArray maintenances;
    for (const QJsonValue &value : summary.value(QStringLiteral("scheduled_maintenances")).toArray()) {
        const QJsonObject maintenance = value.toObject();
        const QString maintenanceStatus = maintenance.value(QStringLiteral("status")).toString();
        if (maintenanceStatus == QLatin1String("completed"))
            continue;
        maintenances.append(QJsonObject{
            {QStringLiteral("id"), maintenance.value(QStringLiteral("id")).toString()},
            {QStringLiteral("name"), maintenance.value(QStringLiteral("name")).toString()},
            {QStringLiteral("status"), maintenanceStatus},
            {QStringLiteral("start"), maintenance.value(QStringLiteral("scheduled_for")).toString()},
            {QStringLiteral("end"), maintenance.value(QStringLiteral("scheduled_until")).toString()},
            {QStringLiteral("url"), urlOf(maintenance, page)},
            {QStringLiteral("message"),
             elided(latestUpdate(maintenance).value(QStringLiteral("body")).toString())},
        });
    }

    // A degraded page without an open incident still needs attention.
    if (status.attention.isEmpty() && rankOf(indicator) >= rankOf(QStringLiteral("minor"))) {
        AttentionReason reason;
        reason.key = QStringLiteral("indicator:") + indicator;
        reason.text = QCoreApplication::translate("github", "GitHub: %1")
                          .arg(overall.value(QStringLiteral("description")).toString());
        reason.notify = rankOf(indicator) >= rankOf(QStringLiteral("major"));
        status.attention.append(reason);
    }

    QString description = overall.value(QStringLiteral("description")).toString();
    if (description.isEmpty())
        description = levelText(indicator);
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
                QCoreApplication::translate("github", "The GitHub status page did not answer: %1").arg(reply->errorString());
            job.finish(status);
            return;
        }
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            status.state = ItemStatus::State::Error;
            status.message = QCoreApplication::translate("github", "The GitHub status page gave an answer that "
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
        view->setMessage(QCoreApplication::translate("github", "Loading…"));
        return view;
    }
    view->actions()->addAction(QStringLiteral("refresh"), QCoreApplication::translate("github", "Refresh"),
                               QCoreApplication::translate("github", "Refresh"), [source] { source->refresh(); });
    showStatus(view, context, source->current());
    source->subscribe(view, [view, &context](const ItemStatus &status) { showStatus(view, context, status); });
    return view;
}

} // namespace mirante::modules::github
