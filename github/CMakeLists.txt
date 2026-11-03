mirante_add_module(github
    API       0.1
    SOURCES   GitHubClient.cpp GitHubClient.h GitHubIssueLayout.h GitHubIssueListLayout.h GitHubIssueListView.cpp
              GitHubIssueListView.h GitHubIssueView.cpp GitHubIssueView.h GitHubListModel.cpp GitHubListModel.h
              GitHubLists.cpp GitHubLists.h GitHubListSupport.cpp GitHubListSupport.h GitHubModule.cpp
              GitHubPullRequestLayout.h GitHubPullRequestListLayout.h GitHubPullRequestListView.cpp
              GitHubPullRequestListView.h GitHubPullRequestView.cpp GitHubPullRequestView.h GitHubQuickAdd.cpp
              GitHubQuickAdd.h GitHubRunLayout.h GitHubRunListLayout.h GitHubRunListView.cpp GitHubRunListView.h
              GitHubRunView.cpp GitHubRunView.h GitHubSettings.cpp GitHubSettings.h GitHubStatus.cpp GitHubStatus.h
              GitHubSummaryModel.cpp GitHubSummaryModel.h GitHubUrls.cpp GitHubUrls.h GitHubViewSupport.cpp
              GitHubViewSupport.h
    # The layouts of the summaries and the lists, which an app can replace. See
    # README.md#layouts and README.md#list-layouts.
    EXTENSIONS issuelayout=GitHubIssueLayoutDefault.cpp
              pullrequestlayout=GitHubPullRequestLayoutDefault.cpp
              runlayout=GitHubRunLayoutDefault.cpp
              issuelistlayout=GitHubIssueListLayoutDefault.cpp
              pullrequestlistlayout=GitHubPullRequestListLayoutDefault.cpp
              runlistlayout=GitHubRunListLayoutDefault.cpp
    RESOURCES icons/github.svg
    TESTS     tests/tst_client.cpp tests/tst_from_url.cpp tests/tst_list_query.cpp tests/tst_list_views.cpp
              tests/tst_quick_add.cpp tests/tst_register.cpp tests/tst_settings.cpp tests/tst_status.cpp
              tests/tst_summary_models.cpp tests/tst_summary_views.cpp tests/tst_urls.cpp
    DEFAULT   ON)
