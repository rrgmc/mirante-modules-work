// Registers the github module into a fresh registry and checks its types.
#include "items/FieldValidation.h"
#include "items/ItemTypeRegistry.h"

#include <QIcon>
#include <QMap>
#include <QPixmap>
#include <QTest>

namespace mirante::modules::github {
void registerModule(mirante::ModuleRegistrar &registrar);
}

namespace {

void registerGitHub(mirante::ItemTypeRegistry &registry)
{
    registry.registerModule(QStringLiteral("github"), &mirante::modules::github::registerModule);
}

QStringList choicesOf(const mirante::ItemType &type, const QString &key)
{
    for (const mirante::Field &field : type.fields()) {
        if (field.key == key)
            return field.choices;
    }
    return {};
}

mirante::Field fieldOf(const mirante::ItemType &type, const QString &key)
{
    for (const mirante::Field &field : type.fields()) {
        if (field.key == key)
            return field;
    }
    return {};
}

} // namespace

class TestRegister : public QObject
{
    Q_OBJECT

private slots:
    void registersTypes()
    {
        mirante::ItemTypeRegistry registry;
        registerGitHub(registry);

        // Expected id, name and short name of each type. QMap keeps the ids sorted.
        QMap<QString, QStringList> expected;
        expected.insert(QStringLiteral("github.pr.summary"),
                        {QStringLiteral("GitHub PR (summary)"), QStringLiteral("PR")});
        expected.insert(QStringLiteral("github.pr.page"), {QStringLiteral("GitHub PR (page)"), QStringLiteral("PR")});
        expected.insert(QStringLiteral("github.pr.list"), {QStringLiteral("GitHub PR List"), QStringLiteral("PRs")});
        expected.insert(QStringLiteral("github.run"), {QStringLiteral("GitHub Actions Run"), QStringLiteral("Run")});
        expected.insert(QStringLiteral("github.run.list"),
                        {QStringLiteral("GitHub Actions Run List"), QStringLiteral("Runs")});
        expected.insert(QStringLiteral("github.issue.summary"),
                        {QStringLiteral("GitHub Issue (summary)"), QStringLiteral("Issue")});
        expected.insert(QStringLiteral("github.issue.page"),
                        {QStringLiteral("GitHub Issue (page)"), QStringLiteral("Issue")});
        expected.insert(QStringLiteral("github.issue.list"),
                        {QStringLiteral("GitHub Issue List"), QStringLiteral("Issues")});

        QStringList ids;
        for (const mirante::ItemType *type : registry.types())
            ids << type->id();
        ids.sort();
        QCOMPARE(ids, QStringList(expected.keys()));

        for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
            QVERIFY2(registry.contains(it.key()), qPrintable(it.key()));
            const mirante::ItemType &type = registry.type(it.key());
            QCOMPARE(type.name(), it.value().at(0));
            QCOMPARE(type.shortName(), it.value().at(1));
            QVERIFY2(type.hasStatus(), qPrintable(it.key()));
            const bool page = it.key().endsWith(QLatin1String(".page"));
            QCOMPARE(type.rendering(), page ? mirante::Rendering::Web : mirante::Rendering::Native);
        }
    }

    void iconsRender()
    {
        mirante::ItemTypeRegistry registry;
        registerGitHub(registry);

        for (const mirante::ItemType *type : registry.types()) {
            const QIcon icon = type->icon();
            QVERIFY2(!icon.isNull(), qPrintable(type->id()));
            QVERIFY2(!icon.pixmap(16, 16).isNull(), qPrintable(type->id()));
        }
    }

    void fieldKeys()
    {
        mirante::ItemTypeRegistry registry;
        registerGitHub(registry);

        const QStringList single{QStringLiteral("repository"), QStringLiteral("number")};
        const QStringList sizes{QStringLiteral("limit"), QStringLiteral("pageSize")};
        const QStringList list = QStringList{QStringLiteral("scope"), QStringLiteral("repository"),
                                             QStringLiteral("owner"), QStringLiteral("preset"),
                                             QStringLiteral("filter")}
            + sizes;
        QMap<QString, QStringList> expected;
        expected.insert(QStringLiteral("github.pr.summary"),
                        single
                            + QStringList{QStringLiteral("showChecks"), QStringLiteral("showReviews"),
                                          QStringLiteral("showPeople"), QStringLiteral("showLabels")});
        expected.insert(QStringLiteral("github.pr.page"), single);
        expected.insert(QStringLiteral("github.pr.list"),
                        list + QStringList{QStringLiteral("showChecks"), QStringLiteral("showReviews")});
        expected.insert(QStringLiteral("github.run"),
                        {QStringLiteral("repository"), QStringLiteral("branchSource"), QStringLiteral("branch"),
                         QStringLiteral("number"), QStringLiteral("workflow")});
        expected.insert(QStringLiteral("github.run.list"),
                        QStringList{QStringLiteral("repository"), QStringLiteral("source"), QStringLiteral("branch"),
                                    QStringLiteral("number"), QStringLiteral("workflow")}
                            + sizes);
        expected.insert(QStringLiteral("github.issue.summary"),
                        single
                            + QStringList{QStringLiteral("showDetails"), QStringLiteral("showLabels"),
                                          QStringLiteral("showComments")});
        expected.insert(QStringLiteral("github.issue.page"), single);
        expected.insert(QStringLiteral("github.issue.list"), list);

        for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
            const mirante::ItemType &type = registry.type(it.key());
            QStringList keys;
            for (const mirante::Field &field : type.fields())
                keys << field.key;
            QCOMPARE(keys, it.value());
            QVERIFY(type.pastedImageField().isEmpty());
        }
    }

    // The choices of each choice field, in form order; the first is the default.
    void choices()
    {
        mirante::ItemTypeRegistry registry;
        registerGitHub(registry);

        const QStringList scopes{QStringLiteral("repository"), QStringLiteral("owner"), QStringLiteral("all")};
        const mirante::ItemType &prList = registry.type(QStringLiteral("github.pr.list"));
        const mirante::ItemType &issueList = registry.type(QStringLiteral("github.issue.list"));
        const mirante::ItemType &run = registry.type(QStringLiteral("github.run"));
        const mirante::ItemType &runList = registry.type(QStringLiteral("github.run.list"));
        QCOMPARE(choicesOf(prList, QStringLiteral("scope")), scopes);
        QCOMPARE(choicesOf(issueList, QStringLiteral("scope")), scopes);
        QCOMPARE(choicesOf(prList, QStringLiteral("preset")),
                 (QStringList{QStringLiteral("createdByMe"), QStringLiteral("reviewRequested"),
                              QStringLiteral("assignedToMe"), QStringLiteral("allOpen")}));
        QCOMPARE(choicesOf(issueList, QStringLiteral("preset")),
                 (QStringList{QStringLiteral("assignedToMe"), QStringLiteral("createdByMe"),
                              QStringLiteral("allOpen")}));
        QCOMPARE(choicesOf(run, QStringLiteral("branchSource")),
                 (QStringList{QStringLiteral("fixed"), QStringLiteral("mainFolder"), QStringLiteral("pullRequest")}));
        QCOMPARE(choicesOf(runList, QStringLiteral("source")),
                 (QStringList{QStringLiteral("all"), QStringLiteral("mine"), QStringLiteral("branch"),
                              QStringLiteral("pullRequest")}));

        const QJsonObject runDefaults = mirante::defaultConfig(run.fields());
        QCOMPARE(runDefaults.value(QStringLiteral("branchSource")).toString(), QStringLiteral("fixed"));
        QCOMPARE(mirante::defaultConfig(runList.fields()).value(QStringLiteral("source")).toString(),
                 QStringLiteral("all"));
        QCOMPARE(mirante::defaultConfig(prList.fields()).value(QStringLiteral("preset")).toString(),
                 QStringLiteral("createdByMe"));
        QCOMPARE(mirante::defaultConfig(issueList.fields()).value(QStringLiteral("preset")).toString(),
                 QStringLiteral("assignedToMe"));
    }

    // An empty branch means the repository's default branch: no default value,
    // and the placeholder says so.
    void branchDefaultsToTheDefaultBranch_data()
    {
        QTest::addColumn<QString>("typeId");
        QTest::newRow("run") << QStringLiteral("github.run");
        QTest::newRow("run list") << QStringLiteral("github.run.list");
    }

    void branchDefaultsToTheDefaultBranch()
    {
        QFETCH(QString, typeId);
        mirante::ItemTypeRegistry registry;
        registerGitHub(registry);

        for (const mirante::Field &field : registry.type(typeId).fields()) {
            if (field.key == QLatin1String("branch")) {
                QVERIFY(field.defaultValue.isNull() || field.defaultValue.isUndefined());
                QCOMPARE(field.placeholder, QStringLiteral("Default branch"));
                return;
            }
        }
        QFAIL("No branch field");
    }

    // The number is required where it names the item, and optional where it
    // names a linked pull request.
    void numberRequired()
    {
        mirante::ItemTypeRegistry registry;
        registerGitHub(registry);

        for (const mirante::ItemType *type : registry.types()) {
            for (const mirante::Field &field : type->fields()) {
                if (field.key != QLatin1String("number"))
                    continue;
                const bool linked = type->id().startsWith(QLatin1String("github.run"));
                QCOMPARE(field.required, !linked);
            }
        }
    }

    void listsHaveSizes_data()
    {
        QTest::addColumn<QString>("typeId");
        QTest::newRow("pull requests") << QStringLiteral("github.pr.list");
        QTest::newRow("issues") << QStringLiteral("github.issue.list");
        QTest::newRow("runs") << QStringLiteral("github.run.list");
    }

    void listsHaveSizes()
    {
        QFETCH(QString, typeId);
        mirante::ItemTypeRegistry registry;
        registerGitHub(registry);

        const QJsonObject defaults = mirante::defaultConfig(registry.type(typeId).fields());
        QCOMPARE(defaults.value(QStringLiteral("limit")).toInt(), 100);
        QCOMPARE(defaults.value(QStringLiteral("pageSize")).toInt(), 50);
    }

    // Keys and labels are set and keys are unique. A new item's defaults
    // validate, except that each required field without a default is reported.
    void fieldsValidateTheirDefaults()
    {
        mirante::ItemTypeRegistry registry;
        registerGitHub(registry);

        for (const mirante::ItemType *type : registry.types()) {
            const std::vector<mirante::Field> fields = type->fields();
            QStringList keys;
            QStringList missing;
            for (const mirante::Field &field : fields) {
                const QString where = type->id() + QLatin1Char(' ') + field.key;
                QVERIFY2(!field.key.isEmpty(), qPrintable(type->id()));
                QVERIFY2(!keys.contains(field.key), qPrintable(where));
                QVERIFY2(!field.label.isEmpty(), qPrintable(where));
                if (field.type == mirante::Field::Type::Choice) {
                    QVERIFY2(!field.choices.isEmpty(), qPrintable(where));
                    QVERIFY2(field.choiceLabels.size() == field.choices.size(), qPrintable(where));
                }
                keys << field.key;
                if (field.required && (field.defaultValue.isNull() || field.defaultValue.isUndefined()))
                    missing << field.key;
            }

            QStringList reported;
            for (const mirante::FieldProblem &problem : mirante::validate(fields, mirante::defaultConfig(fields)))
                reported << problem.key;
            QCOMPARE(reported, missing);
        }
    }

    // The module has a name and a description, and so has every type, for
    // Help > Modules. See docs/decisions/0056-modules-window.md.
    void namesAndDescriptions()
    {
        mirante::ItemTypeRegistry registry;
        registerGitHub(registry);

        QCOMPARE(registry.moduleName(QStringLiteral("github")), QStringLiteral("GitHub"));
        QVERIFY(!registry.moduleDescription(QStringLiteral("github")).isEmpty());
        for (const mirante::ItemType *type : registry.types())
            QVERIFY2(!type->description().isEmpty(), qPrintable(type->id()));
    }

    // The texts of the plan's identity and item type tables.
    void descriptions()
    {
        mirante::ItemTypeRegistry registry;
        registerGitHub(registry);

        QCOMPARE(registry.moduleDescription(QStringLiteral("github")),
                 QStringLiteral("Pull requests, Actions runs and issues from GitHub or GitHub Enterprise Server. "
                                "Each profile sets its server and token in the module's General Configuration."));
        QMap<QString, QString> expected;
        expected.insert(QStringLiteral("github.pr.summary"),
                        QStringLiteral("A summary of one pull request: its state, branches, checks, reviews and "
                                       "people."));
        expected.insert(QStringLiteral("github.pr.page"),
                        QStringLiteral("The web page of one pull request, on the GitHub server of the item's "
                                       "profile."));
        expected.insert(QStringLiteral("github.pr.list"),
                        QStringLiteral("A table of pull requests in a repository, an owner or everywhere, such as "
                                       "those that wait for your review."));
        expected.insert(QStringLiteral("github.run"),
                        QStringLiteral("The Actions runs of a branch's newest commit, one per workflow, with their "
                                       "jobs. The branch is a fixed one, the main folder's current branch or a pull "
                                       "request's branch."));
        expected.insert(QStringLiteral("github.run.list"),
                        QStringLiteral("A table of a repository's Actions runs: all of them, those you started, or "
                                       "those of a branch or a pull request."));
        expected.insert(QStringLiteral("github.issue.summary"),
                        QStringLiteral("A summary of one issue: its state, assignees, labels, milestone and latest "
                                       "comments."));
        expected.insert(QStringLiteral("github.issue.page"),
                        QStringLiteral("The web page of one issue, on the GitHub server of the item's profile."));
        expected.insert(QStringLiteral("github.issue.list"),
                        QStringLiteral("A table of issues in a repository, an owner or everywhere, such as those "
                                       "assigned to you."));
        for (auto it = expected.cbegin(); it != expected.cend(); ++it)
            QCOMPARE(registry.type(it.key()).description(), it.value());
    }

    // The labels of each choice, in the order of the choices.
    void choiceLabels()
    {
        mirante::ItemTypeRegistry registry;
        registerGitHub(registry);

        const QStringList scopes{QStringLiteral("One repository"), QStringLiteral("One owner"), QStringLiteral("All")};
        const mirante::ItemType &prList = registry.type(QStringLiteral("github.pr.list"));
        const mirante::ItemType &issueList = registry.type(QStringLiteral("github.issue.list"));
        QCOMPARE(fieldOf(prList, QStringLiteral("scope")).choiceLabels, scopes);
        QCOMPARE(fieldOf(issueList, QStringLiteral("scope")).choiceLabels, scopes);
        QCOMPARE(fieldOf(prList, QStringLiteral("preset")).choiceLabels,
                 (QStringList{QStringLiteral("Created by me"), QStringLiteral("Review requested from me"),
                              QStringLiteral("Assigned to me"), QStringLiteral("All open")}));
        QCOMPARE(fieldOf(issueList, QStringLiteral("preset")).choiceLabels,
                 (QStringList{QStringLiteral("Assigned to me"), QStringLiteral("Created by me"),
                              QStringLiteral("All open")}));
        QCOMPARE(fieldOf(registry.type(QStringLiteral("github.run")), QStringLiteral("branchSource")).choiceLabels,
                 (QStringList{QStringLiteral("Fixed branch"), QStringLiteral("Current branch of the main folder"),
                              QStringLiteral("Branch of a linked pull request")}));
        QCOMPARE(fieldOf(registry.type(QStringLiteral("github.run.list")), QStringLiteral("source")).choiceLabels,
                 (QStringList{QStringLiteral("All runs"), QStringLiteral("Started by me"),
                              QStringLiteral("Of a branch"), QStringLiteral("Of a pull request")}));
    }

    // Each section switch is a boolean that is on by default.
    void sectionsDefaultOn()
    {
        mirante::ItemTypeRegistry registry;
        registerGitHub(registry);

        QMap<QString, QStringList> sections;
        sections.insert(QStringLiteral("github.pr.summary"),
                        {QStringLiteral("showChecks"), QStringLiteral("showReviews"), QStringLiteral("showPeople"),
                         QStringLiteral("showLabels")});
        sections.insert(QStringLiteral("github.pr.list"), {QStringLiteral("showChecks"), QStringLiteral("showReviews")});
        sections.insert(QStringLiteral("github.issue.summary"),
                        {QStringLiteral("showDetails"), QStringLiteral("showLabels"), QStringLiteral("showComments")});
        for (auto it = sections.cbegin(); it != sections.cend(); ++it) {
            const QJsonObject defaults = mirante::defaultConfig(registry.type(it.key()).fields());
            for (const QString &key : it.value()) {
                const mirante::Field field = fieldOf(registry.type(it.key()), key);
                const QString where = it.key() + QLatin1Char(' ') + key;
                QVERIFY2(field.type == mirante::Field::Type::Bool, qPrintable(where));
                QVERIFY2(!field.required, qPrintable(where));
                QVERIFY2(defaults.value(key).toBool(), qPrintable(where));
            }
        }
    }

    // The text fields that may stay empty, with their types and placeholders.
    void optionalTextFields()
    {
        mirante::ItemTypeRegistry registry;
        registerGitHub(registry);

        for (const mirante::ItemType *type : registry.types()) {
            const mirante::Field repository = fieldOf(*type, QStringLiteral("repository"));
            QCOMPARE(repository.type, mirante::Field::Type::Text);
            QVERIFY2(!repository.required, qPrintable(type->id()));
            QCOMPARE(repository.placeholder, QStringLiteral("owner/repo"));
            const mirante::Field number = fieldOf(*type, QStringLiteral("number"));
            if (!number.key.isEmpty())
                QCOMPARE(number.type, mirante::Field::Type::Integer);
        }

        const mirante::ItemType &prList = registry.type(QStringLiteral("github.pr.list"));
        QCOMPARE(fieldOf(prList, QStringLiteral("owner")).type, mirante::Field::Type::Text);
        QVERIFY(!fieldOf(prList, QStringLiteral("owner")).required);
        QCOMPARE(fieldOf(prList, QStringLiteral("filter")).placeholder, QStringLiteral("label:bug base:main"));
        QVERIFY(!fieldOf(prList, QStringLiteral("filter")).required);
        for (const char *id : {"github.run", "github.run.list"}) {
            const mirante::Field workflow = fieldOf(registry.type(QString::fromUtf8(id)), QStringLiteral("workflow"));
            QCOMPARE(workflow.type, mirante::Field::Type::Text);
            QVERIFY(!workflow.required);
            QCOMPARE(workflow.placeholder, QStringLiteral("ci.yml"));
        }
        // A choice is required, with its first choice as default, so it is
        // never empty.
        QVERIFY(fieldOf(prList, QStringLiteral("scope")).required);
        QCOMPARE(fieldOf(prList, QStringLiteral("scope")).defaultValue.toString(), QStringLiteral("repository"));
    }

    // The module gives its UUID, version, author and homepage, for Help >
    // Modules. See docs/decisions/0089-module-identity.md.
    void identity()
    {
        mirante::ItemTypeRegistry registry;
        registerGitHub(registry);

        const QString id = QStringLiteral("github");
        QCOMPARE(registry.moduleUuid(id), QUuid(QStringLiteral("6be6a6eb-abdb-41f6-addb-14dd5f6cb935")));
        QCOMPARE(registry.moduleVersion(id), QStringLiteral("0.1.0"));
        QCOMPARE(registry.moduleAuthor(id), QStringLiteral("Rangel Reale"));
        QCOMPARE(registry.moduleHomepage(id),
                 QUrl(QStringLiteral("https://github.com/rrgmc/mirante/tree/master/src/modules/github")));
    }
};

QTEST_MAIN(TestRegister)
#include "tst_register.moc"
