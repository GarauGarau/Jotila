# Jotilla — Linux Port Plan (Wayland + X11)

Goal: make Jotilla 100% compatible with Linux (GNOME, KDE, Hyprland, Sway, etc.), while preserving its native, lightweight, file-based character.

## 0. Implementation status (2026-11-10)

The Linux implementation is in place and builds/tests on this machine.

- **Toolkit:** GTK3 3.24 (GTK4 development headers were not installed in the build environment; GTK3 is available system-wide). The architecture keeps GTK code isolated so a GTK4 migration can be done later.
- **Build:** CMake + Ninja; targets `jotilla`, `test_core`, `test_rtf`, `test_extended`, `window_probe`, `xui`.
- **Implemented:**
  - XDG storage: `$XDG_DATA_HOME/jotilla/notes` (with `notes.ini` inside the archive), machine-local `$XDG_CONFIG_HOME/jotilla/archive.path`.
  - File-compatible notes: UTF-8 `.txt` with CRLF, `.rtf` companion, Windows/macOS-compatible UTF-16LE `notes.ini`.
  - Note windows: 1 px paper border, same-colour title bar with calendar icon, title, collapse (`—`) and close (`×`); right-click menu for all actions; autosave (400 ms debounce) and save-on-quit; title promotion (first line, or Shift+Enter); checklist markers (`☐`/`☑`) with paragraph strikethrough and single undo.
  - Formatting: bold, italic, underline, strikethrough and text colour; undo/redo; word-wrap toggle; five note sizes (Small, Medium, Large, Extra large, Square); opacity control.
  - Palette picker: 83 palettes (3 built-in + 80 popular) with previous/next navigation and a persisted favourite palette.
  - Archive window: tile view, search with match highlighting, multi-select delete, date/tag tree filters, calendar filtering, tag autocomplete.
  - Tags normalisation and catalog; calendar date assignment with monthly and annual (year) views.
  - Tray icon (`GtkStatusIcon`/XEmbed) and global hotkey `Ctrl+Alt++`, both on X11 only.
  - Tray single-click recalls the last used note (falling back to the archive); double-click opens the archive. `[app] last_note` is per archive.
  - Per-note Scrollable text uses the shared `scroll` key and `[app] scroll` default; bars stay hidden while wheel/Page/Ctrl+Home/End navigate. Bounded notes reject overflow and warn on paste.
  - Autostart via an XDG autostart `.desktop` entry; portable mode via `portable.flag`; single instance via `GApplication`/D-Bus.
  - Always-on-top: works on X11/XWayland; documented GNOME Wayland limitation.
  - Packaging: `.desktop`, SVG icon, AppStream metainfo, Flatpak manifest, AppImage script.
- **Verification run:**
  - `ctest` (Debug, Release, ASan/UBSan): core, RTF, extended, E2E all pass.
  - E2E: `xvfb-run` self-test + smoke test + real-window probe (window title observed: "Untitled").
  - Native Wayland smoke (`--smoke-test`) and X11 smoke both exit 0 and create a note.
  - Visual check under Xvfb: note window and archive window render correctly.
  - `desktop-file-validate` passes; `appstreamcli validate --no-net` passes (1 pedantic).
  - `cmake --install` installs binary, desktop file, icon and metainfo correctly.
- **Known gaps / next steps:**
  - Wayland has no system tray (GtkStatusIcon uses X11 XEmbed) and no global hotkeys. Use a GNOME custom shortcut (`Super+Alt+J` is already configured) or run with `GDK_BACKEND=x11`.
  - GNOME Wayland cannot grant client-requested always-on-top; use `GDK_BACKEND=x11` or a Shell extension.
  - No `wlr-layer-shell` integration yet (library not installed); KDE/wlroots AoT currently relies on window rules/keep-above.
  - Exact RichEdit/TOM parity and per-monitor DPI tweaks are not complete.
  - No Linux installer/uninstaller; distribution is via CMake install and packaging.
  - GTK4 migration (fractional scaling, `GtkColumnView`, `gtk4-layer-shell`) remains optional.

### Remaining platform-specific limitations

| Limitation | Affected platform | Workaround / status |
|---|---|---|
| System tray icon (GtkStatusIcon/XEmbed) | Wayland (all compositors) | Run with `GDK_BACKEND=x11` |
| Global hotkey `Ctrl+Alt++` | Wayland (all compositors) | GNOME custom shortcut (`Super+Alt+J` configured) or `GDK_BACKEND=x11` |
| Client-requested always-on-top / sticky | GNOME Wayland | XWayland (`GDK_BACKEND=x11`) or Shell extension |
| wlr-layer-shell overlay notes | wlroots/KDE | Not integrated; window rules/keep-above used instead |
| Exact RichEdit/TOM formatting parity | All | RTF mapped through GtkTextBuffer tags (subset) |
| Per-monitor DPI / fractional scaling | All | GDK scaling; depends on GTK3 backend |
| Installer/uninstaller | Linux | CMake install + deb/rpm/Flatpak/AppImage packaging |

## 1. Executive summary

- Jotilla is a small native sticky-notes app: autosave, archive, calendar, tags, search, formatting, color palettes; no database, UTF-8 text + RTF companion + INI metadata.
- Current platforms: Windows 10/11 x64 (default), macOS 13+ (preview branch). No Linux.
- Linux port must be native (no bundled browser/runtime), file-based, Wayland-first with X11 fallback, and work across GNOME/KDE/Hyprland/Sway.
- Key challenge: Wayland has no standard always-on-top/sticky for normal windows; requires compositor-specific strategy (layer-shell on wlroots/KDE, XWayland/EWMH on GNOME).
- Recommended GUI stack: GTK4 (native Wayland, fractional scaling, accessibility) with an abstraction layer; Qt6 as alternative. Keep C17 core portable.
- Recommended text strategy: keep RTF storage; implement RTF↔GtkTextBuffer tag mapping (or phased migration to HTML/Markdown).
- Packaging: AppImage + Flatpak + deb/rpm via CMake/CPack; CI with xvfb, clang-tidy, sanitizers, valgrind.

## 2. Current codebase (Windows)

### 2.1 Source tree
All sources in `src/`:
```
app.manifest
archive.c/.h       calendar.c/.h      colors.c/.h
date.c/.h          jotilla.c          jotilla.rc
location.c/.h      metadata.c/.h      palette_art.c/.h
palette_picker.c/.h popular_palettes.inc resource.h
search.c/.h        setup.c            setup.rc
tags.c/.h
```

