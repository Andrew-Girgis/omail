![Omail with synthetic messages and the default accent](docs/screenshot.png)

# Omail

A native Omarchy mail and calendar client with a local AI assistant. C++ and Qt Quick, live Omarchy theme and language, one binary, a JSON CLI, and a background login service.

Open **Omail** from the app launcher (`Super + Space`) or run `omail open`. `omail open --demo` opens an isolated preview with made-up mail; it never touches connected accounts.

## Connect your accounts

Run `omail-setup-accounts`, or open the [registration guide](docs/accounts.md). Outlook.com, Microsoft 365, and Google Workspace/Gmail use your own OAuth app registrations. Work/university consent policies still apply. Account passwords are never stored; OAuth credentials use the desktop keyring.

The initial release is intended for personal testing before relying on it as your sole client. Live provider sign-in and delivery require your registrations and account consent. Automated tests use synthetic data and never send real mail.

## Workflows

- Personal and Work profiles group whole accounts. Inbox stays familiar; Priority overlaps it. Conversations stay in Priority until you mark them done, and an important new reply can reopen them.
- Compose plain-text mail, reply, forward, attach files (2.8 MB total), save local drafts, and approve sends. Schedule mail locally; missed send times always become Held for review. Check Sent before retrying an uncertain send.
- View Week, Month, or Agenda; create/edit events and accept/decline invitations. Select All profiles to combine calendars.
- Ollama classifies new mail. Summaries and suggested replies are on demand. Ask Omail searches one profile and returns source links. Attachment analysis requires an explicit request.
- The layout changes from three panes to two, then one. Theme colours, rounding, and language follow Omarchy. Settings changes are live.

Local AI uses `qwen3.5:4b` by default. Install with `ollama pull qwen3.5:4b`. Select another downloaded model in Settings or via the CLI. Cloud AI adapters are not enabled in this version.

## Keyboard

| Shortcut | Action |
| --- | --- |
| Ctrl+N | Compose |
| Ctrl+K | Search mail |
| Ctrl+R | Sync |
| Alt+1 / Alt+2 / Alt+3 | Mail / Calendar / AI |
| Space | Open first conversation or mark selected conversation read |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo local Priority/topic changes, or text edits while editing |
| ? | Show shortcuts |
| Q | Close window, with an unsaved-work guard |

Letter shortcuts are disabled in text fields. The main navigation shortcuts, fonts, density, and pane widths are configurable.

## CLI and agents

```sh
omail help --json
omail mail list --profile personal --view priority --json
omail mail search --profile work --query 'review' --json
omail calendar events --profile work --from 2026-10-16 --to 2026-10-23 --json
omail config set fontSize 16 --json
omail config schema --json
omail skill install --json
```

Mail and calendar commands are read-only. Config commands may change settings. The bundled [agent skill](agent/SKILL.md) installs into `~/.codex/skills/omail`; pass `--path` to choose another skill directory. CLI errors go to stderr and exit 1. Success exits 0. No display is required.

## Build and install

```sh
bin/build
bin/test
bin/install
systemctl --user daemon-reload
systemctl --user enable --now omail.service
```

Requires Qt 6, qmake6, a C++17 compiler, make, pkgconf, and libsecret. Packaging uses `makepkg -fsi`. The installer creates a local source tarball; public releases are a separate workflow.

Runtime config is `~/.config/omail/settings.json`; the private cache is `~/.local/share/omail`. Override with `OMAIL_CONFIG_DIR` and `OMAIL_DATA_DIR` for tests. Back up the entire data directory with the service and GUI stopped. The cache is private but not application-encrypted.

`systemctl --user status omail.service` checks background sync. Stop it with `systemctl --user stop omail.service`; scheduled messages due while stopped will be held at the next start. Window closure does not stop the daemon.

See [architecture, privacy boundaries, and release limits](docs/architecture.md) for cache rules, scheduling semantics, provider limitations, and module contracts. Theme and translation watching adapt Omacut templates under the MIT license; see [LICENSE](LICENSE).
