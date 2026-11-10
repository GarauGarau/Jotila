/* Real native scrolling, scope dialogs and persistence in the private archive. */
static int scroll_choice;
static unsigned scroll_prompts;

static HRESULT CALLBACK select_scroll_scope(HWND window, UINT message, WPARAM wparam, LPARAM lparam, LONG_PTR data) {
    (void)wparam; (void)lparam; (void)data;
    if (message == TDN_CREATED) SendMessageW(window, TDM_CLICK_BUTTON, (WPARAM)scroll_choice, 0);
    return S_OK;
}

static HRESULT inspect_scroll_options(const TASKDIALOGCONFIG *dialog, int *button, int *radio, BOOL *checked) {
    TASKDIALOGCONFIG actual = *dialog;
    if (!dialog->pszMainInstruction ||
        (wcscmp(dialog->pszMainInstruction, L"Enable scrollable text?") &&
         wcscmp(dialog->pszMainInstruction, L"Turn off scrollable text?")))
        return TaskDialogIndirect(dialog, button, radio, checked);
    require(dialog->hwndParent && dialog->cButtons == 2 &&
            dialog->pButtons[0].nButtonID == SCROLL_THIS_NOTE && dialog->pButtons[1].nButtonID == SCROLL_NEW_NOTES &&
            dialog->nDefaultButton == SCROLL_THIS_NOTE && (dialog->dwCommonButtons & TDCBF_CANCEL_BUTTON) &&
            wcsstr(dialog->pButtons[1].pszButtonText, L"after restart") != NULL,
            "native scrolling dialog offers current-note, persistent default and cancellation choices");
    ++scroll_prompts;
    actual.pfCallback = select_scroll_scope;
    return TaskDialogIndirect(&actual, button, radio, checked);
}

static BOOL native_scrolling(Note *note) {
    return (SendMessageW(note->edit, EM_GETOPTIONS, 0, 0) & ECO_AUTOVSCROLL) != 0;
}

static void expect_no_scrollbars(Note *note) {
    RECT window, client;
    require(!(GetWindowLongPtrW(note->edit, GWL_STYLE) & (WS_VSCROLL | WS_HSCROLL)),
            "scrollable text never exposes a native scroll bar");
    GetWindowRect(note->edit, &window); GetClientRect(note->edit, &client);
    require(window.right - window.left == client.right && window.bottom - window.top == client.bottom,
            "hidden scroll bars reserve no space in the note");
}

static void scrolling_key(Note *note, UINT key, BOOL control) {
    BYTE previous[256], state[256] = {0};
    require(GetKeyboardState(previous), "read navigation modifiers");
    state[VK_CONTROL] = state[VK_LCONTROL] = control ? 0x80 : 0;
    require(SetKeyboardState(state), "set navigation modifiers");
    SendMessageW(note->edit, WM_KEYDOWN, key, 1);
    SendMessageW(note->edit, WM_KEYUP, key, 1);
    require(SetKeyboardState(previous), "restore navigation modifiers");
    pump(150); /* Rich Edit completes caret scrolling through its message loop. */
}

