$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $root 'dist'
$stage = Join-Path $root 'build\portable'
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $dist 'Jotilla.exe') -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $stage
$readme = @'
Jotilla - Sticky notes, on silicon

Run Jotilla.exe. Ctrl+Alt++ creates a note exactly at the mouse pointer.
Single-click the tray icon to recall the last used note; double-click to browse notes.
Use Ctrl+Alt++ or right-click the tray and choose New note to create a note.
Tray creation keeps the usual staggered position.
Open notes return to their saved positions, sizes and folded state after restart.
Closed notes remain archived. Unplugged monitors cannot strand a note's title bar.
The Setup EXE offers an optional "Start Jotilla when I sign in to Windows" checkbox.
Updates retain that choice. Portable mode does not register automatic startup.
Minimizing the browser hides it beside the clock; double-click restores it.
Drag the bar to move it; use - to collapse and X to close.
Ctrl++ / Ctrl+- / Ctrl+mouse wheel or a touchpad pinch resizes the note.
With text selected, the same gesture changes only its font size.
With scrolling off, sizing stops before text would be clipped; Ctrl+Z/Ctrl+Y undo/redo font changes.
Right-click the bar for the archive, opacity, title, size and three original palettes.
Right-click the text for the color wheel; Left/Right browse all 80 Color-Hex Popular palettes.
Click a color to apply and close; Escape cancels.
The star applies a favorite to this note and new notes, including after restart.
Select text before right-clicking to change only its text color.
#Title + Shift+Enter makes a title; Enter keeps it as text.
Ctrl+E opens the note explorer; Ctrl+Z/Ctrl+Y undo/redo text and formatting.
Ctrl+C/X/V copy, cut and paste text between notes and other apps; Shift+Insert also pastes.
With scrolling off, if pasted text does not fit, a notice asks you to enlarge the note or paste less text.
The existing text, formatting and selection are preserved.
Right-click the bar and choose Scrollable text for longer text with vertical scrolling.
No scroll bar is visible. Use the mouse wheel, Page Up/Down or Ctrl+Home/End.
Choose Only this note or This note and new notes. Both choices survive restart.
Existing notes retain their own setting. The default is stored in the current archive.
The same menu turns scrolling off; enlarge or shorten overflowing notes first.
The calendar icon assigns one day to a note; click it again to change or remove the day.
The square button beside Archive opens the calendar. Click a circled day to open its notes.
Type a year directly in either calendar. Arrow/Page/Home/End keys navigate day lists; Enter opens and Esc returns.

Notes initially save in the notes folder beside the EXE.
The Archive button beside search can move the archive to an empty folder,
or open/create another archive while leaving the previous one intact.
The chosen folder becomes the default; archive.path stores its location.
Keep portable.flag beside the EXE to retain portable mode.
Back up the entire archive, including notes.ini and RTF formatting files.

https://github.com/GarauGarau/Jotilla
'@
[IO.File]::WriteAllText((Join-Path $stage 'README.txt'), $readme, [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $stage 'portable.flag'), '')
$zip = Join-Path $dist 'Jotilla-portable-x64.zip'
Compress-Archive -LiteralPath (Join-Path $stage 'Jotilla.exe'),(Join-Path $stage 'README.txt'),(Join-Path $stage 'LICENSE'),(Join-Path $stage 'portable.flag') -DestinationPath $zip -Force
$assets = @('Jotilla.exe','Jotilla-Setup-x64.exe','Jotilla-portable-x64.zip')
$lines = foreach ($name in $assets) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $dist $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $name"
}
[IO.File]::WriteAllLines((Join-Path $dist 'SHA256SUMS.txt'), $lines, [Text.UTF8Encoding]::new($false))
Get-Item -LiteralPath ($assets | ForEach-Object { Join-Path $dist $_ }) | Select-Object Name,Length
