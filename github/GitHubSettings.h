// The github module's settings screen, General Configuration, and reading its
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

namespace mirante::modules::github {

// The id of the General Configuration screen.
inline const QString kGeneralScreen = QStringLiteral("general");
// Its key with the list of repositories, "owner/repo".
inline const QString kRepositoriesKey = QStringLiteral("repositories");
// Its key with the web address, such as "https://github.example.com".
inline const QString kBaseUrlKey = QStringLiteral("baseUrl");
// The key of the token field. The token is never stored in the values; the
// core keeps it in the keychain. See decision 0054.
inline const QString kTokenKey = QStringLiteral("token");
// The web address used when the profile names none.
inline const QString kDefaultBaseUrl = QStringLiteral("https://github.com");
// The API address of github.com.
inline const QString kGitHubApiUrl = QStringLiteral("https://api.github.com/");
// The key of the project setting with the default repository, kept in a
// project's moduleSettings.github.repository.
inline const QString kProjectSettingKey = QStringLiteral("repository");

// The screen: the web address, the token, and a list of repositories,
// offered where an item asks for one. The list's "Add from Server…" button
// lists the repositories the token can reach. See decision 0138 in mirante-core.
std::unique_ptr<SettingsScreen> makeGeneralScreen();

// The request of "Add from Server…": the repositories of the token's user,
// by name, in pages of 100, at most kMaxRepositoryChoices.
inline const QString kRepositoriesPath = QStringLiteral("user/repos");
inline constexpr int kMaxRepositoryChoices = 1000;
QUrlQuery repositoriesQuery();
GetOptions repositoriesOptions();
// The values of its answer: each repository's "owner/repo", with "Private"
// and "Archived" as its detail where they apply.
std::vector<ServerChoice> repositoryChoices(const QJsonDocument &json);

// The web address of the screen's values: its baseUrl, or kDefaultBaseUrl
// when none is set. Pages, pasted links and Web links use it. A base URL that
// is not an http or https address gives an empty URL.
QUrl webBaseOf(const QJsonObject &general);

// The API address of the screen's values: kGitHubApiUrl when the web
// address's host is github.com or www.github.com, else the web address with
// "api/v3/" appended, as GitHub Enterprise Server has it, without doubling a
// slash. It always ends with a slash. Empty when webBaseOf() is.
QUrl apiBaseOf(const QJsonObject &general);

// The GitHub server of a profile: the General Configuration screen, its API
// address and its token, sent as a bearer token with GitHub's headers.
class GitHubServerType : public ServerType
{
public:
    QString screenId() const override { return kGeneralScreen; }
    QString tokenKey() const override { return kTokenKey; }
    // The API address, so ApiClient paths are "repos/...", "search/issues"
    // and "user".
    QUrl baseUrl(const QJsonObject &values) const override { return apiBaseOf(values); }
    // Authorization: Bearer <token>, Accept: application/vnd.github+json,
    // X-GitHub-Api-Version: 2022-11-28 and User-Agent: Mirante. GitHub
    // refuses a request without a user agent.
    void authorize(QNetworkRequest &request, const QString &token, const QJsonObject &values) const override;
};

// The project setting: the repository that items use when their own
// "repository" is empty.
Field makeProjectSetting();

// The repositories of the screen's values, in their order. Each is trimmed,
// without slashes at its start and end. Empty and repeated ones are left out.
QStringList repositoryNames(const QJsonObject &general);

// The fields, with the repositories configured in the profile's account
// offered in each "repository" field, and under "Add from List…" in each
// "repositories" field. An empty accountId is the default account. See
// decisions 0139 and 0144 in mirante-core.
std::vector<Field> withRepositories(std::vector<Field> fields, const ModuleSettings &settings,
                                    const QString &profileId, const QString &accountId = QString());

} // namespace mirante::modules::github
