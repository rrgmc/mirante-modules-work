mirante_add_module(claude
    SOURCES   LocalSessionView.cpp LocalSessionView.h ClaudeModule.cpp ClaudeUrls.cpp ClaudeUrls.h
    RESOURCES icons/claude.svg
    TESTS     tests/tst_from_url.cpp tests/tst_register.cpp tests/tst_urls.cpp tests/tst_views.cpp
    DEFAULT   ON)