### 2.2 Build & runtime
- MSVC C17, `/W4 /WX /O2 /MT`; links User32, GDI, Shell, DWM, common controls, UxTheme, Normaliz, OLE/UUID. (`build.cmd`)
- No third-party runtime; statically linked CRT. (`README.md`, `docs/performance.md`)
- Outputs: `dist/Jotilla.exe`, `dist/Jotilla-Setup-x64.exe`.
- Test/profiling targets: `test`, `test-checked`, `test-only`, `screenshots`, `analyze`, `*-profile`.

### 2.3 Win32 API families used
| Family | Files | Purpose |
|---|---|---|
| Windowing/messages | `jotilla.c`, `archive.c`, `calendar.c`, `palette_picker.c` | Register classes, create windows, message loop |
| RichEdit/TOM | `jotilla.c` | Editor control, RTF stream, formatting, undo |
| GDI | `jotilla.c`, `calendar.c`, `palette_art.c`, `archive.c` | Drawing, brushes, fonts, bitmaps, DCs |
| File I/O | `jotilla.c`, `location.c`, `tags.c`, `search.c`, `archive.c` | Read/write/enumerate/copy/remove, temp files |
| Timers/QPC | `jotilla.c`, `archive.c`, `palette_picker.c` | Debounce saves/search, tray clicks, animation, timing seed |
| Monitor/DPI | `jotilla.c`, `calendar.c`, `archive.c`, `palette_picker.c` | DPI scaling, monitor work area |
| Registry | `setup.c` | Startup/uninstall registration (HKCU) |
| Shell | `setup.c`, `jotilla.c`, `location.c` | Resolve folders, open paths, file dialog |
| DWM | `jotilla.c`, `archive.c` | Non-client rendering, caption colors |
| Common controls | `archive.c` | TreeView/ListView for archive/tag/date filtering |
| uxtheme | `archive.c` | Disable visual theme for custom styling |

### 2.4 Entry points
- `setup.c:296` — `wWinMain`, setup/uninstaller (`/S`, `/AUTOSTART`, `/NOAUTOSTART`).
- `jotilla.c:2019` — `wWinMain`, main app; creates controller, runs message loop (`2084–2085`).

### 2.5 Storage format
- Note files: `note-YYYY-MM-DD_HH-MM-SS-mmm-ccc.txt` (UTF-8) + `.rtf` (companion) + `.ini` (per-note metadata: tags, calendar_date).
- `notes.ini` inside the archive: per-note sections with layout keys (`x`, `y`, `color`, `has_title`, `collapsed`, `open`, `palette`, `size`, `opacity`, `width`, `height`, `custom_ink`, `scroll`), `tags`, `calendar_date`; `[app]` keeps `scroll` and `last_note`; UTF-16LE with BOM, CRLF, unknown keys preserved.
- Archive path resolved via `portable.flag` (portable) or XDG defaults; the chosen path is stored in the machine-local `archive.path` file, never in the synced archive.
- Limits: text 16 MB (`TEXT_LIMIT`), tag name 63 chars, 24 tags max, 2 KB tags string, excerpt 1200 chars.

### 2.6 Module responsibilities
- `jotilla.c` — app controller, note windows, RichEdit, editing commands (bold/italic/underline/strikethrough, todo markers, title promotion), save/layout, hotkeys, tray, DPI.
- `archive.c` — archive browser (list/calendar/tags views), search results display, entry indexing, preview loading, archive actions.
- `calendar.c` — calendar picker, year view, date scheduling, keyboard nav.
- `search.c` — worker thread, query compilation (`#tag`, `"quoted"`, `-#` untagged), Unicode folding (NFD + casefold + accent strip), KMP matching, streaming read of `.txt` chunks.
- `tags.c` — tag normalization (ASCII fold uppercase, Unicode NFC+lowercase), dedup/sort, INI read/write, tag catalog build, suggestions.
- `location.c` — archive path resolution, migration (copy/move), config paths.
- `date.c` — Gregorian date utils (days in month, ordinal, weekday, shift, parse).
- `colors.c` / `palette_art.c` / `palette_picker.c` — palette definitions, ink contrast, custom palette rendering, picker popup.
- `metadata.c` — INI cursor iterator (section walk, key/value).
- `setup.c` — installer/uninstaller, registry, file associations.

### 2.7 Note lifecycle
1. Create: generate ID `note-YYYY-MM-DD_HH-MM-SS-mmm-ccc.txt`, pick color via shuffled palette queue, create window with DPI-aware sizing.
2. Edit: RichEdit control; formatting shortcuts; todo markers (`☐`/`☑`); title promotion on Shift+Enter.
3. Save: dirty flags (`DIRTY_TEXT`, `DIRTY_RICH`); `save_text()` writes UTF-8 with CRLF; `save_rich()` streams RTF atomically; 400 ms timer debounce; tags split into `[note] tags=` metadata; archive notified.
4. Close: save layout, cleanup window.
5. Delete: `remove_note_files()` removes TXT+RTF; archive refreshed.

### 2.8 Key algorithms
- Todo marker toggle: detects `☐ ` in paragraph, emits `☑`, strikethroughs whole paragraph, single undo.
- Title promotion: reads first line after `#`, validates selection covers entire line, moves body to title, clears line.
- Text fitting: `EM_REQUESTRESIZE()` asks RichEdit; reverts if doesn't fit.
- Tag normalization: filters duplicates via `qsort`; ASCII uppercase fold; Unicode NFC+lowercase; sorted, joined by `;`.
- Search streaming: chunk read ~8 KB, KMP match, accumulate byte offsets, skip `.rtf`.
- Date normalization: parse `YYYYMMDD`, validate 1601–9999, compute ordinal 0–365.

### 2.9 Keyboard shortcuts
Ctrl+Alt++ new note; Ctrl+E browse; Ctrl+Shift+F search; Ctrl+T tags; Ctrl+B/I/U bold/italic/underline; Ctrl+Shift+X strikethrough; Ctrl+Z/Y undo/redo; Ctrl+A select all; Ctrl+/Ctrl- resize text; Shift+Enter promote title/checklist; Enter open checklist/accept tag.

### 2.10 Accessibility & UX
- Save failure: red title bar border. Hover states on calendar/date/delete. Ink color black/white by palette. Opacity 60–100% layered window. Palette picker with wheel navigation + favorite star.

### 2.11 Performance
- Indexing: file scan on startup; tags scan notes with content check; calendar parses INI per archive refresh; search streams `.txt` only.
- Caching: preview lazy on scroll; search hits store offsets only; tag catalog built once per session; palette art cached with coverage bit mask.
- Concurrency: single search worker thread with wake event; archive UI on event thread; hotkeys posted to main window.

