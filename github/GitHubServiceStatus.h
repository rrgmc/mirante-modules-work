#pragma once

#include <mirante/Status.h>

#include <QJsonObject>
#include <QUrl>

class QWidget;

namespace mirante {
class ItemContext;
}

// The GitHub Status type: the health of github.com, read from the public
// status page, githubstatus.com. It needs no token and no server of the
// profile. See README.md#github-status.
namespace mirante::modules::github {

// The address of the status page's summary, a Statuspage document.
QUrl serviceSummaryUrl();

// The status of an answer from the summary address: the overall indicator,
// the components, the open incidents and the planned maintenance, as the
// view reads them. An open incident needs attention. See
// README.md#github-status for the data.
ItemStatus readServiceSummary(const QJsonObject &summary);

// Fetches serviceSummaryUrl() and finishes the job with
// readServiceSummary(), or with an error.
void fetchServiceStatus(StatusJob &job);

QWidget *createServiceStatusView(ItemContext &context, QWidget *parent);

} // namespace mirante::modules::github
