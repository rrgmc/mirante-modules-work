# github module

GitHub pull requests, Actions runs and issues as items, from github.com or GitHub Enterprise Server. See [item types](../../../docs/item-types.md#github).

The module is built like the [gitlab module](../gitlab/README.md), and this README follows the shape of that one. [Decision 0102](../../../docs/decisions/0102-github-module.md) gives the choices that differ, and the [M11 plan](../../../docs/plans/m11-github-module.md) the details.

## Item types

| Id | Name | Rendering | Description |
|---|---|---|---|
| `github.pr.summary` | GitHub PR (summary) | native | A summary of one pull request: its state, branches, checks, reviews and people. |
| `github.pr.page` | GitHub PR (page) | web | The web page of one pull request, on the GitHub server of the item's profile. |
| `github.pr.list` | GitHub PR List | native | A table of pull requests in a repository, an owner or everywhere, such as those that wait for your review. |
| `github.run` | GitHub Actions Run | native | The Actions runs of a branch's newest commit, one per workflow, with their jobs. The branch is a fixed one, the main folder's current branch or a pull request's branch. |
| `github.run.list` | GitHub Actions Run List | native | A table of a repository's Actions runs: all of them, those you started, or those of a branch or a pull request. |
| `github.issue.summary` | GitHub Issue (summary) | native | A summary of one issue: its state, assignees, labels, milestone and latest comments. |
| `github.issue.page` | GitHub Issue (page) | web | The web page of one issue, on the GitHub server of the item's profile. |
| `github.issue.list` | GitHub Issue List | native | A table of issues in a repository, an owner or everywhere, such as those assigned to you. |

The short names are "PR", "PRs", "Run", "Runs", "Issue" and "Issues". A page type has the short name of its summary, as in the gitlab module.

The module calls itself "GitHub" and describes itself as: "Pull requests, Actions runs and issues from GitHub or GitHub Enterprise Server. Each profile sets its server and token in the module's General Configuration." Help > Modules shows the names and descriptions. See [decision 0056](../../../docs/decisions/0056-modules-window.md).

Its UUID is `6be6a6eb-abdb-41f6-addb-14dd5f6cb935`. It is at version 0.1.0, by Rangel Reale. Help > Modules also shows these, and the module's homepage. See [decision 0089](../../../docs/decisions/0089-module-identity.md).

Every type reports a [status](#status) and takes [pasted links](#items-from-a-link). The two page types show their page in a web view. The summaries have [native views](#summary-views), and the lists have [tables with links on each row](#list-views).

## Server

`GitHubServerType`, in `GitHubSettings.h`, tells the core where each profile's GitHub server is. The module registers it with `setServerType()`:

- the screen is `general` and the token field is `token`;
- the base URL is the API address, `apiBaseOf()` of the screen's values;
- `authorize()` sets `Authorization: Bearer <token>`, `Accept: application/vnd.github+json`, `X-GitHub-Api-Version: 2022-11-28` and `User-Agent: Mirante`. GitHub refuses a request without a user agent;
- `problem()` is always empty: a web address and a token are all GitHub needs.

The screen holds one address, the web address. `webBaseOf()` reads it: the screen's `baseUrl`, or `https://github.com` when it is empty. `apiBaseOf()` derives the API address from it:

| Web address | API address |
|---|---|
| `https://github.com` or `https://www.github.com` | `https://api.github.com/` |
| any other, such as `https://github.example.com` | `<web address>/api/v3/`, without doubling a slash, as GitHub Enterprise Server has it |

So `ApiClient` paths are `repos/...`, `search/issues` and `user`. Pages, pasted links and Web links use the web address. A web address that is not an `http` or `https` address gives neither.

The module never reads the token itself. It makes every request through the `ApiClient` of the item's profile, which adds the headers. See [servers and tokens](../../../docs/modules.md#servers-and-tokens).

**The token.** A fine-grained token needs read access to Metadata, Pull requests, Issues, Checks, Commit statuses and Actions, and write access to Actions for re-run and cancel. A classic token needs `repo`, or `public_repo` alone for public repositories. A `403` on an action says "The token cannot do this. It needs write access to Actions."

**Rate limits.** GitHub answers `403` or `429` with `x-ratelimit-remaining: 0` when the hourly limit is used up, and `403` with `Retry-After` for its secondary limits. `ServerClient` waits and sends again, as for a `429`, and does not pause the server. The search API has a lower limit of 30 requests a minute, which the [lists](#lists) count against.

## The client

`GitHubClient`, in `GitHubClient.cpp`, is a thin, copyable wrapper over an `ApiClient` and a receiver. It has one method per endpoint in [integrations](../../../docs/integrations.md#github). Each takes the parsed arguments, such as a repository and a number, and passes the `ApiReply` on unchanged.

- A repository is `owner/repo`. `repositoryPath()` percent-encodes its owner and its name each on its own: `repos/<owner>/<repo>`. The name is all after the first slash.
- A branch is percent-encoded into its query with `encode()`, so a branch with `+` or `&` in its name reaches GitHub whole. The search query is encoded the same way, in `q`.
- Lists set `followLinks` with a row limit, and ask for `per_page` of at most 100, and of at most that limit, unless the query names its own. A list whose answer wraps its rows in an object names the key of the rows in `GetOptions::arrayKey`, so the core joins the rows of every page under that key.
- `issueComments` asks for one page, without following links, because the [summary](#status) picks the pages.
- The actions send an empty JSON body, `{}`.
- `repositoryOfApiUrl()` reads `owner/repo` from an API address that holds `.../repos/<owner>/<repo>`, such as a search row's `repository_url`.

| Method | Request | `arrayKey` |
|---|---|---|
| `currentUser` | `GET user` | |
| `repository` | `GET repos/<r>` | |
| `pullRequest` | `GET repos/<r>/pulls/<n>` | |
| `pullRequestReviews` | `GET repos/<r>/pulls/<n>/reviews`, up to 100 | |
| `repositoryPullRequests` | `GET repos/<r>/pulls`, with a query such as `state=open&sort=updated&direction=desc` | |
| `checkRuns` | `GET repos/<r>/commits/<sha>/check-runs`, up to 100 | `check_runs` |
| `combinedStatus` | `GET repos/<r>/commits/<sha>/status` | |
| `issue` | `GET repos/<r>/issues/<n>` | |
| `issueComments` | `GET repos/<r>/issues/<n>/comments?per_page=<k>&page=<p>`, one page | |
| `searchIssues` | `GET search/issues?q=<query>&sort=updated&order=desc`, up to the limit | `items` |
| `workflowRuns` | `GET repos/<r>/actions/runs`, or `repos/<r>/actions/workflows/<file>/runs` with a workflow, with a query such as `branch=<b>`, `actor=<login>` or `event=pull_request` | `workflow_runs` |
| `runJobs` | `GET repos/<r>/actions/runs/<id>/jobs?filter=latest`, up to 100 | `jobs` |
| `rerunRun`, `rerunFailedJobs`, `cancelRun` | `POST repos/<r>/actions/runs/<id>/rerun`, `/rerun-failed-jobs`, `/cancel` | |
| `rerunJob` | `POST repos/<r>/actions/jobs/<id>/rerun` | |

The methods are tested in `tests/tst_client.cpp`.

## Status

Every type reports status: `hasStatus()` is true and `fetchStatus()` runs a function of `GitHubStatus.cpp`, or of `GitHubLists.cpp` for the [lists](#lists). The rules for the types that name one object are in pure functions, `pullRequestStatus()`, `issueStatus()`, `runStatus()`, `checksState()`, `latestReviews()`, `reviewState()` and `runGroup()`, declared in `GitHubStatus.h`, so they are tested without a job and the views use the same rules. "Me" is the user of `GET user`, asked in each job that needs it; the client's ETag memory makes it cheap.

| Types | Requests | Attention | Closed |
|---|---|---|---|
| `github.pr.summary`, `github.pr.page` | the pull request, its reviews and the current user, at once; then, at once, `checkRuns` and `combinedStatus` of `head.sha` | "Checks failed" (`checks-failed`, notify) when `checksState` is failed; "Review requested" (`review`) when I am in `requested_reviewers`; "Approved" (`approved`, clears on view) when I am the author and the reviews approve; "Changes requested" (`changes-requested`) when I am the author and a latest review is `CHANGES_REQUESTED`; "Has conflicts" (`conflicts`) when I am the author and `mergeable` is `false` | `state` is `closed` |
| `github.issue.summary`, `github.issue.page` | the issue; for the summary, then its latest comments | none | `state` is `closed` |
| `github.run` | for `pullRequest`, the pull request, for `head.ref` and `head.sha`; for an empty `branch`, the repository, for `default_branch`; for `mainFolder`, the folder's branch; then `workflowRuns` of the branch, up to 20; then the jobs of each run in the [group](#the-run-group), at once | "Run failed" (`run-failed:<id>:<run_attempt>`, notify) for each run of the group whose `conclusion` is `failure`, `timed_out` or `startup_failure` | never |

- **Only open pull requests need attention.** A closed or merged one reports no reasons.
- **Checks.** `checksState()` reads a pull request's head commit. It is failed when a check run's `conclusion` is `failure`, `timed_out` or `startup_failure`, or the combined status has statuses and its `state` is `failure` or `error`. Else it is running when a check run's `status` is not `completed`, or the combined status has statuses and its `state` is `pending`. Else it is passed when there is at least one check run or status, and else none. A `cancelled` or `action_required` conclusion is neither failed nor passed alone; it counts as passed if everything else passed, and shows its own pill in the view.
- **Reviews.** Each reviewer's latest review counts, by `submitted_at`. `COMMENTED` and `PENDING` reviews do not replace an earlier `APPROVED` or `CHANGES_REQUESTED`, and `DISMISSED` removes it. The reviews approve when at least one latest review is `APPROVED` and none is `CHANGES_REQUESTED`. GitHub's required number of approvals is in branch protection, which needs admin rights to read, so the summary counts approvals without a target.
- **Summaries.** A pull request reads like "Open · 2 approvals · checks failed": "Open", "Draft", "Merged" (when `merged` or `merged_at` is set) or "Closed"; the number of approvals, or "changes requested"; the checks as "checks passed", "checks failed" or "checks running", left out for none; and "has conflicts" while open. An issue reads like "Open · assigned to ana". A run group reads like "Failed · main · 1 of 3 runs failed", "Running · main · 3 runs" or "Passed · main · 3 runs"; a single run leaves out the count and names its workflow: "Passed · main · CI".
- **An empty branch is the default branch.** A run with an empty `branch` reads `default_branch` from `GET repos/<repository>`. Its ETag makes the repeat cheap.
- **A branch without runs** is `Ok` with "No runs on <branch>".
- **The main folder's branch.** A run with `branchSource` `mainFolder` reads the main folder's path with `job.folderPath("")` and its branch with `job.gitHead(path)`, at each fetch, as in the gitlab module. Without a path it reports `State::None` with "Set the main folder's path on this device.", and without a branch "The main folder is not on a branch.", both without a request.
- **An incomplete configuration,** without a repository or a number, reports `State::None` with "Set the repository and number in the item's settings.", without a request.
- **Errors.** Any other failed reply becomes its state with `statusFromReply()`. The pull request's own reply wins over those of the reviews and the user. A failed `checkRuns` or `combinedStatus` leaves that part out, and `checksState()` reads what came. A failed request for comments or for a sibling run's jobs is not an error. A failed request for the lead run's jobs is.
- **Comments.** The comments endpoint gives the oldest first and has no sort. The summary reads the issue's `comments` count, asks for the last page with `per_page=20`, and, if that page holds fewer than 5 and is not the first, the page before it.
- **Data.** For a pull request: `pullRequest`, `reviews` (GitHub's answer), `user` (`id` and `login`), `checkRuns` (the array) and `combinedStatus` (the object). For an issue: `issue`, and for the summary `comments`, newest first, at most 20. For a run: `branch`, `headSha`, `run` (the lead), `jobs` (the lead's), `siblings` (`[{"run": ..., "jobs": [...]}]`, without `jobs` if that request failed), `older` (runs without jobs), and `pullRequest` when known. The objects are GitHub's answers, unchanged. Page types store the same as their summary, without comments.

### The run group

One push starts one run of each workflow that matches it, and runs of one commit share its `head_sha`. `runGroup()` takes the runs of the answer, newest `created_at` first. The group's commit is the pull request's `head.sha` when `branchSource` is `pullRequest` and some run has that `head_sha`; otherwise it is the newest run's `head_sha`. The group is every run with that `head_sha`. The newest of them leads: its status, `run` and `jobs`. The others of the group are `siblings`; the rest are `older`. A re-run keeps the run's id and raises its `run_attempt`, so a re-run that fails again counts again.

The tests are in `tests/tst_status.cpp`, on answers in `tests/data/`. They are recorded with `gh api` from public repositories, such as `cli/cli`, trimmed by hand, with logins, names and emails replaced. Those that public repositories cannot give, such as a review with changes requested, are written by hand.

## Summary views

`GitHubPullRequestView.cpp`, `GitHubRunView.cpp` and `GitHubIssueView.cpp` each hold one class, a subclass of `GitHubSummary` in `GitHubViewSupport.h`, which is private to the module. The layouts, fields and rules are in [item types](../../../docs/item-types.md#github). `GitHubSummary` does what `GitLabSummary` does:

- **Drawing.** `follow()` draws the item's status now and at each change. With no data it shows "Loading…" or the status's message. With old data it keeps drawing that data through an error. A status without data, with `State::None` and a message, replaces the old data.
- **Actions.** `runAction()` marks the button busy and makes the request. After a success it refreshes the status. A failure shows the reply's message in the action row, and a `403` shows "The token cannot do this. It needs write access to Actions."
- **Open page.** It calls `ItemContext::showPage()`, or opens the external browser if `Shift` is held. It has the same address as the Web link, and both hide without a web address.
- **Dialogs** are on the heap, as in the gitlab module.

The pull request summary offers "Open page" and "Web", and no re-run: a check run can be re-run only by the GitHub App that made it. See decision 0102. Its links to failed checks open the check's `html_url` or `details_url`, or a status's `target_url`, in the external browser.

`GitHubRunView.cpp` keeps more state, as GitLab's pipeline view does:

- **What it shows.** At first the lead run. With more than one run in the group, a row of pills (`run-pill-<id>`), one per run, named by workflow, switches between them. "Previous" and "Next" (`previous-run`, `next-run`) step through all runs of the status, the group and `older`, in time order.
- **Jobs.** One table of the shown run's jobs, in their order. An older run reads its jobs with `runJobs()` when shown.
- **Actions.** "Re-run failed jobs" (`rerun-failed`) when the shown run's conclusion is failed; "Re-run" (`rerun-run`) when it is completed; "Cancel run" (`cancel-run`) while `queued`, `in_progress`, `waiting`, `requested` or `pending`, after a confirmation; "Pull request" (`pull-request`), hidden without one; "Open page" (`open-page`) and "Web" on the run's `html_url`. A job's buttons are `log-<id>` and `rerun-job-<id>`.
- **The refresh.** A `QTimer` named `run-refresh` asks for a refresh every 15 seconds while the shown run is not `completed` and the view is visible.

### Status pills

The pills of runs, jobs and check runs, from `statusText()` and `statusRole()`:

| `status` | `conclusion` | Pill |
|---|---|---|
| `queued`, `waiting`, `requested`, `pending` | | Waiting "Queued" or "Waiting" |
| `in_progress` | | Waiting "Running" |
| `completed` | `success` | Good "Passed" |
| `completed` | `failure`, `timed_out`, `startup_failure` | Bad "Failed", "Timed out", "Failed" |
| `completed` | `cancelled`, `skipped`, `neutral`, `stale` | Neutral "Cancelled", "Skipped", "Neutral", "Stale" |
| `completed` | `action_required` | Waiting "Action required" |

A commit status of the combined status has a `state` instead: `success` Good "Passed", `failure` Bad "Failed", `error` Bad "Error", `pending` Waiting "Pending". `commitStatusPill()` draws it.

Text from a server is plain text in every label. The tests are in `tests/tst_summary_views.cpp`, with a fake context and a fake status source, on the status tests' data.

## Lists

`github.pr.list`, `github.issue.list` and `github.run.list` report status from `GitHubLists.cpp`, as GitLab's lists do. See decisions [0067](../../../docs/decisions/0067-lists-report-status.md) and [0071](../../../docs/decisions/0071-lists-link-out-without-a-preview.md).

- **Sizes.** `listLimit()` and `listPageSize()` read `limit` (100 by default) and `pageSize` (50 by default); an absent value or one below 1 uses the default.
- **Status.** The state is `Ok`, and the summary reads "12 pull requests", "12 issues" or "12 runs", with singular forms, and "100+ pull requests" when the rows reach `limit`. The texts use `%n` plurals, such as "%n pull request(s)" and "%n+ pull request(s)", so the English forms come from `mirante_en.ts`. A list reports no attention and leaves `closed` unset.
- **Data.** `rows` holds GitHub's rows, unchanged: the search's `items`, or the run list's `workflow_runs`. `total` is their number, not GitHub's `total_count`.
- **Errors.** A failed reply becomes its state with `statusFromReply()`, and `data` stays empty.
- **Descriptions.** `listDescription()` gives the line above the table: the preset ("Created by me", "Review requested from me", "Assigned to me", "All open"), the repository, "owner acme" or "all repositories", and the trimmed filter, joined by " · ", such as "Assigned to me · acme/backend". `runListDescription()` gives the source ("All runs", "Started by me", "Of branch main", "Of the default branch", "Of pull request #42"), the workflow and the repository, such as "Of pull request #42 · acme/backend".

The REST API lists the pull requests and issues of one repository only, so the pull request and issue lists use the search API, `searchIssues()`. `searchQuery()` builds the query from these parts, joined by spaces, in this order:

1. `is:pr` or `is:issue`, and `is:open`;
2. the scope: `repo:<repository>`, `user:<owner>`, or nothing for `all`;
3. the preset: `author:@me` (`createdByMe`), `review-requested:@me` (`reviewRequested`), `assignee:@me` (`assignedToMe`), or nothing for `allOpen`, except `involves:@me` for `allOpen` with scope `all`, because all of GitHub is not a useful list;
4. the trimmed `filter`, as typed: extra search qualifiers, such as `label:bug base:main`.

A preset the type does not have counts as its first one: `createdByMe` for pull requests, `assignedToMe` for issues. An absent `scope` is `repository`. An incomplete scope reports `State::None` with "Set the repository in the item's settings." or "Set the owner in the item's settings.", and an unknown scope with "Set the scope in the item's settings.", without a request; `scopeProblem()` gives the message. An empty `repository` uses the project's `github.repository` setting. The repository and the owner lose spaces and slashes at their ends. `searchIssues()` asks for up to `limit` rows, newest update first.

`listWebUrl()` builds the list's page on the site: `<web>/<repository>/pulls?q=<query>` for the repository scope, `<web>/pulls?q=<query>` otherwise, and `issues` for issues. The query is the same as the search's, percent-encoded. It is empty when the scope is incomplete.

`fetchRunListStatus()` serves `github.run.list`, with `workflowRuns()`, filtered by `workflow` when it is set. An absent `source` is `all`.

| Source | Request |
|---|---|
| `all` | the repository's runs |
| `mine` | `currentUser()`, then `actor=<login>`. If the first fails, the list is not asked for. |
| `branch` | `branch=<branch>`; an empty branch first reads the repository's `default_branch` |
| `pullRequest` | `pullRequest()`, then `branch=<head.ref>&event=pull_request` |

Values in the query, the login and the branches, are encoded with `GitHubClient::encode()`, so that "+" and "&" in a branch reach GitHub whole. A run list without a repository reports `State::None` with "Set the repository in the item's settings.", `pullRequest` without a number "Set the pull request number in the item's settings.", and an unknown source "Set the source in the item's settings.", each without a request. A repository answer without `default_branch` is an `Error`, "The repository's default branch could not be read.".

`runListWebUrl()` gives its Web link, `<web>/<repository>/actions`, or `<web>/<repository>/actions/workflows/<file>` with a workflow.

The tests are in `tests/tst_list_query.cpp`, on `pr_search.json`, `issue_search.json` and `run_list.json`, written by hand in the shape of GitHub's API.

## List views

`GitHubPullRequestListView.cpp`, `GitHubIssueListView.cpp` and `GitHubRunListView.cpp` each hold one class, a subclass of `GitHubList` in `GitHubListSupport.h`, as GitLab's lists. The first two go through `NumberedList`. The columns and rules are in [item types](../../../docs/item-types.md#github).

- **Rows.** A search row's repository comes from its `repository_url` (`.../repos/<owner>/<repo>`), with `rowRepository()`. Its key is `owner/repo#42`, because a list can span repositories. A run's repository is the item's.
- **Links:** "Web", "Open" and "Add", and the row menu, as GitLab's. "Add" adds a GitHub PR (summary) named `owner/repo#42`, a GitHub Issue (summary) named `owner/repo#7`, or a GitHub Actions Run. For a run row whose `event` is `pull_request` with an entry in `pull_requests`, the run follows that pull request (`branchSource` `pullRequest`), named `owner/repo#42 CI`; otherwise it follows `head_branch` (`fixed`), named `owner/repo <branch> CI`. The item's `workflow` is kept in the added run, if set.
- **Checks and Reviews.** The PR List fills these columns for the first 25 visible rows, as GitLab's Pipeline and Approvals. Checks asks `pullRequest()` and then `checkRuns()` and `combinedStatus()` of its `head.sha`; Reviews asks `pullRequestReviews()`. The rules of GitLab's view for pages, sorts, receivers and kept answers apply unchanged.
- **Empty lines:** "No pull requests", "No issues", "No runs".

The tests are in `tests/tst_list_views.cpp`.

## Pages

The page types ask the core for a web view with `ItemContext::createWebView`. `GitHubUrls.cpp` builds the address from the web address of the [General Configuration](#settings-screens) of the item's profile, without doubling a slash:

| Type | Address |
|---|---|
| `github.pr.page` | `<web>/<repository>/pull/<number>` |
| `github.issue.page` | `<web>/<repository>/issues/<number>` |

An empty `repository` uses the project's `github.repository` setting. When something is missing, the cell shows a message instead:

- with a base URL that is not an `http` or `https` address: "Set the GitHub base URL in this profile's Modules > GitHub > General Configuration, in the Profiles menu.";
- without a repository or a number: "Set the repository and number in the item's settings."

## Items from a link

Every type implements `fromUrl()`, through `matchUrl()` in `GitHubUrls.cpp`. `readPageAddress()` reads `<web>/<owner>/<repo>/<kind>[/<rest>]` when the address is below the web address of the profile the new item would use, or on `github.com` whatever the profile. A query and a fragment are ignored. A repository's name keeps its case. See [decision 0049](../../../docs/decisions/0049-items-from-a-pasted-link.md).

| Page | Types | Configuration | Name |
|---|---|---|---|
| `pull/<n>[/<tab>]` | `github.pr.summary`, `github.pr.page` | `repository`, `number` | `owner/repo#42` |
| `pull/<n>[/<tab>]` | `github.run` | `repository`, `branchSource` = `pullRequest`, `number` | `owner/repo#42 CI` |
| `issues/<n>` | `github.issue.summary`, `github.issue.page` | `repository`, `number` | `owner/repo#7` |
| `pulls` | `github.pr.list` | `scope` = `repository`, `repository`, `preset` = `allOpen` | `owner/repo PRs` |
| `issues` | `github.issue.list` | `scope` = `repository`, `repository`, `preset` = `allOpen` | `owner/repo issues` |
| `actions` | `github.run`, then `github.run.list` | run: `repository`, `branchSource` = `fixed`, empty `branch`; list: `repository`, `source` = `all` | `owner/repo CI`, `owner/repo runs` |
| `actions/workflows/<file>` | `github.run`, then `github.run.list` | as `actions`, with `workflow` = `<file>` | `owner/repo <file> CI`, `owner/repo <file> runs` |
| `actions/runs/<id>[/...]` | `github.run.list` | `repository`, `source` = `all` | `owner/repo runs` |
| `tree/<branch>` | `github.run` | `repository`, `branchSource` = `fixed`, `branch` | `owner/repo <branch> CI` |

On the web address itself, without an owner:

| Page | Type | Configuration | Name |
|---|---|---|---|
| `pulls` | `github.pr.list` | `scope` = `all`, `preset` = `createdByMe` | My PRs |
| `pulls/assigned` | `github.pr.list` | `scope` = `all`, `preset` = `assignedToMe` | PRs assigned to me |
| `pulls/review-requested` | `github.pr.list` | `scope` = `all`, `preset` = `reviewRequested` | PRs to review |
| `issues` | `github.issue.list` | `scope` = `all`, `preset` = `createdByMe` | My issues |
| `issues/assigned` | `github.issue.list` | `scope` = `all`, `preset` = `assignedToMe` | Issues assigned to me |

Other pages, such as a repository's home page, `compare/...` or `pull/new/...`, are refused. A branch with a slash in `tree/<branch>` is taken whole. The tests are in `tests/tst_urls.cpp` and `tests/tst_from_url.cpp`.

## Quick add

`GitHubQuickAdd.cpp` holds the module's [quick add](../../../docs/modules.md#quick-add) handler and finder. The tests are in `tests/tst_quick_add.cpp`, on `quick_add_*.json`, written by hand in the shape of GitHub's API.

**`GitHubQuickAddHandler`** takes a pull request's address below the web address of the dialog's profile, or on `github.com`. Any other text finishes with nothing at once, without a request. Otherwise it asks for the pull request, and a failed reply finishes with nothing. It proposes:

- **Title:** the pull request's `title`.
- **Items:** `github.pr.summary` named `#42`, with `repository` and `number`; `github.run` named `#42 CI`, with `repository`, `branchSource` = `pullRequest` and `number`.
- **Project:** the project whose setting `repository` is the address's repository, compared without case.
- **Texts:** the title and `head.ref`.

**`GitHubRelatedItemFinder`** reads Jira-style keys in the texts, as GitLab's finder does. Without keys it finishes at once, without a request.

- **With the project setting `repository`:** one `repositoryPullRequests()` with `state=open&sort=updated&direction=desc`, up to 100. A pull request matches a key when its `title` or `head.ref` holds it, without case.
- **Without it:** one `searchIssues()` with `is:pr is:open in:title (<key1> OR <key2> …)` and a `repo:` qualifier for each repository of the profile's General Configuration, up to 100. The title is checked for the key again. Without listed repositories it finds nothing, because a search without `repo:` would cover all of GitHub.

Each match is a `github.pr.summary` named `#42` in the setting's repository, else `owner/repo#42`.

## Configuration

The keys each type stores in the item's configuration. "Required" means the item dialog refuses an empty value.

| Key | Type | Used by | Description |
|---|---|---|---|
| `repository` | text | all | `owner/repo`. Empty uses the project's `github.repository` setting. The form offers the repositories of [General Configuration](#settings-screens) in the item's profile. |
| `number` | integer | PR and issue summary and page; run; run list | The pull request or issue number. Required for the summaries and pages; for the run and the run list it names the linked pull request. |
| `scope` | choice | PR and issue lists | `repository` (one repository, the default), `owner` (all repositories of a user or organisation) or `all`. |
| `owner` | text | PR and issue lists | The user or organisation, used when `scope` is `owner`. |
| `preset` | choice | PR and issue lists | PR List: `createdByMe` (the default), `reviewRequested`, `assignedToMe` or `allOpen`. Issue List: `assignedToMe` (the default), `createdByMe` or `allOpen`. |
| `filter` | text | PR and issue lists | Extra search qualifiers, such as `label:bug base:main`. |
| `branchSource` | choice | run | `fixed` (the `branch` field, the default), `mainFolder` (the current branch of the main folder) or `pullRequest` (the head branch of pull request `number`). |
| `branch` | text | run; run list | The branch, used when `branchSource` or `source` says so. Empty means the repository's default branch, and the placeholder says so. |
| `workflow` | text | run; run list | A workflow file name, such as `ci.yml`. Empty means all workflows. |
| `source` | choice | run list | `all` (the default), `mine` (started by the current user), `branch` (of the `branch` field) or `pullRequest` (of pull request `number`). |
| `limit` | integer | lists | How many rows it fetches. 100 by default, also when absent or below 1. |
| `pageSize` | integer | lists | How many rows the view shows at a time. 50 by default, also when absent or below 1. |
| `showChecks`, `showReviews`, `showPeople`, `showLabels` | boolean | PR summary | Its sections, on when absent. |
| `showChecks`, `showReviews` | boolean | PR list | Its Checks and Reviews columns, on when absent. |
| `showDetails`, `showLabels`, `showComments` | boolean | issue summary | Its sections, on when absent. |

The keys of each type, in the order the form shows them:

- `github.pr.summary`: `repository`, `number`, `showChecks`, `showReviews`, `showPeople`, `showLabels`.
- `github.pr.page`, `github.issue.page`: `repository`, `number`.
- `github.issue.summary`: `repository`, `number`, `showDetails`, `showLabels`, `showComments`.
- `github.pr.list`: `scope`, `repository`, `owner`, `preset`, `filter`, `limit`, `pageSize`, `showChecks`, `showReviews`.
- `github.issue.list`: `scope`, `repository`, `owner`, `preset`, `filter`, `limit`, `pageSize`.
- `github.run`: `repository`, `branchSource`, `branch`, `number`, `workflow`.
- `github.run.list`: `repository`, `source`, `branch`, `number`, `workflow`, `limit`, `pageSize`.

## Settings

The project setting `repository`, "GitHub repository", a text field registered with `addProjectSetting()`. It is kept in the project's `moduleSettings.github.repository`, and read as `github.repository`: the default repository for items whose `repository` is empty.

## Settings screens

The module calls itself "GitHub", so each profile's Modules submenu shows Profiles > <profile> > Modules > GitHub. It has one [settings screen](../../../docs/modules.md#settings-screens), in `GitHubSettings.cpp`:

| Id | Name | Key | Type | Description |
|---|---|---|---|---|
| `general` | General Configuration | `baseUrl` | URL | The GitHub web address, such as `https://github.example.com`. `https://github.com` by default, and when empty. |
| | | `token` | secret | A personal access token. Never stored with the values: the core keeps it in the keychain, under `<workspace id>/<profile id>/github/token`. |
| | | `repositories` | text list | Repositories the user works on, `owner/repo`, one per line. |

Each profile has its own values, stored in its entry of `workspace.mirante` under `moduleSettings.github.general`, and synced. They hold no token.

The `repository` field of every type offers the repositories of the profile the item uses, in an editable drop-down list, through `fieldsForProfile(profileId)` and `withRepositories()`. Each is trimmed, slashes at its start and end are removed, and empty or repeated ones are left out, by `repositoryNames()`. Any other repository can still be typed.
