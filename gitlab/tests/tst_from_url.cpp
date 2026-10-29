// Reading pasted GitLab addresses: the page address, what each type makes of
// it, servers of the profile and others, and addresses that are refused.
#include "../GitLabUrls.h"

#include "items/ItemTypeRegistry.h"

#include <mirante/ItemType.h>

#include <QJsonObject>
#include <QTest>

#include <optional>

namespace mirante::modules::gitlab {
void registerModule(mirante::ModuleRegistrar &registrar);
}

using mirante::UrlMatch;
using mirante::modules::gitlab::matchUrl;
using mirante::modules::gitlab::readPageAddress;

namespace {

QUrl url(const char *text) { return QUrl(QString::fromUtf8(text)); }

} // namespace

class TestFromUrl : public QObject
{
    Q_OBJECT

private slots:
    void mergeRequestOnGitLabCom()
    {
        const auto address = readPageAddress(url("https://gitlab.com/acme/backend/-/merge_requests/42"), {});
        QVERIFY(address);
        QCOMPARE(address->baseUrl, url("https://gitlab.com"));
        QCOMPARE(address->project, QStringLiteral("acme/backend"));
        QCOMPARE(address->kind, QStringLiteral("merge_requests"));
        QCOMPARE(address->number, 42);

        for (const char *type : {"gitlab.mr.summary", "gitlab.mr.page"}) {
            const std::optional<UrlMatch> match =
                matchUrl(QString::fromUtf8(type), url("https://gitlab.com/acme/backend/-/merge_requests/42"), {});
            QVERIFY(match);
            QCOMPARE(match->config, (QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")},
                                                 {QStringLiteral("iid"), 42}}));
            QCOMPARE(match->name, QStringLiteral("acme/backend!42"));
            QCOMPARE(match->baseUrl, url("https://gitlab.com"));
            QVERIFY(!match->generic);
        }
    }

    // A tab of the page, a query and a fragment do not change the item.
    void mergeRequestTabs()
    {
        const auto match = matchUrl(QStringLiteral("gitlab.mr.page"),
                                    url("https://gitlab.com/a/b/c/-/merge_requests/7/diffs?view=inline#note_1"), {});
        QVERIFY(match);
        QCOMPARE(match->config.value(QStringLiteral("project")).toString(), QStringLiteral("a/b/c"));
        QCOMPARE(match->config.value(QStringLiteral("iid")).toInteger(), 7);
    }

    void issue()
    {
        const QUrl page = url("https://gitlab.com/acme/backend/-/issues/7");
        for (const char *type : {"gitlab.issue.summary", "gitlab.issue.page"}) {
            const auto match = matchUrl(QString::fromUtf8(type), page, {});
            QVERIFY(match);
            QCOMPARE(match->config.value(QStringLiteral("iid")).toInteger(), 7);
            QCOMPARE(match->name, QStringLiteral("acme/backend#7"));
        }
        // An issue is not a merge request, and the reverse.
        QVERIFY(!matchUrl(QStringLiteral("gitlab.mr.summary"), page, {}));
        QVERIFY(!matchUrl(QStringLiteral("gitlab.issue.page"),
                          url("https://gitlab.com/acme/backend/-/merge_requests/7"), {}));
    }

    void lists()
    {
        const auto mrs = matchUrl(QStringLiteral("gitlab.mr.list"),
                                  url("https://gitlab.com/acme/backend/-/merge_requests"), {});
        QVERIFY(mrs);
        QCOMPARE(mrs->config, (QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")},
                                           {QStringLiteral("scope"), QStringLiteral("project")},
                                           {QStringLiteral("preset"), QStringLiteral("allOpen")}}));
        QCOMPARE(mrs->name, QStringLiteral("acme/backend MRs"));
        const auto issues =
            matchUrl(QStringLiteral("gitlab.issue.list"), url("https://gitlab.com/acme/backend/-/issues"), {});
        QVERIFY(issues);
        QCOMPARE(issues->name, QStringLiteral("acme/backend issues"));
        // One merge request is not a list.
        QVERIFY(!matchUrl(QStringLiteral("gitlab.mr.list"),
                          url("https://gitlab.com/acme/backend/-/merge_requests/1"), {}));
    }

    void pipeline()
    {
        // A merge request's pipeline follows the merge request.
        const auto fromMr = matchUrl(QStringLiteral("gitlab.pipeline"),
                                     url("https://gitlab.com/acme/backend/-/merge_requests/42"), {});
        QVERIFY(fromMr);
        QCOMPARE(fromMr->config, (QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")},
                                              {QStringLiteral("branchSource"), QStringLiteral("mergeRequest")},
                                              {QStringLiteral("iid"), 42}}));
        QCOMPARE(fromMr->name, QStringLiteral("acme/backend!42 CI"));

        // A pipelines page with a branch follows that branch.
        const auto fromRef = matchUrl(QStringLiteral("gitlab.pipeline"),
                                      url("https://gitlab.com/acme/backend/-/pipelines?ref=feature%2Flogin"), {});
        QVERIFY(fromRef);
        QCOMPARE(fromRef->config, (QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")},
                                               {QStringLiteral("branchSource"), QStringLiteral("fixed")},
                                               {QStringLiteral("branch"), QStringLiteral("feature/login")}}));
        QCOMPARE(fromRef->name, QStringLiteral("acme/backend feature/login CI"));

        // One pipeline gives the project only; its id is not kept.
        const auto one = matchUrl(QStringLiteral("gitlab.pipeline"),
                                  url("https://gitlab.com/acme/backend/-/pipelines/123456"), {});
        QVERIFY(one);
        QCOMPARE(one->config, (QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")},
                                           {QStringLiteral("branchSource"), QStringLiteral("fixed")}}));

        // An issue has no pipeline.
        QVERIFY(!matchUrl(QStringLiteral("gitlab.pipeline"), url("https://gitlab.com/acme/backend/-/issues/1"), {}));
    }

    // A self-hosted server at a path is read with the profile's base URL,
    // so the path is not taken for a group.
    void selfHostedAtPath()
    {
        const QList<QUrl> bases{url("https://example.org"), url("https://example.org/gitlab/")};
        const auto match = matchUrl(QStringLiteral("gitlab.mr.summary"),
                                    url("https://example.org/gitlab/ops/infra/-/merge_requests/3"), bases);
        QVERIFY(match);
        QCOMPARE(match->config.value(QStringLiteral("project")).toString(), QStringLiteral("ops/infra"));
        QCOMPARE(match->baseUrl, url("https://example.org/gitlab/"));
    }

    // A server that is not the profile's is still recognised by GitLab's "/-/".
    void serverWithoutConnection()
    {
        const auto match = matchUrl(QStringLiteral("gitlab.issue.page"),
                                    url("https://git.example.net:8443/team/app/-/issues/9"), {});
        QVERIFY(match);
        QCOMPARE(match->baseUrl, url("https://git.example.net:8443"));
        QCOMPARE(match->config.value(QStringLiteral("project")).toString(), QStringLiteral("team/app"));
    }

    void refused()
    {
        const QString type = QStringLiteral("gitlab.mr.summary");
        QVERIFY(!matchUrl(type, url("https://gitlab.com/acme/backend"), {}));
        QVERIFY(!matchUrl(type, url("https://gitlab.com/acme/backend/-/merge_requests/new"), {}));
        QVERIFY(!matchUrl(type, url("https://gitlab.com/acme/backend/-/merge_requests/0"), {}));
        QVERIFY(!matchUrl(type, url("https://gitlab.com/acme/backend/-/tree/main"), {}));
        QVERIFY(!matchUrl(type, url("https://gitlab.com/backend/-/merge_requests/1"), {}));   // no group
        QVERIFY(!matchUrl(type, url("ftp://gitlab.com/acme/backend/-/merge_requests/1"), {}));
        QVERIFY(!matchUrl(type, url("https://example.com/page"), {}));
        QVERIFY(!matchUrl(type, QUrl(), {}));
        QVERIFY(!matchUrl(QStringLiteral("gitlab.unknown"), url("https://gitlab.com/a/b/-/merge_requests/1"), {}));
    }

    // The registered types answer through fromUrl().
    void registeredTypes()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("gitlab"), &mirante::modules::gitlab::registerModule);
        const QUrl page = url("https://gitlab.com/acme/backend/-/merge_requests/42");
        QStringList taking;
        for (const mirante::ItemType *type : registry.types()) {
            if (type->fromUrl(page, {}))
                taking << type->id();
        }
        taking.sort();
        QCOMPARE(taking, (QStringList{QStringLiteral("gitlab.mr.page"), QStringLiteral("gitlab.mr.summary"),
                                      QStringLiteral("gitlab.pipeline")}));
    }
};

QTEST_MAIN(TestFromUrl)
#include "tst_from_url.moc"
