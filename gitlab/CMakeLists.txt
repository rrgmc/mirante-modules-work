mirante_add_module(gitlab
    SOURCES   GitLabClient.cpp GitLabClient.h GitLabIssueListView.cpp GitLabIssueListView.h
              GitLabIssueView.cpp GitLabIssueView.h GitLabLists.cpp GitLabLists.h
              GitLabMergeRequestListView.cpp GitLabMergeRequestListView.h GitLabMergeRequestView.cpp
              GitLabMergeRequestView.h GitLabModule.cpp GitLabPipelineView.cpp GitLabPipelineView.h
              GitLabSettings.cpp GitLabSettings.h GitLabStatus.cpp GitLabStatus.h GitLabUrls.cpp GitLabUrls.h
    RESOURCES icons/gitlab.svg
    TESTS     tests/tst_client.cpp tests/tst_from_url.cpp tests/tst_register.cpp tests/tst_settings.cpp
              tests/tst_status.cpp tests/tst_urls.cpp
    DEFAULT   ON)
