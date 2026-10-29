mirante_add_module(gitlab
    SOURCES   GitLabModule.cpp GitLabUrls.cpp GitLabUrls.h
    RESOURCES icons/gitlab.svg
    TESTS     tests/tst_register.cpp tests/tst_urls.cpp
    DEFAULT   ON)
