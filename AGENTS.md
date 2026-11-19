# Omail

Native Omarchy mail and calendar client: C++17 and Qt Quick 6, qmake, one binary. `docs/architecture.md` describes the design.

- Build: `bin/build`. Test: `bin/test` (headless Qt Test; prints `Totals:`).
- Format C++ with the repo's `.clang-format`, on changed lines only: `git clang-format`.

## Privacy

The repository is public. Every screenshot, fixture, and issue uses `--demo` and synthetic mail from `tests/fixtures/mail/`. When a bug comes from real mail, write a fixture that copies the pattern with made-up content, and work from that.

## Repeatable runs

A **repro** reaches the same state on every run:

```sh
TZ=UTC OMAIL_NOW=2026-01-15T09:00:00Z build/omail --demo --fixtures tests/fixtures/mail \
  --account demo --thread fixture-NAME --width 420 --height 1100 --screenshot out.png
```

- `OMAIL_NOW` pins the app clock (`src/clock.h`); read time through `Clock`, never the system clock, so pinned runs stay identical. `TZ=UTC` pins "today".
- `--fixtures DIR` loads `NAME.json` + its HTML body into the demo inbox as thread `fixture-NAME`.
- Check at 1440 (three panes), 1000 (two), and 420 (one).
- Pinned demo runs produce byte-identical PNGs; a hash change means the pixels changed.

## Bugs and fixes

Reporting, reproducing, fixing, or QA-ing an issue follows `docs/bug-workflow.md`: read it before starting any of those.

## Review guidelines

- A fix PR shows **red then green**: its regression test or fixture fails on `main` and passes on the branch, with before/after screenshots from the issue's repro.
- Flag any wall-clock read outside `src/clock.h` (OAuth token expiry excepted), any real mail or personal data in fixtures or screenshots, and any change that widens what a remote email can load or run (scripts, remote images, file URLs).
- Flag scope creep: a fix PR changes only what its issue needs.