### 2.12 Security & durability
- Atomic writes: TXT/RTF write to `.tmp` then `MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH`; INI via `WritePrivateProfileStringW`.
- Crash recovery: hotkeys positioned before window shown; layout saved on `WM_EXITSIZEMOVE`; dirty timer continues on save failure; `WM_QUERYENDSESSION` triggers `save_all`.
- Empty notes deleted (TXT+RTF) when content removed; failed writes keep old files and retry.

### 2.13 Testing tools
- `tests/`: tests.c, startup_checks, archive_cache, archive_layout, calendar_checks, calendar_paint, palette_checks, session_checks, setup_checks, text_size_checks.
- `tools/`: calendar_profile, search_profile, storage_profile, tags_profile, profile, capture.ps1.

## 3. Linux/Wayland landscape

- No standard Wayland always-on-top for regular toplevels; compositor owns stacking.
- `wlr-layer-shell-unstable-v1`: Background/Bottom/Top/Overlay layers with anchors/exclusive zones. Works on wlroots compositors (Sway, Hyprland, river, niri, Wayfire, labwc), KDE/KWin, COSMIC. Not GNOME/Mutter or X11.
- GNOME: no programmatic AoT; use GNOME Shell extension or XWayland override-redirect fallback. `meta_window_set_above` not exposed; extensions: pip-on-top, auto-on-top, gnome-window-rules. GNOME 49 merged `xdg_toplevel_tag_v1` for compositor-side restore (still not client-settable).
- KDE/KWin: supports `_NET_WM_STATE_ABOVE`/`keepAbove`, `onAllDesktops`; `WindowStaysOnTopHint` works; `layer-shell-qt` available.
- Hyprland/wlroots: overlay via layer-shell; toplevel AoT compositor-specific (window rules/dispatchers).
- X11 (XWayland): `_NET_WM_STATE_ABOVE` client message honored by Mutter/KWin/wlroots XWMs; reliable AoT/sticky via EWMH.
- `ext-foreign-toplevel-list-v1` / `wlr-foreign-toplevel-management`: read-only listing/control on wlroots; GNOME uses DBus introspect; KDE uses plasma-window-management.

## 4. Always-on-top / sticky-notes strategy

Recommended hybrid approach (max compatibility):

1. Primary (wlroots/KDE/COSMIC): use `wlr-layer-shell` Top/Overlay for true sticky overlays; per-monitor surfaces (like waynote).
2. GNOME: XWayland override-redirect fallback for AoT, or ship a minimal GNOME Shell extension that calls `MetaWindow.make_above()`/`stick()`. Document fallback.
3. X11 sessions: EWMH `_NET_WM_STATE_ABOVE` + `_NET_WM_STATE_STICKY`.
4. Persist placement: xdg-session-management where available; otherwise save/restore geometry in app config.
5. Do not abuse layer-shell for AoT on GNOME (unsupported); document compositor limitations.

Existing app precedents: BetterNotes/Karmicore force `GDK_BACKEND=x11` for AoT; xpad/xnots are X11-only; waynote is layer-shell (Hyprland/Sway only).

## 5. GUI framework options (C/C++)

| Framework | Wayland | Notes |
|---|---|---|
| GTK4 | Native, Wayland-first; CSD; fractional scale | No layer-shell/idle-inhibit; RTF must be mapped via GtkTextBuffer tags |
| Qt6 | Native via QPA; client+server; CSD | Heavier; `WindowStaysOnTopHint` works on KWin |
| FLTK 1.4 | Hybrid X11+Wayland, runtime detect, XWayland fallback | Lightest; pure Wayland needs XWayland |
| wxWidgets | GTK3 backend; improved 3.3.x | Missing protocols; Nvidia issues |
| IUP | GTK2/3 | Historical Wayland sizing/CSD bugs |
| Nuklear/ImGui | No native windowing; via GLFW/SDL | Immediate-mode; not for RTF docs |
| EFL | Native via ecore_wl2 | Niche; compositor work ongoing |

Recommendation: GTK4 for native Wayland + ecosystem (or Qt6 if C++/richer widgets). Keep C17 core logic portable; isolate platform UI behind an abstraction layer.

### 5.1 GTK4 deep dive
- `GtkWindow`: toplevel; CSD by default on Wayland; custom titlebar via `gtk_window_set_titlebar()` (HeaderBar) before realize; `set_decorated(false)` for borderless notes. CSS node `window.background[.csd/.solid-csd]`.
- `GtkTextView` + `GtkTextBuffer`: rich-text body; built-in undo/redo/clipboard; CSS node `textview.view`. Use `GtkSourceView` for Markdown/code.
- `GtkTreeView` removed in GTK4 → `GtkColumnView` (virtualized, factory rows, sortable/resizable/reorderable). Ideal for note list/tags.
- `GtkCalendar`: date picker; CSS `calendar.view`.
- `GtkColorButton` deprecated 4.10 → use `GtkColorDialogButton`.
- `GtkSearchEntry`: debounced `search-changed`; CSS `entry.search`; role `search_box`.
- CSD/placement: Wayland forbids apps from positioning their own windows; GTK uses CSD on Wayland. For stay-on-desktop placement use GNOME Shell extension (xnote) or XWayland (`GDK_BACKEND=x11`) for always-on-top (BetterNotes), or layer-shell on wlroots compositors.
- gtk4-layer-shell (wlroots/Sway/KDE/COSMIC only; not GNOME/Mutter, not X11): `gtk_layer_init_for_window()` before realize; set layer, anchors, margins, keyboard mode, exclusive zone. Waynote puts notes on desktop layer with click-through input regions.
- Fractional scaling: GTK4 supports fractional scaling since 4.14 on Wayland; NGL/Vulkan renderers (default Vulkan on Wayland since 4.16) handle 125/150/175% via subpixel glyph positioning. Requires Mesa 24.2+. Override via `GSK_RENDERER`.
- Theming/CSS: load CSS via `GtkCssProvider` at `GTK_STYLE_PROVIDER_PRIORITY_USER`; auto-loads `~/.config/gtk-4.0/gtk.css`. libadwaita themes need that file. Use GTK Inspector (`Ctrl+Shift+D`) to inspect nodes.
- Accessibility: built-in widgets expose AT info; set `accessible-label` on icon-only buttons; custom widgets need `AccessibleRole`, focusable, focus-ring CSS, keyboard activation. Color must not be sole info carrier.
- Performance: virtualized lists (ColumnView/ListView) create widgets only for visible rows. New renderers batch glyph draws; GtkSourceView caches line-number widths. Avoid many columns (horizontal not virtualized). Use dmabuf offload for images.
- Packaging: Flatpak (GNOME runtime) recommended; also .deb/.rpm/.snap/AppImage. Ship desktop entry + AppStream metainfo; meson build. Sandboxed packages can't install GNOME Shell extensions (xnote placement).
- Reference apps: xnote (GNOME Wayland, placement extension), waynote (wlroots layer-shell, Rust), noor-notes (full-featured, SQLCipher, packaging), betternotes (XWayland always-on-top).

