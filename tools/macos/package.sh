#!/bin/sh
# Local packaging only: no upload, publishing, or notarization submission.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
mkdir -p build/macos dist/macos
stage=$(mktemp -d "$root/build/macos/package.XXXXXX")
trap 'rm -rf "$stage"' EXIT HUP INT TERM
app="$stage/install/Jotilla.app"
portable="$stage/portable/Jotilla.app"
ditto build/macos/arm64/Jotilla.app "$app"
lipo -create build/macos/arm64/Jotilla.app/Contents/MacOS/Jotilla \
    build/macos/x86_64/Jotilla.app/Contents/MacOS/Jotilla -output "$app/Contents/MacOS/Jotilla"
lipo "$app/Contents/MacOS/Jotilla" -verify_arch arm64 x86_64
ditto "$app" "$portable"
/usr/libexec/PlistBuddy -c 'Add :JotillaPortable bool true' "$portable/Contents/Info.plist"
# Separate identity prevents Launch Services from routing a portable launch to
# an already-running installed app. Archive locking still protects the data.
/usr/libexec/PlistBuddy -c 'Set :CFBundleIdentifier io.github.GarauGarau.Jotila.portable' "$portable/Contents/Info.plist"
for bundle in "$app" "$portable"; do
    codesign --force --sign "${SIGN_IDENTITY:--}" --options runtime "$bundle"
    codesign --verify --strict "$bundle"
done
mkdir -p "$stage/portable/Notes.jotillaarchive"
cp LICENSE "$stage/portable/LICENSE"
cat > "$stage/portable/README.txt" <<'EOF'
Jotilla for macOS — development preview

Run Jotilla.app and choose Notes.jotillaarchive (or an existing archive folder).
The portable app asks for the folder each launch; it writes no archive location
to your user preferences. Keep the app and archive together when moving drives.
You can also use --portable --archive PATH. Do not store notes inside Jotilla.app.
Back up the entire archive, including RTF and notes.ini. Test a copy of your data.

Global new note: Control+Option+Command+N. In the app: Command+N new note,
Command+E explorer, Command+F search, Command+T tags, Command+D date,
Command+P color palette, Command+S save and Command+R reload external edits.
Command+B/I/U and Command+Shift+X format selected text. Command+plus/minus
and Command+wheel change selected font size or resize the note.
Closing a note keeps it in the archive. Closing the explorer hides it.
Notes need either a title or text; completely blank notes stay out of the archive.
Single-click the menu bar icon to recall the last used note; double-click for the explorer.
Right-click the note bar for Scrollable text, for this note or as a saved default.
Scroll with the wheel/trackpad or keyboard, without visible scrollbars.
A rejected paste in a bounded note explains how to make the content fit. Quit Jotilla before moving an archive.

This preview requires macOS 13 or later, Intel or Apple Silicon.
Native Apple Silicon build, storage and UI tests are available in the repository.
This local development build is not notarized for public distribution.
EOF
ditto -c -k --sequesterRsrc --keepParent "$stage/portable" dist/macos/Jotilla-portable-universal.zip
version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$app/Contents/Info.plist")
# Keep the receipt identity, but install under the new app name even when an
# older copy is present elsewhere. No script removes a user's previous app.
pkgbuild --analyze --root "$stage/install" "$stage/components.plist"
/usr/libexec/PlistBuddy -c 'Set :0:BundleIsRelocatable false' "$stage/components.plist"
if [ -n "${INSTALLER_IDENTITY:-}" ]; then
    pkgbuild --root "$stage/install" --component-plist "$stage/components.plist" --install-location /Applications --identifier io.github.GarauGarau.Jotila \
        --version "$version" --sign "$INSTALLER_IDENTITY" dist/macos/Jotilla-Setup-universal.pkg
else
    pkgbuild --root "$stage/install" --component-plist "$stage/components.plist" --install-location /Applications --identifier io.github.GarauGarau.Jotila \
        --version "$version" dist/macos/Jotilla-Setup-universal.pkg
fi
(cd dist/macos && LC_ALL=C shasum -a 256 Jotilla-portable-universal.zip Jotilla-Setup-universal.pkg > SHA256SUMS.txt)
printf 'Downloads prepared in %s/dist/macos\n' "$root"
