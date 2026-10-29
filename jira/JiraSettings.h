// The jira module's settings screen, General Configuration, and reading its
// values. See README.md#settings-screens.
#pragma once

#include <mirante/ModuleSettings.h>

#include <QJsonObject>
#include <QString>
#include <QUrl>

#include <memory>

namespace mirante::modules::jira {

// The id of the General Configuration screen.
inline const QString kGeneralScreen = QStringLiteral("general");
// Its key with the Jira site, such as "https://acme.atlassian.net".
inline const QString kBaseUrlKey = QStringLiteral("baseUrl");
// The key of the token field. The token is never stored in the values; the
// core keeps it in the keychain. See decision 0054.
inline const QString kTokenKey = QStringLiteral("token");

// The screen: the Jira site's base URL and its token.
std::unique_ptr<SettingsScreen> makeGeneralScreen();

// The site of the screen's values. Empty when none is set, or when it is not
// an http or https address.
QUrl baseUrlOf(const QJsonObject &general);

} // namespace mirante::modules::jira
