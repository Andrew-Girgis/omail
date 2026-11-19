#include "ai.h"
#include "clock.h"
#include "backend.h"
#include "config.h"
#include "mailcodec.h"
#include "maildocument.h"
#include "provider.h"
#include "store.h"
#include "theme.h"
#include "translator.h"
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextDocument>
#include <QUrlQuery>
#include <QtTest>

struct Request {
    QByteArray method;
    QUrl url;
    QJsonObject body;
    QByteArray raw;
    QMap<QByteArray, QByteArray> headers;
};
class FakeHttp : public HttpClient {
  public:
    QList<Request> requests;
    std::function<HttpResult(const Request &)> respond;
    void request(const QByteArray &method, const QUrl &url, const QByteArray &body,
                 const QMap<QByteArray, QByteArray> &headers, HttpCallback done, int) override {
        Request request{method, url, QJsonDocument::fromJson(body).object(), body, headers};
        requests << request;
        QTimer::singleShot(0, this, [this, request, done] { done(respond(request)); });
    }
    static HttpResult ok(QJsonObject body = {}, int status = 200) {
        return {status, QJsonDocument(body).toJson(), {}};
    }
};
class MemoryVault : public CredentialVault {
  public:
    QHash<QString, QJsonObject> values;
    void read(const QString &key, VaultCallback done) override {
        QTimer::singleShot(0, this, [this, key, done] { done(values[key], {}); });
    }
    void write(const QString &key, const QJsonObject &value, VaultCallback done) override {
        values[key] = value;
        QTimer::singleShot(0, this, [value, done] { done(value, {}); });
    }
    void authorize(const QString &key) {
        values[key] = {{"access_token", "fake-token"},
                       {"refresh_token", "fake-refresh"},
                       {"client_id", "public-id"},
                       {"expires_at", QDateTime::currentSecsSinceEpoch() + 3600}};
    }
};

