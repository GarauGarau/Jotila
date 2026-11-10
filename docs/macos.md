# Native macOS preview

This branch contains the native macOS implementation of Jotilla, built with
Objective-C/AppKit and the shared C date and palette modules. The note editor,
archive cards, color wheel, tags, calendar and archive transfer are implemented
natively. See [feature coverage and local validation](macos-validation.md).

This remains a development build. Native build, storage, UI and launch tests run
on both Apple Silicon and Intel with macOS 15 in GitHub Actions. Installed-login
sessions, physical display changes and clean-machine installation still need
manual release validation.

[Download the universal Mac app and installer](https://github.com/GarauGarau/Jotilla/releases/tag/v1.10.0).

## Build on a Mac

Target: macOS 13 or later, Intel and Apple Silicon. Install Apple's Command Line
Tools (`xcode-select --install`), then from the repository root:

```sh
make
make test
make ui-test
make smoke-test
make analyze
make profile
make run
```

`make` builds for the current CPU. No third-party runtime libraries, package
manager or Xcode project are needed. The build uses C17, Objective-C with ARC,
AppKit, Carbon hotkeys and ServiceManagement. Warnings are errors. `make test`
needs a logged-in graphical Mac session; it uses an isolated temporary archive.
`make ui-test` exercises actual AppKit controls and writes screenshots under
`build/macos/ui-snapshots`. `make profile` adds an isolated 10,000-note workload.
The smoke test launches the real executable with an isolated archive and exits.
It does not register login startup or use the installed app's settings.

```sh
make package
```

This builds both architectures and produces local downloads in `dist/macos`:

- `Jotilla-portable-universal.zip`
- `Jotilla-Setup-universal.pkg`
- `SHA256SUMS.txt`

The default app signature is ad hoc, for development. For Developer ID signing:

```sh
make package SIGN_IDENTITY='Developer ID Application: …' \
  INSTALLER_IDENTITY='Developer ID Installer: …'
```

Packaging never uploads or publishes anything. Public distribution also needs
Apple notarization and validation on a clean Mac. The PKG installs only
`/Applications/Jotilla.app`; there are no install scripts that touch notes.
Disable Start at login before removing the installed app. Moving the app to
Trash leaves its archive intact.

## Portable and installed data

Installed default: `~/Library/Application Support/Jotilla/notes`. An explicitly
chosen archive is remembered in `~/Library/Application Support/Jotilla/archive.path`.
An unavailable remembered folder stops startup instead of creating a replacement.

Upgrades from Jotila keep the existing profile folder and archive in place.
The old `.jotilaarchive` extension remains supported alongside `.jotillaarchive`.
System bundle identifiers, the menu-bar preference and the archive lock filename
retain their original spelling for compatibility; the app's display name and
downloads use Jotilla. Quit the previous app before installing the update. If
`Jotila.app` remains in Applications, remove that older app after opening Jotilla
and checking your notes; keep the archive folder. Recheck Start at login after
changing the app's location.

The portable bundle has its own application identity and asks for the archive
directory each launch. Select the included `Notes.jotillaarchive`, an ordinary
folder displayed as a package by Finder, or another existing folder. It writes
no archive location into the user's preferences and cannot enable Start at login.
This avoids relying on relative paths during macOS App Translocation. The OS may
still maintain its normal recent-file/security records; portable does not mean
the operating system leaves no traces.

An explicit path can also be supplied:

```sh
build/macos/arm64/Jotilla.app/Contents/MacOS/Jotilla \
  --portable --archive '/Volumes/USB/Notes.jotillaarchive'
```

Use `x86_64` instead of `arm64` on Intel. Keep data outside the signed `.app`.
Quit before moving the archive. The Archive dialog offers Open or create archive and Move current archive.
Moving requires an empty, separate destination. The destination is locked,
copies are flushed and verified, and the saved location changes before verified
originals are removed. A failed copy leaves the current archive active. Files
changed externally during cleanup are kept in the old folder.

## Architecture and persistence

`src/date.c`, `src/colors.c` and the immutable palette catalog are shared C.
The Windows side of `colors.h` retains its original types and operations; the
Mac side preserves COLORREF byte ordering without importing Windows headers.
The AppKit frontend is under `src/macos`:

- `Store`: bounded metadata decoding, archive lock, TXT/RTF and atomic replacement.
- `Note`: native editor, note window, formatting and debounced persistence.
- `Search`: bounded streaming UTF-8 search with cancellation and tag prefiltering.
- `Browser`: virtualized note cards, cached previews, filters and search queue.
- `Calendar`: annual view, hit testing, keyboard and accessibility navigation.
- `Palette`: vector wheel, short Core Animation transitions and favorite controls.
- `Tags`: native token editor and bounded completion suggestions.
- `App`: lifecycle, menus, archive switching, hotkey and login item integration.

TXT remains authoritative UTF-8. Explicit titles retain the Windows `#title`
header, CRLF output and `has_title` metadata. RTF is accepted only if its text
matches TXT after newline normalization. Metadata writes use UTF-16LE with a BOM
and preserve unknown sections and keys (comments/order are not preserved).
Legacy ANSI metadata uses CP1252 as a fallback; other legacy Windows code pages
need conversion to Unicode on Windows before import.

Each write uses a sibling temporary file, flush and rename. RTF is replaced
before TXT so an interrupted pair cannot resurrect an old body. This is not a
transaction across all archive files. A failed save keeps the note open. External
TXT changes and metadata replacement block stale writes; export an RTF copy of
unsaved edits, then use Reload and explicitly discard them if necessary. The
archive lock coordinates Mac instances; it is not a synchronization protocol
with Windows, cloud clients or other editors.

Clean saves do no work. Window geometry changes write metadata only. Formatting
changes write RTF without rewriting TXT. Body edits compare the record's metadata
with its saved snapshot, avoiding full INI serialization when metadata is unchanged.
The visible archive updates that one card for ordinary edits; query or tag changes
trigger a new filtered snapshot.

Search uses a cancellable serial operation queue, 8 KiB chunks, bounded overlap
and short contextual excerpts. Tag-only filters use metadata synchronously. A
separate serial preview queue reads at most 2 KiB for requested visible cards;
its cache is capped at 256 entries / 1 MiB. Archive changes invalidate the cache
with a generation key. Hidden explorers schedule no searches or preview reads.
Calendar hover invalidates only the affected cells; month/day labels and palette
paths are retained. Color transitions last 120 ms, respect Reduce Motion and use
Core Animation without a polling timer. See the validation report for measured
10,000-note results.

Mac geometry uses `mac_x/y/width/height` keys so it does not overwrite Windows
coordinates. Note state, palette indices, tags and calendar dates are shared.

## Native controls

Global new note: **Control+Option+Command+N**; **Control+Option+plus** is also
registered. Inside the app: **Command+N** new note, **Command+E** explorer,
**Command+F** search, **Command+R** reload, **Command+T** tags, **Command+D** day,
**Command+P** color wheel and **Command+S** save. F5 also reloads from the editor
and archive. Standard Command shortcuts handle clipboard and undo.
Command+B/I/U and Command+Shift+X format selected text or the insertion style.
Command+plus/minus, Command+wheel and pinch resize the selection or the note.

Notes retain the bordered card, divided title bar, left calendar button and
fold/close buttons. Double-click the bar to rename. Closing archives the note.
Notes are saved when either the title or body contains non-whitespace text.
Legacy empty TXT files are excluded on reload without deleting the originals;
clearing a saved note's body and title removes it from the archive, with files recoverable
in Trash. Undo can save the restored note again. Shift+Enter promotes a first-line
`#title` in one undo group, or starts a checklist after an isolated `*`. Enter
continues checklist items, Backspace removes their marker, and clicking a box
toggles both the marker and the task's strikethrough. Pasted text is plain text.

Right-click text to open the 80-palette wheel. A selection changes foreground
color; otherwise it changes the note and session palette. Left/Right browse,
Up/Down/Tab select a wedge, Enter applies, F toggles the favorite, and Escape
cancels. The three original palettes remain in the title-bar context menu,
alongside note sizes, opacity, tags, export, retry and folder access. Popular
palettes draw new colors from shuffled bags without repeating a distinct color
within one cycle. Background changes preserve manual foreground colors.

The explorer shows colored cards with lazy previews, creation-date and tag
filters, full-text search with highlighted excerpts, multi-selection and Trash.
Click a tag on a card to filter it. The annual calendar replaces the card area,
fits the available space and scrolls on smaller windows. Circled days open their
note or a compact title list with arrows, Page Up/Down, Home/End, Enter and Escape.
The year can be typed directly, including in the note's native date picker.
The explorer uses the native calendar symbol; the menu bar uses a vector Jotilla
template icon that adapts to light and dark backgrounds.

New notes use native text layout to reject text/font growth or shrinking that
would clip content. Already oversized imported text remains scrollable. Notes
float across Spaces; the explorer moves to the active Space. Disconnecting a
display keeps title bars reachable. The menu bar item recalls the last note on single
click, opens the archive on double-click and presents its menu on right-click.
Start at login uses ServiceManagement and is available only for an installed app
under `/Applications`.

Physical multi-monitor transitions, every input method, real sign-out/login,
clean-machine installation and upgrade are environment-dependent checks listed
in the validation report; automated tests are not a substitute for those runs.

## Retrieve the preview on a Mac

The `macos` branch is published on the public repository and tracks
`origin/macos`. The default `windows` branch contains the Windows version. The
`v1.10.0` release provides the universal portable app, PKG installer and
SHA-256 checksums built from the tagged commit.

Clone it directly:

```sh
git clone --branch macos --single-branch https://github.com/GarauGarau/Jotilla.git
cd Jotilla
```

Then follow the build and validation steps above. To update an existing checkout
of this branch:

```sh
git pull --ff-only
```

If the existing checkout still calls the branch `feature/macos-native`, rename it
and update its tracking branch first:

```sh
git branch -m feature/macos-native macos
git remote set-branches origin '*'
git fetch origin --prune
git branch -u origin/macos macos
git remote set-head origin -a
git switch macos
git pull --ff-only
```

## Notes and menu-bar behavior in 1.10.0

Single-click the menu-bar icon to recall the last used note, including an archived
note. A folded note expands. If the note is unavailable, the explorer opens
without creating a blank note. Double-click still opens the explorer; use the
right-click **New note** command, Cmd+N, or the global shortcut to create a note.
The last-note identity is saved with the session, separately for each archive.

Right-click the note bar and choose **Scrollable text**. Choose **Only this note**
or **This note and new notes** to save the default across restarts. Existing notes
retain their own setting. Scroll using the wheel/trackpad, Page Up/Down or native
document navigation shortcuts; no scrollbars or gutters appear. Turning scrolling
off requires the full text to fit. With scrolling off, a rejected paste shows an
explanation and leaves the note unchanged. Copy/cut/paste use the native pasteboard
for interchange with other apps. The shared `scroll` settings are compatible with
the Windows archive format.
