mirante_add_module(jira
    SOURCES   JiraModule.cpp JiraUrls.cpp JiraUrls.h
    RESOURCES icons/jira.svg
    TESTS     tests/tst_from_url.cpp tests/tst_register.cpp tests/tst_urls.cpp
    DEFAULT   ON)
