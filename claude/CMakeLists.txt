mirante_add_module(claude
    API       0.1
    SOURCES   ClaudeAgents.cpp ClaudeAgents.h ClaudeAgentsFetch.cpp ClaudeAgentsFetch.h ClaudeAgentsView.cpp
              ClaudeAgentsView.h ClaudeModule.cpp ClaudeUrls.cpp ClaudeUrls.h
    RESOURCES icons/claude.svg
    TESTS     tests/tst_agents.cpp tests/tst_from_url.cpp tests/tst_register.cpp tests/tst_urls.cpp
    DEFAULT   ON)
