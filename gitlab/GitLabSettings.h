// The gitlab module's settings screen, General Configuration, and reading its
// values. See README.md#settings.
#pragma once

#include <mirante/ApiClient.h>
#include <mirante/Fields.h>
#include <mirante/ModuleSettings.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

#include <memory>
#include <vector>

namespace mirante::modules::gitlab {

// The id of the General Configuration screen.
inline const QString kGeneralScreen = QStringLiteral("general");
// Its key with the list of project paths.
inline const QString kProjectsKey = QStringLiteral("projects");
// Its key with the server's base URL, such as "https://gitlab.com".
inline const QString kBaseUrlKey = QStringLiteral("baseUrl");
// The key of the token field. The token is never stored in the values; the
// core keeps it in the keychain. See decision 0054.
inline const QString kTokenKey = QStringLiteral("token");
// The base URL used when the profile names none.
inline const QString kDefaultBaseUrl = QStringLiteral("https://gitlab.com");
// The key of the project setting with the default project path, kept in a
// project's moduleSettings.gitlab.project.
inline const QString kProjectSettingKey = QStringLiteral("project");

// The screen: the server's base URL, its token, and a list of project paths,
// offered where an item asks for one. The list's "Add from Server…" button
// lists the projects the token's user is a member of. See decision 0138 in
// mirante-core.
std::unique_ptr<SettingsScreen> makeGeneralScreen();

// The request of "Add from Server…": the projects the token's user is a
// member of, without archived ones, by path, in pages of 100, at most
// kMaxProjectChoices.
inline const QString kProjectsPath = QStringLiteral("api/v4/projects");
inline constexpr int kMaxProjectChoices = 1000;
QUrlQuery projectsQuery();
GetOptions projectsOptions();
// The values of its answer: each project's path, with "Private" or
// "Internal" as its detail where it applies.
std::vector<ServerChoice> projectChoices(const QJsonDocument &json);

// The server of the screen's values: its base URL, or kDefaultBaseUrl when
// none is set. A base URL that is not an http or https address gives an empty
// URL.
QUrl baseUrlOf(const QJsonObject &general);

// The GitLab server of a profile: the General Configuration screen, its base
// URL and its token, sent in the PRIVATE-TOKEN header.
class GitLabServerType : public ServerType
{
public:
    QString screenId() const override { return kGeneralScreen; }
    QString tokenKey() const override { return kTokenKey; }
    QUrl baseUrl(const QJsonObject &values) const override { return baseUrlOf(values); }
    void authorize(QNetworkRequest &request, const QString &token, const QJsonObject &values) const override;
};

// The project setting: the project path that items use when their own
// "project" is empty.
Field makeProjectSetting();

// The project paths of the screen's values, in their order. Each is trimmed,
// without slashes at its start and end. Empty and repeated paths are left out.
QStringList projectPaths(const QJsonObject &general);

// The fields, with the project paths configured in the profile's account
// offered in each "project" field, and under "Add from List…" in each
// "projects" field. An empty accountId is the default account. See decisions
// 0139 and 0144 in mirante-core.
std::vector<Field> withProjectPaths(std::vector<Field> fields, const ModuleSettings &settings,
                                    const QString &profileId, const QString &accountId = QString());

} // namespace mirante::modules::gitlab
