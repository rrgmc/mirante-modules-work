mirante_add_module(gitlab
    API       0.1
    SOURCES   GitLabQuickAdd.cpp GitLabQuickAdd.h GitLabClient.cpp GitLabClient.h GitLabIssueLayout.h
              GitLabIssueListLayout.h GitLabIssueListView.cpp GitLabIssueListView.h GitLabIssueView.cpp
              GitLabIssueView.h GitLabListModel.cpp GitLabListModel.h GitLabLists.cpp GitLabLists.h
              GitLabListSupport.cpp GitLabListSupport.h GitLabMergeRequestLayout.h GitLabMergeRequestListLayout.h
              GitLabMergeRequestListView.cpp GitLabMergeRequestListView.h GitLabMergeRequestView.cpp
              GitLabMergeRequestView.h GitLabModule.cpp GitLabPipelineLayout.h GitLabPipelineListLayout.h
              GitLabPipelineListView.cpp GitLabPipelineListView.h GitLabPipelineView.cpp GitLabPipelineView.h
              GitLabSettings.cpp GitLabSettings.h GitLabStatus.cpp GitLabStatus.h GitLabSummaryModel.cpp
              GitLabSummaryModel.h GitLabUrls.cpp GitLabUrls.h GitLabViewSupport.cpp GitLabViewSupport.h
    # The layouts of the summaries and the lists, which an app can replace. See
    # README.md#layouts and README.md#list-layouts.
    EXTENSIONS issuelayout=GitLabIssueLayoutDefault.cpp
              mergerequestlayout=GitLabMergeRequestLayoutDefault.cpp
              pipelinelayout=GitLabPipelineLayoutDefault.cpp
              issuelistlayout=GitLabIssueListLayoutDefault.cpp
              mergerequestlistlayout=GitLabMergeRequestListLayoutDefault.cpp
              pipelinelistlayout=GitLabPipelineListLayoutDefault.cpp
    RESOURCES icons/gitlab.svg
    TESTS     tests/tst_client.cpp tests/tst_from_url.cpp tests/tst_list_models.cpp tests/tst_list_query.cpp
              tests/tst_list_views.cpp
              tests/tst_quick_add.cpp tests/tst_register.cpp tests/tst_settings.cpp tests/tst_status.cpp
              tests/tst_summary_models.cpp tests/tst_summary_views.cpp tests/tst_urls.cpp
    DEFAULT   ON)
