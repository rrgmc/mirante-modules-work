#include "JiraServerType.h"

#include "JiraSettings.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QNetworkRequest>

namespace mirante::modules::jira {

QString JiraServerType::screenId() const
{
    return kGeneralScreen;
}

QString JiraServerType::tokenKey() const
{
    return kTokenKey;
}

QUrl JiraServerType::baseUrl(const QJsonObject &values) const
{
    return baseUrlOf(values);
}

QString JiraServerType::problem(const QJsonObject &values) const
{
    if (deploymentOf(values) == Deployment::Cloud && emailOf(values).isEmpty())
        return QCoreApplication::translate("jira", "Set the email for Jira Cloud in the profile's Jira settings.");
    return {};
}

void JiraServerType::authorize(QNetworkRequest &request, const QString &token, const QJsonObject &values) const
{
    // Neither the token nor the email is ever logged.
    if (deploymentOf(values) == Deployment::Cloud) {
        const QByteArray credentials = (emailOf(values) + QLatin1Char(':') + token).toUtf8();
        request.setRawHeader("Authorization", "Basic " + credentials.toBase64());
    } else {
        request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    }
}

} // namespace mirante::modules::jira
