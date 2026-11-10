# Windows

The `windows` branch is the default branch and contains the native C17 application
for Windows 10/11, x64. See the [download table](../README.md#download) for the
installer and portable ZIP. Binaries are unsigned.

## Installation and startup

Run `Jotilla-Setup-x64.exe`, or extract the portable ZIP and run `Jotilla.exe`.
Keep `portable.flag` beside the portable EXE to retain portable mode.

Setup offers **Start Jotilla when I sign in to Windows**. Updates retain this
choice; rerun Setup to change it. At sign-in, Jotilla restores the session without
taking focus or creating an empty note; notes closed with **×** stay in the
archive. Silent Setup accepts `/S /AUTOSTART` or `/S /NOAUTOSTART`; `/S` alone
retains the existing choice. Portable mode does not register automatic startup.
Uninstall removes Jotilla's startup entry and preserves notes.

Setup also upgrades earlier Jotila installations. It retains the startup choice,
replaces the old program and shortcuts, and adopts the existing archive in place.
Existing notes are not moved or renamed. Portable users can replace the old EXE
with `Jotilla.exe` while keeping `portable.flag`, `archive.path` and the archive.

**Ctrl + Alt + +** places the new note's top-left corner exactly at the mouse
pointer. Creating a note from the tray keeps the usual staggered position.
Single-click the tray to recall the last used note, including a closed note in the archive.
If it was deleted or no recent note is available, the explorer opens without creating a note.
The recent-note identity is saved with the session for each archive.
Minimizing the explorer hides it in the tray; double-click the tray icon to return.

**Ctrl + C / X / V** copy, cut and paste text, including between Jotilla and other
apps. **Shift + Insert** also pastes. With scrolling off, if the pasted text exceeds the note's visible
space, an information dialog explains how to proceed: enlarge the note or paste
less text. The existing text, formatting and selection are preserved.

Right-click the note's title bar and choose **Scrollable text**. The dialog offers
**Only this note** or **This note and new notes**. Text wraps to the note width.
Use the mouse wheel, Page Up/Down or Ctrl+Home/End to navigate longer text;
no scroll bar is shown or reserves space inside the note. Wheel input respects
the Windows lines/page preference and accumulates partial wheel movements.
The note keeps its mode when closed, reopened, folded or restored after restart.
The default is saved in the current archive and applies to future notes; existing
notes retain their own setting. The same menu can turn scrolling off and reset
the default. If the text cannot fit without scrolling, enlarge the note or
shorten the text first: switching modes never truncates content.

## Notes and portable data

Notes initially save in `%LOCALAPPDATA%\Jotilla\notes`, or in the `notes` folder
beside the portable EXE. **Archive** moves or switches folders. Back up the whole
archive, including `notes.ini` and RTF files. Updates and uninstall preserve notes.

## Build and checks

Clone the platform branch:

```sh
git clone --branch windows --single-branch https://github.com/GarauGarau/Jotilla.git
cd Jotilla
```

Build with MSVC and the Windows SDK:

```bat
build.cmd
build.cmd test
build.cmd test-checked
build.cmd analyze
```

Package downloads with `tools/package.ps1`.
See [performance measurements](performance.md).

Run `build\tests.exe --clipboard-only` for the clipboard and paste-warning checks.
This optional test replaces the system clipboard contents; CI runs it separately
from the default suite.
The default suite also checks scrolling, its scope dialog and persisted choices;
`build\tests.exe --scrolling-only` runs that group separately. Startup checks
verify long notes and the default across two launches of the release executable.

## Update an existing checkout

If this checkout still calls the branch `main`, rename it and update its tracking
branch before pulling:

```sh
git branch -m main windows
git remote set-branches origin '*'
git fetch origin --prune
git branch -u origin/windows windows
git remote set-head origin -a
git switch windows
git pull --ff-only
```