class OmailTests : public QObject {
    Q_OBJECT
  private:
    static bool capture(QQuickWindow *window, const QString &name) {
        const auto directory = qEnvironmentVariable("OMAIL_TEST_CAPTURE_DIR");
        return directory.isEmpty() || (QDir().mkpath(directory) &&
                                       window->grabWindow().save(directory + "/" + name + ".png"));
    }
    static void account(Store &store, const QString &id = "a",
                        const QString &profile = "personal") {
        QVERIFY(store.saveAccount({{"id", id},
                                   {"provider", "microsoft"},
                                   {"email", id + "@example.test"},
                                   {"profile_id", profile},
                                   {"name", id}}));
    }
    static QJsonObject message(const QString &id, const QString &accountId = "a", int seconds = 0) {
        return {{"id", id},
                {"remote_id", id},
                {"account_id", accountId},
                {"thread_id", "thread"},
                {"subject", "Planning"},
                {"body", "Please review the launch plan."},
                {"folder", "inbox"},
                {"received",
                 QDateTime::currentDateTimeUtc().addSecs(seconds).toString(Qt::ISODateWithMs)},
                {"attachments", "[]"}};
    }
    static QJsonObject classification(bool action = true) {
        return {{"action_needed", action},
                {"important", false},
                {"reason", action ? "Review requested" : "No action needed"},
                {"topic", "Planning"}};
    }
  private slots:
    static QString fixture(const QString &name) {
        QFile file(QFINDTESTDATA("fixtures/mail/" + name + ".html"));
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    }
    // Renders Markdown the way the simplified view does; returns link targets and visible text.
    static QPair<QStringList, QString> rendered(const QString &markdown) {
        QTextDocument document;
        document.setMarkdown(markdown, QTextDocument::MarkdownDialectGitHub);
        QStringList links;
        for (auto block = document.begin(); block.isValid(); block = block.next())
            for (auto it = block.begin(); !it.atEnd(); ++it)
                if (it.fragment().charFormat().isAnchor())
                    links << it.fragment().charFormat().anchorHref();
        links.removeDuplicates();
        return {links, document.toPlainText()};
    }
    void initTestCase() {
        QQuickStyle::setStyle("Material");
        qmlRegisterType<MailDocument>("Omail", 1, 0, "MailDocument");
    }
    void latestConversationFieldsAreDeterministic() {
        QTemporaryDir dir;
        Store store(dir.path());
        account(store);
        auto first = message("first");
        first["subject"] = "Old";
        first["unread"] = 1;
        QVERIFY(store.upsertMessage(first));
        auto last = message("last", "a", 20);
        last["subject"] = "New";
        last["unread"] = 0;
        QVERIFY(store.upsertMessage(last));
        const auto row = store.conversations("personal", "inbox").first().toObject();
        QCOMPARE(row["subject"].toString(), QString("New"));
        QCOMPARE(row["count"].toInt(), 2);
        QCOMPARE(row["unread"].toInt(), 1);
    }
    void simplifiedViewKeepsStructureAndLinks() {
        // Issue #10: headings, bullets, and links survive; layout tables do not become pipes.
        const auto newsletter = MailCodec::markdown(fixture("newsletter-fixed-width"));
        QVERIFY2(!newsletter.contains('|'), qPrintable(newsletter));
        QVERIFY2(newsletter.contains(QRegularExpression("^# This week in quiet software$",
                                                        QRegularExpression::MultilineOption)),
                 qPrintable(newsletter));
        QVERIFY(newsletter.contains(QRegularExpression("^- Why the slowest feature is often",
                                                       QRegularExpression::MultilineOption)));
        QVERIFY(
            rendered(newsletter).first.contains("https://northwind.example/stories/slow-features"));

        const auto statement = MailCodec::markdown(fixture("account-statement"));
        QVERIFY2(!statement.contains("|-"), qPrintable(statement));
        // A label/value row stays on one line.
        QVERIFY2(rendered(statement).second.contains(QRegularExpression(
                     "^Amount due:\\s+\\$84\\.20", QRegularExpression::MultilineOption)),
                 qPrintable(rendered(statement).second));
        const auto statementLinks = rendered(statement).first;
        for (const QString path : {"/account", "/help", "/outages", "/contact", "/privacy"})
            QVERIFY2(statementLinks.contains("https://harbour.example" + path),
                     qPrintable(statementLinks.join(' ')));
    }
    void simplifiedViewLinksWrittenAddresses() {
        // Issue #10: written-out addresses are clickable, without trailing punctuation.
        const auto notice = rendered(MailCodec::markdown(fixture("security-notice"))).first;
        QVERIFY2(notice.contains("https://codehost.example/settings/security-log") &&
                     notice.contains("https://codehost.example/contact") &&
                     notice.contains(
                         "https://codehost.example/settings/connections/applications/5a7e3c19"),
                 qPrintable(notice.join(' ')));
        // Plain-text mail: Markdown characters stay literal, the address becomes a link.
        const auto plain = rendered(MailCodec::markdown(
            {}, "Price *today* only. #1 pick: https://example.test/a_b_c.\nThanks"));
        QVERIFY2(plain.second.contains("Price *today* only. #1 pick:"), qPrintable(plain.second));
        QCOMPARE(plain.first, QStringList{"https://example.test/a_b_c"});
        // Links around images keep no empty link; spaces at link edges survive.
        const auto edges =
            MailCodec::markdown("<p><a href='https://example.test/logo'><img src='x.png'></a></p>"
                                "<p><a href='https://example.test/a'>Alert for </a>"
                                "<a href='https://example.test/a'><b>data</b></a></p>");
        QVERIFY2(!edges.contains("[]"), qPrintable(edges));
        QVERIFY2(rendered(edges).second.trimmed() == "Alert for data", qPrintable(edges));
    }
    void simplifiedViewIsStyledForTheTheme() {
        // Issue #10: links use the theme accent, and lines broken within a paragraph stay in it.
        QTemporaryDir dir;
        Backend backend(dir.path() + "/data", dir.path() + "/config", true);
        QTextDocument document;
        document.setHtml(
            backend.simplified(fixture("account-statement"), {}, QColor("#ff8800"), 15));
        QStringList links;
        bool amountWithTax = false;
        for (auto block = document.begin(); block.isValid(); block = block.next()) {
            const auto lines = block.text().split(QChar::LineSeparator);
            amountWithTax |= lines.size() >= 2 && lines[0].startsWith("Amount due:") &&
                             lines[1].startsWith("(including");
            for (auto it = block.begin(); !it.atEnd(); ++it)
                if (it.fragment().charFormat().isAnchor()) {
                    links << it.fragment().charFormat().anchorHref();
                    QCOMPARE(it.fragment().charFormat().foreground().color(), QColor("#ff8800"));
                }
        }
        QVERIFY(amountWithTax);
        // Bold that ends at a line break still closes (no literal asterisks).
        QTextDocument bold;
        bold.setHtml(
            backend.simplified("<p><b>Your bill is ready.</b><br><b>Hello Alex,</b><br>Text</p>",
                               {}, QColor("#ff8800"), 15));
        QVERIFY2(!bold.toPlainText().contains('*'), qPrintable(bold.toPlainText()));
        QVERIFY(links.contains("https://harbour.example/help"));
        QVERIFY(!document.toPlainText().contains(QChar(0xE000)));
    }
    void mailTextDropsImagePlaceholdersAndPreviewPadding() {
        const auto text = MailCodec::plainText(QString::fromUtf8(
            "<div>Sale ends today͏ ‌ ͏ ‌ ­</div><img src='x.png'>"
            "<p>Read <b>this</b>​</p><p></p><p></p><p>Bye</p>"));
        for (const int c : {0xFFFC, 0x034F, 0x200C, 0x00AD, 0x200B})
            QVERIFY2(!text.contains(QChar(c)), qPrintable(QString::number(c, 16)));
        QVERIFY(text.startsWith("Sale ends"));
        QVERIFY(text.contains("Read this"));
        QVERIFY(!text.contains("\n\n\n"));
        // A non-joiner between letters is real text, as in Persian.
        const auto persian = QString::fromUtf8("می‌خواهم");
        QCOMPARE(MailCodec::cleanText(persian), persian);
    }
    void cachedBodiesAreCleanedOnceAndListsCarryOnlyPreviews() {
        QTemporaryDir dir;
        {
            Store store(dir.path());
            account(store);
            auto m = message("m");
            m["body"] = QString::fromUtf8("Hello ￼͏ ‌ world ") + QString(500, 'x');
            QVERIFY(store.upsertMessage(m));
            QVERIFY(store.exec("PRAGMA user_version=0"));
        }
        Store store(dir.path());
        const auto body = store.one("SELECT body FROM messages")["body"].toString();
        QVERIFY(body.simplified().startsWith("Hello world x"));
        QVERIFY(!body.contains(QChar(0xFFFC)) && !body.contains(QChar(0x200C)));
        const auto row = store.conversations("personal", "inbox").first().toObject();
        QVERIFY(!row.contains("body") && !row.contains("html"));
        QCOMPARE(row["preview"].toString().size(), 300);
    }
    void folderCountsMatchConversationLists() {
        QTemporaryDir dir;
        Store store(dir.path());
        account(store);
        account(store, "w", "work");
        QVERIFY(store.upsertMessage(message("one")));
        QVERIFY(store.upsertMessage(message("two", "a", 5)));
        auto other = message("three");
        other["thread_id"] = "other";
        other["flagged"] = 1;
        QVERIFY(store.upsertMessage(other));
        auto sent = message("four");
        sent["folder"] = "sent";
        QVERIFY(store.upsertMessage(sent));
        QVERIFY(store.upsertMessage(message("five", "w")));
        QVERIFY(store.applyClassification("two", classification()));
        for (const QString &profile : {"personal", "work", "all"}) {
            const auto counts = store.counts(profile);
            for (const QString &folder :
                 {"inbox", "priority", "flagged", "sent", "archive", "junk", "trash"})
                QCOMPARE(counts[folder].toInt(), store.conversations(profile, folder).size());
        }
        QCOMPARE(store.counts("personal")["inbox"].toInt(), 2);
        QVERIFY(store.dismissThread("a", "thread"));
        QCOMPARE(store.counts("personal")["priority"].toInt(), 0);
    }
    void microsoftPaginationAndRefreshTokenAreHandled() {
        QTemporaryDir dir;
        Store store(dir.path());
        Config config(dir.path() + "/config");
        account(store);
        MemoryVault vault;
        vault.authorize("a");
        vault.values["a"]["expires_at"] = 0;
        FakeHttp http;
        int refreshes = 0;
        http.respond = [&](const Request &r) {
            if (r.url.host() == "login.microsoftonline.com") {
                ++refreshes;
                return FakeHttp::ok({{"access_token", "new-fake"},
                                     {"refresh_token", "rotated-fake"},
                                     {"expires_in", 3600}});
            }
            const auto path = r.url.path();
            if (path.contains("/mailFolders/"))
                return FakeHttp::ok({{"id", path.section('/', -1)}});
            const bool second = QUrlQuery(r.url).hasQueryItem("page");
            QJsonObject raw{
                {"id", second ? "two" : "one"},
                {"conversationId", "thread"},
                {"subject", second ? "Second" : "First"},
                {"from",
                 QJsonObject{{"emailAddress", QJsonObject{{"name", "Sender"},
                                                          {"address", "sender@example.test"}}}}},
                {"body", QJsonObject{{"contentType", "html"}, {"content", "<p>Please review</p>"}}},
                {"receivedDateTime", QDateTime::currentDateTimeUtc()
                                         .addSecs(second ? 1 : 0)
                                         .toString(Qt::ISODateWithMs)},
                {"parentFolderId", "inbox"}};
            QJsonObject response{{"value", QJsonArray{raw}}};
            if (!second)
                response["@odata.nextLink"] = "https://graph.microsoft.com/v1.0/me/messages?page=2";
            return FakeHttp::ok(response);
        };
        Provider provider(&store, &config, &http, &vault);
        bool done = false;
        QString error;
        provider.syncMail(store.accounts().first().toObject(), [&](const QString &e) {
            error = e;
            done = true;
        });
        QTRY_VERIFY(done);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(refreshes, 1);
        QCOMPARE(store.thread("a", "thread").size(), 2);
        QCOMPARE(vault.values["a"]["refresh_token"].toString(), QString("rotated-fake"));
        QCOMPARE(store.conversations("personal", "inbox").first().toObject()["subject"].toString(),
                 QString("Second"));
    }
    void googlePaginationAndTrashRestoration() {
        QTemporaryDir dir;
        Store store(dir.path());
        Config config(dir.path() + "/config");
        QVERIFY2(store.saveAccount({{"id", "g"},
                                    {"provider", "google"},
                                    {"email", "g@example.test"},
                                    {"profile_id", "personal"}}),
                 qPrintable(store.error()));
        MemoryVault vault;
        vault.authorize("g");
        FakeHttp http;
        http.respond = [](const Request &r) {
            const QString path = r.url.path();
            if (r.method == "POST")
                return FakeHttp::ok();
            if (path.endsWith("/messages")) {
                if (QUrlQuery(r.url).hasQueryItem("pageToken"))
                    return FakeHttp::ok({{"messages", QJsonArray{QJsonObject{{"id", "two"}}}}});
                return FakeHttp::ok({{"messages", QJsonArray{QJsonObject{{"id", "one"}}}},
                                     {"nextPageToken", "next-page"}});
            }
            return FakeHttp::ok(
                {{"id", path.section('/', -1)},
                 {"threadId", "g-thread"},
                 {"internalDate", QString::number(QDateTime::currentMSecsSinceEpoch())},
                 {"labelIds", QJsonArray{"INBOX", "UNREAD"}},
                 {"payload",
                  QJsonObject{
                      {"mimeType", "text/plain"},
                      {"headers",
                       QJsonArray{QJsonObject{{"name", "Subject"}, {"value", "Google mail"}},
                                  QJsonObject{{"name", "From"},
                                              {"value", "Sender <sender@example.test>"}}}},
                      {"body",
                       QJsonObject{{"data", QString::fromLatin1(QByteArray("Hello").toBase64(
                                                QByteArray::Base64UrlEncoding))}}}}}});
        };
        Provider provider(&store, &config, &http, &vault);
        bool done = false;
        QString error;
        provider.syncMail(store.accounts().first().toObject(), [&](const QString &e) {
            error = e;
            done = true;
        });
        QTRY_VERIFY(done);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(store.thread("g", "g-thread").size(), 2);
        auto m = store.one("SELECT * FROM messages WHERE remote_id='one'");
        m["folder"] = "trash";
        done = false;
        provider.mailAction(m, "inbox", [&](const QString &e) {
            error = e;
            done = true;
        });
        QTRY_VERIFY(done);
        QVERIFY(error.isEmpty());
        QVERIFY(http.requests[http.requests.size() - 2].url.path().endsWith("/untrash"));
        QVERIFY(http.requests.last().url.path().endsWith("/modify"));
    }
    void providerRejectsCredentialRedirectAndCalendarUsesTimezones() {
        QTemporaryDir dir;
        Store store(dir.path());
        Config config(dir.path() + "/config");
        account(store);
        MemoryVault vault;
        FakeHttp http;
        Provider provider(&store, &config, &http, &vault);
        QString error;
        provider.api(store.accounts().first().toObject(), "GET",
                     QUrl("https://attacker.example.test/messages"), {},
                     [&](const HttpResult &r) { error = r.error; });
        QVERIFY(!error.isEmpty());
        QVERIFY(http.requests.isEmpty());
        QCOMPARE(MailCodec::utc("2026-10-17T10:00:00", "Eastern Standard Time"),
                 QString("2026-10-17T14:00:00.000Z"));
        const auto payload = MailCodec::eventPayload({{"all_day", 1},
                                                      {"start", "2026-10-17T04:00:00.000Z"},
                                                      {"end", "2026-10-18T04:00:00.000Z"},
                                                      {"title", "Holiday"}},
                                                     "google");
        QVERIFY(payload["start"].toObject().contains("date"));
    }
    void repliesRespectReplyToAndAvoidReplyingToOurselves() {
        QTemporaryDir dir;
        Store store(dir.path());
        account(store);
        auto incoming = message("incoming");
        incoming["sender_email"] = "notice@example.test";
        incoming["reply_to"] = "\"Support, Team\" <support@example.test>";
        incoming["internet_id"] = "<incoming@example.test>";
        incoming["subject"] = "Re: Planning";
        auto outgoing = message("outgoing", "a", 20);
        outgoing["folder"] = "sent";
        outgoing["sender_email"] = "a@example.test";
        outgoing["recipients"] = "Support <support@example.test>, a@example.test";
        QVERIFY(store.upsertMessage(incoming));
        QVERIFY(store.upsertMessage(outgoing));
        const auto draft = MailCodec::replyDraft(store.thread("a", "thread"));
        QCOMPARE(draft["to"].toString(), QString("support@example.test"));
        QCOMPARE(draft["subject"].toString(), QString("Re: Planning"));
        QCOMPARE(draft["remote_id"].toString(), QString("incoming"));
        QVERIFY(MailCodec::validateDraft(draft).isEmpty());
        QVERIFY(MailCodec::mime(draft).contains("In-Reply-To: <incoming@example.test>\r\n"));
        QVERIFY(store.exec("DELETE FROM messages WHERE id='incoming'"));
        QCOMPARE(MailCodec::replyDraft(store.thread("a", "thread"))["to"].toString(),
                 QString("support@example.test"));
        auto unsafe = draft;
        unsafe["to"] = "Name\r\nInjected: header <support@example.test>";
        QVERIFY(!MailCodec::validateDraft(unsafe).isEmpty());
        const auto graph = MailCodec::microsoftMessage(
            {{"replyTo", QJsonArray{QJsonObject{
                             {"emailAddress", QJsonObject{{"address", "reply@example.test"}}}}}}},
            "a", {});
        QCOMPARE(graph["reply_to"].toString(), QString("reply@example.test"));
        const auto gmail = MailCodec::googleMessage(
            {{"payload",
              QJsonObject{{"headers", QJsonArray{QJsonObject{{"name", "Reply-To"},
                                                             {"value", "reply@example.test"}}}}}}},
            "a");
        QCOMPARE(gmail["reply_to"].toString(), QString("reply@example.test"));
    }
    void repliesAttachThroughGraphAndNeverRetryAmbiguousSend() {
        QTemporaryDir dir;
        Store store(dir.path());
        Config config(dir.path() + "/config");
        account(store);
        MemoryVault vault;
        vault.authorize("a");
        FakeHttp http;
        http.respond = [](const Request &r) {
            if (r.url.path().endsWith("/send"))
                return HttpResult{0, {}, "connection interrupted"};
            return FakeHttp::ok({{"id", "remote-draft"}});
        };
        Provider provider(&store, &config, &http, &vault);
        bool done = false;
        QString error;
        const QJsonObject draft{
            {"id", "local"},
            {"account_id", "a"},
            {"to", "test@example.test"},
            {"subject", "Re: Plan"},
            {"body", "Review complete"},
            {"mode", "reply"},
            {"remote_id", "original"},
            {"attachments", QJsonArray{QJsonObject{{"name", "test.txt"}, {"data", "aGVsbG8="}}}}};
        provider.send(draft, [&](const QString &e) {
            error = e;
            done = true;
        });
        QTRY_VERIFY(done);
        QCOMPARE(error, QString("connection interrupted"));
        QCOMPARE(http.requests.size(), 4);
        QVERIFY(http.requests[0].url.path().endsWith("/createReply"));
        QCOMPARE(http.requests[1].method, QByteArray("PATCH"));
        QVERIFY(!http.requests[1].body.contains("attachments"));
        QVERIFY(http.requests[2].url.path().endsWith("/attachments"));
        QVERIFY(http.requests[3].url.path().endsWith("/send"));
        auto unsafe = draft;
        unsafe["subject"] = "Test\r\nBcc: extra@example.test";
        QVERIFY(!MailCodec::validateDraft(unsafe).isEmpty());
    }
    void aiRejectsRemoteModelsAndForeignSources() {
        QTemporaryDir dir;
        Store store(dir.path());
        Config config(dir.path() + "/config");
        account(store);
        account(store, "b", "work");
        store.upsertMessage(message("personal"));
        auto secret = message("work", "b");
        secret["body"] = "CONFIDENTIAL_WORK_ONLY";
        store.upsertMessage(secret);
        FakeHttp http;
        bool remote = true;
        bool leak = false;
        http.respond = [&](const Request &r) {
            if (r.url.path() == "/api/show")
                return FakeHttp::ok(
                    remote ? QJsonObject{{"remote_host", "https://cloud.example.test"}}
                           : QJsonObject{{"model_info", QJsonObject{{"architecture", "fake"}}}});
            leak = r.raw.contains("CONFIDENTIAL_WORK_ONLY");
            return FakeHttp::ok(
                {{"message",
                  QJsonObject{{"content", "{\"text\":\"Invented "
                                          "[mail:work]\",\"source_ids\":[\"mail:work\"]}"}}}});
        };
        Ai ai(&config, &store, &http);
        bool done = false;
        QString error;
        ai.ask("personal", "launch", [&](const QJsonObject &, const QString &e) {
            error = e;
            done = true;
        });
        QTRY_VERIFY(done);
        QVERIFY(!error.isEmpty());
        QCOMPARE(http.requests.size(), 1);
        remote = false;
        done = false;
        ai.ask("personal", "launch", [&](const QJsonObject &, const QString &e) {
            error = e;
            done = true;
        });
        QTRY_VERIFY(done);
        QVERIFY(!error.isEmpty());
        QVERIFY(!leak);
    }
    void realQmlLoadsAndComposePersistsAcrossWidths() {
        QTemporaryDir dir;
        Backend backend(dir.path() + "/data", dir.path() + "/config", true);
        Theme theme(dir.path() + "/theme");
        Translator translator(dir.path() + "/language");
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &e : errors)
                warnings << e.toString();
        });
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("initialWidth", 1360);
        engine.rootContext()->setContextProperty("initialHeight", 860);
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QTest::qWait(100);
        QQuickItem *compose = nullptr;
        for (auto *item : window->findChildren<QQuickItem *>("composeButton"))
            if (item->isVisible())
                compose = item;
        QVERIFY(compose);
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            compose->mapToScene(QPointF(compose->width() / 2, compose->height() / 2)).toPoint());
        QTest::qWait(100);
        auto body = window->findChild<QQuickItem *>("composeBody");
        QVERIFY(body);
        QVERIFY(body->isVisible());
        body->forceActiveFocus();
        for (char c : QByteArray("Saved from the actual QML editor"))
            QTest::keyClick(window, c);
        backend.newDraft();
        auto confirmation = window->findChild<QObject *>("confirmationDialog");
        QVERIFY(confirmation);
        QTRY_VERIFY(confirmation->property("opened").toBool());
        QCOMPARE(body->property("text").toString(), QString("Saved from the actual QML editor"));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!confirmation->property("visible").toBool());
        auto save = window->findChild<QQuickItem *>("saveDraftButton");
        QVERIFY(save);
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            save->mapToScene(QPointF(save->width() / 2, save->height() / 2)).toPoint());
        QTRY_COMPARE(backend.data()["drafts"].toList().size(), 1);
        Store store(dir.path() + "/data", true);
        QVERIFY(store.one("SELECT payload FROM drafts")["payload"].toString().contains(
            "Saved from the actual QML editor"));
        for (int width : {1360, 960, 420}) {
            window->resize(width, 860);
            QTest::qWait(80);
            QVERIFY(!window->grabWindow().isNull());
            QVERIFY(window->grabWindow().save(dir.filePath(QString::number(width) + ".png")));
        }
        window->setProperty("tab", "calendar");
        QTest::qWait(80);
        window->setProperty("tab", "ai");
        QTest::qWait(80);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
    void fieldLabelsStayInsideTheBorder() {
        QTemporaryDir dir;
        Backend backend(dir.path() + "/data", dir.path() + "/config", true);
        Theme theme(dir.path() + "/theme");
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("theme", &theme);
        QQmlComponent component(&engine, QUrl("qrc:/OField.qml"));
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto field = qobject_cast<QQuickItem *>(object.data());
        QVERIFY(field);
        QQuickWindow window;
        window.resize(620, 160);
        window.setColor(theme.background());
        field->setParentItem(window.contentItem());
        field->setPosition({20, 20});
        field->setWidth(580);
        field->setProperty("placeholderText", "Microsoft Application (client) ID");
        window.show();
        for (int fontSize : {11, 14, 28}) {
            QVERIFY(backend.setSetting("fontSize", fontSize));
            field->setHeight(field->implicitHeight());
            for (const auto &text : {QString(""), QString("public-client-id")}) {
                field->setProperty("text", text);
                for (bool focused : {false, true}) {
                    if (focused)
                        field->forceActiveFocus();
                    else
                        window.contentItem()->forceActiveFocus();
                    QTest::qWait(250);
                    int labels = 0;
                    const auto children = field->findChildren<QQuickItem *>();
                    for (auto *label : children) {
                        if (label->property("text").toString() !=
                                field->property("placeholderText").toString() ||
                            !label->isVisible() || label->opacity() == 0)
                            continue;
                        ++labels;
                        const auto bounds = label->mapRectToItem(
                            field, QRectF(0, 0, label->width(), label->height()));
                        QVERIFY2(bounds.top() >= 3,
                                 qPrintable(QString("Label overlaps border: top=%1, font=%2, "
                                                    "focused=%3, text=%4")
                                                .arg(bounds.top())
                                                .arg(fontSize)
                                                .arg(focused)
                                                .arg(text)));
                        QVERIFY(bounds.bottom() <= field->height() - 3);
                    }
                    QVERIFY(labels > 0);
                    if (focused && text.isEmpty())
                        QVERIFY(capture(&window, QString("field-%1").arg(fontSize)));
                }
            }
        }
    }
    void emptyAccountDropdownDoesNotOpen() {
        QTemporaryDir dir;
        Backend backend(dir.path() + "/data", dir.path() + "/config");
        Theme theme(dir.path() + "/theme");
        Translator translator(dir.path() + "/language");
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("initialWidth", 1360);
        engine.rootContext()->setContextProperty("initialHeight", 860);
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QMetaObject::invokeMethod(window, "showSettings", Q_ARG(QVariant, "advanced")));
        QTest::qWait(250);
        QQuickItem *emptyAccount = nullptr;
        for (auto *item : window->findChildren<QQuickItem *>())
            if (item->property("textRole").toString() == "email" &&
                item->property("count").toInt() == 0)
                emptyAccount = item;
        QVERIFY(emptyAccount);
        QVERIFY2(!emptyAccount->isEnabled(), "An empty account selector must not open a popup.");
        QVERIFY(!emptyAccount->property("displayText").toString().isEmpty());
        auto popup = emptyAccount->property("popup").value<QObject *>();
        QVERIFY(popup);
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            emptyAccount->mapToScene({emptyAccount->width() / 2, emptyAccount->height() / 2})
                .toPoint());
        emptyAccount->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Space);
        QVERIFY(!popup->property("visible").toBool());
        QVERIFY(capture(window, "empty-dropdown"));
        auto settings = window->findChild<QObject *>("settingsDialog");
        QVERIFY(settings);
        settings->setProperty("currentSection", 1);
        QVERIFY(QMetaObject::invokeMethod(settings, "close"));
        QTRY_VERIFY(!settings->property("visible").toBool());
        auto click = [&](QQuickItem *button) {
            QTest::mouseClick(
                window, Qt::LeftButton, Qt::NoModifier,
                button->mapToScene({button->width() / 2, button->height() / 2}).toPoint());
        };
        auto connectButton = window->findChild<QQuickItem *>("mailConnectButton");
        QVERIFY(connectButton);
        click(connectButton);
        QTRY_VERIFY(settings->property("opened").toBool());
        QCOMPARE(settings->property("currentSection").toInt(), 0);
        settings->setProperty("currentSection", 1);
        QVERIFY(QMetaObject::invokeMethod(settings, "close"));
        QTRY_VERIFY(!settings->property("visible").toBool());
        window->setProperty("tab", "calendar");
        QTest::qWait(100);
        auto calendarButton = window->findChild<QQuickItem *>("calendarManageAccountsButton");
        QVERIFY(calendarButton);
        click(calendarButton);
        QTRY_VERIFY(settings->property("opened").toBool());
        QCOMPARE(settings->property("currentSection").toInt(), 0);
    }
    void settingsRoutingPresetsAndSidebarDragPersist() {
        QTemporaryDir dir;
        Backend backend(dir.path() + "/data", dir.path() + "/config", true);
        Theme theme(dir.path() + "/theme");
        Translator translator(dir.path() + "/language");
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &e : errors)
                warnings << e.toString();
        });
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("initialWidth", 1360);
        engine.rootContext()->setContextProperty("initialHeight", 960);
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QTest::qWait(100);
        auto settings = window->findChild<QObject *>("settingsDialog");
        QVERIFY(settings);
        QVERIFY(QMetaObject::invokeMethod(window, "showSettings", Q_ARG(QVariant, "appearance")));
        QTRY_VERIFY(settings->property("opened").toBool());
        QCOMPARE(settings->property("currentSection").toInt(), 1);
        auto sidebarPreset = window->findChild<QQuickItem *>("sidebarWidthPreset");
        auto listPreset = window->findChild<QQuickItem *>("listWidthPreset");
        QVERIFY(sidebarPreset);
        QVERIFY(listPreset);
        QCOMPARE(sidebarPreset->property("currentText").toString(), QString("Medium"));
        sidebarPreset->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Down);
        QTRY_COMPARE(backend.settings()["sidebarWidth"].toInt(), 300);
        QVERIFY(backend.setSetting("sidebarWidth", 237));
        QTRY_COMPARE(sidebarPreset->property("currentText").toString(), QString("Custom"));
        QVERIFY(backend.setSetting("listWidth", 427));
        QTRY_COMPARE(listPreset->property("currentText").toString(), QString("Custom"));
        QTest::qWait(100);
        QVERIFY(capture(window, "appearance"));
        listPreset->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Up);
        QTRY_COMPARE(backend.settings()["listWidth"].toInt(), 480);
        QVERIFY(backend.setSetting("sidebarWidth", 224));
        QVERIFY(backend.setSetting("listWidth", 360));
        QVERIFY(QMetaObject::invokeMethod(settings, "close"));
        QTRY_VERIFY(!settings->property("visible").toBool());
        // Repeater delegates belong to the visual tree, not the window's QObject tree.
        std::function<QQuickItem *(QQuickItem *, const QString &)> findVisual =
            [&](QQuickItem *item, const QString &name) -> QQuickItem * {
            if (item->objectName() == name && item->isVisible())
                return item;
            for (auto *child : item->childItems())
                if (auto match = findVisual(child, name))
                    return match;
            return nullptr;
        };
        auto visibleItem = [&](const QString &name) {
            return findVisual(window->contentItem(), name);
        };
        auto click = [&](QQuickItem *item) {
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                              item->mapToScene({item->width() / 2, item->height() / 2}).toPoint());
        };
        auto manage = visibleItem("manageAccountsButton");
        QVERIFY(manage);
        click(manage);
        QTRY_VERIFY(settings->property("opened").toBool());
        QCOMPARE(settings->property("currentSection").toInt(), 0);
        QTest::qWait(100);
        QVERIFY(capture(window, "accounts"));
        QVERIFY(QMetaObject::invokeMethod(settings, "close"));
        QTRY_VERIFY(!settings->property("visible").toBool());

        // Drive the actual handle; testing moveView alone would miss broken pointer routing.
        QQuickItem *handle = nullptr;
        QQuickItem *target = nullptr;
        QTRY_VERIFY((handle = visibleItem("viewDrag-inbox")));
        QTRY_VERIFY((target = visibleItem("mailView-flagged")));
        const auto start =
            handle->mapToScene({handle->width() / 2, handle->height() / 2}).toPoint();
        const auto end = target->mapToScene({target->width() - 14, target->height() / 2}).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
        for (int step = 1; step <= 12; ++step)
            QTest::mouseMove(window, start + (end - start) * step / 12, 20);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, end);
        QTRY_COMPARE(backend.settings()["viewOrder"].toString(),
                     QString("priority,flagged,inbox,drafts,sent,archive,junk,trash,all"));
        QCOMPARE(backend.data()["folder"].toString(), QString("inbox"));
        Config persisted(dir.path() + "/config");
        QCOMPARE(persisted.get("viewOrder").toString(), backend.settings()["viewOrder"].toString());
        // External config changes update the sidebar without restarting the GUI.
        QString error;
        QVERIFY(persisted.set("viewOrder", "sent,inbox,priority", &error));
        QTRY_COMPARE(backend.settings()["viewOrder"].toString(), QString("sent,inbox,priority"));
        QTRY_VERIFY(visibleItem("mailView-sent"));
        QTRY_VERIFY(visibleItem("mailView-sent")->mapToScene({0, 0}).y() <
                    visibleItem("mailView-inbox")->mapToScene({0, 0}).y());
        auto sent = visibleItem("mailView-sent");
        sent->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Down, Qt::AltModifier);
        QTRY_COMPARE(backend.settings()["viewOrder"].toString(), QString("inbox,sent,priority"));
        QTest::qWait(100);
        QVERIFY(capture(window, "reordered-sidebar"));
        QVERIFY(QMetaObject::invokeMethod(window, "showSettings", Q_ARG(QVariant, "appearance")));
        QFile language(dir.path() + "/language");
        QCoreApplication::installTranslator(&translator);
        connect(&translator, &Translator::changed, &engine, &QQmlApplicationEngine::retranslate);
        QVERIFY(language.open(QIODevice::WriteOnly));
        language.write("da\n");
        language.close();
        QTRY_COMPARE(translator.language(), QString("da"));
        QTest::qWait(250);
        QCOMPARE(backend.settings()["sidebarWidth"].toInt(), 224);
        QCOMPARE(backend.settings()["listWidth"].toInt(), 360);
        QCOMPARE(sidebarPreset->property("currentText").toString(), QString("Mellem"));
        QCOMPARE(listPreset->property("currentText").toString(), QString("Mellem"));
        QVERIFY(capture(window, "appearance-da"));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
    void pinnedClockMakesDemoDataRepeatable() {
        const QDateTime start(QDate(2026, 1, 15), QTime(9, 0, 30), QTimeZone::utc());
        Clock::pin(start);
        const auto unpin = qScopeGuard([] { Clock::unpin(); });
        // A pinned clock keeps running from its start, so timers and the scheduler still work.
        QVERIFY(Clock::nowUtc() >= start && Clock::nowUtc() < start.addSecs(5));
        QCOMPARE(Clock::today(), start.toLocalTime().date());
        QTemporaryDir dir;
        Store store(dir.path());
        QVERIFY2(store.good(), qPrintable(store.error()));
        store.seedDemo();
        auto received = [&store](const QString &id) {
            return store.one("SELECT received FROM messages WHERE id=?", {id})["received"]
                .toString();
        };
        QCOMPARE(received("demo:0"), QString("2026-01-15T09:00:00.000Z"));
        QCOMPARE(received("demo:1"), QString("2026-01-15T07:50:00.000Z"));
        const auto tomorrow =
            QDateTime(start.toLocalTime().date().addDays(1), QTime(10, 0)).toUTC();
        QCOMPARE(store.one("SELECT start FROM events WHERE id='demo:event'")["start"].toString(),
                 tomorrow.toString(Qt::ISODateWithMs));
    }
    void profileSearchIsIsolated() {
        QTemporaryDir dir;
        Store store(dir.path());
        QVERIFY2(store.good(), qPrintable(store.error()));
        account(store);
        account(store, "b", "work");
        QVERIFY(store.upsertMessage(message("personal-mail")));
        QVERIFY(store.upsertMessage(message("work-mail", "b")));
        const auto results = store.sources("personal", "launch");
        QCOMPARE(results.size(), 1);
        QCOMPARE(results.first().toObject()["id"].toString(), QString("personal-mail"));
        QCOMPARE(store.conversations("work", "inbox").size(), 1);
    }
    void fixturesLoadOnlyIntoTheDemo() {
        const QString fixtures = QFINDTESTDATA("fixtures/mail");
        QVERIFY(!fixtures.isEmpty());
        QTemporaryDir dir;
        {
            Backend real(dir.path() + "/real", dir.path() + "/config");
            QVERIFY(!real.loadFixtures(fixtures).isEmpty());
        }
        Backend demo(dir.path() + "/data", dir.path() + "/config", true);
        QVERIFY(!demo.loadFixtures(dir.path() + "/missing").isEmpty());
        QCOMPARE(demo.loadFixtures(fixtures), QString());
        Store store(dir.path() + "/data");
        const auto m =
            store.one("SELECT * FROM messages WHERE thread_id=?", {"fixture-account-statement"});
        QCOMPARE(m["account_id"].toString(), QString("demo"));
        QCOMPARE(m["sender"].toString(), QString("Harbour Utilities"));
        QVERIFY(m["html"].toString().contains("Statement details"));
        QVERIFY(m["body"].toString().contains("$84.20"));
    }
    void notificationTargetOpensItsConversationAndListPagesGrow() {
        QTemporaryDir dir;
        {
            Store store(dir.path() + "/data");
            account(store);
            account(store, "w", "work");
            for (int i = 0; i < Backend::PageSize + 5; ++i) {
                auto m = message("m" + QString::number(i), "a", -i);
                m["thread_id"] = "t" + QString::number(i);
                QVERIFY(store.upsertMessage(m));
            }
            auto work = message("work-mail", "w");
            work["thread_id"] = "work-thread";
            QVERIFY(store.upsertMessage(work));
        }
        Backend backend(dir.path() + "/data", dir.path() + "/config");
        QCOMPARE(backend.data()["threads"].toList().size(), Backend::PageSize);
        QCOMPARE(backend.data()["counts"].toMap()["inbox"].toInt(), Backend::PageSize + 5);
        backend.loadMore();
        QCOMPARE(backend.data()["threads"].toList().size(), Backend::PageSize + 5);
        QSignalSpy opened(&backend, &Backend::sourceOpened);
        backend.openThread("w", "work-thread");
        QCOMPARE(opened.size(), 1);
        QCOMPARE(backend.data()["profile"].toString(), QString("work"));
        QCOMPARE(backend.data()["selectedThread"].toString(), QString("work-thread"));
        QCOMPARE(backend.data()["messages"].toList().size(), 1);
        QSignalSpy changed(&backend, &Backend::changed);
        backend.refresh();
        QCOMPARE(changed.size(), 0);
    }
    void cliRejectsWritesAndLeavesMailUntouched() {
        QTemporaryDir dir;
        Store store(dir.path() + "/data");
        account(store);
        QVERIFY(store.upsertMessage(message("mail")));
        const QString executable = QCoreApplication::applicationDirPath() + "/../omail";
        QVERIFY(QFileInfo::exists(executable));
        auto run = [&](const QStringList &args) {
            QProcess process;
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert("OMAIL_DATA_DIR", dir.path() + "/data");
            environment.insert("OMAIL_CONFIG_DIR", dir.path() + "/config");
            environment.remove("DISPLAY");
            environment.remove("WAYLAND_DISPLAY");
            process.setProcessEnvironment(environment);
            process.start(executable, args);
            if (!process.waitForFinished(10000))
                return -1;
            return process.exitCode();
        };
        QCOMPARE(run({"mail", "list", "--json"}), 0);
        QCOMPARE(run({"mail", "send", "mail", "--json"}), 1);
        QCOMPARE(run({"mail", "delete", "mail", "--json"}), 1);
        QCOMPARE(run({"calendar", "create", "--json"}), 1);
        QCOMPARE(run({"config", "set", "fontSize", "18", "--json"}), 0);
        QCOMPARE(store.one("SELECT COUNT(*) AS n FROM messages")["n"].toInt(), 1);
        QCOMPARE(store.one("SELECT folder FROM messages WHERE id='mail'")["folder"].toString(),
                 QString("inbox"));
        Config config(dir.path() + "/config");
        QCOMPARE(config.get("fontSize").toInt(), 18);
    }
    void sentDraftsAndInvalidSchedulesCannotBeResubmitted() {
        QTemporaryDir dir;
        Store store(dir.path());
        account(store);
        QJsonObject draft{
            {"id", "d"},
            {"account_id", "a"},
            {"body", "Keep my text"},
            {"attachments", QJsonArray{QJsonObject{{"data", "aGVsbG8="}, {"name", "test.txt"}}}}};
        QVERIFY(store.saveDraft(draft));
        QVERIFY(!store.queueDraft("missing", QDateTime::currentDateTimeUtc().addSecs(60)));
        QVERIFY(store.finishSend("d", true));
        QCOMPARE(store.one("SELECT status FROM drafts WHERE id='d'")["status"].toString(),
                 QString("sent"));
        QVERIFY(!store.saveDraft(draft));
        QVERIFY(!store.queueDraft("d", QDateTime::currentDateTimeUtc().addSecs(60)));
        QVERIFY(store.one("SELECT payload FROM drafts WHERE id='d'")["payload"].toString().contains(
            "aGVsbG8="));
    }
    void actualLocalModelClassifiesSummarizesAndCitesSyntheticMail() {
        if (!qEnvironmentVariableIsSet("OMAIL_TEST_OLLAMA"))
            QSKIP("Set OMAIL_TEST_OLLAMA=1 for the installed-model integration check.");
        QTemporaryDir dir;
        Store store(dir.path());
        store.seedDemo();
        Config config(dir.path() + "/config");
        HttpClient http;
        Ai ai(&config, &store, &http);
        bool done = false;
        QString error;
        QJsonObject result;
        auto callback = [&](const QJsonObject &r, const QString &e) {
            result = r;
            error = e;
            done = true;
        };
        ai.classify(store.one("SELECT * FROM messages WHERE id='demo:0'"), callback);
        QTRY_VERIFY_WITH_TIMEOUT(done, 180000);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(result["action_needed"].toBool());
        done = false;
        ai.summarize(store.thread("demo", "0"), false, callback);
        QTRY_VERIFY_WITH_TIMEOUT(done, 180000);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(result["text"].toString().size() > 20);
        done = false;
        ai.ask("personal", "What did Morgan ask me to approve?", callback);
        QTRY_VERIFY_WITH_TIMEOUT(done, 180000);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!result["sources"].toArray().isEmpty());
    }
    void prioritySurvivesReadingButReopensOnlyForRelevantReplies() {
        QTemporaryDir dir;
        Store store(dir.path());
        account(store);
        QVERIFY(store.upsertMessage(message("original")));
        QVERIFY(store.applyClassification("original", classification()));
        QCOMPARE(store.conversations("personal", "priority").size(), 1);
        QVERIFY(store.exec("UPDATE messages SET unread=0 WHERE id='original'"));
        QCOMPARE(store.conversations("personal", "priority").size(), 1);
        QVERIFY(store.dismissThread("a", "thread"));
        QCOMPARE(store.conversations("personal", "priority").size(), 0);
        QVERIFY(store.upsertMessage(message("reply", "a", 10)));
        QVERIFY(store.applyClassification("reply", classification(false)));
        QCOMPARE(store.conversations("personal", "priority").size(), 0);
        QVERIFY(store.upsertMessage(message("action", "a", 20)));
        QVERIFY(store.applyClassification("action", classification()));
        QCOMPARE(store.conversations("personal", "priority").size(), 1);
    }
    void correctionSurvivesSyncAndClassifierRaces() {
        QTemporaryDir dir;
        Store store(dir.path());
        account(store);
        auto m = message("corrected");
        QVERIFY(store.upsertMessage(m));
        QVERIFY(store.correctMessage("corrected", false, "Not important"));
        QVERIFY(store.applyClassification("corrected", classification()));
        QVERIFY(store.upsertMessage(m));
        QCOMPARE(
            store.one("SELECT priority FROM messages WHERE id='corrected'")["priority"].toInt(), 0);
        QVERIFY(store.upsertMessage(message("other", "a", 30)));
        QVERIFY(store.applyClassification("other", classification()));
        QCOMPARE(store.one("SELECT priority FROM messages WHERE id='other'")["priority"].toInt(),
                 1);
    }
    void schedulerHoldsMissedSendsAndClaimsOnce() {
        QTemporaryDir dir;
        Store store(dir.path());
        account(store);
        const auto due = QDateTime::currentDateTimeUtc().addSecs(3600);
        QVERIFY(store.saveDraft(
            {{"id", "late"}, {"account_id", "a"}, {"to", "test@example.test"}, {"body", "Hello"}}));
        QVERIFY(store.queueDraft("late", due));
        QCOMPARE(store.takeDue(due.addSecs(1), due.addSecs(-60), true).size(), 0);
        QCOMPARE(store.one("SELECT status FROM drafts WHERE id='late'")["status"].toString(),
                 QString("held"));
        QVERIFY(store.saveDraft({{"id", "ontime"}, {"account_id", "a"}, {"body", "Hello"}}));
        QVERIFY(store.queueDraft("ontime", due));
        QCOMPARE(store.takeDue(due, due.addSecs(-1), true).size(), 1);
        Store other(dir.path());
        QCOMPARE(other.takeDue(due.addSecs(1), due, true).size(), 0);
        store.recoverInterrupted();
        QCOMPARE(store.one("SELECT status FROM drafts WHERE id='ontime'")["status"].toString(),
                 QString("held"));
    }
    void schedulerHoldsOfflineAndStartupSends() {
        QTemporaryDir dir;
        Store store(dir.path());
        account(store);
        const auto due = QDateTime::currentDateTimeUtc().addSecs(60);
        QVERIFY(store.saveDraft({{"id", "offline"}, {"account_id", "a"}}));
        QVERIFY(store.queueDraft("offline", due));
        QCOMPARE(store.takeDue(due, due.addSecs(-1), false).size(), 0);
        QCOMPARE(store.one("SELECT status FROM drafts WHERE id='offline'")["status"].toString(),
                 QString("held"));
        QVERIFY(store.saveDraft({{"id", "startup"}, {"account_id", "a"}}));
        QVERIFY(store.queueDraft("startup", due));
        QCOMPARE(store.takeDue(due, {}, true).size(), 0);
        QCOMPARE(store.one("SELECT status FROM drafts WHERE id='startup'")["status"].toString(),
                 QString("held"));
    }
    void configurationRejectsCloudRoutesAndBadTypes() {
        QTemporaryDir dir;
        Config config(dir.path());
        QString error;
        QVERIFY(!config.set("ollamaUrl", "https://remote.example.test", &error));
        QVERIFY(!config.set("fontSize", "large", &error));
        QVERIFY(!config.set("fontSize", 100, &error));
        QVERIFY(!config.set("ollamaModel", "model:cloud", &error));
        QVERIFY(config.set("fontSize", 17, &error));
        Config reloaded(dir.path());
        QCOMPARE(reloaded.get("fontSize").toInt(), 17);
        QVERIFY(!config.set("unknown", true, &error));
        QCOMPARE(config.get("messageView").toString(), QString("formatted"));
        QVERIFY(!config.set("messageView", "html", &error));
        QVERIFY(config.set("messageView", "plain", &error));
    }
    void themesFollowAtomicReplacement() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QFile file(dir.filePath("theme/colors.toml"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("accent = \"#123456\"\n");
        file.close();
        QTRY_COMPARE(theme.accent(), QString("#123456"));
        QFile replacement(dir.filePath("theme/replacement"));
        QVERIFY(replacement.open(QIODevice::WriteOnly));
        replacement.write("accent = \"#abcdef\"\n");
        replacement.close();
        QVERIFY(QFile::remove(file.fileName()));
        QVERIFY(QFile::rename(replacement.fileName(), file.fileName()));
        QTRY_COMPARE(theme.accent(), QString("#abcdef"));
    }
    void languageFallbackAndLiveSwitch() {
        QTemporaryDir dir;
        auto write = [&](const QString &name, const QByteArray &bytes) {
            QFile f(dir.filePath(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(bytes);
        };
        write("pt.json", R"({"Save":"Guardar","Open":"Abrir"})");
        write("pt-BR.json", R"({"Save":"Salvar"})");
        write("language", "pt-BR\n");
        Translator translator(dir.filePath("language"), dir.path());
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        write("language", "en\n");
        QTRY_COMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }
};
QTEST_MAIN(OmailTests)
#include "omail_tests.moc"
