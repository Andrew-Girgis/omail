# Bug workflow

Each issue moves through five stages. The label says where it is; exactly one stage label is on an open bug at a time.

| Stage | Label on entry | Done when |
| --- | --- | --- |
| Report | `needs-repro` | Issue uses the Bug report form |
| Repro | `repro-confirmed` | Repro command and before screenshots are on the issue |
| Fix | `needs-qa` | PR is open with red/green proof |
| QA | `qa-passed` or `qa-failed` | QA verdict comment is on the PR |
| Merge | closed | The maintainer merged; agents never merge |

Screenshots go through `bin/evidence ISSUE FILE...`, which pushes them to the `qa-evidence` branch and prints Markdown image links. Name files `before-WIDTH.png` and `after-WIDTH.png`, adding the fixture or view when an issue has several (`before-statement-1000.png`).

## Report

Fill every field of the Bug report form. The **Agent repro** field holds commands, not prose. When no fixture shows the bug yet, say so: the repro stage writes one.

## Repro

1. On a clean `main`, build and run the issue's repro at each relevant width.
2. When the bug needs a new email pattern, add a fixture in `tests/fixtures/mail/` (synthetic content only, see AGENTS.md).
3. Look at every screenshot. Done when the bug is visible in it, or, for logic bugs, a Qt test in `tests/omail_tests.cpp` fails on it.
4. Upload with `bin/evidence`, comment the exact commands and images on the issue, and swap `needs-repro` for `repro-confirmed`.

When the bug does not appear, comment what you ran and what you saw, and leave `needs-repro`.

## Fix

1. Branch `fix/ISSUE-short-slug` from `main`.
2. Commit the regression test or fixture first. Run it: it must go **red**.
3. Fix with the smallest change the issue needs. Run it again: **green**, and `bin/test` reports 0 failed.
4. Take after screenshots with the same repro, at the same widths. Run the other fixtures at 1440, 1000, and 420 and look for regressions.
5. Open a PR from `.github/pull_request_template.md`, `Fixes #ISSUE`, every section filled. Swap the label to `needs-qa`.

## QA

QA runs in a fresh context with the `qa` agent (`.claude/agents/qa.md`), or Codex review guided by AGENTS.md. QA trusts nothing in the PR text: it rebuilds and re-runs everything itself. The verdict is `qa-passed` only when every check in the agent's list holds.
