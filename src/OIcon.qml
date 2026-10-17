import QtQuick

Image {
    property string name: "mail"
    property color tint: theme.foreground
    readonly property var paths: ({
            mail: "M3 5h18v14H3z M3 6l9 7 9-7",
            inbox: "M4 4h16l2 12v4H2v-4z M2 15h6l2 3h4l2-3h6",
            calendar: "M4 5h16v16H4z M8 2v6 M16 2v6 M4 10h16 M8 14h2 M14 14h2 M8 18h2",
            ai: "M12 2l3 7 7 3-7 3-3 7-3-7-7-3 7-3z",
            priority: "M12 3l9 17H3z M12 9v5 M12 17v.5",
            flag: "M5 22V3 M5 3h14l-3 5 3 5H5",
            archive: "M3 3h18v5H3z M5 8v13h14V8 M9 12h6",
            trash: "M3 6h18 M9 6V3h6v3 M5 6l1 15h12l1-15 M10 10v7 M14 10v7",
            junk: "M8 2h8l6 6v8l-6 6H8l-6-6V8z M12 7v6 M12 17v.5",
            send: "M22 2L9 15 M22 2l-7 20-6-7-7-6z",
            draft: "M14 3H4v18h16V11 M9 15l2-5 8-8 3 3-8 8z",
            search: "M10 3a7 7 0 1 0 0 14 7 7 0 0 0 0-14 M15 15l7 7",
            plus: "M12 4v16 M4 12h16",
            close: "M5 5l14 14 M19 5L5 19",
            back: "M15 4l-8 8 8 8",
            next: "M9 4l8 8-8 8",
            menu: "M3 6h18 M3 12h18 M3 18h18",
            check: "M3 12l6 6L21 5",
            reply: "M10 4l-7 7 7 7 M3 11h11a7 7 0 0 1 7 7",
            forward: "M14 4l7 7-7 7 M21 11H10a7 7 0 0 0-7 7",
            sync: "M20 8a9 9 0 0 0-15-3L2 8 M2 2v6h6 M4 16a9 9 0 0 0 15 3l3-3 M16 16h6v6",
            settings: "M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8 M12 2v3 M12 19v3 M2 12h3 M19 12h3 M5 5l2 2 M17 17l2 2 M19 5l-2 2 M7 17l-2 2",
            clock: "M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20 M12 6v6l4 3",
            tag: "M2 3h9l11 11-8 8L2 10z M7 7h.1",
            clip: "M8 12l7-7a4 4 0 0 1 6 6L10 22a6 6 0 0 1-8-8L13 3 M5 16l10-10",
            external: "M14 3h7v7 M21 3L10 14 M10 5H3v16h16v-7",
            more: "M4 12h1 M11 12h1 M18 12h1",
            help: "M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20 M9 8a3 3 0 0 1 6 0c0 3-3 2-3 6 M12 17v.1",
            folder: "M2 5h8l2 3h10v13H2z",
            eye: "M2 12s4-7 10-7 10 7 10 7-4 7-10 7-10-7-10-7 M12 9a3 3 0 1 0 0 6 3 3 0 0 0 0-6"
        })
    source: "data:image/svg+xml," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><path d="' + (paths[name] || paths.mail) + '" fill="none" stroke="' + tint + '" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/></svg>')
    sourceSize.width: 24
    sourceSize.height: 24
    width: 20
    height: 20
    fillMode: Image.PreserveAspectFit
}