## 6. Text editing replacement (RichEdit/TOM → Linux)

- GTK TextView (GtkTextBuffer tags): rich formatting built-in; no RTF import/export (map tags↔RTF); `forward_search`/`backward_search`; autosave manual.
- Qt QTextEdit: WYSIWYG via HTML/Markdown (`toHtml`/`setHtml`, `toMarkdown`), built-in find, undo/redo; no native RTF (convert); heavier dependency.
- Scintilla: code editor; styling ephemeral/not persisted; RTF only as export (SciTE `exportasrtf`); not for RTF docs.
- GtkSourceView: extends TextView with syntax highlighting, regex search/replace, file I/O, printing; still tag-based, no RTF; best for code.
- Custom: needed for full RTF parity; pair a tag buffer with an RTF parser/writer (unrtf, RTFParserKit, etc.).
- Autosave (all): dirty flag + periodic timer + atomic write (temp + rename); see gedit/gtk-edit.

Jotilla uses RTF for formatting; port must either (a) keep RTF via custom parser/writer on GTK, or (b) migrate storage to HTML/Markdown. Recommend (a) for data compatibility, phased.

## 7. Win32 → Linux/POSIX API mapping

### Windowing & message loop
- `RegisterClass`/`WNDCLASS`/`WndProc` → X11 `XCreateWindow` + event mask; Wayland `wl_compositor_create_surface` + `xdg_toplevel`; toolkits `gtk_window_new`/`QWidget`.
- `CreateWindowEx` → `XCreateWindow`/`XCreateSimpleWindow` (X11); `wl_surface`+`xdg_toplevel` (Wayland); `gtk_window_new`/`QWidget`.
- `GetMessage`/`DispatchMessage`/`TranslateMessage` → `XNextEvent` (X11); `wl_display_dispatch` (Wayland); `gtk_main()`/`QApplication::exec()`.
- `DefWindowProc` → default X11 event handling / toolkit default handlers.
- `PostQuitMessage`/`WM_QUIT` → exit event loop / `wl_display_disconnect`.
- `ShowWindow`/`UpdateWindow` → `XMapWindow` (X11); surface commit (Wayland).

### GDI drawing
- `HDC`/`BeginPaint`/`EndPaint` → Cairo `cairo_t` + `cairo_surface_t` (`cairo_xlib_surface_create` for X11, image surface for Wayland).
- GDI primitives → Cairo `cairo_rectangle`/`cairo_line_to`/`cairo_show_text`; Pango for complex text.
- `BitBlt`/DIB → Cairo image surfaces / `gdk_pixbuf`.
- Toolkits: GTK4 uses GSK (Vulkan/GL) renderers; Cairo reduced role.

### File I/O
- `CreateFile` → `open(2)` (`O_CREAT`/`O_RDWR`); `fopen`.
- `ReadFile`/`WriteFile` → `read(2)`/`write(2)`; `fread`/`fwrite`.
- `CloseHandle` (file) → `close(2)`/`fclose`.
- `SetFilePointer` → `lseek(2)`/`fseek`.
- `GetFileSize` → `stat(2)` (`st_size`) / `fseek`+`ftell`.
- `FlushFileBuffers` → `fsync(2)`/`fflush`.
- `DeleteFile` → `unlink(2)`/`remove`.
- `MoveFile` → `rename(2)`.
- `CreateDirectory` → `mkdir(2)`.
- `FindFirstFile`/`FindNextFile` → `opendir`/`readdir`/`closedir`.
- `GetLastError` → `errno`.

### Timers
- `SetTimer` (window/TimerProc) → POSIX `timer_create`/`timer_settime` (signal-based, awkward); Linux `timerfd_create`/`timerfd_settime` (fd-based, composes with `poll`); GLib `g_timeout_add`; Qt `QTimer`; GTK `g_timeout_add`.
- `QueryPerformanceCounter`/`QPC` → `clock_gettime(CLOCK_MONOTONIC)` (nanosecond); `CLOCK_MONOTONIC_RAW` for raw.
- `GetTickCount` → `clock_gettime(CLOCK_MONOTONIC)` ms.
- `KillTimer` → `timer_delete`/`timerfd` close/`g_source_remove`.

### Monitor / DPI
- `MonitorFromWindow` → X11: `XRRGetMonitors`/`Xinerama`; Wayland: `wl_output`/`xdg_output`; GTK: `gdk_display_get_monitor_at_window`.
- `GetDpiForWindow`/`GetDpiForMonitor` → X11: `Xft.dpi` / `XRR` EDID; Wayland: `wl_output.scale` + `wp_fractional_scale_v1`; GDK: `gdk_monitor_get_scale_factor`/`gdk_surface_get_scale`.
- DPI awareness (`SetProcessDpiAwareness`) → toolkit/GDK scaling; env `GDK_SCALE`, `GDK_DPI_SCALE`, `QT_SCALE_FACTOR`.
- Fractional scaling: Wayland `wp_fractional_scale_v1`; X11 limited to integer server scaling.

### Registry / config
- `RegOpenKeyEx`/`RegQueryValueEx`/`RegSetValueEx` → no central registry; text/config files under `/etc` (system) and `~/.config` (user) per XDG Base Directory Spec; GNOME: GSettings/dconf (binary gvdb, `~/.config/dconf/user`).
- GSettings API (GLib/GIO) → portable settings abstraction; on Linux uses dconf backend; schemas compiled with `glib-compile-schemas`.
- GLib helpers: `g_get_user_config_dir()` → `$XDG_CONFIG_HOME` (default `~/.config`).
- Other: Elektra (registry-like), KConfig (KDE), Xfconf (Xfce).

### Shell / folders / launch
- `SHGetFolderPath`/`SHGetKnownFolderPath` (CSIDL) → XDG dirs: `~/.config` (`XDG_CONFIG_HOME`), `~/.local/share` (`XDG_DATA_HOME`), `~/.cache` (`XDG_CACHE_HOME`); GLib `g_get_user_config_dir`/`g_get_user_data_dir`/`g_get_user_cache_dir`.
- `ShellExecute`/`ShellExecuteEx` → `xdg-open` (freedesktop); `gio open`; DE-specific openers (`gnome-open`, `kde-open`).
- Launching executables/URLs → `fork`+`execve`/`posix_spawn`; `g_spawn_async`; Qt `QProcess`.
- `SHBrowseForFolder`/common dialogs → GTK `GtkFileChooser`/`GtkFileDialog`; Qt `QFileDialog`.

