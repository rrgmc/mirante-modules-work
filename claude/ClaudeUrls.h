// The address of a Claude web session, read from an item's configuration. A
// pure function, so it is tested without a view.
#pragma once

#include <QJsonObject>
#include <QUrl>

namespace mirante::modules::claude {

// The "url" key, if it is an http or https address with a host. Otherwise an
// empty URL.
QUrl sessionUrl(const QJsonObject &config);

} // namespace mirante::modules::claude
