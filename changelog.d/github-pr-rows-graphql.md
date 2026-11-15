### Changed

- The GitHub pull request lists read the Checks and Reviews columns of the shown rows with one GraphQL request, instead of up to four REST requests per row. A page of 25 rows costs one request instead of up to 100. When GraphQL fails, the list asks the REST API row by row, as before; a server without the GraphQL API is asked with REST alone from then on. `PullRequestListModel` in `GitHubPullRequestListLayout.h` has new private members, so an app that replaces the pull request list layout must build again.
