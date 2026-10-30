// The jira module's settings screen, General Configuration, and reading its
// values. See README.md#settings-screens.
#pragma once

#include <mirante/ModuleSettings.h>

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <memory>

namespace mirante::modules::jira {

// The id of the General Configuration screen.
inline const QString kGeneralScreen = QStringLiteral("general");
// Its key with the Jira site, such as "https://acme.atlassian.net".
inline const QString kBaseUrlKey = QStringLiteral("baseUrl");
// Its key with the deployment: "auto", "cloud" or "dc". See decision 0060.
inline const QString kDeploymentKey = QStringLiteral("deployment");
// Its key with the account's email, used only by Jira Cloud.
inline const QString kEmailKey = QStringLiteral("email");
// The key of the token field. The token is never stored in the values; the
// core keeps it in the keychain. See decision 0054.
inline const QString kTokenKey = QStringLiteral("token");
// Its key with the names of the extra fields to show. See decision 0073.
inline const QString kExtraFieldsKey = QStringLiteral("extraFields");

// Jira Cloud or Jira Data Center. They differ in the API root, the
// authentication and the text format. See docs/integrations.md#jira.
enum class Deployment { Cloud, DataCenter };

// The screen: the Jira site's base URL, its deployment, the email for Jira
// Cloud, the token and the extra fields.
std::unique_ptr<SettingsScreen> makeGeneralScreen();

// The site of the screen's values. Empty when none is set, or when it is not
// an http or https address.
QUrl baseUrlOf(const QJsonObject &general);

// The deployment of the screen's values. "cloud" and "dc" choose it. Anything
// else, "auto" included, means Jira Cloud when the base URL's host ends in
// .atlassian.net or .jira.com, and Jira Data Center otherwise.
Deployment deploymentOf(const QJsonObject &general);

// The email of the screen's values, trimmed. Empty when none is set.
QString emailOf(const QJsonObject &general);

// The names of the extra fields of the screen's values, trimmed, without empty
// ones and without a second copy of a name, whatever its case.
QStringList extraFieldsOf(const QJsonObject &general);

} // namespace mirante::modules::jira
