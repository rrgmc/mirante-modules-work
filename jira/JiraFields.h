// Fields of an issue found by name and read without knowing their type, for the
// extra fields of the General Configuration. See decision 0073.
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace mirante::modules::jira {

// The id of the field called name in names, Jira's map from field id to name.
// Case does not count. A name that differs only in a final "s" or "/s" also
// matches, as "Fix version" does "Fix versions" and Data Center's "Fix Version/s",
// but an exact name comes first. Empty if the site has no such field.
QString fieldIdByName(const QJsonObject &names, const QString &name);

// The value of a field as plain text: text and numbers as they are, an object by
// its name, value or displayName, and a list joined with commas. Empty for
// anything else, such as null.
QString fieldText(const QJsonValue &value);

// From the answer of GET field, a map from field id to name, as in the names of
// an issue, with only the fields that wanted names.
QJsonObject wantedFieldNames(const QJsonArray &fields, const QStringList &wanted);

} // namespace mirante::modules::jira
