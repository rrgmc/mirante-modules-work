### Added

- GitHub CI Health (`github.ci.list`): a table of an organization's or a user's repositories, the most recently pushed first, with the checks of each default branch's newest commit. One GraphQL request reads up to 100 repositories. Forks and archived repositories are left out unless the item's settings include them. A filter on Checks shows only the failing ones, and "Add" adds a GitHub Actions Run that follows the repository's default branch. See [CI health](github/README.md#ci-health).
- The extension point `repositorylistlayout`, with `RepositoryListModel` in `GitHubRepositoryListLayout.h`, through which an app can replace the layout of the CI health list.
