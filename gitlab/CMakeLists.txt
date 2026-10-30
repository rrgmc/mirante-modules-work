mirante_add_module(gitlab
    SOURCES   GitLabQuickAdd.cpp GitLabQuickAdd.h GitLabClient.cpp GitLabClient.h GitLabIssueListView.cpp GitLabIssueListView.h
              GitLabIssueView.cpp GitLabIssueView.h GitLabLists.cpp GitLabLists.h GitLabListSupport.cpp
              GitLabListSupport.h GitLabMergeRequestListView.cpp GitLabMergeRequestListView.h GitLabMergeRequestView.cpp
              GitLabMergeRequestView.h GitLabModule.cpp GitLabPipelineListView.cpp GitLabPipelineListView.h
              GitLabPipelineView.cpp GitLabPipelineView.h
              GitLabSettings.cpp GitLabSettings.h GitLabStatus.cpp GitLabStatus.h GitLabUrls.cpp GitLabUrls.h
              GitLabViewSupport.cpp GitLabViewSupport.h
    RESOURCES icons/gitlab.svg
    TESTS     tests/tst_client.cpp tests/tst_from_url.cpp tests/tst_list_query.cpp tests/tst_list_views.cpp
              tests/tst_quick_add.cpp tests/tst_register.cpp tests/tst_settings.cpp tests/tst_status.cpp
              tests/tst_summary_views.cpp tests/tst_urls.cpp
    DEFAULT   ON)
