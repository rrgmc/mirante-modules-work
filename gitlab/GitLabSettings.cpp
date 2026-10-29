#include "GitLabSettings.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonValue>

#include <utility>

namespace mirante::modules::gitlab {

namespace {

class GeneralScreen : public SettingsScreen
{
public:
    QString id() const override { return kGeneralScreen; }
    QString name() const override { return QCoreApplication::translate("gitlab", "General Configuration"); }

    std::vector<Field> fields() const override
    {
        Field baseUrl;
        baseUrl.key = kBaseUrlKey;
        baseUrl.label = QCoreApplication::translate("gitlab", "Base URL");
        baseUrl.type = Field::Type::Url;
        baseUrl.defaultValue = kDefaultBaseUrl;
        baseUrl.placeholder = kDefaultBaseUrl;

        Field token;
        token.key = kTokenKey;
        token.label = QCoreApplication::translate("gitlab", "Token");
        token.type = Field::Type::Secret;
        token.placeholder = QCoreApplication::translate("gitlab", "Kept in this device's keychain");

        Field projects;
        projects.key = kProjectsKey;
        projects.label = QCoreApplication::translate("gitlab", "Project paths");
        projects.type = Field::Type::TextList;
        projects.placeholder = QStringLiteral("group/project\ngroup/other-project");
        return {baseUrl, token, projects};
    }
};

} // namespace

std::unique_ptr<SettingsScreen> makeGeneralScreen()
{
    return std::make_unique<GeneralScreen>();
}

QUrl baseUrlOf(const QJsonObject &general)
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

QStringList projectPaths(const QJsonObject &general)
{
    QStringList paths;
    for (const QJsonValue &value : general.value(kProjectsKey).toArray()) {
        QString path = value.toString().trimmed();
        while (path.startsWith(QLatin1Char('/')))
            path.remove(0, 1);
        while (path.endsWith(QLatin1Char('/')))
            path.chop(1);
        if (!path.isEmpty() && !paths.contains(path))
            paths << path;
    }
    return paths;
}

std::vector<Field> withProjectPaths(std::vector<Field> fields, const ModuleSettings &settings,
                                    const QString &profileId)
{
    const QStringList paths = projectPaths(settings.values(profileId, kGeneralScreen));
    for (Field &field : fields) {
        if (field.key == QLatin1String("project"))
            field.suggestions = paths;
    }
    return fields;
}

} // namespace mirante::modules::gitlab
