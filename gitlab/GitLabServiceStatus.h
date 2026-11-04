#pragma once

#include <mirante/Status.h>

#include <QJsonObject>
#include <QUrl>

class QWidget;

namespace mirante {
class ItemContext;
}

// The GitLab Status type: the health of gitlab.com, read from the public
// status page, status.gitlab.com, which status.io serves. It needs no token
// and no server of the profile. See README.md#gitlab-status.
namespace mirante::modules::gitlab {

// The address of the status page's summary, from status.io's public API.
QUrl serviceSummaryUrl();

// The status of an answer from the summary address: the overall indicator,
// the components, the open incidents and the planned maintenance, as the
// view reads them. An open incident needs attention. See
// README.md#gitlab-status for the data.
ItemStatus readServiceSummary(const QJsonObject &summary);

// Fetches serviceSummaryUrl() and finishes the job with
// readServiceSummary(), or with an error.
void fetchServiceStatus(StatusJob &job);

QWidget *createServiceStatusView(ItemContext &context, QWidget *parent);

} // namespace mirante::modules::gitlab