### DWM (compositor)
- DWM composition → X11 window manager (server-side decorations); Wayland compositor (Mutter/KWin/Sway) with client-side decorations (CSD) optional.
- `DwmExtendFrameIntoClientArea` → X11: `_NET_WM` state / compositor ARGB; Wayland: `wl_surface` alpha + compositor protocols.
- `DwmSetWindowAttribute` (rounded corners, backdrop) → compositor-specific (KWin/Mutter) or `xdg-decoration`/`wp_viewporter`; no universal equivalent.
- `DwmEnableBlurBehindWindow` → compositor blur protocols (e.g. `kde-blur`); not standardized.
- `IsCompositionActive` → always true on modern Wayland; X11: check compositor running.

### Common controls (TreeView/ListView)
- `TreeView` (Win32 `SysTreeView32`) → GTK3 `GtkTreeView` (deprecated 4.10); GTK4 `GtkListView`/`GtkColumnView` + `GtkTreeListModel`/`GtkTreeExpander`.
- `ListView` (Win32 `SysListView32`) → GTK4 `GtkListView` (linear) / `GtkColumnView` (tabular) / `GtkGridView` (grid).
- Tree model (`HTREEITEM`, `TVM_INSERTITEM`) → `GListModel`/`GtkTreeListModel`; selection → `GtkSelectionModel`.
- Other common controls: Button/Checkbox/Combo → `GtkButton`/`GtkCheckButton`/`GtkComboBox`/`GtkDropDown`; ProgressBar → `GtkProgressBar`; TrackBar → `GtkScale`.

### uxtheme / visual styles
- `OpenThemeData`/`DrawThemeBackground`/`DrawThemeText` → GTK CSS theming (`gtk.css`, `settings.ini` `gtk-theme-name`); Qt stylesheets/QStyle. No direct uxtheme equivalent; GTK has built-in `win32` theme on Windows using uxtheme.
- `IsAppThemed`/`IsCompositionActive` → GTK theme detection via `GtkSettings`; Qt `QStyleFactory`.
- Buffered paint/animations (`BeginBufferedPaint`) → Cairo double-buffering / GTK rendering pipeline.

### Libraries summary
- Low-level windowing: `libX11` (Xlib), `libxcb`, `libwayland-client`, `libwayland-cursor`, `libxkbcommon`.
- Toolkit/abstraction: `GDK` (X11/Wayland/Win32 backends), `GTK` (3/4), `Qt` (5/6), `SDL3`, `winit` (Rust).
- System: `glib`/`glib-gio` (GSettings, file utils, main loop, timers), `cairo`/`pango` (2D/text), `dconf` (settings backend), `libnotify` (tray/notifications), `xdg-utils` (shell).
- Porting aids: WineLib, NanoWin32 (non-GUI subset), GTK/Qt cross-platform.

### Notes
- Win32 is synchronous message-driven; X11/Wayland are async client-server with compositors. Toolkits (GTK/Qt) abstract the difference.
- No single "Win32 equivalent" on Linux; choose toolkit (GTK/Qt) or low-level (X11/Wayland). WineLib for minimal-change ports.

## 8. Wayland protocols deep dive

- `xdg-shell`: toplevel role; `configure` (size + state array), `close` request; states: maximized/fullscreen/resizing/activated/tiled_*. Universal (GNOME✓ KDE✓ Hyprland✓ Sway✓).
- `wlr-layer-shell`: surfaces on layers (background/bottom/top/overlay); anchors (bitfield), exclusive zone (reserve space / -1 stretch), keyboard interactivity (none/exclusive/on-demand). wlroots-style; GNOME✗ KDE✗ Hyprland✓ Sway✓.
- `xdg-session-management`: persist/restore toplevel WM state across restarts (add/restore_toplevel, session id). GNOME✓ (Mutter 51, off by default until 48) KDE✓ (KWin 6.7) Hyprland✗ Sway✗.
- `ext-foreign-toplevel-list`: handles for toplevels from other clients (minimal; title/app_id via handle). GNOME✗ KDE✗ Hyprland✓ Sway✓.
- `wlr-foreign-toplevel-management`: list/control opened apps (maximize/minimize/fullscreen/activate/close) for docks/taskbars. GNOME✗ KDE✗ Hyprland✓ Sway✓.
- `xdg-toplevel-tag`: stable tag + description per toplevel for compositor rules/state restore. GNOME✓ KDE✓ Hyprland✓ Sway✗.
- Clipboard/primary selection: core `wl_data_device` clipboard universal (all four ✓); primary selection (`zwp_primary_selection`) also all four ✓. Clipboard-manager control: `ext-data-control-v1` GNOME✗ KDE✓ Hyprland✓ Sway✓; `wlr-data-control` GNOME✗ KDE✗ Hyprland✓ Sway✓.
- Drag-and-drop: core `wl_data_device` DnD (offers/accept/receive/actions) universal (all four ✓). Window-move-while-dragging: `xdg-toplevel-drag-v1` only KDE✓ (+Jay); others✗.
- Fractional scale: `wp_fractional_scale_v1` suggests fractional scale (numerator/120); paired with viewporter. All four ✓.
- Viewporter: `wp_viewport` crop/scale of surface content (source rect + destination size). Universal (all four ✓; GameScope only exception).
- Idle inhibit: `zwp_idle_inhibit_manager_v1`: client prevents idle while surface visible. All four ✓. (Idle-notify for daemons is `ext-idle-notify-v1`: GNOME✗ KDE✓ Hyprland✓ Sway✓.)

## 9. Compositor-specific integration

### GNOME
- No client-side "sticky/all-workspaces" API on Wayland; `gtk_window_stick()` only works under X11. Prefer a GNOME Shell extension (panel menu, local JSON). For a standalone app, force XWayland (`GDK_BACKEND=x11`) so X11 WM hints (`_NET_WM_STATE_STICKY/ABOVE`) apply.
- Legacy gnome-applets stickynotes used gsettings (`org.gnome.stickynotes`-style keys: `sticky`, `locked`, `date-format`, default color/font).
- Extensions: maoschanz/notes (extensions.gnome.org/extension/1357/notes), suvanbanerjee/sticky-notes.
- Docs: https://gjs.guide/extensions/ · https://extensions.gnome.org · Discourse thread on gtk_window_stick: https://discourse.gnome.org/t/is-gtk-sticky-window-gtk-window-stick-supported-in-gnome/8372

