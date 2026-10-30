mirante_add_module(jira
    SOURCES   JiraClient.cpp JiraClient.h JiraFields.cpp JiraFields.h JiraIssueView.cpp JiraIssueView.h JiraLists.cpp JiraLists.h
              JiraListView.cpp JiraListView.h JiraModule.cpp JiraServerType.cpp JiraServerType.h
              JiraSettings.cpp JiraSettings.h JiraStatus.cpp JiraStatus.h JiraUrls.cpp JiraUrls.h
    RESOURCES icons/jira.svg
    TESTS     tests/tst_client.cpp tests/tst_from_url.cpp tests/tst_list_jql.cpp tests/tst_register.cpp
              tests/tst_settings.cpp tests/tst_status.cpp tests/tst_urls.cpp tests/tst_views.cpp
    DEFAULT   ON)
