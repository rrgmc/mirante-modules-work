# Changelog

All notable changes to these modules are documented in this file. The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the repository follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html). While the version is 0.x, a minor version may break apps.

A release is tagged when a merged pull request raises `VERSION`. See [decision 0134](https://github.com/rrgmc/mirante-core/blob/master/docs/decisions/0134-release-tags-from-a-version-file.md) in the core.

## [Unreleased]

### Added

- The `github` module's summaries are extension points: `issuelayout`, `pullrequestlayout` and `runlayout`. An app can replace a summary's whole layout at build time with code in its own repository, which draws the module's model. The models and the points' headers are an interface that apps build against. See [layouts](github/README.md#layouts).
- An example extension for `runlayout`, in `examples/extensions/github-run-table`.
- The `gitlab` module's summaries are extension points: `issuelayout`, `mergerequestlayout` and `pipelinelayout`. See [its layouts](gitlab/README.md#layouts).
- The `jira` module's issue summary is an extension point: `issuelayout`. See [its layouts](jira/README.md#layouts).
- The `github` module's lists are extension points: `issuelistlayout`, `pullrequestlistlayout` and `runlistlayout`. The pull request model reads the checks and reviews of the rows that a layout shows. See [list layouts](github/README.md#list-layouts).
- The README explains how an app names an extension in its own repository or in a repository that holds only extensions. See [replacing the layout of a view](README.md#replacing-the-layout-of-a-view).

### Changed

- The `github` summaries are split into a model, which reads the status and runs the actions, and a default layout, which draws them as before.
- The `gitlab` summaries and the `jira` issue summary are split the same way.
- The `github` lists are split into a model and a default layout, which draws the core's table as before.

## [0.8.0] - 2026-11-03

The first release of the work modules on their own.

### Added

- The modules `claude`, `github`, `gitlab` and `jira`, which moved from [`mirante-modules`](https://github.com/rrgmc/mirante-modules) with their history. See [decision 0133](https://github.com/rrgmc/mirante-core/blob/master/docs/decisions/0133-work-modules-in-their-own-repository.md) in the core.
- The repository builds alone, as an app of its own folders, with CI on Windows, macOS and Linux that checks the modules' includes and translations.

[Unreleased]: https://github.com/rrgmc/mirante-modules-work/compare/v0.8.0...HEAD
[0.8.0]: https://github.com/rrgmc/mirante-modules-work/tree/v0.8.0
