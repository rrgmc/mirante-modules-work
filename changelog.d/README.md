# Changelog fragments

Each pull request that changes what users of these modules see adds one file here, instead of editing [CHANGELOG.md](../CHANGELOG.md). Pull requests then never change the same lines, so they do not conflict. A release moves the files into `CHANGELOG.md` and deletes them. See [decision 0154](https://github.com/rrgmc/mirante-core/blob/master/docs/decisions/0154-changelog-fragments.md) in the core.

Name the file after the branch, such as `status-filter-all-values.md`. It holds one or more headings of [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), each with its entries:

```markdown
### Fixed

- The Status filter of the GitHub run lists always lists every status.
```

The headings are `### Added`, `### Changed`, `### Deprecated`, `### Removed`, `### Fixed` and `### Security`. An entry is a line that starts with `- `, with any further lines indented under it. Write it as in `CHANGELOG.md`: for the people who use a release. A change to an interface that apps build against, such as a list layout's model, says so and says what an app must do. Links are relative to the repository's root, as they are in `CHANGELOG.md`.

CI checks every fragment, with the core's `tools/release-changelog.py`. To check them locally, from this repository's root, with the core checked out beside it:

```sh
python3 ../mirante-core/tools/release-changelog.py --root . check
```

A release runs `python3 ../mirante-core/tools/release-changelog.py --root . release X.Y.Z`, which writes `VERSION`, moves the fragments into a section for the new version of `CHANGELOG.md`, and deletes them.
