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
        Field projects;
        projects.key = kProjectsKey;
        projects.label = QCoreApplication::translate("gitlab", "Project paths");
        projects.type = Field::Type::TextList;
        projects.placeholder = QStringLiteral("group/project\ngroup/other-project");
        return {projects};
    }
};

} // namespace

std::unique_ptr<SettingsScreen> makeGeneralScreen()
{
    return std::make_unique<GeneralScreen>();
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
