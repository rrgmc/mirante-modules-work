#include "GitLabSettings.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonValue>
#include <QNetworkRequest>
#include <QUrlQuery>

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

    QStringList serverChoiceFields() const override { return {kProjectsKey}; }

    void listServerChoices(const QString &key, ApiClient &server, QObject *receiver,
                           std::function<void(const std::vector<ServerChoice> &, const QString &)> done) const override
    {
        if (key != kProjectsKey) {
            done({}, QString());
            return;
        }
        server.get(kProjectsPath, projectsQuery(), projectsOptions(), receiver,
                   [done = std::move(done)](const ApiReply &reply) {
                       if (!reply.ok()) {
                           done({}, reply.message);
                           return;
                       }
                       done(projectChoices(reply.json), QString());
                   });
    }
};

} // namespace

QUrlQuery projectsQuery()
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("membership"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("archived"), QStringLiteral("false"));
    query.addQueryItem(QStringLiteral("order_by"), QStringLiteral("path"));
    query.addQueryItem(QStringLiteral("sort"), QStringLiteral("asc"));
    query.addQueryItem(QStringLiteral("per_page"), QStringLiteral("100"));
    return query;
}

GetOptions projectsOptions()
{
    GetOptions options;
    options.followLinks = true;
    options.maxRows = kMaxProjectChoices;
    return options;
}

std::vector<ServerChoice> projectChoices(const QJsonDocument &json)
{
    std::vector<ServerChoice> choices;
    for (const QJsonValue &value : json.array()) {
        const QJsonObject project = value.toObject();
        const QString path = project.value(QStringLiteral("path_with_namespace")).toString();
        if (path.isEmpty())
            continue;
        const QString visibility = project.value(QStringLiteral("visibility")).toString();
        QString detail;
        if (visibility == QLatin1String("private"))
            detail = QCoreApplication::translate("gitlab", "Private");
        else if (visibility == QLatin1String("internal"))
            detail = QCoreApplication::translate("gitlab", "Internal");
        choices.push_back({path, detail});
    }
    return choices;
}

std::unique_ptr<SettingsScreen> makeGeneralScreen()
{
    return std::make_unique<GeneralScreen>();
}

void GitLabServerType::authorize(QNetworkRequest &request, const QString &token, const QJsonObject &values) const
{
    Q_UNUSED(values)
    request.setRawHeader("PRIVATE-TOKEN", token.toUtf8());
}

Field makeProjectSetting()
{
    Field field;
    field.key = kProjectSettingKey;
    field.label = QCoreApplication::translate("gitlab", "GitLab project path");
    field.type = Field::Type::Text;
    field.placeholder = QStringLiteral("group/project");
    return field;
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
