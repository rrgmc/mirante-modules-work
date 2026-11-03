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

The GitHub, GitLab and Jira modules keep a token for their server in the keychain. Use the narrowest token that works. See [what people who build with a module should know](https://github.com/rrgmc/mirante-core/blob/master/docs/writing-a-module.md#what-people-who-build-with-your-module-should-know).

## Replacing the layout of a view

An app can replace the whole layout of a module's native views when it builds Mirante: their widgets, buttons, colours and tables. Each view is an [extension point](https://github.com/rrgmc/mirante-core/blob/master/docs/modules.md#extension-points). The module keeps a model, which reads the item's status and runs the actions, so a layout only draws the model.

| Module | Points |
|---|---|
| GitHub | `issuelayout`, `pullrequestlayout`, `runlayout`, and for its lists `issuelistlayout`, `pullrequestlistlayout`, `runlistlayout`. See [its layouts](github/README.md#layouts) and [list layouts](github/README.md#list-layouts). |
| GitLab | `issuelayout`, `mergerequestlayout`, `pipelinelayout`, and for its lists `issuelistlayout`, `mergerequestlistlayout`, `pipelinelistlayout`. See [its layouts](gitlab/README.md#layouts) and [list layouts](gitlab/README.md#list-layouts). |
| Jira | `issuelayout`, and for its list `listlayout`. See [its layouts](jira/README.md#layouts). |

The Claude module has no native views, so it has no points.

**The contract.** Each point has a header in the module's folder, such as `GitHubRunLayout.h`, which declares the model and a function `make<View>Layout(model, parent)`. An extension defines that function in the module's namespace, such as `mirante::modules::github`. It returns the view's widget, a child of `parent`, which draws the model at once and again at each `changed()`. The model then becomes a child of that widget. A layout may use the model, Qt and the module interface (`<mirante/...>`), and nothing else of the module. An app may replace one point of a module and keep the defaults of the others.

**An extension in the app's own repository.** The app names a folder of its repository for the point, in the module's folder entry of its manifest:

```json
{
  "folder": "github",
  "extensions": { "runlayout": { "local": "extensions/github-run-layout" } }
}
```

The core compiles every `.cpp` and `.h` file of that folder into the module, in place of the point's default, with the module's folder on the include path. The extension includes the point's header by name, such as `#include "GitHubRunLayout.h"`. A `resources.txt` in the folder lists resources, one path per line, served as `:/modules/<module>/<path>`.

**Extensions in another repository.** A repository can hold only extensions: folders of sources, each with an optional `resources.txt`, and no `CMakeLists.txt` and no module. The app names a folder of it with `name`, `repo`, `ref` and `folder` in place of `local`:

```json
{
  "folder": "github",
  "extensions": {
    "runlayout": {
      "name": "acme_layouts",
      "repo": "https://github.com/acme/mirante-layouts",
      "ref": "<a commit or a tag>",
      "folder": "github-run-table"
    }
  }
}
```

The core fetches that repository but never runs CMake code from it, and compiles only the named folder. `FETCHCONTENT_SOURCE_DIR_ACME_LAYOUTS=<path>` builds from a checkout on disk instead, as for a repository of modules. The About dialog lists each extension as `<module>/<point>`, with its repository and commit, and `build-app.yml` checks its includes. Two cautions:

- An extension must match the interface of the module version that the app pins. A repository of extensions should say which version of these modules it targets, and the app should move both pins together.
- Points that come from the same repository should give the same `name`, `repo` and `ref`. FetchContent keeps the first declaration of a name, so a second entry with the same `name` and another `ref` would silently use the first entry's `ref`.

[`examples/extensions/github-run-table`](examples/extensions/github-run-table) is an example for GitHub's `runlayout`: the runs and the jobs as two tables in tabs, with coloured buttons. This repository's own build does not compile it, because its CI tests the defaults.

**Stability.** The point headers and the models are an interface that apps build against. A change to them is listed in [`CHANGELOG.md`](CHANGELOG.md), and while the version is 0.x a minor version may change them. Texts that only an extension uses are not in the module's translations.

## Author

Rangel Reale <realerangel@gmail.com>

## License

These modules are released under the [MIT License](LICENSE).
