/* Included by tests.c; opt-in locally because this uses the system clipboard. */
static void clipboard_fixture(HWND owner, const wchar_t *text) {
    size_t bytes = (wcslen(text) + 1) * sizeof(wchar_t);
    HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, bytes);
    void *memory;
    require(data != NULL, "allocate Unicode clipboard fixture");
    memory = GlobalLock(data);
    require(memory != NULL, "lock clipboard fixture");
    memcpy(memory, text, bytes); GlobalUnlock(data);
    require(OpenClipboard(owner) && EmptyClipboard(), "open test clipboard");
    require(SetClipboardData(CF_UNICODETEXT, data) != NULL, "publish Unicode clipboard fixture");
    require(CloseClipboard(), "release test clipboard");
}

static void shift_insert(Note *note) {
    BYTE previous[256], state[256] = {0};
    require(GetKeyboardState(previous), "read Shift+Insert keyboard state");
    state[VK_SHIFT] = state[VK_LSHIFT] = 0x80;
    require(SetKeyboardState(state), "set Shift+Insert modifiers");
    SendMessageW(note->edit, WM_KEYDOWN, VK_INSERT, 1);
    require(SetKeyboardState(previous), "restore Shift+Insert keyboard state");
}

static void clipboard_checks(void) {
    Note *note = new_note();
    wchar_t overflow[4096], text[128];
    CHARRANGE selection;
    HWND external;
    unsigned expected = 0;
    int i;
    require(note != NULL, "create clipboard note");
    external = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_MULTILINE,
                               0, 0, 400, 300, controller, NULL, instance, NULL);
    require(external != NULL, "create independent plain-text clipboard destination");
    replace_text(note, L"Caffè € 😀");
    SendMessageW(note->edit, EM_SETSEL, 0, 5); shortcut(note, 'I', FALSE);
    require(save_note(note), "save styled clipboard fixture");
    for (i = 0; i < 4095; ++i) overflow[i] = L'W';
    overflow[4095] = 0;
    clipboard_fixture(controller, overflow);
    /* Direct paste, both keyboard routes and explicit native paste must all
       preserve the selection and show exactly one notice per failed attempt. */
    for (i = 0; i < 4; ++i) {
        SendMessageW(note->edit, EM_SETSEL, 0, 5);
        if (i == 0) SendMessageW(note->edit, WM_PASTE, 0, 0);
        else if (i == 1) shortcut(note, 'V', FALSE);
        else if (i == 2) shift_insert(note);
        else SendMessageW(note->edit, EM_PASTESPECIAL, CF_UNICODETEXT, 0);
        SendMessageW(note->edit, EM_EXGETSEL, 0, (LPARAM)&selection);
        require(paste_notices == ++expected && !wcscmp(note->accepted_text, L"Caffè € 😀") &&
                (effects_at(note, 1) & CFE_ITALIC) && selection.cpMin == 0 && selection.cpMax == 5 &&
                !note->dirty, "rejected paste warns once and preserves text, styles, selection and saved state");
    }
    clipboard_fixture(controller, L"Test");
    shortcut(note, 'V', FALSE);
    require(paste_notices == expected && !wcscmp(note->accepted_text, L"Test € 😀"),
            "fitting paste after rejection succeeds once without a stale warning");
    shortcut(note, 'Z', FALSE);
    require(!wcscmp(note->accepted_text, L"Caffè € 😀") && (effects_at(note, 1) & CFE_ITALIC),
            "undo restores the original text and formatting after a successful paste");
    shortcut(note, 'Y', FALSE);
    require(!wcscmp(note->accepted_text, L"Test € 😀"), "redo restores the successful paste");
    SendMessageW(note->edit, EM_SETSEL, 0, GetWindowTextLengthW(note->edit));
    shortcut(note, 'C', FALSE);
    SendMessageW(external, WM_PASTE, 0, 0); GetWindowTextW(external, text, 128);
    require(!wcscmp(text, L"Test € 😀"), "copied note text pastes into an independent Windows editor");
    shortcut(note, 'X', FALSE);
    SetWindowTextW(external, L""); SendMessageW(external, WM_PASTE, 0, 0);
    GetWindowTextW(external, text, 128);
    require(!*note->accepted_text && !wcscmp(text, L"Test € 😀"), "cut note text remains available outside the note");
    SetWindowTextW(external, L"Outside è 😀\r\nSecond line");
    SendMessageW(external, EM_SETSEL, 0, -1); SendMessageW(external, WM_COPY, 0, 0);
    shift_insert(note);
    require(!wcscmp(note->accepted_text, L"Outside è 😀\r\nSecond line") && paste_notices == expected,
            "external Unicode multiline text pastes silently when it fits");
    SendMessageW(note->edit, EM_SETSEL, 0, 0);
    SendMessageW(note->edit, EM_REPLACESEL, TRUE, (LPARAM)overflow);
    require(paste_notices == expected, "non-paste overflow does not show a paste warning");
    require(OpenClipboard(controller) && EmptyClipboard() && CloseClipboard(), "clear fixture clipboard");
    shortcut(note, 'V', FALSE);
    require(paste_notices == expected && !wcscmp(note->accepted_text, L"Outside è 😀\r\nSecond line"),
            "empty clipboard is a silent no-op");
    require(save_note(note), "save successful clipboard edit");
    expect_file(note->path, "Outside è 😀\r\nSecond line");
    require(set_note_scrolling(note, TRUE), "enable scrolling for long clipboard input");
    clipboard_fixture(controller, overflow);
    SendMessageW(note->edit, EM_SETSEL, 0, -1); shortcut(note, 'V', FALSE);
    require(paste_notices == expected && !wcscmp(note->accepted_text, overflow),
            "scrollable mode accepts the complete oversized clipboard without a warning");
    shortcut(note, 'Z', FALSE);
    require(!wcscmp(note->accepted_text, L"Outside è 😀\r\nSecond line"), "undo reverses the complete long paste");
    shortcut(note, 'Y', FALSE);
    require(!wcscmp(note->accepted_text, overflow) && paste_notices == expected,
            "redo restores a long paste in scrollable mode without rejecting it");
    DestroyWindow(external); destroy_note(note);
    puts("PASS: clipboard overflow notices, native paste routes, preserved content, undo/redo and external text interchange.");
}
