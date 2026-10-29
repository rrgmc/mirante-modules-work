// The gitlab module's settings screen, General Configuration, and reading its
// values. See README.md#settings.
#pragma once

#include <mirante/Fields.h>
#include <mirante/ModuleSettings.h>

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace mirante::modules::gitlab {

// The id of the General Configuration screen.
inline const QString kGeneralScreen = QStringLiteral("general");
// Its key with the list of project paths.
inline const QString kProjectsKey = QStringLiteral("projects");

// The screen: a list of project paths, offered where an item asks for one.
std::unique_ptr<SettingsScreen> makeGeneralScreen();

// The project paths of the screen's values, in their order. Each is trimmed,
// without slashes at its start and end. Empty and repeated paths are left out.
QStringList projectPaths(const QJsonObject &general);

// The fields, with the project paths configured in the profile offered in
// each "project" field.
std::vector<Field> withProjectPaths(std::vector<Field> fields, const ModuleSettings &settings,
                                    const QString &profileId);

} // namespace mirante::modules::gitlab
