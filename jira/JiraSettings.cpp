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

        Field token;
        token.key = kTokenKey;
        token.label = QCoreApplication::translate("jira", "Token");
        token.type = Field::Type::Secret;
        token.placeholder = QCoreApplication::translate("jira", "Kept in this device's keychain");
        return {baseUrl, token};
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

} // namespace mirante::modules::jira
