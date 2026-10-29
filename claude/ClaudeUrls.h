// The address of a Claude web session, read from an item's configuration, and
// the reading of a pasted address. Pure functions, so they are tested without
// a view.
#pragma once

#include <mirante/UrlMatch.h>

#include <QJsonObject>
#include <QUrl>

#include <optional>

namespace mirante::modules::claude {

// The "url" key, if it is an http or https address with a host. Otherwise an
// empty URL.
QUrl sessionUrl(const QJsonObject &config);

// An http or https address on claude.ai or one of its subdomains, as a Claude
// session (web), for ItemType::fromUrl(). Null for any other address.
std::optional<UrlMatch> matchUrl(const QUrl &url);

} // namespace mirante::modules::claude