static void scrolling_checks(void) {
    Note *note, *other, *fresh;
    wchar_t id[80], other_id[80], long_text[8192], previous_setting[32];
    BOOL previous_default = default_scrolling;
    HWND editor;
    HMENU menu;
    CHARRANGE selection;
    POINT before = {0}, after = {0}, origin = {0};
    HANDLE lock;
    unsigned prompts = scroll_prompts;
    int i;
    GetPrivateProfileStringW(L"app", L"scroll", L"", previous_setting, 32, state_path);
    require(write_setting(L"app", L"scroll", FALSE), "start with bounded new notes");
    default_scrolling = FALSE;
    note = new_note(); other = new_note();
    require(note && other && !native_scrolling(note) && !native_scrolling(other), "bounded notes remain the initial default");
    replace_text(note, L"Styled è 😀"); replace_text(other, L"Existing bounded note");
    require(save_note(note) && save_note(other), "save scrolling test notes");
    editor = note->edit; wcscpy_s(other_id, 80, other->id);
    SendMessageW(note->edit, EM_SETSEL, 0, 6); shortcut(note, 'I', FALSE);
    require(save_note(note), "save emphasis before changing scrolling");
    scroll_choice = IDCANCEL; note_command(note, CMD_SCROLL);
    require(scroll_prompts == ++prompts && !note->scrolling && !default_scrolling,
            "cancelling the scrolling dialog changes neither scope");
    metadata_writes = heap_requests = 0;
    scroll_choice = SCROLL_THIS_NOTE; note_command(note, CMD_SCROLL);
    SendMessageW(note->edit, EM_EXGETSEL, 0, (LPARAM)&selection);
    require(scroll_prompts == ++prompts && note->scrolling && native_scrolling(note) && !default_scrolling &&
            !other->scrolling && note->edit == editor && selection.cpMin == 0 && selection.cpMax == 6 &&
            (effects_at(note, 1) & CFE_ITALIC), "current-note choice retains editor, formatting, selection and other notes");
    require(metadata_writes == 1 && !heap_requests, "mode-only change writes one metadata key without application allocations");
    expect_no_scrollbars(note);
    menu = build_note_menu(note);
    require(menu && (GetMenuState(menu, CMD_SCROLL, MF_BYCOMMAND) & MF_CHECKED), "bar menu reflects scrollable mode");
    DestroyMenu(menu);
    shortcut(note, 'Z', FALSE);
    require(!(effects_at(note, 1) & CFE_ITALIC), "mode change retains the native undo history");
    shortcut(note, 'Y', FALSE);
    require(effects_at(note, 1) & CFE_ITALIC, "format redo still works in scrollable mode");

    long_text[0] = 0;
    for (i = 0; i < 160; ++i) wcscat_s(long_text, 8192, L"Long Unicode line è 😀 with wrapped text.\r\n");
    replace_text(note, long_text);
    require(!wcscmp(note->accepted_text, long_text) && accepts_text(note) && !text_fits(note),
            "scrollable note accepts the complete document beyond the viewport");
    expect_no_scrollbars(note);
    SendMessageW(note->edit, EM_SETSCROLLPOS, 0, (LPARAM)&origin);
    override_wheel_lines = TRUE; wheel_lines = 3;
    {
        POINT wheel = {10,10};
        ShowWindow(note->window, SW_SHOWNOACTIVATE);
        SetFocus(note->edit);
        ClientToScreen(note->edit, &wheel);
        SendMessageW(note->edit, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), MAKELPARAM(wheel.x, wheel.y));
        pump(80);
        ShowWindow(note->window, SW_HIDE);
    }
    SendMessageW(note->edit, EM_GETSCROLLPOS, 0, (LPARAM)&after);
    require(after.y > 0, "ordinary wheel input scrolls native text");
    expect_no_scrollbars(note);
    SendMessageW(note->edit, EM_SETSCROLLPOS, 0, (LPARAM)&origin);
    SendMessageW(note->edit, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA / 2), 0);
    SendMessageW(note->edit, EM_GETSCROLLPOS, 0, (LPARAM)&before);
    require(!before.y, "partial wheel input is accumulated without moving text prematurely");
    SendMessageW(note->edit, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA / 2), 0);
    SendMessageW(note->edit, EM_GETSCROLLPOS, 0, (LPARAM)&after);
    require(after.y > 0, "two partial wheel events complete one scroll step");
    wheel_lines = 0;
    SendMessageW(note->edit, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
    SendMessageW(note->edit, EM_GETSCROLLPOS, 0, (LPARAM)&before);
    require(before.y == after.y, "disabled Windows wheel scrolling is respected");
    wheel_lines = WHEEL_PAGESCROLL;
    SendMessageW(note->edit, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
    SendMessageW(note->edit, EM_GETSCROLLPOS, 0, (LPARAM)&after);
    require(after.y > before.y, "Windows page-at-a-time wheel scrolling works without a bar");
    override_wheel_lines = FALSE;
    ShowWindow(note->window, SW_SHOWNOACTIVATE); SetFocus(note->edit);
    scrolling_key(note, VK_END, TRUE);
    SendMessageW(note->edit, EM_GETSCROLLPOS, 0, (LPARAM)&before);
    require(before.y > after.y, "Ctrl+End reaches the document end with no scroll bar");
    scrolling_key(note, VK_HOME, TRUE);
    SendMessageW(note->edit, EM_GETSCROLLPOS, 0, (LPARAM)&after);
    require(!after.y, "Ctrl+Home returns to the document start");
    scrolling_key(note, VK_NEXT, FALSE);
    SendMessageW(note->edit, EM_GETSCROLLPOS, 0, (LPARAM)&before);
    require(before.y > 0, "Page Down navigates below the viewport");
    scrolling_key(note, VK_PRIOR, FALSE);
    SendMessageW(note->edit, EM_GETSCROLLPOS, 0, (LPARAM)&after);
    require(after.y < before.y, "Page Up returns toward the document start");
    ShowWindow(note->window, SW_HIDE);
    expect_no_scrollbars(note);
    screenshot(note, L"build\\scrolling-no-bars.bmp");
    SendMessageW(note->edit, EM_LINESCROLL, 0, 12);
    SendMessageW(note->edit, EM_GETSCROLLPOS, 0, (LPARAM)&before);
    require(before.y > after.y, "native line scrolling moves through the document");
    require(resize_note(note, 0) && !wcscmp(note->accepted_text, long_text), "scrollable notes can shrink without rejecting or truncating text");
    expect_no_scrollbars(note);
    SendMessageW(note->edit, EM_GETSCROLLPOS, 0, (LPARAM)&before);
    require(!set_note_scrolling(note, FALSE), "bounded mode cannot hide an overflowing document");
    SendMessageW(note->edit, EM_GETSCROLLPOS, 0, (LPARAM)&after);
    require(note->scrolling && native_scrolling(note) && before.y == after.y && !wcscmp(note->accepted_text, long_text),
            "refused disable restores native scrolling, position and all text");
    expect_no_scrollbars(note);
    SendMessageW(note->edit, EM_SETSEL, 0, 4);
    i = format_range(note, 0, 4).yHeight;
    SendMessageW(note->edit, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, WHEEL_DELTA), 0);
    require(format_range(note, 0, 4).yHeight > i, "selected text can grow past the viewport in scrollable mode");
    require(save_note(note), "save long scrollable note with rich formatting");
    wcscpy_s(id, 80, note->id);
    toggle_note(note); destroy_note(note);
    note = open_note(id, 0, TRUE);
    require(note && note->collapsed && note->scrolling && native_scrolling(note) &&
            !wcscmp(note->accepted_text, long_text) && format_range(note, 0, 4).yHeight > i,
            "reopening restores scroll mode before loading the full document, including folded state and RTF");
    expect_no_scrollbars(note);
    replace_text(note, L"Short while folded");
    require(set_note_scrolling(note, FALSE) && set_note_scrolling(note, TRUE) && note->collapsed && native_scrolling(note),
            "the bar can change scrolling in both directions while the note is folded");
    expect_no_scrollbars(note);
    toggle_note(note);
    replace_text(note, L"Short again");
    require(set_note_scrolling(note, FALSE) && !native_scrolling(note) && text_fits(note),
            "shortening the document allows bounded mode again");
    require(!(GetWindowLongPtrW(note->edit, GWL_STYLE) & (WS_VSCROLL | ES_AUTOVSCROLL)), "bounded mode removes its native scroll controls");
    replace_text(note, long_text);
    require(!wcscmp(note->accepted_text, L"Short again"), "bounded mode resumes overflow protection");
    lock = CreateFileW(state_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    require(lock != INVALID_HANDLE_VALUE, "lock settings against a mode write");
    require(!set_note_scrolling(note, TRUE) && !note->scrolling && !native_scrolling(note),
            "failed persistence restores the previous native mode");
    CloseHandle(lock);
    require(save_note(note), "recover from the locked settings fixture");

    scroll_choice = SCROLL_NEW_NOTES; note_command(note, CMD_SCROLL);
    require(scroll_prompts == ++prompts && note->scrolling && default_scrolling && !other->scrolling,
            "default choice changes this note and future notes, leaving existing notes alone");
    default_scrolling = FALSE; load_preferences();
    fresh = new_note();
    require(default_scrolling && fresh && fresh->scrolling && native_scrolling(fresh),
            "loading saved preferences restores the default for newly created notes");
    expect_no_scrollbars(fresh);
    replace_text(fresh, L"Saved scrollable default"); require(save_note(fresh), "save a note created with the default");
    require(WritePrivateProfileStringW(other_id, L"scroll", NULL, state_path), "simulate an older archive without the new metadata key");
    destroy_note(other); other = open_note(other_id, 0, TRUE);
    require(other && !other->scrolling && !native_scrolling(other), "legacy notes keep bounded text despite a new scrollable default");
    note_command(note, CMD_SCROLL);
    require(scroll_prompts == ++prompts && !note->scrolling && !default_scrolling && fresh->scrolling,
            "the default can be disabled without changing notes that already use scrolling");
    default_scrolling = TRUE; load_preferences();
    require(!default_scrolling, "disabled default also survives preference reload");
    require(delete_archived_note(note->id) && delete_archived_note(other->id) && delete_archived_note(fresh->id),
            "remove private scrolling fixtures");
    require(WritePrivateProfileStringW(L"app", L"scroll", *previous_setting ? previous_setting : NULL, state_path),
            "restore the previous archive default");
    default_scrolling = previous_default;
    puts("PASS: native scroll/wheel, scope dialogs, safe mode changes, undo, long-text and RTF persistence, legacy notes and durable defaults.");
}
