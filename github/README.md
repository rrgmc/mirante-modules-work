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

A path in the web address is kept, so `http://example.org:8080/github` gives `http://example.org:8080/github/api/v3/`. A query or fragment in it is not. So `ApiClient` paths are `repos/...`, `search/issues` and `user`. Pages, pasted links and Web links use the web address. A web address that is not an `http` or `https` address gives neither. The tests are in `tests/tst_settings.cpp`.

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
- **Summaries.** A pull request reads like "Open · 2 approvals · checks failed": "Open", "Draft", "Merged" (when `merged` or `merged_at` is set) or "Closed"; the number of approvals, or "changes requested" instead when a latest review requests changes; the checks as "checks passed", "checks failed" or "checks running", left out for none; and "has conflicts" while open. Only a `mergeable` of `false` means conflicts: `null` means GitHub has not computed it yet. An issue reads like "Open · assigned to ana", with the assignees' logins. A run group reads like "Failed · main · 1 of 3 runs failed", "Running · main · 3 runs" or "Passed · main · 3 runs". The group's word is "Failed" when a run of the group failed; else "Running" while one is `in_progress`, or "Queued" or "Waiting" for the newest that has not completed; else "Passed" when every run's conclusion is `success`, `skipped` or `neutral`, or else the word of the newest that is not, such as "Cancelled". A single run leaves out the count, names its workflow, and takes its own word, the same as its [status pill](#status-pills): "Passed · main · CI", "Timed out · main · CI". Counts use `%n` plural forms.
- **An empty branch is the default branch.** A run with an empty `branch` reads `default_branch` from `GET repos/<repository>`. Its ETag makes the repeat cheap.
- **A branch without runs** is `Ok` with "No runs on <branch>".
- **The main folder's branch.** A run with `branchSource` `mainFolder` reads the main folder's path with `job.folderPath("")` and its branch with `job.gitHead(path)`, at each fetch, as in the gitlab module, and then follows that branch as a fixed one. Without a path it reports `State::None` with "Set the main folder's path on this device.", and without a branch "The main folder is not on a branch.", both without a request.
- **An incomplete configuration** reports `State::None` without a request: "Set the repository and number in the item's settings." for a pull request or an issue without a repository or a number. A run without a repository reports "Set the repository in the item's settings.", and a run with `branchSource` `pullRequest` without a number "Set the pull request number in the item's settings.", as the gitlab module words them.
- **Errors.** Any other failed reply becomes its state with `statusFromReply()`. The pull request's own reply wins over those of the reviews and the user. A failed reply of the reviews or the user is an error too, and the checks are then not asked for. A failed `checkRuns` or `combinedStatus` leaves that part out, and `checksState()` reads what came. For a run, a failed reply of the pull request, the repository, the runs or the lead run's jobs is an error. A failed request for comments or for a sibling run's jobs is not.
- **Comments.** The comments endpoint gives the oldest first and has no sort. The summary reads the issue's `comments` count. With none, it asks for nothing. Else it asks for the last page with `per_page=20`. If that page holds fewer than 5 and is not the first, it then asks for the page before it. A failed last page leaves the comments out. A failed page before it keeps the last page's comments.
- **Data.** The objects are GitHub's answers, unchanged. Page types store the same as their summary, without comments.

