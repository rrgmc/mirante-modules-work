mirante_add_module(jira
    SOURCES   JiraModule.cpp JiraSettings.cpp JiraSettings.h JiraUrls.cpp JiraUrls.h
    RESOURCES icons/jira.svg
    TESTS     tests/tst_from_url.cpp tests/tst_register.cpp tests/tst_settings.cpp tests/tst_urls.cpp
    DEFAULT   ON)
