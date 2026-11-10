# Text sizing

Ctrl + plus/minus, Ctrl + mouse wheel and the touchpad's pinch gesture resize the selected text.
Without a selection they resize the note, preserving its aspect ratio. Collapsed
notes resize their stored expanded dimensions. Dragging an edge or corner always
resizes the note.

Text keeps its colors, emphasis and mixed sizes. Each wheel step follows Rich
Edit's native font-size increments. With scrolling off, growth stops before any
content would be clipped; reducing the text immediately works in the other direction. Ctrl + Z
and Ctrl + Y undo and redo font changes. Sizes persist in the existing RTF file,
without changing the plain-text file or adding metadata.

With **Scrollable text** enabled in the note-bar menu, text can extend below the
visible area. Resizing the note or its text keeps the complete document and
updates native scrolling without displaying a scroll bar. The mouse wheel and
navigation keys move through the text. Mode changes retain formatting, selection and undo
history; returning to bounded text requires the document to fit the note.

## Native implementation

Windows supplies the normal pinch-to-zoom fallback as
[Ctrl + wheel input](https://learn.microsoft.com/en-us/windows/win32/wintouch/windows-touch-gestures-overview).
The same handler accumulates fractional deltas and discards a remainder when
the target selection changes. No gesture framework or background worker is added.
Keyboard shortcuts use a complete size step and clear any pending wheel remainder;
both the main keyboard and numeric keypad are supported.

[EM_SETFONTSIZE](https://learn.microsoft.com/en-us/windows/win32/controls/em-setfontsize)
updates each selected formatting run through Rich Edit's existing undo engine.
The size step requires no application heap allocation or font creation. Saves
use the existing debounce timer and update only rich formatting.

[EM_REQUESTRESIZE](https://learn.microsoft.com/en-us/windows/win32/controls/em-requestresize)
measures the native layout, including mixed font sizes, wrapping and the last
line. This replaces the fixed-height line estimate for all content-fit checks.
Checklist clicks query native glyph bounds so the full box stays clickable at
its new size. These changes remain in C and use the existing Windows dependencies.
