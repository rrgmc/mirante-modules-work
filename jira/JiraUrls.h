// The address of a Jira issue page, built from an item's configuration. A pure
// function, so it is tested without a view.
#pragma once

#include <QJsonObject>
#include <QUrl>

namespace mirante::modules::jira {

// <baseUrl>/browse/<key>, or an empty URL if the "key" key is empty.
QUrl issueUrl(const QUrl &baseUrl, const QJsonObject &config);

} // namespace mirante::modules::jira
