#include "JiraFields.h"

namespace mirante::modules::jira {

namespace {

// A name without its case and a final "/s" or "s".
QString singular(const QString &name)
{
    QString text = name.trimmed().toCaseFolded();
    if (text.endsWith(QLatin1String("/s")))
        text.chop(2);
    else if (text.endsWith(QLatin1Char('s')))
        text.chop(1);
    return text;
}

} // namespace

QString fieldIdByName(const QJsonObject &names, const QString &name)
{
    const QString wanted = name.trimmed();
    if (wanted.isEmpty())
        return {};
    for (auto entry = names.constBegin(); entry != names.constEnd(); ++entry) {
        if (entry.value().toString().trimmed().compare(wanted, Qt::CaseInsensitive) == 0)
            return entry.key();
    }
    const QString stem = singular(wanted);
    for (auto entry = names.constBegin(); entry != names.constEnd(); ++entry) {
        if (singular(entry.value().toString()) == stem)
            return entry.key();
    }
    return {};
}

QString fieldText(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (value.isDouble())
        return QString::number(value.toDouble(), 'g', 15);
    if (value.isArray()) {
        QStringList texts;
        const QJsonArray items = value.toArray();
        for (const QJsonValue &item : items) {
            const QString text = fieldText(item);
            if (!text.isEmpty())
                texts << text;
        }
        return texts.join(QStringLiteral(", "));
    }
    const QJsonObject object = value.toObject();
    for (const QString &key : {QStringLiteral("name"), QStringLiteral("value"), QStringLiteral("displayName")}) {
        const QString text = fieldText(object.value(key));
        if (!text.isEmpty())
            return text;
    }
    return {};
}

QJsonObject wantedFieldNames(const QJsonArray &fields, const QStringList &wanted)
{
    QJsonObject all;
    for (const QJsonValue &value : fields) {
        const QJsonObject field = value.toObject();
        all.insert(field.value(QStringLiteral("id")).toString(), field.value(QStringLiteral("name")));
    }
    QJsonObject names;
    for (const QString &name : wanted) {
        const QString id = fieldIdByName(all, name);
        if (!id.isEmpty())
            names.insert(id, all.value(id));
    }
    return names;
}

} // namespace mirante::modules::jira
