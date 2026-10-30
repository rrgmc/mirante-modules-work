mirante_add_module(jira
    SOURCES   JiraClient.cpp JiraClient.h JiraIssueView.cpp JiraIssueView.h JiraLists.cpp JiraLists.h
              JiraListView.cpp JiraListView.h JiraModule.cpp JiraServerType.cpp JiraServerType.h
              JiraSettings.cpp JiraSettings.h JiraStatus.cpp JiraStatus.h JiraUrls.cpp JiraUrls.h
    RESOURCES icons/jira.svg
    TESTS     tests/tst_client.cpp tests/tst_from_url.cpp tests/tst_register.cpp tests/tst_settings.cpp
              tests/tst_status.cpp tests/tst_urls.cpp
    DEFAULT   ON)
