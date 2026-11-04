# Changelog

All notable changes to these modules are documented in this file. The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the repository follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html). While the version is 0.x, a minor version may break apps.

A release is tagged when a merged pull request raises `VERSION`. See [decision 0134](https://github.com/rrgmc/mirante-core/blob/master/docs/decisions/0134-release-tags-from-a-version-file.md) in the core.

## [Unreleased]

### Added

- Claude agents (`claude.agents`), a list of the Claude Code sessions on this device, with their title, status and repository, for one or more accounts. It runs `claude agents --json` and shows no session text. See decision 0143 in mirante-core.
- `mirante-bridge`, an optional Claude Code plugin in `claude/plugin`. Its hooks and status line wrapper write each session's latest event and status line input into an exchange folder. Claude agents then shows each session's tool, model and effort, context use and cost, says "Needs permission" when a session waits for one, and shows each account's 5-hour and weekly rate limits above the table. The list refreshes when the folder changes. See [the bridge plugin](claude/README.md#the-bridge-plugin) and decision 0148 in mirante-core.

### Changed

- In a narrow GitHub Run List, the Workflow column hides first, then Branch, then Repository. Before, Repository hid first and Branch never hid.

## [0.11.0] - 2026-11-04

Settings and list forms can add repositories and projects from the server or the profile, and lists keep the user's last filter choice. It needs the core's v0.8.6.

### Added

- The GitHub and GitLab settings screens have an **Add from Server…** button under their lists of repositories and project paths. It lists those the token can reach, to check and add, so they need not be typed. It needs the core with decision 0138. See [GitHub settings screens](github/README.md#settings-screens) and [GitLab settings screens](gitlab/README.md#settings-screens).
- The GitHub lists over several repositories and the GitLab lists over several projects have an **Add from List…** button under their `repositories` and `projects` fields. It offers the repositories and project paths of the profile's settings, to check and add. It needs the core with decision 0139. See [several repositories](github/README.md#several-repositories) and [several projects](gitlab/README.md#several-projects).

### Changed

- A list's filters keep the user's last choice. Changing a filter writes it to the item's `statusFilter`, `workflowFilter`, `repositoryFilter` or `projectFilter`, and the next start chooses it again. The item's form no longer shows these keys in a "Filters at the start" group. Jira's Status filter keeps its choice too, in `statusFilter`. Items that set these keys in the form keep their values. See [kept filters](github/README.md#kept-filters).
- The list layouts' interface: the GitHub, GitLab and Jira `ListModel` have `keepFilter(column, values)`; Jira's also has `filterDefault(column)`. An app's own list layout keeps building, and keeps no choice until it calls them. The modules need the core's `ItemListView::filterChanged()`.

## [0.10.0] - 2026-11-03

Lists can cover several repositories or projects, and a list's filters can start with values from its settings.

### Added

- New list types over several repositories or projects: `github.pr.multilist`, `github.run.multilist`, `gitlab.mr.multilist` and `gitlab.pipeline.multilist`. Each has a list of repositories or projects in place of the scope, and leaves out the fields that fit one repository only, such as a pull request number. GitHub's pull requests come from one search; the others ask each repository or project and sort the rows by date. Their tables show each row's repository or project, with a filter on it. See [several repositories](github/README.md#several-repositories) and [several projects](gitlab/README.md#several-projects).
- A list's filters start with values from the item's settings: `statusFilter`, `workflowFilter`, `repositoryFilter` and `projectFilter`. See [filters at the start](github/README.md#filters-at-the-start).

### Changed

- The list layouts' interface: `ListModel` has `spansRepositories()` (GitHub) or `spansProjects()` (GitLab) and `filterDefault()`; `RunListModel::Run` has `repository` and `PipelineListModel::Pipeline` has `project`; the models' constructors take a `ListSpan`. An app's own list layout keeps building, and shows the new types' rows without their repository or project until it reads them.
- The default run, pipeline, pull request and merge request list layouts have a Repository or Project column, hidden for a list of one repository or project. The modules need the core's `ItemListView::setFilterDefault()`.

## [0.9.0] - 2026-11-03

Apps can replace the layout of every native view of the `github`, `gitlab` and `jira` modules at build time: the summaries and the lists. The default layouts draw the views as before.

### Added

- The `github` module's summaries are extension points: `issuelayout`, `pullrequestlayout` and `runlayout`. An app can replace a summary's whole layout at build time with code in its own repository, which draws the module's model. The models and the points' headers are an interface that apps build against. See [layouts](github/README.md#layouts).
- An example extension for `runlayout`, in `examples/extensions/github-run-table`.
- The `gitlab` module's summaries are extension points: `issuelayout`, `mergerequestlayout` and `pipelinelayout`. See [its layouts](gitlab/README.md#layouts).
- The `jira` module's issue summary is an extension point: `issuelayout`. See [its layouts](jira/README.md#layouts).
- The `github` module's lists are extension points: `issuelistlayout`, `pullrequestlistlayout` and `runlistlayout`. The pull request model reads the checks and reviews of the rows that a layout shows. See [list layouts](github/README.md#list-layouts).
- The `gitlab` module's lists are extension points: `issuelistlayout`, `mergerequestlistlayout` and `pipelinelistlayout`. See [list layouts](gitlab/README.md#list-layouts).
- The `jira` module's list is an extension point: `listlayout`. See [its layouts](jira/README.md#layouts).
- The README explains how an app names an extension in its own repository or in a repository that holds only extensions. See [replacing the layout of a view](README.md#replacing-the-layout-of-a-view).

### Changed

- The `github` summaries are split into a model, which reads the status and runs the actions, and a default layout, which draws them as before.
- The `gitlab` summaries and the `jira` issue summary are split the same way.
- The `github`, `gitlab` and `jira` lists are split into a model and a default layout, which draws the core's table as before.

## [0.8.0] - 2026-11-03

The first release of the work modules on their own.

### Added

- The modules `claude`, `github`, `gitlab` and `jira`, which moved from [`mirante-modules`](https://github.com/rrgmc/mirante-modules) with their history. See [decision 0133](https://github.com/rrgmc/mirante-core/blob/master/docs/decisions/0133-work-modules-in-their-own-repository.md) in the core.
- The repository builds alone, as an app of its own folders, with CI on Windows, macOS and Linux that checks the modules' includes and translations.

[Unreleased]: https://github.com/rrgmc/mirante-modules-work/compare/v0.11.0...HEAD
[0.11.0]: https://github.com/rrgmc/mirante-modules-work/compare/v0.10.0...v0.11.0
[0.10.0]: https://github.com/rrgmc/mirante-modules-work/compare/v0.9.0...v0.10.0
[0.9.0]: https://github.com/rrgmc/mirante-modules-work/compare/v0.8.0...v0.9.0
[0.8.0]: https://github.com/rrgmc/mirante-modules-work/tree/v0.8.0
