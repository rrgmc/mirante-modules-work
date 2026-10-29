mirante_add_module(gitlab
    SOURCES   GitLabModule.cpp GitLabSettings.cpp GitLabSettings.h GitLabUrls.cpp GitLabUrls.h
    RESOURCES icons/gitlab.svg
    TESTS     tests/tst_from_url.cpp tests/tst_register.cpp tests/tst_settings.cpp tests/tst_urls.cpp
    DEFAULT   ON)