### KDE Plasma
- KWin Window Rules (System Settings → Window Management → Window Rules, or `~/.config/kwinrulesrc`): match by `WM_CLASS`/title/role; set Keep Above, Desktop=all, Skip Pager/Taskbar. Docs include a KNotes "suppress from pager" example.
- Programmatic: `KWin::Window::keepAbove`, `onAllDesktops` (Q_PROPERTY).
- For overlay UI use LayerShellQt (Qt6) to place on `Top`/`Overlay` layers instead of normal windows.
- Docs: https://docs.kde.org/stable_kf6/en/kwin/kcontrol/windowspecific/ · https://userbase.kde.org/KWin_Rules_Examples · https://develop.kde.org/docs/plasma/

Example `~/.config/kwinrulesrc` (manual):
```ini
[StickyNotes]
Description=Sticky notes
clientmachine=localhost
clientmachine_match=0
position=100,100
positionrule=3
size=320,240
sizerule=3
desktops=4294967295
desktopsrule=3
above=true
aboverule=3
skippager=true
skippagerrule=3
skiptaskbar=true
skiptaskbarrule=3
wmclass=stickynotes
wmclassmatch=1
```

### Hyprland (`~/.config/hypr/hyprland.conf`)
- Window rules: `float`, `pin` (all workspaces; floating only), `move`, `size`, `nofocus`, `noblur`, `opacity`.
- Layer surfaces (waybar/rofi-style): `layerrule` by namespace from `hyprctl layers` — `blur`, `noanim`, `dim_around`, `above_lock`, `order`, `ignore_alpha`.
```ini
windowrule = float, match:class ^(stickynotes)$
windowrule = pin, match:class ^(stickynotes)$
windowrule = move 100 100, match:class ^(stickynotes)$
windowrule = size 320 240, match:class ^(stickynotes)$
windowrule = nofocus, match:class ^(stickynotes)$
layerrule = blur on, match:namespace stickynotes
layerrule = noanim on, match:namespace stickynotes
```
- Docs: https://wiki.hypr.land/Configuring/Window-Rules/ · https://wiki.hypr.land/Configuring/Layer-Rules/

### Sway (`~/.config/sway/config`)
- `for_window [criteria] floating enable, sticky enable, resize set W H, move position X Y`.
- Criteria: `app_id`, `class`, `title`, `shell` (xwayland/xdg_shell), `window_type`, `window_role`, `instance`.
- `sticky enable` shows floating window on all workspaces of current output.
- Layer-shell surfaces are compositor-managed; swayfx adds `layer_effects "namespace" { blur enable; ... }`.
```sway
for_window [app_id="^stickynotes$"] floating enable, sticky enable, border pixel 1, resize set 320 240, move position 100 100
for_window [shell="xwayland"] title_format "[X] %title"
```
- Docs: https://man.archlinux.org/man/sway.5 · https://wiki.archlinux.org/title/Sway

General: prefer layer-shell (`zwlr_layer_shell_v1`) for true overlay notes; fall back to floating+sticky window rules when the app is a normal window.

## 10. XDG paths, desktop entries, icons, AppStream

### XDG Base Directory
- `$XDG_DATA_HOME` (default `~/.local/share`), `$XDG_CONFIG_HOME` (`~/.config`), `$XDG_CACHE_HOME` (`~/.cache`), `$XDG_STATE_HOME` (`~/.local/state`), `$XDG_RUNTIME_DIR` (user-owned, mode 0700).
- Search paths: `$XDG_DATA_DIRS` (default `/usr/local/share:/usr/share`), `$XDG_CONFIG_DIRS` (default `/etc/xdg`); colon-separated, first = highest precedence.
- All paths must be absolute; relative paths ignored.

### Desktop Entry (.desktop)
- Files in `$XDG_DATA_DIRS/applications/`; filename (minus `.desktop`) must be valid D-Bus well-known name (reverse DNS, CamelCase app name), e.g. `org.example.FooViewer.desktop`.
- Desktop file ID = path relative to `$XDG_DATA_DIRS` applications dir with `/`→`-`.
- Recognized keys: `Type`, `Name`, `Exec` (required unless `DBusActivatable=true`), `Icon`, `MimeType`, `Categories`, `TryExec`, `Terminal`, `StartupNotify`, `StartupWMClass`, `NoDisplay`, `Hidden`, `OnlyShowIn`/`NotShowIn`, `DBusActivatable`, `Actions`, `Keywords`, `Implements`, `URL`.
- `Exec` supports field codes (`%f`, `%F`, `%u`, `%U`, `%i`, `%c`, `%k`).

### Icon themes / sizes
- Base dirs: `$XDG_DATA_DIRS/icons` (and `$XDG_DATA_HOME/icons`); always fall back to `hicolor` theme.
- Theme dirs like `48x48/apps`, `scalable/apps`, `48x48@2/apps` (HiDPI scale); `index.theme` declares `Directories`, `Inherits`, per-dir `Size`, `Scale`, `Context`, `Type` (Fixed/Scalable/Threshold), `MinSize`/`MaxSize`/`Threshold`.
- Apps install icons to `hicolor/<size>x<size>/apps/<name>.png` (and `scalable/apps/<name>.svg`).

### AppStream metainfo XML
- Files in `/usr/share/metainfo/%{id}.metainfo.xml` (legacy `/usr/share/appdata/`).
- Root `<component type="desktop-application">`; required `<id>`, `<name>`, `<summary>`, `<metadata_license>`; `<launchable type="desktop-id">` links `.desktop` ID; also `<description>`, `<icon>`, `<categories>`, `<url>`, `<screenshots>`, `<releases>`, `<provides>`, `<content_rating>`, `<developer>`.
- IDs should be reverse-DNS, lowercase; metainfo overrides `.desktop` data.

### Shared MIME-info
- Packages install one XML file into `$XDG_DATA_DIRS/mime/packages/`, then run `update-mime-database <mime-dir>`.
- Source XML root `<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">` with `<mime-type type="...">` containing `<glob>`, `<magic>`, `<alias>`, `<sub-class-of>`, `<comment>`, `<icon>`, `<generic-icon>`, `<root-XML>`, `<treemagic>`.
- Generated files: `globs2`, `magic`, `subclasses`, `aliases`, `icons`, `generic-icons`, `XMLnamespaces`, `MEDIA/SUBTYPE.xml`, `mime.cache`.

### Autostart
- `.desktop` files in `$XDG_CONFIG_DIRS/autostart` (user: `~/.config/autostart`); most-important dir wins; `Hidden=true` disables.
- `OnlyShowIn`/`NotShowIn` and `TryExec` apply; user can override system entry by same-named file with `Hidden=true`.

