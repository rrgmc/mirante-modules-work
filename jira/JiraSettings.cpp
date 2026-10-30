#include "JiraSettings.h"

#include <mirante/Fields.h>

#include <QCoreApplication>

#include <vector>

namespace mirante::modules::jira {

namespace {

class GeneralScreen : public SettingsScreen
{
public:
    QString id() const override { return kGeneralScreen; }
    QString name() const override { return QCoreApplication::translate("jira", "General Configuration"); }

    std::vector<Field> fields() const override
    {
        Field baseUrl;
        baseUrl.key = kBaseUrlKey;
        baseUrl.label = QCoreApplication::translate("jira", "Base URL");
        baseUrl.type = Field::Type::Url;
        baseUrl.placeholder = QStringLiteral("https://acme.atlassian.net");

        Field deployment;
        deployment.key = kDeploymentKey;
        deployment.label = QCoreApplication::translate("jira", "Deployment");
        deployment.type = Field::Type::Choice;
        deployment.choices = {QStringLiteral("auto"), QStringLiteral("cloud"), QStringLiteral("dc")};
        deployment.choiceLabels = {QCoreApplication::translate("jira", "Automatic"),
                                   QCoreApplication::translate("jira", "Jira Cloud"),
                                   QCoreApplication::translate("jira", "Jira Data Center")};
        deployment.defaultValue = deployment.choices.first();

        Field email;
        email.key = kEmailKey;
        email.label = QCoreApplication::translate("jira", "Email");
        email.placeholder = QCoreApplication::translate("jira", "Your Atlassian account's email, for Jira Cloud");

        Field token;
        token.key = kTokenKey;
        token.label = QCoreApplication::translate("jira", "Token");
        token.type = Field::Type::Secret;
        token.placeholder = QCoreApplication::translate("jira", "Kept in this device's keychain");
        return {baseUrl, deployment, email, token};
    }
};

} // namespace

std::unique_ptr<SettingsScreen> makeGeneralScreen()
{
    return std::make_unique<GeneralScreen>();
}

QUrl baseUrlOf(const QJsonObject &general)
{
    const QString text = general.value(kBaseUrlKey).toString().trimmed();
    const QUrl url(text, QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    if (text.isEmpty() || !url.isValid() || url.host().isEmpty()
        || (scheme != QLatin1String("http") && scheme != QLatin1String("https")))
        return {};
    return url;
}

Deployment deploymentOf(const QJsonObject &general)
{
    const QString chosen = general.value(kDeploymentKey).toString();
    if (chosen == QLatin1String("cloud"))
        return Deployment::Cloud;
    if (chosen == QLatin1String("dc"))
        return Deployment::DataCenter;
    const QString host = baseUrlOf(general).host();
    if (host.endsWith(QLatin1String(".atlassian.net"), Qt::CaseInsensitive)
        || host.endsWith(QLatin1String(".jira.com"), Qt::CaseInsensitive))
        return Deployment::Cloud;
    return Deployment::DataCenter;
}

QString emailOf(const QJsonObject &general)
{
    return general.value(kEmailKey).toString().trimmed();
}

} // namespace mirante::modules::jira
