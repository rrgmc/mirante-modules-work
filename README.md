# Mirante work modules

The first-party modules of [Mirante](https://github.com/rrgmc/mirante), a personal desktop dashboard, for work: servers, issues and sessions. Each folder holds one module, with its own README. The other first-party modules are in [`mirante-modules`](https://github.com/rrgmc/mirante-modules).

This repository is part of the trial of custom builds. The core and the design documents are in [`mirante-core`](https://github.com/rrgmc/mirante-core).

| Module | What it provides |
|---|---|
| [Claude](claude/README.md) | Claude sessions on claude.ai |
| [GitHub](github/README.md) | github.com or GitHub Enterprise Server |
| [GitLab](gitlab/README.md) | A GitLab server |
| [Jira](jira/README.md) | A Jira site |

## Adding them to an app

List this repository in the app's `mirante.json`, with the folders you want:

```json
{
  "name": "mirante_modules_work",
  "repo": "https://github.com/rrgmc/mirante-modules-work",
  "ref": "<a release tag, such as v0.8.0>",
  "folders": ["claude", "github", "gitlab", "jira"]
}
```

An app can replace the layout of the GitHub summaries with code of its own. See [layouts](github/README.md#layouts).

The GitHub, GitLab and Jira modules keep a token for their server in the keychain. Use the narrowest token that works. See [what people who build with a module should know](https://github.com/rrgmc/mirante-core/blob/master/docs/writing-a-module.md#what-people-who-build-with-your-module-should-know).

## Author

Rangel Reale <realerangel@gmail.com>

## License

These modules are released under the [MIT License](LICENSE).