### D-Bus service files
- `.service` files; session bus searches `$XDG_DATA_DIRS/dbus-1/services`; system bus searches `/etc/dbus-1/system-services`, `/run/dbus-1/system-services`, `/usr/local/share/dbus-1/system-services`, `/usr/share/dbus-1/system-services`, `/lib/dbus-1/system-services`.
- `[D-BUS Service]` group requires `Name` (well-known bus name) and `Exec`; system services also require `User`; optional `SystemdService` (systemd activation), `AssumedAppArmorLabel`.
- Filename must be `<well-known-name>.service` on system bus; session bus recommended. Higher-priority directory wins on conflicts.

## 11. Build & packaging

- CMake (preferred over raw Makefile): target-based, out-of-source `cmake -S . -B build`, `CMakePresets.json`, `install()` rules, CPack. https://thelinuxcode.com/how-to-use-cmake-for-linux-a-practical-productionready-guide
- AppImage: relocatable (no hardcoded `/usr`), build on oldest target base (Ubuntu 18.04/20.04), bundle non-base libs, validate desktop file + AppStream metainfo; AppDir + `appimagetool`/`linuxdeploy`. https://docs.appimage.org/reference/best-practices.html
- Flatpak: manifest with reverse-DNS id, runtime+SDK, `command`, `finish-args`, cleanup; export `.desktop`/icon/metainfo named with app id; `flatpak-builder`. https://docs.flatpak.org/en/latest/manifests.html
- deb: `cpack -G DEB` or `dpkg-buildpackage`; set `CPACK_DEBIAN_PACKAGE_DEPENDS`, install to `/usr`. https://www.educative.io/courses/modern-cmake-for-cpp/packaging-with-cpack
- rpm: `cpack -G RPM` or `rpmbuild`; declare `BuildRequires`; RPM auto-detects ELF deps. https://docs.fedoraproject.org/en/packaging-guidelines/C_and_C++/
- CI: GitHub Actions matrix (os/build_type), ccache, `ctest`, CPack per platform, upload artifacts, release on tags. https://www.studyplan.dev/cmake/cmake-build-servers

## 12. CI / testing

- xvfb-run headless GUI smoke: `xvfb-run -a --server-args="-screen 0 1920x1080x24"`; install GTK/WebKit libs + xvfb.
- Unit tests: `ctest` (CMake/GoogleTest/Catch2) or zig; fast, every push, matrix.
- Integration/UI tests: Playwright/WebDriver under xvfb-run; health checks, accessibility snapshots, screenshot goldens; upload artifacts on failure.
- clang-format: `clang-format --dry-run --Werror -style=file`.
- clang-tidy: needs `compile_commands.json` (`CMAKE_EXPORT_COMPILE_COMMANDS=ON`); `run-clang-tidy`, filter project sources, `--warnings-as-errors='*'`.
- Sanitizers (ASan/UBSan): separate build dir with `-fsanitize=address,undefined -fno-omit-frame-pointer`; `ASAN_OPTIONS`/`UBSAN_OPTIONS` with `halt_on_error=1`, `detect_leaks=1`; run `ctest`.
- valgrind: `--error-exitcode=1 --leak-check=full --track-origins=yes`; Linux-only; suppression files.
- Reference templates: 219-design/qt-qml-project-template-with-ci, jballoffet/c-template.

## 13. Porting roadmap (phases)

Phase 0 — Setup
- Add CMake build (Linux first), keep build.cmd for Windows.
- Introduce platform abstraction: `platform_win.c` (existing), `platform_linux.c` (new), `platform_mac.c`.
- Move storage/config paths to XDG (`$XDG_DATA_HOME/jotilla`, `$XDG_CONFIG_HOME`).

Phase 1 — Core portability
- Replace Win32 file I/O/timers with POSIX (`inotify`/poll for watchers, `timerfd`/glib timers).
- Replace registry (setup) with XDG desktop entries / `.desktop` + autostart.
- Replace monitor/DPI with GDK/Qt screen APIs.

Phase 2 — UI
- Implement GTK4 (or Qt6) main window, note editor, archive, calendar, palette picker, search.
- Map RTF↔GtkTextBuffer tags (or migrate storage).
- Implement autosave + atomic writes.

Phase 3 — Window behavior
- Implement AoT/sticky per compositor strategy (layer-shell on wlroots/KDE; XWayland/EWMH on GNOME/X11).
- Implement on-all-workspaces where supported.
- Persist/restore geometry and state.

Phase 4 — Packaging & CI
- AppImage + Flatpak + deb/rpm via CPack.
- GitHub Actions: build matrix, xvfb smoke, clang-format/tidy, sanitizers, valgrind.
- Add `.desktop`, icons, AppStream metainfo.

Phase 5 — Validation
- Test on GNOME (Wayland + XWayland), KDE Plasma, Hyprland, Sway.
- Document limitations (GNOME native AoT unavailable; use extension/XWayland).

## 14. Risks / limitations

- GNOME has no client-requested always-on-top on Wayland; must use XWayland or extension.
- Layer-shell is not available on GNOME/Mutter; apps relying on it exclude GNOME.
- RTF parity requires custom parser/writer or storage migration.
- Wayland compositor differences (input, clipboard, drag-and-drop) require per-backend handling.
- No native Linux build currently; all Win32 code must be abstracted.
- Flatpak sandboxing prevents installing GNOME Shell extensions (placement).

## 15. Accessibility, i18n, security notes

- Accessibility: AT-SPI via GTK built-ins; set `accessible-label` on icon-only buttons; color must not be sole info carrier; focus rings via CSS.
- i18n: use gettext/mo or Qt linguist; date formatting locale-aware; RTL via GTK/Qt layout direction.
- Security: Flatpak sandbox (`finish-args` minimal permissions), seccomp default; no network needed (file-based); validate archive paths to avoid traversal; atomic writes prevent corruption.

## 16. Release process & versioning

- Semantic versioning; tag `vX.Y.Z` triggers release workflow.
- Artifacts: AppImage, Flatpak bundle, .deb, .rpm, tarball.
- Checksums + GPG signing; AppStream release notes; update desktop file `StartupWMClass`.
- Rollback: keep previous AppImage; versioned config migration (if any).

## 17. References

