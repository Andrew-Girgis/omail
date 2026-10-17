# Connect accounts to Omail

Omail 0.1 uses your own OAuth registrations. Email passwords never go into Omail. Microsoft and Google tokens are stored in the desktop Secret Service keyring; public application IDs are stored in `~/.config/omail/settings.json`.

Run `omail-setup-accounts` for a guided walkthrough, or follow these steps. Register applications under an account you control. A work or university administrator may need to approve consent. Omail cannot bypass that policy.

## Microsoft: Outlook.com and Microsoft 365

1. Open [Microsoft Entra app registrations](https://entra.microsoft.com/#view/Microsoft_AAD_RegisteredApps/ApplicationsListBlade). Select a directory where you can register applications. If your account has no suitable directory or permission, arrange one with your administrator; a personal Outlook mailbox alone is not an app registration.
2. Choose **New registration**, name it **Omail**, and select **Accounts in any organizational directory and personal Microsoft accounts**. This enables your personal Outlook account and organizational Microsoft 365 accounts to use the same client ID.
3. Register. Copy **Application (client) ID** from Overview.
4. Under **Authentication**, add a **Mobile and desktop applications** platform with redirect URI **http://localhost**. Omail uses a random loopback port at runtime. Use the native/public client platform, not a Web platform. No client secret is needed for Microsoft's authorization-code + PKCE flow.
5. Under **API permissions**, add Microsoft Graph **Delegated permissions**: `User.Read`, `Mail.ReadWrite`, `Mail.Send`, `Calendars.ReadWrite`, `offline_access`, `openid`, `profile`. Omail requests these scopes during browser consent; use delegated permissions, not application permissions.
6. In Omail → Settings → Accounts, enter the client ID, choose Personal or Work, and click **Connect Microsoft**. Sign into your personal Outlook account. Repeat with the Waterloo Microsoft 365 account under the Work profile. If consent is blocked, ask the organization to approve this application and its delegated permissions.

The redirect listener binds only to loopback, validates a random state, and exchanges a PKCE authorization code. Never paste a password, authorization code, refresh token, or client secret into an agent conversation.

Reference: [Register an application](https://learn.microsoft.com/en-us/entra/identity-platform/quickstart-register-app), [authorization code and PKCE](https://learn.microsoft.com/en-us/entra/identity-platform/v2-oauth2-auth-code-flow).

## Google Workspace / Gmail

1. Open [Google Cloud Console](https://console.cloud.google.com/projectcreate) and create a project called Omail, or choose a project you control.
2. Enable [Gmail API](https://console.cloud.google.com/apis/library/gmail.googleapis.com) and [Google Calendar API](https://console.cloud.google.com/apis/library/calendar-json.googleapis.com) in that project.
3. Open **Google Auth Platform**. Configure Branding (app name Omail, support email, developer contact), Audience, and Data Access. If you are testing an External application, add your own Google account under Test users. Internal audience is available only for eligible Workspace organizations and restricts who can sign in.
4. Request these scopes: `openid`, `email`, `https://www.googleapis.com/auth/gmail.modify`, `https://www.googleapis.com/auth/gmail.send`, `https://www.googleapis.com/auth/calendar.events`, `https://www.googleapis.com/auth/calendar.calendarlist.readonly`.
5. Under **Clients**, create an OAuth client with application type **Desktop app** and name Omail. Download its JSON file. This must contain an `installed` object, not `web`.
6. In Omail → Settings → Accounts, choose **Import Google OAuth JSON**. Omail writes the public client ID to settings and the client secret to the desktop keyring. It never copies the secret into its database or config. Secure or remove the downloaded credential file after import if you no longer need it.
7. Select Work, click **Connect Google**, and complete browser sign-in and consent. If Workspace blocks the app, its administrator must approve OAuth access.

An External application in Testing can receive refresh tokens that expire after seven days when requesting these mail/calendar scopes. Reconnecting is expected during testing. Moving to a public distribution requires Google's restricted-scope verification process; this personal registration guide does not complete that process.

Reference: [Desktop OAuth](https://developers.google.com/identity/protocols/oauth2/native-app), [refresh token expiration](https://developers.google.com/identity/protocols/oauth2#expiration), [Gmail scopes](https://developers.google.com/workspace/gmail/api/auth/scopes).

## Verify your connection

- Click Sync. Settings shows each account's last successful mail sync or an error.
- Check Inbox, Sent, and a known event in Calendar against your provider.
- Test a draft and schedule only with a recipient you choose. Omail's automated tests never send real mail.
- Unlock your desktop keyring if credential access reports an error.
- `omail accounts list --profile all --json` shows connection status, never credentials.

Custom-domain addresses hosted by Microsoft 365 or Google Workspace use the matching provider. Generic IMAP/SMTP and CalDAV accounts are not implemented in 0.1; choose hosting before adding those adapters.
