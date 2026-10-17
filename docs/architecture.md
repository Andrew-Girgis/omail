# Omail 0.1 contracts

## Processes and storage

`omail open` is the Qt Quick GUI. `omail service` owns background sync, classification, and scheduling; a per-user lock prevents two daemons. The GUI starts a daemon if needed. The installed systemd user service starts at login and survives window closure. The CLI sets Qt's offscreen platform before application construction.

SQLite WAL stores cached message bodies, normalized events, local drafts, classification metadata, dismissals, and AI conversation history under `~/.local/share/omail/omail.sqlite3`. The directory is private (0700); files are 0600. The cache is not application-encrypted: disk encryption and your login protect it. OAuth tokens and Google's native client secret use Secret Service, never SQLite. `OMAIL_DATA_DIR` and `OMAIL_CONFIG_DIR` isolate tests and development. Never point a development fixture at a real account store.

`Config` validates settings and atomically writes a locked JSON file. `Store` owns SQL and durable transitions. `Provider` owns Graph/Gmail/Calendar protocol differences. `OAuth` owns browser sign-in and PKCE. `Ai` retrieves profile-scoped evidence, verifies a downloaded local model, and requests schema-constrained results. `Backend` coordinates these for QML. `MailDocument` renders formatted mail without scripts or resource loading. The xdg portal handles file selection.

## Accounts and views

One whole account belongs to one profile. Inbox is an ordinary provider folder; Priority overlaps mail folders and stores metadata only in Omail. Action needed OR important means priority. A dismissal records the newest received timestamp; a later important incoming message reopens the thread. Reading and replying do not dismiss it. Manual corrections are stored per message and win against an in-flight classifier response. They never create rules or training examples.

Rows show conversations within the current view. Opening a conversation reads its cached messages across folders. Unknown Microsoft custom folders appear in Archive in 0.1. Tags are local per-message topics and appear as views; automatic classification does not mutate provider labels/folders. Explicit read, flag, move, calendar, and send actions synchronize with the provider.

## Cache and sync

Sync reconciles paginated snapshots rather than provider push/delta streams. The default cache window is 90 days. Inbox messages older than a day at the first sync are cached without automatic classification; new incoming messages are classified afterward. Unresolved Priority and flagged messages remain available beyond the window. Explicit older searches retain fetched messages for another cache window. Mail outside the window may not reflect later provider changes until fetched again. Sync is serial within each account and guarded across processes. Calendar sync covers the previous 90 days and next 365 days; navigation outside that interval shows only already cached data.

## Scheduled delivery

The GUI requires explicit approval before sending or queuing. Draft text and attachment bytes are stored together. Queued messages are claimed transactionally by the daemon. The scheduler must observe a continuous clock interval and online connectivity at the due time. Startup with an overdue message, an offline due time, a suspend signal, or a pause over ten seconds changes it to Held. Held messages never retry automatically. In-flight sends interrupted by process termination are also held. A provider error after a send attempt may be ambiguous: inspect Sent before reapproving.

The provider's acceptance is not proof of delivery to the recipient. The app does not promise exactly-once delivery across provider/network failures. It prevents automatic retries and concurrent claims. Editing a scheduled draft cancels its schedule immediately; it must be approved again.

## AI and agents

Ollama is the only enabled AI adapter in 0.1. Endpoint validation permits loopback HTTP; model metadata must contain local model information and no remote host/model. No external AI permissions are granted by default, and there is no cloud adapter toggle that silently bypasses profile consent. Future adapters must enforce consent at the retrieval boundary for every profile.

The AI has no action tools. Mail and calendar text are untrusted evidence. Replies are suggestions in the editor. Chat retrieves from one profile and validates source IDs against the supplied evidence; citations open mail threads or events. Citations establish provenance, not factual correctness. The cache and bounded model context may omit relevant information. Attachment text is excluded from automatic classification and chat. Explicit attachment analysis supports plain text and text-based PDFs (first 30 pages, bounded text); it cannot OCR images.

The v1 CLI intentionally exposes no mail, calendar, send, delete, or approval mutators. Config changes are supported. This is an API contract, not an OS sandbox: an agent running as your Unix user can independently access your files or automate the GUI, so do not treat the CLI as a security boundary against such an agent.

## Current limits

- Personal Outlook, Microsoft 365 and Google accounts need user-owned OAuth registrations and possibly administrator consent. Live authentication is not verified until the user completes setup.
- Outgoing attachments total at most 2.8 MB to stay within direct upload limits. Generic IMAP/SMTP, CalDAV, aliases/send-as, delegated/shared mailboxes, and provider-native scheduled send are not implemented.
- Mail composition is plain text. Forwarding includes the cached text; reattach files explicitly. Formatted display blocks remote images and all automatic external resources.
- Calendars support create/edit, daily/weekly/monthly recurrence on creation, individual occurrence edits, and invitation responses. Complex recurrence editing, free/busy lookup, room/resource scheduling, and offline calendar mutation queues are not implemented.
- Calendar conflicts can overlap visually in Week; Agenda shows every cached event separately.
- English, Danish, and French UI catalogs are bundled; other Omarchy languages fall back to English.

## Verification on this desktop

The Qt Test suite reports 23 passed, 0 failed, and 1 optional local-model check skipped in its default offline run. It exercises provider pagination and token refresh with synthetic HTTP responses, reply recipients and attachments, Priority transitions, scheduling holds and concurrency, profile isolation, read-only CLI behavior, live theme/language changes, and the real QML editor at three window widths. UI regressions also cover floating field labels at all supported font extremes, empty account dropdowns, direct account-settings navigation, pane presets with custom config values, and actual sidebar pointer dragging with persisted and externally edited order.

The optional check was also run separately against the installed local `qwen3.5:4b` Ollama model and passed classification, summarization, and an answer with valid source citations. It used only made-up mail. Live Microsoft/Google authentication, synchronization, meeting responses, and actual delivery remain unverified until account registration and user sign-in are complete.

Run `bin/test` for offline checks. To explicitly exercise the downloaded local model with synthetic data, run `OMAIL_TEST_OLLAMA=1 bin/test actualLocalModelClassifiesSummarizesAndCitesSyntheticMail`.