- Jotilla: https://github.com/GarauGarau/Jotilla · https://garaugarau.github.io/Jotilla/
- Wayland layer-shell: https://wayland.app/protocols/wlr-layer-shell-unstable-v1
- waynote (layer-shell sticky notes): https://github.com/mryll/waynote
- BetterNotes (XWayland for AoT): https://github.com/0xpaperhead/betternotes
- GTK4 text widget: https://docs.gtk.org/gtk4/section-text-widget.html
- Qt Wayland: https://doc.qt.io/qt-6/wayland-and-qt.html
- CMake packaging: https://www.educative.io/courses/modern-cmake-for-cpp/packaging-with-cpack
- AppImage best practices: https://docs.appimage.org/reference/best-practices.html
- Flatpak manifests: https://docs.flatpak.org/en/latest/manifests.html
- GNOME AoT discourse: https://discourse.gnome.org/t/any-way-to-set-window-always-on-top-programmatically/31579
- EWMH spec: https://specifications.freedesktop.org/wm/latest-single/
- Hyprland window rules: https://wiki.hypr.land/Configuring/Basics/Window-Rules
- XDG base dir: https://specifications.freedesktop.org/basedir-spec/latest/
- Desktop entry spec: https://specifications.freedesktop.org/desktop-entry-spec/latest/
- Icon theme spec: https://specifications.freedesktop.org/icon-theme-spec/latest/
- AppStream: https://www.freedesktop.org/software/appstream/docs/
- Shared MIME-info: https://specifications.freedesktop.org/shared-mime-info/latest/
- Autostart spec: https://specifications.freedesktop.org/autostart-spec/latest/
- D-Bus spec: https://dbus.freedesktop.org/doc/dbus-specification.html
- GTK4 column view: https://docs.gtk.org/gtk4/class.ColumnView.html
- gtk4-layer-shell: https://github.com/wmww/gtk4-layer-shell
- noor-notes: https://github.com/saamaamr/noor-notes
- xnote: https://github.com/spencernorton/xnote

## 18. Detailed file mapping (porting)

| Windows file | Linux target | Action |
|---|---|---|
| `src/jotilla.c` | `src/jotilla.c` + `src/platform_linux.c` | Split Win32 UI from core logic; keep note lifecycle core |
| `src/archive.c` | `src/archive.c` + GTK ColumnView | Rewrite UI widgets; keep search/filter logic |
| `src/calendar.c` | `src/calendar.c` + GtkCalendar | Rewrite rendering; keep date logic |
| `src/search.c` | `src/search.c` (mostly portable) | Replace Win32 threads/events with pthread/glib; keep KMP |
| `src/tags.c` | `src/tags.c` (mostly portable) | Replace INI write via GKeyFile; keep normalization |
| `src/location.c` | `src/location.c` | Replace registry/paths with XDG; keep migration |
| `src/date.c` | `src/date.c` | Fully portable (no changes) |
| `src/colors.c` | `src/colors.c` | Fully portable |
| `src/metadata.c` | `src/metadata.c` or GKeyFile | Replace INI cursor with GKeyFile parser |
| `src/setup.c` | `packaging/` + install scripts | Replace with CMake install + .desktop; no registry |
| `src/palette_art.c` | `src/palette_art.c` + Cairo | Rewrite GDI drawing with Cairo |
| `src/palette_picker.c` | `src/palette_picker.c` + GTK | Rewrite popup UI |
| `build.cmd` | `CMakeLists.txt` + `CMakePresets.json` | New build system; keep build.cmd for Windows |

## 19. Migration checklist

- [ ] Add `CMakeLists.txt` with Linux target; keep MSVC build.cmd.
- [ ] Define platform abstraction headers (`platform.h`).
- [ ] Implement `platform_linux.c`: file I/O, timers, paths, DPI, monitor.
- [ ] Move config to XDG paths; add `portable.flag` parity.
- [ ] Implement GTK4 note window (borderless, CSD, opacity).
- [ ] Implement GtkTextView + RTF tag mapping.
- [ ] Implement archive view with GtkColumnView (list/calendar/tags).
- [ ] Implement calendar picker (GtkCalendar) + year view.
- [ ] Implement palette picker popup.
- [ ] Implement search worker (pthread + GLib main loop integration).
- [ ] Implement tags normalization + catalog (portable).
- [ ] Implement window behavior per compositor (layer-shell + fallback).
- [ ] Add on-all-workspaces where supported.
- [ ] Persist/restore geometry and state (XDG config).
- [ ] Add `.desktop`, icons (hicolor), AppStream metainfo.
- [ ] Add autostart desktop entry (optional).
- [ ] Package: AppImage, Flatpak, deb, rpm.
- [ ] CI: build matrix, xvfb smoke, clang-format, clang-tidy, ASan/UBSan, valgrind.
- [ ] Docs: install, build, limitations, compositor configs.
- [ ] Test on GNOME (Wayland + XWayland), KDE, Hyprland, Sway.

## 20. Performance targets (Linux)

- Cold start < 200 ms with 1000 notes.
- Note open < 50 ms (text + RTF load).
- Autosave debounce 400 ms; atomic write < 10 ms typical.
- Search: stream 8 KB chunks; highlight offsets only; no full-text storage.
- Memory: < 60 MB RSS idle with 50 open notes.
- Rendering: 60 fps palette picker animation; no frame drops on archive scroll (virtualized).
- Fractional scaling: crisp text at 125/150/175% (GTK4 + Mesa 24.2+).

## 21. Open questions

1. GUI toolkit final choice (GTK4 vs Qt6) — decide after prototype of note window + text editing.
2. RTF strategy: custom parser/writer vs migrate storage to HTML/Markdown (data compatibility vs simplicity).
3. GNOME always-on-top: ship XWayland default vs optional Shell extension; document tradeoffs.
4. Layer-shell vs normal windows: decide per compositor; may need both code paths.
5. Packaging priority: AppImage first (simplest) vs Flatpak first (sandboxed, store-ready).
6. Test framework: GoogleTest vs Catch2 vs custom (match existing C test harness style).
7. i18n approach: gettext vs Qt linguist (depends on toolkit).

## 22. Glossary

- AoT: always-on-top.
- CSD: client-side decorations.
- EWMH: Extended Window Manager Hints (X11).
- layer-shell: Wayland protocol for overlay surfaces.
- RTF: Rich Text Format.
- XDG: freedesktop.org base directory specification.
- wlroots: Wayland compositor library (Sway, Hyprland, etc.).

## 23. FAQ

Q: Will it work on GNOME Wayland natively with always-on-top?
A: No native client-requested AoT on GNOME Wayland; use XWayland fallback or a Shell extension.

Q: Can I keep my existing notes archive?
A: Yes, storage format (TXT/RTF/INI) remains compatible; only paths change to XDG defaults.

Q: Is a database required?
A: No, file-based as before.

Q: Which compositors are fully supported?
A: wlroots (Sway/Hyprland/river/niri), KDE Plasma, COSMIC via layer-shell; GNOME and X11 via fallback.

Q: How to build from source on Linux?
A: `cmake -S . -B build && cmake --build build` (after Phase 0).
