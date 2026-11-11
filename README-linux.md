# Jotilla on Linux

Jotilla is a lightweight, native sticky-notes application built with C17 and
GTK3. It runs on both Wayland and X11 sessions and stores everything as plain
files.

## Build prerequisites

- A C17 compiler (gcc or clang)
- CMake >= 3.20
- Ninja (recommended)
- GTK3 development headers

Fedora:

```sh
sudo dnf install gcc gtk3-devel cmake ninja-build
```

Debian/Ubuntu:

```sh
sudo apt install build-essential libgtk-3-dev cmake ninja-build
```

## Build and run

```sh
cmake -S . -B build -G Ninja
cmake --build build
./build/jotilla
```

To install system-wide:

```sh
sudo cmake --install build --prefix /usr/local
```

## Data locations

Jotilla follows the XDG Base Directory specification:

- Notes archive: `$XDG_DATA_HOME/jotilla/notes`
  (defaults to `~/.local/share/jotilla/notes`)
- Machine-local configuration: `$XDG_CONFIG_HOME/jotilla/archive.path`
  (defaults to `~/.config/jotilla/archive.path`)

The archive folder is self-contained: UTF-8 `.txt` notes with optional `.rtf`
companions and a Windows/macOS-compatible `notes.ini` (UTF-16LE with BOM,
`width`/`height`, `1`/`0` flags, per-note `scroll`, `[app] scroll`/`last_note`,
CRLF text). The same folder can be synced and
opened by Jotilla on Windows, macOS and Linux; only the archive location
preference stays local to each machine. A `.jotila.lock` file prevents
concurrent writers.

## Features

- Sticky-note windows draggable by the title bar with edge/corner resizing,
  double-click title rename, title promotion (first line, or Shift+Enter) and
  checklist markers (`☐`/`☑`)
- Rich-text formatting: bold, italic, underline, strikethrough and text colour
- Undo/redo
- Palette picker with 80+ palettes (3 built-in plus the 80 popular palettes)
  and a favourite palette
- Note sizes (Small, Medium, Large, Extra large, Square) and opacity control
- Autosave (400 ms debounce), save-on-focus-loss, save-on-quit, plain-text
  paste, and save failures keep the note open and retry
- Per-note **Scrollable text** (bar menu, with an optional default for new
  notes) for text taller than the note; scroll bars stay hidden and the wheel,
  Page Up/Down and Ctrl+Home/End navigate. Bounded notes reject overflow with a
  paste notice and keep existing text/formatting/selection
- Archive tile view ordered newest first, with search-match highlighting,
  Ctrl/Shift multi-select, delete, creation-date/tag trees, calendar filtering,
  and open/move archive location
- Annual calendar view with filtered day counts, Today, year navigation,
  focus/Enter navigation and same-day note list
- Tag normalisation (24 tags, 63 UTF-16 units), filtering and per-token
  autocomplete with usage counts and assigned-tag exclusion
- System tray and true global hotkey on X11 (see *Platform limitations*);
  single-click recalls the last used note (or opens the archive), double-click
  opens the archive, and the app stays in the tray when hosted
- Optional autostart via an XDG autostart desktop entry
- Portable mode (`portable.flag`) and single-instance behaviour
  (`GApplication`/D-Bus)
- Notes are always-on-top by default (X11 keep-above; compositor-controlled on Wayland)
- File-based storage with no database and no bundled runtime

## Using notes

Each note is a borderless sticky note with a coloured title bar:

- Calendar icon: assign a date to the note.
- Title: taken from the first line of the note.
- `—`: collapse the note to its title bar.
- `×`: save and close.

Right-click the note for the full menu: open notes archive, browse notes,
opacity, edit title, note size, new-note palette, tags, assign day, fold,
save now and export an RTF copy.

Keyboard shortcuts: `Ctrl+N` new note, `Ctrl+S` save, `Ctrl+Z`/`Ctrl+Y`
(also `Ctrl+Shift+Z`) undo/redo, `Ctrl+B/I/U` bold/italic/underline,
`Ctrl+Shift+X` strikethrough, `Ctrl+A` select all, `Ctrl++`/`Ctrl+-` and the
mouse wheel resize text, `Shift+Enter` promotes the title, `Enter` continues a
checklist item, `Esc` saves and closes.

## Always-on-top notes

Always-on-top and sticky behaviour uses the X11/XWayland window manager
protocols. On a native X11 session (and on XWayland) it works as expected.

On GNOME Wayland the compositor does not let applications control stacking, so
always-on-top may be ignored. In that case run Jotilla through XWayland:

```sh
GDK_BACKEND=x11 jotilla
```

KDE and wlroots-based compositors may honour keep-above depending on their
window rules; Jotilla prints a warning when the request may be ignored.

## Platform limitations

Jotilla on Linux is built with GTK3 (3.24). Two desktop-integration features
are only available under X11:

- **System tray:** implemented with `GtkStatusIcon`, which depends on the X11
  XEmbed system-tray protocol. There is no tray icon on native Wayland
  sessions.
- **Global hotkeys:** `Ctrl+Alt++` is grabbed through X11 key grabbing and is
  unavailable on native Wayland.

Workarounds on Wayland (for example GNOME):

- Bind a desktop shortcut to launch Jotilla. A `Super+Alt+J` GNOME custom
  shortcut is already configured; it starts Jotilla, or creates a new note if
  it is already running (the Windows `Ctrl+Alt++` equivalent).
- Or run Jotilla through XWayland, which restores both the tray icon and the
  global hotkey:

  ```sh
  GDK_BACKEND=x11 jotilla
  ```

See *Always-on-top notes* above for the related Wayland stacking restriction.

## Packaging

- Desktop entry: `packaging/jotilla.desktop`
- Icon: `packaging/jotilla.svg`
- AppStream metadata: `packaging/org.jotilla.Jotilla.metainfo.xml`
- Flatpak manifest: `packaging/flatpak/org.jotilla.Jotilla.yml`
- AppImage builder: `packaging/appimage/build-appimage.sh`

Build an AppImage:

```sh
./packaging/appimage/build-appimage.sh
```

Build the Flatpak:

```sh
flatpak-builder --user --install --force-clean build-flatpak \
    packaging/flatpak/org.jotilla.Jotilla.yml
```

Validate metadata:

```sh
desktop-file-validate packaging/jotilla.desktop
appstreamcli validate packaging/org.jotilla.Jotilla.metainfo.xml
```

## Testing

Unit tests are registered with CTest:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

The end-to-end smoke test exercises the binary under a virtual X server:

```sh
xvfb-run -a ./tests/e2e_smoke.sh ./build/jotilla
```

It runs `--self-test` and `--smoke-test` with temporary archives and asserts
that a first note is created.
