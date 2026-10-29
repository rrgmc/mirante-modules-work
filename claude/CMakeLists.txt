mirante_add_module(claude
    SOURCES   ClaudeModule.cpp ClaudeUrls.cpp ClaudeUrls.h
    RESOURCES icons/claude.svg
    TESTS     tests/tst_register.cpp tests/tst_urls.cpp
    DEFAULT   ON)