| Types | `data` |
|---|---|
| `github.pr.summary`, `github.pr.page` | `pullRequest` (the object); `reviews` (GitHub's array of all reviews, oldest first, which `latestReviews()` reads); `user` (`{"id", "login"}` of "me"); `checkRuns` (the `check_runs` array, empty when that request failed or the head has no `sha`); `combinedStatus` (the object, empty when that request failed) |
| `github.issue.summary` | `issue`; `comments`, newest first, at most 20, absent when the issue has no comments or they could not be read. A view tells the two apart by `issue.comments`. |
| `github.issue.page` | `issue` |
| `github.run`, with runs | `branch` (the branch followed); `headSha` (the group's commit); `run` (the lead); `jobs` (the lead's `jobs` array); `siblings` (`[{"run": {...}, "jobs": [...]}]`, newest first, without `jobs` when that request failed, empty for a group of one); `older` (the runs of other commits, newest first, without jobs); `pullRequest` (the object) for `branchSource` `pullRequest` |
| `github.run`, without runs | `branch`, and `pullRequest` for `branchSource` `pullRequest` |

### The run group

One push starts one run of each workflow that matches it, and runs of one commit share its `head_sha`. `fetchRunStatus()` asks for up to 20 runs with `branch=<branch>`, of the item's `workflow` when it names one. `runGroup()` takes the runs of the answer, newest `created_at` first. Runs created in the same second, as one push makes them, go by id, the highest first. A date that cannot be read counts as the oldest. The group's commit is the pull request's `head.sha` when `branchSource` is `pullRequest` and some run has that `head_sha`; otherwise it is the newest run's `head_sha`. The group is every run with that `head_sha`. The newest of them leads: its status, `run` and `jobs`. The others of the group are `siblings`; the rest are `older`. The jobs of the lead and of each sibling are asked for at once. "Run failed" counts for the lead and for each sibling whose conclusion failed, never for `older`. Its key is `run-failed:<id>:<run_attempt>`. A re-run keeps the run's id and raises its `run_attempt`, so a re-run that fails again counts again.

The tests are in `tests/tst_status.cpp`, on answers in `tests/data/`. They are recorded with `gh api` from public repositories and trimmed by hand to what the code and the views read. Logins, names, emails and branch names are replaced by placeholders, and the repository is `acme/widgets`. "Me" is `ana-reviewer`, id 1001.

| File | Source |
|---|---|
| `pull_request.json` | `cli/cli` pull request 14845, as number 42 |
| `pull_request_reviews.json` | the reviews of `cli/cli` pull request 14858: two comments and an approval |
| `check_runs.json`, `combined_status_empty.json` | the head commit of `cli/cli` pull request 14845: eleven check runs that passed, and a combined status without statuses |
| `combined_status.json` | the head commit of a `kubernetes/kubernetes` pull request, which uses commit statuses, with three kept: success, pending and error |
| `issue.json`, `issue_comments_page1.json`, `issue_comments_page2.json` | `cli/cli` issue 6235, as number 7, with its 21 comments, their bodies replaced |
| `workflow_runs.json`, `run_jobs.json`, `run_jobs_lint.json`, `run_jobs_codeql.json` | the runs of the branch of `cli/cli` pull request 14845, three of its head commit and five of the commit before, and the jobs of the three |
| `repository.json` | `cli/cli`, whose default branch is `trunk` |
| `user.json` | written by hand, in the shape of `GET user` |
| `pull_request_reviews_changes.json` | written by hand: an approval replaced by changes requested, an approval kept after a comment, a dismissed review and a pending one, out of order |
| `workflow_runs_failed.json`, `run_jobs_failed.json` | written by hand: a failed run on its second attempt, a timed-out sibling, a passed sibling and an older failed run, and the jobs of the first |

## Summary views

`GitHubPullRequestView.cpp`, `GitHubRunView.cpp` and `GitHubIssueView.cpp` each hold one class, a subclass of `GitHubSummary` in `GitHubViewSupport.h`, which is private to the module. The layouts, fields and rules are in [item types](../../../docs/item-types.md#github). `GitHubSummary` does what `GitLabSummary` does:

- **Drawing.** `follow()` draws the item's status now and at each change. With no data it shows "Loading…" or the status's message. With old data it keeps drawing that data through an error. A status without data, with `State::None` and a message, replaces the old data.
- **Actions.** `runAction()` marks the button busy and makes the request. After a success it refreshes the status. A failure shows the reply's message in the action row, and a `403` shows "The token cannot do this. It needs write access to Actions."
- **Open page.** It calls `ItemContext::showPage()`, or opens the external browser if `Shift` is held. It has the same address as the Web link, and both hide without a web address.
- **Dialogs** are on the heap, as in the gitlab module.

The pull request summary offers "Open page" and "Web", and no re-run: a check run can be re-run only by the GitHub App that made it. See decision 0102. Its sections:

- **Checks.** The pill of `checksState()`, then each check run and commit status that did not pass, in the order of the data, with its name, its pill and a "Details" link: the check run's `html_url`, else its `details_url` (`check-details-<id>`), or the status's `target_url` (`status-details-<id>`), in the external browser. A check run passes with the conclusion `success`, `skipped` or `neutral`, and a status with the state `success`. When every one passed, the count follows, such as "11 checks passed".
- **Reviews.** Each reviewer's latest review from `latestReviews()`, with `reviewPill()`; then the requested reviewers and teams who have no latest review, as "Waiting". "No reviews" when there are none of either.
- **People.** "Author: <login>" and "Assignees: <logins>", or "none".
- **Labels.** `labelChips()`, or "No labels".

The issue summary shows the title and `statePill()`, Details (the assignees and the milestone's title), Labels and Comments: the first five of `comments`, newest first, each with a byline of the login and the local date and time. Without `comments` in the data, `issue.comments` of 0 reads "No comments", and any other count "The comments could not be read."

`GitHubRunView.cpp` keeps more state, as GitLab's pipeline view does:

- **What it shows.** At first the lead run: its workflow name and run number as the title, such as "CI #812"; its pill; the branch; the duration from `run_started_at` to `updated_at` once completed; and when it started. With more than one run in the group, a row of pills (`run-pill-<id>`), one per run, named by workflow and coloured by status, switches between them. The shown run's pill is bold. "Previous" and "Next" (`previous-run`, `next-run`) step through all runs of the status, the group and `older`, sorted as `runGroup()` sorts: newest `created_at` first, then the highest id. "Previous" goes back in time. A run the user chose stays shown through a new status while it is still there; otherwise the view follows the lead.
- **Jobs.** One table of the shown run's jobs, in the order of the answer, each with its pill, its name, its duration from `started_at` to `completed_at`, "Log" (`log-<id>`, its `html_url` in the external browser) when it has a `started_at`, and "Re-run" (`rerun-job-<id>`) when it is completed. A run without jobs in the data, an older run or a sibling whose jobs could not be read, reads them with `runJobs()` when shown, as GitLab's pipeline view does: "Loading…", or what was read before, until the answer comes, and "The jobs could not be read." if it fails. Only the answer for the run shown draws.
- **Actions.** "Re-run failed jobs" (`rerun-failed`) when the shown run is completed with a failed conclusion; "Re-run" (`rerun-run`) when it is completed; "Cancel run" (`cancel-run`) while `queued`, `in_progress`, `waiting`, `requested` or `pending`, after a confirmation in a `QMessageBox` on the heap, "Cancel CI #812?"; "Pull request" (`pull-request`), in the external browser, hidden without one; "Open page" (`open-page`) and "Web" on the run's `html_url`. For `branchSource` `pullRequest` the pull request is `pullRequest.html_url` of the data, else the address built from the configuration. For a fixed or main-folder branch it is `<web>/<repository>/pull/<number>` of the shown run's first entry in `pull_requests`, which holds no web address. Showing another run clears the action row's error.
- **Without runs** the title is the status's summary, "No runs on <branch>", and the head, the jobs, every action and the Web link hide.
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

The tests are in `tests/tst_urls.cpp`.

## Items from a link

Every type implements `fromUrl()`, through `matchUrl()` in `GitHubUrls.cpp`. `readPageAddress()` reads `<web>/<owner>/<repo>/<kind>[/<rest>]` when the address is below the web address of the profile the new item would use, or on `github.com` or `www.github.com` whatever the profile. Of several web addresses, the longest that the address is below wins, so a server at a path does not take the path for an owner. Any other server is refused, because nothing in a GitHub address tells it apart from another site. A query and a fragment are ignored. A repository's name keeps its case. The match's `baseUrl` is the web address it was read with. See [decision 0049](../../../docs/decisions/0049-items-from-a-pasted-link.md).

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

These items have no `repository`. Their names are translated; the names of a repository's items are not, as in the gitlab module.

Details:

- The item dialog lists types by name, so "GitHub Actions Run" comes before "GitHub Actions Run List".
- `github.run` follows a branch, not one run, so it does not take `actions/runs/<id>`. The run list does not keep the run's id.
- `actions/workflows/<file>` takes the file alone. A longer path, such as the workflow's `badge.svg`, is refused.
- A branch with a slash in `tree/<branch>` is taken whole. The path is decoded once, so `tree/fix%2Fa` gives `fix/a`.
- A number must be positive, so `pull/0` and `issues/new` are refused.
- Other pages, such as a repository's home page, an owner's page, `blob/...`, `commit/...`, `compare/...`, `pull/new/...`, `issues/review-requested` or `pulls/mentioned`, are refused.

The tests are in `tests/tst_urls.cpp` and `tests/tst_from_url.cpp`.

## Quick add

`GitHubQuickAdd.cpp` holds the module's [quick add](../../../docs/modules.md#quick-add) handler and finder. The tests are in `tests/tst_quick_add.cpp`, on `quick_add_pull_request.json`, `quick_add_open.json` and `quick_add_search.json`, which were written by hand in the shape of GitHub's API.

**`GitHubQuickAddHandler`** takes a pull request's address, `<web>/<owner>/<repo>/pull/<number>[/<tab>]`, read by `readPageAddress()` as [items from a link](#items-from-a-link) does. The address must be below the web address of the General Configuration screen in the dialog's profile, because the request goes to that server. `readPageAddress()` also reads `github.com`, so such an address proposes nothing for a profile on a GitHub Enterprise Server. Any other text, an address on another host, or no server finishes with nothing at once, without a request. Otherwise it asks for the pull request with `pullRequest()`, and a failed reply finishes with nothing. It proposes:

- **Title:** the pull request's `title`, trimmed.
- **Items:** `github.pr.summary` named `#42`, with `repository` and `number`; `github.run` named `#42 CI`, with `repository`, `branchSource` = `pullRequest` and `number`.
- **Project:** the project whose setting `repository` is the address's repository, compared without case. The repository keeps the address's case.
- **Texts:** the title and `head.ref`.

**`GitHubRelatedItemFinder`** reads Jira-style keys in the texts, as GitLab's finder does: `[A-Za-z][A-Za-z0-9_]+-\d+`, in upper case, at most five distinct ones in the order they appear. Without keys it finishes at once, without a request.

- **With the project setting `repository`:** one `repositoryPullRequests()` with `state=open&sort=updated&direction=desc`, up to 100. A pull request matches a key when its `title` or `head.ref` holds it, without case.
- **Without it:** one `searchIssues()` with `is:pr is:open in:title (<key1> OR <key2> …)` and a `repo:` qualifier for each repository of the profile's General Configuration, up to 100. GitHub's search also matches the key's words apart, so the title is checked for the key again. The repository of a row comes from its `repository_url`. Without listed repositories it finds nothing, without a request, because a search without `repo:` would cover all of GitHub.

Each match is a `github.pr.summary` with `repository` and `number`. It is named `#42` in the setting's repository, else `owner/repo#42`. A failed request finds nothing. The finder finishes once, after the reply, with each pull request once, in the order of the keys.

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

The `repository` field of every type offers the repositories of the profile the item uses, in an editable drop-down list, through `fieldsForProfile(profileId)` and `withRepositories()`. Each is trimmed, slashes at its start and end are removed, and empty or repeated ones are left out, by `repositoryNames()`. Any other repository can still be typed. The tests are in `tests/tst_settings.cpp`.
