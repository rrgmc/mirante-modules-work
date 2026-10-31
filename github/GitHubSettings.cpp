#include "GitHubSettings.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonValue>
#include <QNetworkRequest>

#include <utility>

namespace mirante::modules::github {

namespace {

QString trimSlashes(QString text)
{
    while (text.startsWith(QLatin1Char('/')))
        text.remove(0, 1);
    while (text.endsWith(QLatin1Char('/')))
        text.chop(1);
    return text;
}

class GeneralScreen : public SettingsScreen
{
public:
    QString id() const override { return kGeneralScreen; }
    QString name() const override { return QCoreApplication::translate("github", "General Configuration"); }

    std::vector<Field> fields() const override
    {
        Field baseUrl;
        baseUrl.key = kBaseUrlKey;
        baseUrl.label = QCoreApplication::translate("github", "Base URL");
        baseUrl.type = Field::Type::Url;
        baseUrl.defaultValue = kDefaultBaseUrl;
        baseUrl.placeholder = kDefaultBaseUrl;

        Field token;
        token.key = kTokenKey;
        token.label = QCoreApplication::translate("github", "Token");
        token.type = Field::Type::Secret;
        token.placeholder = QCoreApplication::translate("github", "Kept in this device's keychain");

        Field repositories;
        repositories.key = kRepositoriesKey;
        repositories.label = QCoreApplication::translate("github", "Repositories");
        repositories.type = Field::Type::TextList;
        repositories.placeholder = QStringLiteral("owner/repo\nowner/other-repo");
        return {baseUrl, token, repositories};
    }
};

} // namespace

std::unique_ptr<SettingsScreen> makeGeneralScreen()
{
    return std::make_unique<GeneralScreen>();
}

QUrl webBaseOf(const QJsonObject &general)
{
    QString text = general.value(kBaseUrlKey).toString().trimmed();
    if (text.isEmpty())
        text = kDefaultBaseUrl;
    const QUrl url(text, QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || url.host().isEmpty()
        || (scheme != QLatin1String("http") && scheme != QLatin1String("https")))
        return {};
    return url;
}

QUrl apiBaseOf(const QJsonObject &general)
{
    const QUrl web = webBaseOf(general);
    if (web.isEmpty())
        return {};
    const QString host = web.host().toLower();
    if (host == QLatin1String("github.com") || host == QLatin1String("www.github.com"))
        return QUrl(kGitHubApiUrl);

    QUrl api = web;
    const QString path = trimSlashes(web.path());
    api.setPath(QLatin1Char('/') + (path.isEmpty() ? QString() : path + QLatin1Char('/')) + QStringLiteral("api/v3/"));
    api.setQuery(QString());
    api.setFragment(QString());
    return api;
}

void GitHubServerType::authorize(QNetworkRequest &request, const QString &token, const QJsonObject &values) const
{
    Q_UNUSED(values)
    request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setRawHeader("User-Agent", "Mirante");
}

Field makeProjectSetting()
{
    Field field;
    field.key = kProjectSettingKey;
    field.label = QCoreApplication::translate("github", "GitHub repository");
    field.type = Field::Type::Text;
    field.placeholder = QStringLiteral("owner/repo");
    return field;
}

QStringList repositoryNames(const QJsonObject &general)
{
    QStringList names;
    for (const QJsonValue &value : general.value(kRepositoriesKey).toArray()) {
        const QString name = trimSlashes(value.toString().trimmed());
        if (!name.isEmpty() && !names.contains(name))
            names << name;
    }
    return names;
}

std::vector<Field> withRepositories(std::vector<Field> fields, const ModuleSettings &settings,
                                    const QString &profileId)
{
    const QStringList names = repositoryNames(settings.values(profileId, kGeneralScreen));
    for (Field &field : fields) {
        if (field.key == kProjectSettingKey)
            field.suggestions = names;
    }
    return fields;
}

} // namespace mirante::modules::github
