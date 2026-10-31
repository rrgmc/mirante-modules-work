mirante_add_module(github
    SOURCES   GitHubClient.cpp GitHubClient.h GitHubIssueListView.cpp GitHubIssueListView.h
              GitHubIssueView.cpp GitHubIssueView.h GitHubLists.cpp GitHubLists.h GitHubListSupport.cpp
              GitHubListSupport.h GitHubModule.cpp GitHubPullRequestListView.cpp GitHubPullRequestListView.h
              GitHubPullRequestView.cpp GitHubPullRequestView.h GitHubQuickAdd.cpp GitHubQuickAdd.h
              GitHubRunListView.cpp GitHubRunListView.h GitHubRunView.cpp GitHubRunView.h
              GitHubSettings.cpp GitHubSettings.h GitHubStatus.cpp GitHubStatus.h GitHubUrls.cpp GitHubUrls.h
              GitHubViewSupport.cpp GitHubViewSupport.h
    RESOURCES icons/github.svg
    TESTS     tests/tst_client.cpp tests/tst_from_url.cpp tests/tst_list_query.cpp tests/tst_list_views.cpp
              tests/tst_quick_add.cpp tests/tst_register.cpp tests/tst_settings.cpp tests/tst_status.cpp
              tests/tst_summary_views.cpp tests/tst_urls.cpp
    DEFAULT   ON)
