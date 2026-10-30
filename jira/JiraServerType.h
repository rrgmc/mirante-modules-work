// How the core reaches a profile's Jira site: the General Configuration
// screen, its token field, and the authentication of each deployment. See
// README.md#server-and-authentication and decision 0060.
#pragma once

#include <mirante/ApiClient.h>

#include <QJsonObject>
#include <QString>
#include <QUrl>

namespace mirante::modules::jira {

class JiraServerType : public ServerType
{
public:
    QString screenId() const override;
    QString tokenKey() const override;
    QUrl baseUrl(const QJsonObject &values) const override;
    // Asks for the email on Jira Cloud without one.
    QString problem(const QJsonObject &values) const override;
    // Basic with "email:token" on Jira Cloud, Bearer with the token on Jira
    // Data Center.
    void authorize(QNetworkRequest &request, const QString &token, const QJsonObject &values) const override;
};

} // namespace mirante::modules::jira
