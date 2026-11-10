/* End-to-end Win32 / filesystem checks; C17, no test framework. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdlib.h>
#include <commctrl.h>
#include "../src/search.h"

static unsigned metadata_writes, metric_queries, graphic_creations, heap_requests;
/* Physical input checks are opt-in so ordinary runs do not type on the user's desktop. */
static BOOL interactive_tests;
static unsigned paste_notices;
static int inspect_message_box(HWND owner, LPCWSTR message, LPCWSTR title, UINT flags);
static HRESULT inspect_scroll_options(const TASKDIALOGCONFIG *dialog, int *button, int *radio, BOOL *checked);
static BOOL fake_cursor;
static BOOL override_wheel_lines;
static UINT wheel_lines;
static BOOL trace_system_parameters(UINT action, UINT parameter, PVOID value, UINT flags) {
    if (override_wheel_lines && action == SPI_GETWHEELSCROLLLINES) { *(UINT *)value = wheel_lines; return TRUE; }
    return SystemParametersInfoW(action, parameter, value, flags);
}
static POINT test_cursor;
static BOOL trace_cursor(LPPOINT point) {
    if (!fake_cursor) return GetCursorPos(point);
    *point=test_cursor; return TRUE;
}
static BOOL trace_profile(LPCWSTR section, LPCWSTR key, LPCWSTR value, LPCWSTR path) {
    ++metadata_writes;
    return WritePrivateProfileStringW(section, key, value, path);
}
static BOOL trace_section(LPCWSTR section, LPCWSTR values, LPCWSTR path) {
    ++metadata_writes;
    return WritePrivateProfileSectionW(section, values, path);
}
static BOOL trace_metrics(HDC dc, LPTEXTMETRICW metrics) {
    ++metric_queries; return GetTextMetricsW(dc, metrics);
}
static HPEN trace_pen(int style, int width, COLORREF color) {
    ++graphic_creations; return CreatePen(style, width, color);
}
static HBRUSH trace_brush(COLORREF color) {
    ++graphic_creations; return CreateSolidBrush(color);
}
static void *trace_malloc(size_t size) { ++heap_requests; return malloc(size); }
static void *trace_calloc(size_t count, size_t size) { ++heap_requests; return calloc(count, size); }
static void *trace_realloc(void *memory, size_t size) { ++heap_requests; return realloc(memory, size); }
#define WritePrivateProfileStringW trace_profile
#define WritePrivateProfileSectionW trace_section
#define GetTextMetricsW trace_metrics
#define CreatePen trace_pen
#define CreateSolidBrush trace_brush
#define malloc trace_malloc
#define calloc trace_calloc
#define realloc trace_realloc
#define GetCursorPos trace_cursor
#define MessageBoxW inspect_message_box
#define TaskDialogIndirect inspect_scroll_options
#define SystemParametersInfoW trace_system_parameters
#include "../src/jotilla.c"
#undef SystemParametersInfoW
#undef TaskDialogIndirect
#undef MessageBoxW
#undef WritePrivateProfileStringW
#undef WritePrivateProfileSectionW
#undef GetTextMetricsW
#undef CreatePen
#undef CreateSolidBrush
#undef malloc
#undef calloc
#undef realloc
#undef GetCursorPos

static void require(BOOL condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s (Windows error %lu)\n", message, GetLastError());
        exit(1);
    }
}

static int inspect_message_box(HWND owner, LPCWSTR message, LPCWSTR title, UINT flags) {
    Note *note = (Note *)GetWindowLongPtrW(owner, GWLP_USERDATA);
    require(note && note->edit && !note->validating && editor_matches_text(note) && text_fits(note),
            "paste warning appears only after the native editor has restored the accepted text");
    require(!wcscmp(title, L"Jotilla") && flags == (MB_OK | MB_ICONINFORMATION) &&
            wcsstr(message, L"Enlarge the note or paste less text") != NULL,
            "paste warning explains how to make the content fit");
    ++paste_notices;
    return IDOK;
}

static int note_count(void) {
    int count = 0;
    Note *note;
    for (note = notes; note; note = note->next) ++count;
    return count;
}

static void pump(DWORD milliseconds) {
    ULONGLONG end = GetTickCount64() + milliseconds;
    MSG event;
    do {
        while (PeekMessageW(&event, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&event); DispatchMessageW(&event);
        }
        Sleep(5);
    } while (GetTickCount64() < end);
}

static HWND thread_window(const wchar_t *name, const wchar_t *title) {
    HWND window=NULL;
    while ((window=FindWindowExW(NULL,window,name,title))!=NULL)
        if (GetWindowThreadProcessId(window,NULL)==GetCurrentThreadId()) return window;
    return NULL;
}

static void tray_checks(void) {
    DWORD wait = GetDoubleClickTime() + 80;
    int count = note_count();
    HWND browser;
    Note *recent = new_note(), *other = new_note();
    wchar_t id[80];
    require(recent && other, "create recent-note fixtures");
    SetWindowTextW(recent->edit, L"Last used note");
    require(save_note(recent), "persist recent-note fixture");
    wcscpy_s(id, 80, recent->id);
    SendMessageW(recent->window, WM_ACTIVATE, WA_ACTIVE, 0);
    toggle_note(recent);
    count += 2;
    SendMessageW(controller, TRAY_MESSAGE, 1, WM_LBUTTONDOWN);
    SendMessageW(controller, TRAY_MESSAGE, 1, WM_LBUTTONUP);
    require(note_count() == count && !thread_window(L"Jotilla.Archive", NULL),
            "first tray click waits without creating a note or opening the browser");
    pump(wait);
    require(note_count() == count && !recent->collapsed && IsWindowVisible(recent->window) &&
            !wcscmp(last_note, id) && !thread_window(L"Jotilla.Archive", NULL),
            "single tray click recalls and unfolds the last used note without creating one");
    ShowWindow(recent->window, SW_HIDE);
    pump(wait);
    require(note_count() == count && !IsWindowVisible(recent->window), "single tray click timer does not repeat");
    require(save_all(), "save recent identity with session");
    last_note[0] = 0; load_preferences();
    require(!wcscmp(last_note,id), "recent identity survives preferences reload");
    SendMessageW(recent->window,WM_CLOSE,0,0);
    require(!find_note(id), "close recent note into archive");
    wcscpy_s(last_note,80,id);
    recall_note(); recent=find_note(id);
    require(recent && IsWindowVisible(recent->window) && note_count()==count,
            "recall reopens the same archived note");
    ShowWindow(recent->window,SW_HIDE);

    /* Shell notification order: down, up, double-click, up. */
    SendMessageW(controller, TRAY_MESSAGE, 1, WM_LBUTTONDOWN);
    SendMessageW(controller, TRAY_MESSAGE, 1, WM_LBUTTONUP);
    SendMessageW(controller, TRAY_MESSAGE, 1, WM_LBUTTONDBLCLK);
    browser = thread_window(L"Jotilla.Archive", NULL);
    require(browser && IsWindowVisible(browser) && note_count() == count,
            "double tray click opens the browser immediately without creating a note");
    SendMessageW(controller, TRAY_MESSAGE, 1, WM_LBUTTONUP);
    /* KillTimer can leave an already queued WM_TIMER behind. */
    SendMessageW(controller, WM_TIMER, TRAY_CLICK_TIMER, 0);
    pump(wait);
    require(note_count() == count && !IsWindowVisible(recent->window),
            "double-click release and stale timer cannot recall or create a stray note");

    SendMessageW(controller, TRAY_MESSAGE, 1, WM_LBUTTONDOWN);
    SendMessageW(controller, TRAY_MESSAGE, 1, WM_LBUTTONUP);
    pump(wait);
    require(note_count() == count && IsWindowVisible(recent->window), "single tray click still works after a complete double click");
    ShowWindow(recent->window,SW_HIDE);

    ShowWindow(browser, SW_MINIMIZE);
    require(!IsWindowVisible(browser), "minimized browser is hidden in the notification area");
    SendMessageW(controller, TRAY_MESSAGE, 1, WM_LBUTTONDOWN);
    SendMessageW(controller, TRAY_MESSAGE, 1, WM_LBUTTONUP);
    SendMessageW(controller, TRAY_MESSAGE, 1, WM_LBUTTONDBLCLK);
    require(thread_window(L"Jotilla.Archive", NULL) == browser && IsWindowVisible(browser) && !IsIconic(browser),
            "another double tray click restores and reuses the browser");
    /* Opening a window can divert the final release away from the tray icon. */
    SendMessageW(controller, TRAY_MESSAGE, 1, WM_LBUTTONDOWN);
    SendMessageW(controller, TRAY_MESSAGE, 1, WM_LBUTTONUP);
    pump(wait);
    require(note_count() == count && IsWindowVisible(recent->window), "a missing double-click release cannot swallow the next single click");
    ShowWindow(browser, SW_MAXIMIZE);
    browse_notes();
    require(IsZoomed(browser), "opening the visible browser preserves its maximized placement");
    SendMessageW(browser, WM_SYSCOMMAND, SC_MINIMIZE, 0);
    require(!IsWindowVisible(browser), "system minimize hides a maximized browser too");
    browse_notes();
    require(IsWindowVisible(browser) && IsZoomed(browser), "tray restore retains maximized placement");
    require(delete_archived_note(id), "delete recent fixture");
    wcscpy_s(last_note,80,id); ShowWindow(browser,SW_HIDE); recall_note();
    require(IsWindowVisible(browser) && !find_note(id) && note_count()==count-1,
            "deleted recent note falls back to explorer without recreating it");
    destroy_note(other); last_note[0]=0;
    ShowWindow(browser,SW_HIDE); recall_note();
    require(IsWindowVisible(browser) && note_count()==count-2,"empty recent identity opens explorer without creating a note");
    SendMessageW(controller,NEW_NOTE_MESSAGE,0,0);
    require(note_count()==count-1,"explicit New note command still creates a note");
    destroy_note(notes); last_note[0]=0;
    require(save_all(),"clear recent fixture preference");
    archive_close();
    puts("PASS: tray recall, activation tracking, archived/deleted notes, saved identity, double clicks and explicit creation.");
}

static void replace_text(Note *note, const wchar_t *text) {
    SendMessageW(note->edit, EM_SETSEL, 0, -1);
    SendMessageW(note->edit, EM_REPLACESEL, TRUE, (LPARAM)text);
}

static void expect_file(const wchar_t *path, const char *expected) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, 0, NULL);
    char buffer[4096];
    DWORD read;
    size_t length = strlen(expected);
    require(file != INVALID_HANDLE_VALUE, "open saved TXT");
    require(ReadFile(file, buffer, sizeof(buffer), &read, NULL), "read saved TXT");
    CloseHandle(file);
    require(read == length && !memcmp(buffer, expected, length), "exact UTF-8 TXT content");
}

static void click_button(Note *note, int number) {
    RECT rect;
    int bar = bar_height(note);
    LPARAM point;
    GetClientRect(note->window, &rect);
    point = MAKELPARAM(rect.right - (number == 1 ? bar * 3 / 2 : bar / 2), bar / 2);
    SendMessageW(note->window, WM_LBUTTONDOWN, MK_LBUTTON, point);
    SendMessageW(note->window, WM_LBUTTONUP, 0, point);
}

static void screenshot(Note *note, const wchar_t *path) {
    RECT rect, bounds;
    BITMAPINFO info = {0};
    BITMAPFILEHEADER header = {0};
    HDC screen, memory;
    HBITMAP bitmap;
    HGDIOBJ old;
    void *pixels;
    DWORD written, size;
    HANDLE file;
    ShowWindow(note->window, SW_SHOWNOACTIVATE);
    SetWindowPos(note->window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    RedrawWindow(note->window, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    pump(150);
    GetWindowRect(note->window, &bounds);
    SetRect(&rect, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top);
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = rect.right; info.bmiHeader.biHeight = -rect.bottom;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    screen = GetDC(NULL);
    memory = CreateCompatibleDC(screen);
    bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, NULL, 0);
    require(bitmap && memory, "allocate UI snapshot");
    old = SelectObject(memory, bitmap);
    require(BitBlt(memory, 0, 0, rect.right, rect.bottom, screen, bounds.left, bounds.top, SRCCOPY), "capture UI snapshot");
    size = (DWORD)(rect.right * rect.bottom * 4);
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + size;
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    require(file != INVALID_HANDLE_VALUE, "create UI snapshot");
    require(WriteFile(file, &header, sizeof(header), &written, NULL) &&
            WriteFile(file, &info.bmiHeader, sizeof(BITMAPINFOHEADER), &written, NULL) &&
            WriteFile(file, pixels, size, &written, NULL), "write UI snapshot");
    CloseHandle(file);
    SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(NULL, screen);
    ShowWindow(note->window, SW_HIDE);
}

static void press_enter(Note *note, BOOL shift) {
    BYTE previous[256], state[256];
    require(GetKeyboardState(previous), "read test thread keyboard state");
    memcpy(state, previous, sizeof(state));
    state[VK_SHIFT] = state[VK_LSHIFT] = state[VK_RSHIFT] = shift ? 0x80 : 0;
    require(SetKeyboardState(state), "set test thread Shift state");
    SendMessageW(note->edit, WM_KEYDOWN, VK_RETURN, 1);
    SendMessageW(note->edit, WM_CHAR, L'\r', 1);
    SendMessageW(note->edit, WM_KEYUP, VK_RETURN, 1);
    require(SetKeyboardState(previous), "restore test thread keyboard state");
}

static void title_checks(void) {
    Note *titled = new_note(), *literal = new_note();
    wchar_t titled_id[80], literal_id[80], text[256];
    LOGFONTW font;
    SIZE narrow, wide;
    HDC dc;
    HGDIOBJ old;
    require(titled && literal, "create title test notes");
    require(!titled->title && !literal->title, "new notes have no default title");
    require(GetObjectW(titled->font, sizeof(font), &font) != 0 && font.lfWeight == FW_BOLD,
            "font uses bold weight");
    dc = GetDC(titled->edit); old = SelectObject(dc, titled->font);
    GetTextExtentPoint32W(dc, L"iiii", 4, &narrow);
    GetTextExtentPoint32W(dc, L"WWWW", 4, &wide);
    require(narrow.cx == wide.cx, "actual selected font is monospaced");
    SendMessageW(titled->window, WM_CTLCOLOREDIT, (WPARAM)dc, (LPARAM)titled->edit);
    require(GetTextColor(dc) == RGB(0, 0, 0), "body text is pure black");
    SelectObject(dc, old); ReleaseDC(titled->edit, dc);

    replace_text(titled, L"#Spesa è urgente");
    press_enter(titled, TRUE);
    require(titled->title && !wcscmp(titled->title, L"Spesa è urgente"), "Shift+Enter promotes #first line without hash");
    require(GetWindowTextLengthW(titled->edit) == 0, "promoted title is removed from body without blank line");
    replace_text(titled, L"Caffè e pane");
    pump(650);
    require(!titled->dirty, "title and body autosave");
    expect_file(titled->path, "#Spesa è urgente\r\nCaffè e pane");
    screenshot(titled, L"build\\titled-expanded.bmp");
    click_button(titled, 1);
    screenshot(titled, L"build\\titled-collapsed.bmp");

    replace_text(literal, L"#appunto");
    press_enter(literal, FALSE);
    GetWindowTextW(literal->edit, text, 256);
    require(!literal->title && !wcscmp(text, L"#appunto\r\n"), "Enter keeps hashtag line in body");
    SendMessageW(literal->edit, EM_REPLACESEL, TRUE, (LPARAM)L"Resta nel testo");
    press_enter(literal, FALSE);
    require(!literal->title, "Enter on later lines cannot promote initial literal hashtag");
    SendMessageW(literal->edit, EM_REPLACESEL, TRUE, (LPARAM)L"Anche qui.");
    require(save_note(literal), "save literal hashtag note");
    expect_file(literal->path, "#appunto\r\nResta nel testo\r\nAnche qui.");
    wcscpy_s(titled_id, 80, titled->id); wcscpy_s(literal_id, 80, literal->id);
    destroy_note(titled); destroy_note(literal);
    titled = open_note(titled_id, 0, TRUE); literal = open_note(literal_id, 0, TRUE);
    require(titled && literal && titled->collapsed, "reopen title notes and collapsed state");
    require(titled->title && !wcscmp(titled->title, L"Spesa è urgente"), "Unicode title survives restart");
    GetWindowTextW(titled->edit, text, 256);
    require(!wcscmp(text, L"Caffè e pane"), "title header is excluded from reopened body");
    GetWindowTextW(literal->edit, text, 256);
    require(!literal->title && !wcscmp(text, L"#appunto\r\nResta nel testo\r\nAnche qui."),
            "literal Enter note remains literal after restart");
    replace_text(literal, L"#");
    press_enter(literal, FALSE);
    require(!literal->title, "a bare hash does not create an empty title");
    SendMessageW(titled->window, WM_CLOSE, 0, 0);
    SendMessageW(literal->window, WM_CLOSE, 0, 0);
    puts("PASS: bold monospaced black font, Shift+Enter title, Enter literal, body separation and restart persistence.");
}

static void efficiency_checks(BOOL enforce) {
    Note *note = new_note();
    wchar_t body[64];
    unsigned clean_writes, typing_allocations, typing_metrics, repaint_objects;
    int i;
    require(note != NULL, "create efficiency check note");
    metadata_writes = 0;
    for (i = 0; i < 20; ++i) require(save_note(note), "save an unchanged note");
    clean_writes = metadata_writes;
    heap_requests = metric_queries = 0;
    for (i = 0; i < 50; ++i) SendMessageW(note->edit, WM_CHAR, L'a', 1);
    typing_allocations = heap_requests; typing_metrics = metric_queries;
    GetWindowTextW(note->edit, body, 64);
    require(wcslen(body) == 50 && text_fits(note), "all fifty typed characters remain visible");
    require(save_note(note), "save typed input");
    ShowWindow(note->window, SW_SHOWNOACTIVATE); pump(30);
    graphic_creations = 0;
    for (i = 0; i < 100; ++i)
        RedrawWindow(note->window, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    repaint_objects = graphic_creations;
    ShowWindow(note->window, SW_HIDE);
    if (enforce) {
        require(clean_writes == 0, "unchanged saves do not rewrite metadata");
        require(typing_allocations <= 1 && typing_metrics == 0, "typing reuses text buffer and font metrics");
        require(repaint_objects == 0, "repaints reuse graphic resources");
    }
    printf("MEASURE: 20 clean saves: %u metadata calls; 50 typed characters: %u allocations, %u font queries; "
           "100 repaints: %u graphic creations.\n", clean_writes, typing_allocations, typing_metrics, repaint_objects);
    SendMessageW(note->window, WM_CLOSE, 0, 0);
}

static void storage_checks(void) {
    Note *note;
    wchar_t id[80], rich_path[PATH_CAP], custom[2500], actual[2500], *tags = NULL;
    HANDLE lock;
    int i, old_opacity;
    metadata_writes = 0; note = new_note();
    require(note && metadata_writes == 0, "creating a blank note does not write preferences");
    wcscpy_s(id, 80, note->id);
    replace_text(note, L"First line\r\nCaffè 😀");
    metadata_writes = 0;
    require(save_note(note) && metadata_writes == 1, "first save writes all layout properties in one operation");
    destroy_note(note); metadata_writes = 0;
    note = open_note(id, 0, TRUE);
    require(note && metadata_writes == 0, "restoring an open note does not rewrite unchanged metadata");
    require(editor_matches_text(note), "streaming text comparison preserves CRLF and supplementary Unicode");
    require(set_tags(note, L"café;work") && save_note(note), "prepare Unicode tags for a layout update");
    for (i = 0; i < 2499; ++i) custom[i] = L'v';
    custom[2499] = 0;
    require(WritePrivateProfileStringW(id, L"custom", custom, state_path), "prepare an unfamiliar metadata field larger than the initial buffer");
    old_opacity = note->opacity; note->opacity = 80;
    lock = CreateFileW(state_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    require(lock != INVALID_HANDLE_VALUE && !save_layout(note, FALSE), "locked metadata refuses the whole layout update");
    require(setting(id, L"opacity", -1) == old_opacity && setting(id, L"open", -1) == TRUE,
            "a failed layout update does not leave partially changed properties");
    CloseHandle(lock); metadata_writes = 0;
    require(save_layout(note, FALSE) && metadata_writes == 1, "retry batches all changed layout properties");
    GetPrivateProfileStringW(id, L"custom", L"", actual, 2500, state_path);
    require(!wcscmp(actual, custom) && tags_read(state_path, id, &tags) && tags && !wcscmp(tags, L"café;work"),
            "batched layout writes retain Unicode tags and unfamiliar long fields");
    free(tags); destroy_note(note); metadata_writes = 0;
    note = open_note(id, 0, TRUE);
    require(note && metadata_writes == 1 && setting(id, L"open", -1) == TRUE,
            "reopening a closed note only writes its open flag");
    lock = CreateFileW(note->path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    require(lock != INVALID_HANDLE_VALUE, "lock TXT while changing formatting");
    SendMessageW(note->edit, EM_SETSEL, 0, -1); toggle_format(note, CFE_ITALIC);
    require(note->dirty == DIRTY_RICH && save_note(note), "format-only changes save while TXT cannot be replaced");
    SendMessageW(note->edit, EM_UNDO, 0, 0);
    require(note->dirty == DIRTY_RICH && save_note(note), "format-only undo does not rewrite TXT");
    SendMessageW(note->edit, EM_REDO, 0, 0);
    require(note->dirty == DIRTY_RICH && save_note(note), "format-only redo does not rewrite TXT");
    CloseHandle(lock);
    swprintf_s(rich_path, PATH_CAP, L"%s\\%s.rtf", directory, id);
    lock = CreateFileW(rich_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    require(lock != INVALID_HANDLE_VALUE && set_title(note, L"Titolo è") && save_note(note),
            "title-only changes save while RTF cannot be replaced");
    require(set_title(note, L"Titolo è") && !note->dirty, "accepting an unchanged title leaves content clean");
    replace_text(note, L"Changed body");
    require(!save_note(note) && note->dirty, "a body change still requires its RTF companion");
    expect_file(note->path, "#Titolo è\r\nFirst line\r\nCaffè 😀");
    CloseHandle(lock); require(save_note(note), "retry persists both body representations");
    destroy_note(note); note = open_note(id, 0, TRUE);
    require(note && note->title && !wcscmp(note->title, L"Titolo è") && !wcscmp(note->accepted_text, L"Changed body"),
            "selective saves preserve title and body on restart");
    SendMessageW(note->window, WM_CLOSE, 0, 0);
    require(save_all() && setting(L"app", L"next_color", -1) == next_color, "save-all persists the deferred color preference");
    puts("PASS: batched metadata, unchanged reopen, preserved unknown fields, selective TXT/RTF saves, format undo/redo and deferred preferences.");
}

static void border_checks(void) {
    Note *note = new_note();
    int collapsed, hover;
    require(note != NULL, "create border check note");
    for (collapsed = 0; collapsed < 2; ++collapsed) {
        for (hover = 0; hover < 3; ++hover) {
            HDC dc;
            RECT rect;
            int bar = bar_height(note), x;
            COLORREF bar_color = hover == 2 ? RGB(235,161,151) :
                RGB(GetRValue(note_color(note)) * 94 / 100, GetGValue(note_color(note)) * 94 / 100,
                    GetBValue(note_color(note)) * 94 / 100);
            note->hover = hover;
            ShowWindow(note->window, SW_SHOWNOACTIVATE);
            RedrawWindow(note->window, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
            GetClientRect(note->window, &rect);
            dc = GetDC(note->window); x = rect.right - 8;
            require(GetPixel(dc, x, 0) == RGB(0,0,0) && GetPixel(dc, x, 1) == bar_color,
                    "top outline is exactly one black pixel, including hover");
            require(GetPixel(dc, 0, bar / 2) == RGB(0,0,0) && GetPixel(dc, 1, bar / 2) != RGB(0,0,0) &&
                    GetPixel(dc, rect.right - 2, bar / 2) != RGB(0,0,0) &&
                    GetPixel(dc, rect.right - 1, bar / 2) == RGB(0,0,0), "both outer edges stay black");
            require(GetPixel(dc, x, bar - 2) == bar_color && GetPixel(dc, x, bar - 1) == RGB(0,0,0) &&
                    GetPixel(dc, x, rect.bottom - 1) == RGB(0,0,0), "title bar and note bottom are outlined");
            if (!collapsed) require(GetPixel(dc, x, bar) == note_color(note), "divider leaves the body intact");
            ReleaseDC(note->window, dc);
        }
        if (!collapsed) toggle_note(note);
    }
    destroy_note(note);
    puts("PASS: one-pixel black outlines on expanded, collapsed and hovered notes.");
}

static void serialization_checks(void) {
    Note *note = new_note();
    wchar_t body[128];
    require(note != NULL, "create serialization check note");
    replace_text(note, L"#  Titolo 😀  ");
    press_enter(note, TRUE);
    require(note->title && !wcscmp(note->title, L"Titolo 😀"), "trimmed Unicode title");
    require(save_note(note), "save a title with an empty body");
    expect_file(note->path, "#Titolo 😀\r\n");
    replace_text(note, L"#Nuovo\r\nCorpo già presente: 漢字");
    SendMessageW(note->edit, EM_SETSEL, 6, 6);
    press_enter(note, TRUE);
    GetWindowTextW(note->edit, body, 128);
    require(!wcscmp(note->title, L"Nuovo") && !wcscmp(body, L"Corpo già presente: 漢字"),
            "promoting an existing first line preserves following body");
    require(save_note(note), "save replacement title and remaining body");
    expect_file(note->path, "#Nuovo\r\nCorpo già presente: 漢字");
    replace_text(note, L"\xd800");
    require(!save_note(note), "incomplete Unicode surrogate cannot replace a valid saved file");
    expect_file(note->path, "#Nuovo\r\nCorpo già presente: 漢字");
    replace_text(note, L"😀");
    require(save_note(note), "complete Unicode pair can be saved");
    expect_file(note->path, "#Nuovo\r\n😀");
    SendMessageW(note->window, WM_CLOSE, 0, 0);
    puts("PASS: empty title body, Unicode serialization, in-place title promotion and invalid UTF-16 protection.");
}

static Note *dialog_note;
static const wchar_t *dialog_input;
static BOOL native_menu_seen;
static DWORD WINAPI double_click_title(void *unused) {
    INPUT clicks[2]={{0},{0}};
    (void)unused;
    clicks[0].type=clicks[1].type=INPUT_MOUSE;
    clicks[0].mi.dwFlags=MOUSEEVENTF_LEFTDOWN; clicks[1].mi.dwFlags=MOUSEEVENTF_LEFTUP;
    if (SendInput(1,clicks,sizeof(INPUT))!=1) return 1;
    Sleep(30);
    if (SendInput(1,clicks+1,sizeof(INPUT))!=1) return 1;
    /* Keep input timing independent of the native caption move loop. */
    Sleep(80);
    if (SendInput(1,clicks,sizeof(INPUT))!=1) return 1;
    Sleep(30);
    return SendInput(1,clicks+1,sizeof(INPUT))==1 ? 0 : 1;
}
static void CALLBACK inspect_native_menu(HWND window,UINT message,UINT_PTR timer,DWORD time) {
    GUITHREADINFO info={sizeof(info)};
    (void)window; (void)message; (void)time;
    if (GetGUIThreadInfo(GetCurrentThreadId(),&info) && (info.flags&GUI_POPUPMENUMODE)) {
        native_menu_seen=TRUE; KillTimer(NULL,timer); EndMenu();
    }
}
static void CALLBACK fill_title_dialog(HWND window,UINT message,UINT_PTR timer,DWORD time) {
    HWND dialog=thread_window(L"#32770",L"Edit title");
    (void)window; (void)message; (void)time;
    if (dialog && GetWindow(dialog,GW_OWNER)==dialog_note->window) {
        KillTimer(NULL,timer);
        SetDlgItemTextW(dialog,IDC_TITLE,dialog_input);
        SendMessageW(dialog,WM_COMMAND,IDOK,0);
    }
}

static void options_checks(void) {
    Note *note=new_note(), *white, *black, *yellow;
    wchar_t id[80], body[1024], restored[1024];
    HMENU menu;
    BYTE alpha;
    COLORREF key;
    DWORD flags;
    CHARFORMAT2W ink = {0};
    unsigned year,month,day,hour,minute,second,millisecond,suffix;
    int i;
    POINT previous;
    RECT bounds;
    INPUT right_click[2]={{0},{0}};
    UINT_PTR menu_timer;
    require(note!=NULL,"create options note");
    require(swscanf_s(note->id,L"note-%4u-%2u-%2u_%2u-%2u-%2u-%3u-%u",&year,&month,&day,&hour,&minute,&second,&millisecond,&suffix)==8,
            "archive names expose sortable creation date and time");
    menu=build_note_menu(note);
    require(menu && GetMenuItemCount(menu)==11 && GetMenuItemID(menu,10)==CMD_TAGS && GetMenuItemID(menu,0)==CMD_ARCHIVE && GetMenuItemID(menu,1)==CMD_BROWSE &&
            GetMenuItemID(menu,4)==CMD_TITLE && GetMenuItemCount(GetSubMenu(menu,3))==5 &&
            GetMenuItemCount(GetSubMenu(menu,5))==5 && GetMenuItemID(menu,6)==CMD_SCROLL &&
            GetMenuItemCount(GetSubMenu(menu,8))==3,"context menu exposes all requested choices");
    DestroyMenu(menu);
    replace_text(note,L"Saved size preset");
    require(resize_note(note,4),"square preset is available");
    GetWindowRect(note->window,&bounds);
    require(bounds.right-bounds.left==bounds.bottom-bounds.top,"square note has equal width and height");
    wcscpy_s(id,80,note->id);
    require(save_note(note),"save square preset");
    destroy_note(note); note=open_note(id,0,TRUE);
    require(note && note->size==4 && note->width==note->height,"square shape survives reopening");
    require(resize_note(note,1),"return to default size");
    if (interactive_tests) {
        GetCursorPos(&previous); ShowWindow(note->window,SW_SHOWNORMAL);
        SetWindowPos(note->window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
        SetForegroundWindow(note->window); pump(80);
        GetWindowRect(note->window,&bounds);
        SetCursorPos(bounds.left+scaled(note,45),bounds.top+bar_height(note)/2);
        menu_timer=SetTimer(NULL,0,100,inspect_native_menu);
        require(menu_timer!=0,"inspect real right-click menu");
        right_click[0].type=right_click[1].type=INPUT_MOUSE;
        right_click[0].mi.dwFlags=MOUSEEVENTF_RIGHTDOWN; right_click[1].mi.dwFlags=MOUSEEVENTF_RIGHTUP;
        require(SendInput(2,right_click,sizeof(INPUT))==2,"right-click the actual note bar");
        pump(500); KillTimer(NULL,menu_timer);
        SetCursorPos(previous.x,previous.y); ShowWindow(note->window,SW_HIDE);
        require(native_menu_seen,"right-clicking the bar opens the actual native popup menu");
    }
    for (i=0;i<5;++i) {
        note_command(note,CMD_OPACITY+i);
        require(note->opacity==OPACITIES[i],"opacity preset selected");
        if (i) require(GetLayeredWindowAttributes(note->window,&key,&alpha,&flags) &&
                       alpha==(BYTE)MulDiv(255,OPACITIES[i],100),"native window uses requested alpha");
    }
    note_command(note,CMD_OPACITY);
    require(!(GetWindowLongPtrW(note->window,GWL_EXSTYLE)&WS_EX_LAYERED),"opaque preset releases layered rendering");
    require(resize_note(note,3),"increase note size");
    body[0]=0;
    for (i=0;i<12;++i) wcscat_s(body,1024,L"A visible line of text\r\n");
    replace_text(note,body);
    require(!wcscmp(note->accepted_text,body),"larger note accepts more content");
    require(!resize_note(note,0) && note->size==3,"shrinking refuses to hide or truncate existing text");
    require(!wcscmp(note->accepted_text,body),"refused shrink preserves text");
    dialog_note=note; dialog_input=L"Edited title è";
    require(SetTimer(NULL,0,40,fill_title_dialog)!=0,"schedule native title dialog input");
    note_command(note,CMD_TITLE);
    require(note->title && !wcscmp(note->title,dialog_input),"title can be edited through actual dialog");
    dialog_input=L"Double-click title";
    if (interactive_tests) {
        ShowWindow(note->window,SW_SHOWNORMAL);
        SetWindowPos(note->window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
        SetForegroundWindow(note->window); pump(80);
        GetWindowRect(note->window,&bounds); GetCursorPos(&previous);
        SetCursorPos(bounds.left+scaled(note,45),bounds.top+bar_height(note)/2);
        {
            HANDLE input_thread;
            DWORD exit_code;
            UINT_PTR timer=SetTimer(NULL,0,100,fill_title_dialog);
            input_thread=CreateThread(NULL,0,double_click_title,NULL,0,NULL);
            require(timer && input_thread,"schedule a real double click while the native message loop runs");
            pump(500); KillTimer(NULL,timer);
            require(WaitForSingleObject(input_thread,1000)==WAIT_OBJECT_0 &&
                    GetExitCodeThread(input_thread,&exit_code) && exit_code==0,"deliver both physical title clicks");
            CloseHandle(input_thread);
        }
        SetCursorPos(previous.x,previous.y); ShowWindow(note->window,SW_HIDE);
    } else {
        require(SetTimer(NULL,0,40,fill_title_dialog)!=0,"schedule double-click title dialog input");
        SendMessageW(note->window,WM_NCLBUTTONDBLCLK,HTCAPTION,0);
    }
    require(note->title && !wcscmp(note->title,dialog_input) && !note->collapsed,
            "double-click opens the same title editor without collapsing the note");
    dialog_input=L"";
    require(SetTimer(NULL,0,40,fill_title_dialog)!=0,"schedule title removal");
    note_command(note,CMD_TITLE);
    require(note->title==NULL && !wcscmp(note->accepted_text,body),"dialog removes title while preserving body");
    note_command(note,CMD_PALETTE+1);
    white=new_note(); black=new_note();
    require(white && black && white->palette==1 && black->palette==1 &&
            note_color(white)==RGB(255,255,255) && ink_color(white)==RGB(0,0,0) &&
            note_color(black)==RGB(28,28,28) && ink_color(black)==RGB(255,255,255),"monochrome generation preserves contrast");
    ink.cbSize = sizeof(ink);
    SendMessageW(black->edit, EM_GETCHARFORMAT, SCF_DEFAULT, (LPARAM)&ink);
    require(ink.crTextColor == RGB(255,255,255), "rich text on dark notes uses white ink");
    note_command(note,CMD_PALETTE+2);
    yellow=new_note();
    require(yellow && yellow->palette==2 && note_color(yellow)==PALETTES[2][0],"yellow-only generation");
    require(note->palette==0,"palette preference does not recolor existing notes");
    note_command(note,CMD_OPACITY+2);
    wcscpy_s(id,80,note->id);
    require(save_note(note),"save all note options");
    destroy_note(note); note=open_note(id,0,TRUE);
    require(note && note->size==3 && note->opacity==80 && note->palette==0 && !note->title,"size, alpha, palette and deleted title persist");
    GetWindowTextW(note->edit,restored,1024);
    require(!wcscmp(restored,body),"options round trip preserves text");
    SendMessageW(note->window,WM_CLOSE,0,0); SendMessageW(white->window,WM_CLOSE,0,0);
    SendMessageW(black->window,WM_CLOSE,0,0); SendMessageW(yellow->window,WM_CLOSE,0,0);
    current_palette=0; next_color=0;
    puts("PASS: archive names, menu structure, native opacity, safe sizing, title dialog, palettes and option persistence.");
}

static void shortcut(Note *note, UINT key, BOOL shift) {
    BYTE previous[256], state[256];
    require(GetKeyboardState(previous), "read shortcut keyboard state");
    memset(state, 0, sizeof(state));
    state[VK_CONTROL] = state[VK_LCONTROL] = 0x80;
    state[VK_SHIFT] = state[VK_LSHIFT] = shift ? 0x80 : 0;
    require(SetKeyboardState(state), "set shortcut modifiers");
    SendMessageW(note->edit, WM_KEYDOWN, key, 1);
    SendMessageW(note->edit, WM_CHAR, key - 'A' + 1, 1);
    require(SetKeyboardState(previous), "restore shortcut keyboard state");
}

static DWORD effects_at(Note *note, LONG position) {
    CHARRANGE selection;
    CHARFORMAT2W format = {0};
    format.cbSize = sizeof(format);
    SendMessageW(note->edit, EM_EXGETSEL, 0, (LPARAM)&selection);
    SendMessageW(note->edit, EM_SETSEL, position, position + 1);
    SendMessageW(note->edit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&format);
    SendMessageW(note->edit, EM_EXSETSEL, 0, (LPARAM)&selection);
    return format.dwEffects;
}

static int default_line_height(Note *note) {
    TEXTMETRICW metrics;
    HDC dc=GetDC(note->edit);
    HGDIOBJ old=SelectObject(dc,note->font);
    require(trace_metrics(dc,&metrics),"measure the default font for test input coordinates");
    SelectObject(dc,old); ReleaseDC(note->edit,dc);
    return metrics.tmHeight;
}

static void click_checkbox(Note *note, LONG position) {
    POINTL point;
    LPARAM click;
    SendMessageW(note->edit, EM_POSFROMCHAR, (WPARAM)&point, position);
    click = MAKELPARAM(point.x + 2, point.y + default_line_height(note) / 2);
    SendMessageW(note->edit, WM_LBUTTONDOWN, MK_LBUTTON, click);
    SendMessageW(note->edit, WM_LBUTTONUP, 0, click);
}

static void type_input(const wchar_t *text) {
    INPUT input[2] = {{0}, {0}};
    input[0].type = input[1].type = INPUT_KEYBOARD;
    input[0].ki.dwFlags = KEYEVENTF_UNICODE;
    input[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    while (*text) {
        input[0].ki.wScan = input[1].ki.wScan = *text++;
        require(SendInput(2, input, sizeof(INPUT)) == 2, "type actual keyboard text");
    }
    pump(80);
}

static void key_input(WORD key, BOOL shift) {
    INPUT input[4] = {{0}, {0}, {0}, {0}};
    int i;
    for (i = 0; i < 4; ++i) input[i].type = INPUT_KEYBOARD;
    input[0].ki.wVk = input[3].ki.wVk = VK_LSHIFT;
    input[1].ki.wVk = input[2].ki.wVk = key;
    input[2].ki.dwFlags = input[3].ki.dwFlags = KEYEVENTF_KEYUP;
    require(SendInput(shift ? 4 : 2, input + (shift ? 0 : 1), sizeof(INPUT)) == (shift ? 4u : 2u),
            "press actual keyboard keys");
    pump(80);
}

static void keyboard_checks(void) {
    Note *note = new_note();
    INPUT click[2] = {{0}, {0}};
    POINT previous, checkbox;
    POINTL position;
    require(note != NULL, "create real keyboard input note");
    ShowWindow(note->window, SW_SHOWNORMAL);
    SetForegroundWindow(note->window); SetFocus(note->edit); pump(100);
    type_input(L"*");
    require(!wcscmp(note->accepted_text, L"*"), "real keyboard writes the checklist trigger");
    key_input(VK_RETURN, TRUE);
    require(!wcscmp(note->accepted_text, L"\x2610 "), "real Shift+Enter starts a todo");
    type_input(L"Pane"); key_input(VK_RETURN, FALSE);
    require(!wcscmp(note->accepted_text, L"\x2610 Pane\r\n\x2610 "), "real Enter adds exactly one todo");
    type_input(L"Latte"); key_input(VK_RETURN, FALSE); key_input(VK_BACK, FALSE);
    type_input(L"Fine"); key_input(VK_BACK, FALSE);
    require(!wcscmp(note->accepted_text, L"\x2610 Pane\r\n\x2610 Latte\r\nFin"),
            "real Backspace exits an empty todo and deletes one ordinary character");
    key_input(VK_RETURN, FALSE);
    require(!wcscmp(note->accepted_text, L"\x2610 Pane\r\n\x2610 Latte\r\nFin\r\n"),
            "real Enter outside the todo inserts only one line break");
    SendMessageW(note->edit, EM_POSFROMCHAR, (WPARAM)&position, 0);
    checkbox.x = position.x + 2; checkbox.y = position.y + default_line_height(note) / 2;
    ClientToScreen(note->edit, &checkbox); GetCursorPos(&previous); SetCursorPos(checkbox.x, checkbox.y);
    click[0].type = click[1].type = INPUT_MOUSE;
    click[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN; click[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    require(SendInput(2, click, sizeof(INPUT)) == 2, "click a real checkbox");
    pump(80); SetCursorPos(previous.x, previous.y);
    require(note->accepted_text[0] == L'\x2611' && (effects_at(note, 2) & CFE_STRIKEOUT),
            "real mouse click checks and strikes an item");
    screenshot(note, L"build\\keyboard-active.bmp");
    SendMessageW(note->window, WM_CLOSE, 0, 0);
    puts("PASS: real keyboard checklist creation, continuation, Backspace, plain Enter and mouse completion.");
}

static void editing_checks(void) {
    Note *note = new_note();
    wchar_t id[80], overflow[4096];
    const DWORD all = CFE_BOLD | CFE_ITALIC | CFE_UNDERLINE | CFE_STRIKEOUT;
    int i;
    CHARFORMAT2W actual = {0};
    require(note != NULL, "create rich editing note");
    actual.cbSize = sizeof(actual);
    SendMessageW(note->edit, EM_GETCHARFORMAT, SCF_DEFAULT, (LPARAM)&actual);
    require(!wcscmp(actual.szFaceName, L"Consolas") && actual.crTextColor == ink_color(note),
            "native rich editor uses the intended font and palette ink");
    replace_text(note, L"Testo è 😀");
    SendMessageW(note->edit, EM_SETSEL, 0, 5);
    shortcut(note, 'B', FALSE);
    require(!(effects_at(note, 1) & CFE_BOLD), "Ctrl+B toggles existing bold off");
    shortcut(note, 'B', FALSE); shortcut(note, 'I', FALSE);
    shortcut(note, 'U', FALSE); shortcut(note, 'X', TRUE);
    require((effects_at(note, 1) & all) == all && !(effects_at(note, 6) & (all & ~CFE_BOLD)),
            "all shortcuts format only the selection without inserting control characters");
    require(!wcscmp(note->accepted_text, L"Testo è 😀"), "formatting preserves Unicode text");
    for (i = 0; i < 4095; ++i) overflow[i] = L'W';
    overflow[4095] = 0;
    replace_text(note, overflow);
    require(!wcscmp(note->accepted_text, L"Testo è 😀") && (effects_at(note, 1) & all) == all,
            "overflow rollback preserves both content and character formatting");
    require(save_note(note), "save rich character styles");
    wcscpy_s(id, 80, note->id); destroy_note(note); note = open_note(id, 0, TRUE);
    require(note && (effects_at(note, 1) & all) == all && !(effects_at(note, 6) & CFE_ITALIC),
            "selected formatting survives reopening");
    SendMessageW(note->edit, EM_SETSEL, 0, 5); shortcut(note, 'I', FALSE);
    require(save_note(note), "save a format-only edit");
    shortcut(note, 'Z', FALSE);
    require(note->dirty && (effects_at(note, 1) & CFE_ITALIC), "undoing a format-only edit schedules autosave");
    SendMessageW(note->window, WM_CLOSE, 0, 0);

    note = new_note();
    replace_text(note, L"*"); press_enter(note, TRUE);
    require(!wcscmp(note->accepted_text, L"\x2610 "), "asterisk and Shift+Enter start a checklist in place");
    SendMessageW(note->edit, EM_REPLACESEL, TRUE, (LPARAM)L"Primo"); press_enter(note, FALSE);
    SendMessageW(note->edit, EM_REPLACESEL, TRUE, (LPARAM)L"Secondo"); press_enter(note, FALSE);
    require(!wcscmp(note->accepted_text, L"\x2610 Primo\r\n\x2610 Secondo\r\n\x2610 "),
            "Enter continues the checklist");
    SendMessageW(note->edit, WM_KEYDOWN, VK_BACK, 1);
    SendMessageW(note->edit, WM_CHAR, L'\b', 1);
    SendMessageW(note->edit, WM_KEYUP, VK_BACK, 1);
    SendMessageW(note->edit, EM_REPLACESEL, TRUE, (LPARAM)L"Normale");
    require(!wcscmp(note->accepted_text, L"\x2610 Primo\r\n\x2610 Secondo\r\nNormale"),
            "Backspace removes the final empty item and resumes ordinary text");
    click_checkbox(note, 0);
    require(note->accepted_text[0] == L'\x2611' && (effects_at(note, 2) & CFE_STRIKEOUT) &&
            !(effects_at(note, 10) & CFE_STRIKEOUT), "checking strikes only that item's text");
    SendMessageW(note->edit, WM_UNDO, 0, 0);
    require(note->accepted_text[0] == L'\x2610' && !(effects_at(note, 2) & CFE_STRIKEOUT),
            "one undo restores both checkbox and text style");
    SendMessageW(note->edit, EM_REDO, 0, 0);
    require(note->accepted_text[0] == L'\x2611' && (effects_at(note, 2) & CFE_STRIKEOUT),
            "redo restores the completed item");
    click_checkbox(note, 8);
    require((effects_at(note, 10) & CFE_STRIKEOUT), "checkbox on a later paragraph is clickable");
    click_checkbox(note, 8);
    require(!(effects_at(note, 10) & CFE_STRIKEOUT), "unchecking restores ordinary text");
    require(save_note(note), "save checklist");
    expect_file(note->path, "☑ Primo\r\n☐ Secondo\r\nNormale");
    wcscpy_s(id, 80, note->id); destroy_note(note); note = open_note(id, 0, TRUE);
    require(note && note->accepted_text[0] == L'\x2611' && (effects_at(note, 2) & CFE_STRIKEOUT),
            "checkbox state and strikethrough survive reopening");
    SendMessageW(note->edit, EM_SETSEL, 7, 7); press_enter(note, FALSE);
    SendMessageW(note->edit, EM_REPLACESEL, TRUE, (LPARAM)L"Nuovo");
    require(!(effects_at(note, 10) & CFE_STRIKEOUT), "new item after a completed item is not struck");
    press_enter(note, FALSE); press_enter(note, FALSE);
    SendMessageW(note->edit, EM_REPLACESEL, TRUE, (LPARAM)L"Libero");
    require(wcsstr(note->accepted_text, L"\r\nLibero\r\n") != NULL,
            "Enter on an empty item also exits the checklist");
    require(!(effects_at(note, 10) & CFE_STRIKEOUT), "unchecked text stays ordinary after continuing the list");
    screenshot(note, L"build\\checklist.bmp");
    SendMessageW(note->window, WM_CLOSE, 0, 0);
    puts("PASS: formatting shortcuts, styled overflow rollback, checklists, checkbox undo/redo and rich persistence.");
}

static POINT drag_target;
static int drag_phase;
static void CALLBACK resize_drag(HWND window, UINT message, UINT_PTR timer, DWORD time) {
    INPUT release = {0};
    (void)window; (void)message; (void)time;
    if (!drag_phase++) { SetCursorPos(drag_target.x, drag_target.y); return; }
    KillTimer(NULL, timer);
    release.type = INPUT_MOUSE; release.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &release, sizeof(release));
}

static void resizing_checks(void) {
    Note *note = new_note();
    RECT bounds, proposed;
    wchar_t id[80];
    int width, height, i;
    POINT previous;
    INPUT down = {0};
    const int hits[] = {HTTOPLEFT, HTTOPRIGHT, HTBOTTOMLEFT, HTBOTTOMRIGHT};
    BOOL nonclient_rendering = TRUE;
    require(note != NULL, "create custom sizing note");
    replace_text(note,L"Resize me");
    require(SUCCEEDED(DwmGetWindowAttribute(note->window, DWMWA_NCRENDERING_ENABLED,
                                          &nonclient_rendering, sizeof(nonclient_rendering))) && !nonclient_rendering,
            "native resize handles do not enable the standard gray window frame");
    SetWindowPos(note->window, NULL, 120, 140, scaled(note, 370), scaled(note, 210), SWP_NOZORDER | SWP_NOACTIVATE);
    width = note->width; height = note->height;
    GetWindowRect(note->window, &bounds);
    for (i = 0; i < 4; ++i)
        require(SendMessageW(note->window, WM_NCHITTEST, 0,
                            MAKELPARAM((i & 1) ? bounds.right - 1 : bounds.left + 1,
                                       (i & 2) ? bounds.bottom - 1 : bounds.top + 1)) == hits[i],
                "all four corners expose native resize handles");
    if (interactive_tests) {
        GetCursorPos(&previous); ShowWindow(note->window, SW_SHOWNORMAL);
        SetForegroundWindow(note->window); pump(80);
        SetCursorPos(bounds.right - 1, bounds.bottom - 1);
        drag_target.x = bounds.right + 39; drag_target.y = bounds.bottom + 29; drag_phase = 0;
        require(SetTimer(NULL, 0, 100, resize_drag) != 0, "schedule a real native resize drag");
        down.type = INPUT_MOUSE; down.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        require(SendInput(1, &down, sizeof(down)) == 1, "press the real bottom-right corner");
        pump(450); SetCursorPos(previous.x, previous.y); ShowWindow(note->window, SW_HIDE);
        require(note->width > width && note->height > height, "a real corner drag freely resizes the native window");
        width = note->width; height = note->height;
        GetWindowRect(note->window, &bounds);
    }
    SendMessageW(note->edit, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, WHEEL_DELTA / 2), 0);
    require(note->width == width && note->height == height, "partial wheel steps accumulate");
    SendMessageW(note->edit, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, WHEEL_DELTA / 2), 0);
    require(note->width > width && note->height > height &&
            abs(note->height - MulDiv(height, note->width, width)) <= 1,
            "Ctrl+wheel on the editor scales the current aspect ratio");
    SendMessageW(note->window, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, -WHEEL_DELTA), 0);
    require(abs(note->width - width) <= 1 && abs(note->height - height) <= 1,
            "reverse wheel scales the note down");
    proposed = bounds; proposed.right += scaled(note, 50); proposed.bottom += scaled(note, 70);
    require(SendMessageW(note->window, WM_SIZING, WMSZ_BOTTOMRIGHT, (LPARAM)&proposed) != 0, "native sizing gesture");
    SetWindowPos(note->window, NULL, proposed.left, proposed.top, proposed.right - proposed.left,
                 proposed.bottom - proposed.top, SWP_NOZORDER | SWP_NOACTIVATE);
    width = note->width; height = note->height;
    toggle_note(note);
    SendMessageW(note->window, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, WHEEL_DELTA), 0);
    require(note->height > height && note_height(note) == bar_height(note),
            "a collapsed note scales its stored expanded dimensions");
    toggle_note(note);
    width = note->width; height = note->height;
    wcscpy_s(id, 80, note->id); require(save_note(note), "save arbitrary dimensions");
    destroy_note(note); note = open_note(id, 0, TRUE);
    require(note && note->width == width && note->height == height && note->size == -1,
            "arbitrary dimensions and aspect ratio survive reopening");
    SendMessageW(note->window, WM_CLOSE, 0, 0);
    puts("PASS: corner resize handles, proportional Ctrl+wheel, partial wheel input, collapsed resizing and persistence.");
}

static HTREEITEM find_date(HWND tree, HTREEITEM item, unsigned date) {
    while (item) {
        TVITEMW value={0};
        HTREEITEM child;
        value.hItem=item; value.mask=TVIF_PARAM;
        TreeView_GetItem(tree,&value);
        if ((unsigned)value.lParam==date) return item;
        child=find_date(tree,TreeView_GetChild(tree,item),date);
        if (child) return child;
        item=TreeView_GetNextSibling(tree,item);
    }
    return NULL;
}

static void archive_fixture(const wchar_t *id,const char *text) {
    wchar_t path[PATH_CAP];
    HANDLE file;
    DWORD written, length=(DWORD)strlen(text);
    swprintf_s(path,PATH_CAP,L"%s\\%s.txt",directory,id);
    file=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    require(file!=INVALID_HANDLE_VALUE,"create dated archive fixture");
    require(WriteFile(file,text,length,&written,NULL) && written==length,"write archive preview fixture");
    CloseHandle(file);
    require(write_setting(id,L"open",FALSE) && write_setting(id,L"has_title",TRUE),"archive fixture metadata");
}

static void CALLBACK confirm_archive_delete(HWND window,UINT message,UINT_PTR timer,DWORD time) {
    HWND dialog=thread_window(L"#32770",L"Jotilla");
    (void)window; (void)message; (void)time;
    if (dialog && GetWindow(dialog,GW_OWNER)==thread_window(L"Jotilla.Archive",NULL)) {
        KillTimer(NULL,timer); SendMessageW(dialog,WM_COMMAND,IDYES,0);
    }
}

static BOOL archive_menu_seen;
static void CALLBACK choose_archive_delete(HWND window,UINT message,UINT_PTR timer,DWORD time) {
    GUITHREADINFO info={sizeof(info)};
    INPUT keys[4]={{0},{0},{0},{0}};
    int i;
    (void)window; (void)message; (void)time;
    if (!GetGUIThreadInfo(GetCurrentThreadId(),&info) || !(info.flags&GUI_POPUPMENUMODE)) return;
    KillTimer(NULL,timer); archive_menu_seen=TRUE;
    for (i=0;i<4;++i) { keys[i].type=INPUT_KEYBOARD; keys[i].ki.wVk=i<2?VK_DOWN:VK_RETURN; }
    keys[1].ki.dwFlags=keys[3].ki.dwFlags=KEYEVENTF_KEYUP;
    SendInput(4,keys,sizeof(INPUT));
}

static void archive_selection_colors(HWND tree, HWND list, HTREEITEM selected) {
    RECT bounds, label;
    HDC dc=GetDC(tree), memory=CreateCompatibleDC(dc);
    HBITMAP bitmap;
    HGDIOBJ old;
    int focused;
    GetClientRect(tree,&bounds);
    bitmap=CreateCompatibleBitmap(dc,bounds.right,bounds.bottom);
    require(memory && bitmap,"allocate native tree rendering check");
    old=SelectObject(memory,bitmap);
    TreeView_SelectItem(tree,selected); TreeView_EnsureVisible(tree,selected);
    require(TreeView_GetItemRect(tree,selected,&label,TRUE),"locate selected date label");
    for (focused=0;focused<2;++focused) {
        SetFocus(focused ? tree : list);
        require(PrintWindow(tree,memory,PW_CLIENTONLY),"render the actual date tree");
        require(GetPixel(memory,label.right-1,label.top+1)==RGB(211,194,162),
                "focused and unfocused date selections use the beige highlight");
    }
    SelectObject(memory,old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(tree,dc);
}

/* A resize must paint exactly the same toolbar as a subsequent full repaint.
   Capture the existing pixels first: PrintWindow would hide stale paint bugs. */
static void archive_resize_checks(HWND window) {
    const int widths[] = {740, 960, 520, 820};
    HWND edit = GetDlgItem(window, 3), list = GetDlgItem(window, 2);
    wchar_t query[SEARCH_CAP], actual[SEARCH_CAP];
    RECT original;
    int dpi = (int)GetDpiForWindow(window), mode, step;
    GetWindowTextW(edit, query, SEARCH_CAP); GetWindowRect(window, &original);
    SetFocus(list);
    for (mode = 0; mode < 2; ++mode) {
        const wchar_t *input = mode ? L"Resize: café and tags" : L"";
        SetWindowTextW(edit, input); pump(200);
        for (step = 0; step < 7; ++step) {
            RECT strip, list_rect;
            BITMAPINFO info = {0};
            HDC dc, memory;
            HBITMAP bitmap;
            HGDIOBJ previous;
            void *pixels, *before;
            size_t bytes;
            BOOL same;
            if (step < 4) SetWindowPos(window, NULL, 0, 0, MulDiv(widths[step], dpi, 96), MulDiv(440, dpi, 96),
                                       SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            else if (step < 6) ShowWindow(window, step == 4 ? SW_MAXIMIZE : SW_RESTORE);
            else {
                int width, pass;
                /* Coalesce repeated size changes without pumping WM_PAINT,
                   first from the right edge, then from the left edge. */
                for (pass = 0; pass < 2; ++pass)
                    for (width = 520; width <= 960; width += 11) {
                        int pixels_wide = MulDiv(pass ? 1480 - width : width, dpi, 96);
                        SetWindowPos(window, NULL, pass ? original.right - pixels_wide : original.left, original.top,
                                     pixels_wide, MulDiv(440, dpi, 96), SWP_NOZORDER | SWP_NOACTIVATE);
                    }
            }
            pump(50);
            GetClientRect(window, &strip); GetWindowRect(list, &list_rect);
            MapWindowPoints(NULL, window, (POINT *)&list_rect, 2); strip.bottom = list_rect.top;
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = strip.right; info.bmiHeader.biHeight = -strip.bottom;
            info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
            bytes = (size_t)strip.right * (size_t)strip.bottom * 4;
            dc = GetDC(window); memory = CreateCompatibleDC(dc);
            bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, NULL, 0);
            before = malloc(bytes);
            require(memory && bitmap && before, "allocate toolbar repaint comparison");
            previous = SelectObject(memory, bitmap);
            require(BitBlt(memory, 0, 0, strip.right, strip.bottom, dc, 0, 0, SRCCOPY), "capture resized toolbar");
            GdiFlush(); memcpy(before, pixels, bytes);
            RedrawWindow(window, &strip, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            require(BitBlt(memory, 0, 0, strip.right, strip.bottom, dc, 0, 0, SRCCOPY), "capture fully repainted toolbar");
            GdiFlush(); same = !memcmp(before, pixels, bytes);
            SelectObject(memory, previous); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(window, dc); free(before);
            require(same, "resizing, maximizing and restoring leave no stale search-bar pixels");
            GetWindowTextW(edit, actual, SEARCH_CAP);
            require(!wcscmp(actual, input), "resizing preserves empty and Unicode search text");
        }
    }
    SetWindowPos(window, NULL, original.left, original.top, original.right - original.left, original.bottom - original.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowTextW(edit, query); pump(200);
    puts("PASS: search toolbar repaint after narrowing, widening, maximizing, restoring and rapid resize bursts, with empty and Unicode queries.");
}

static void archive_checks(void) {
    const wchar_t *first=L"note-2024-01-03_08-00-00-000-001", *second=L"note-2024-02-04_09-00-00-000-001";
    const wchar_t *third=L"note-2025-03-05_10-00-00-000-001";
    HWND window,tree,list;
    HTREEITEM year,month,day;
    NMITEMACTIVATE click={0};
    wchar_t preview[256],path[PATH_CAP],id[80];
    Note *note;
    HANDLE lock;
    int before;
    RECT row;
    archive_fixture(first,"#Spesa è urgente\r\nPane, caffè e latte");
    archive_fixture(second,"#Viaggio\r\nPartenza domani");
    archive_fixture(third,"#Progetto\r\nUna nota di un altro anno");
    browse_notes();
    window=thread_window(L"Jotilla.Archive",NULL);
    require(window!=NULL,"archive opens through the note menu command");
    tree=GetDlgItem(window,1); list=GetDlgItem(window,2);
    require(tree && list,"archive consists of a native date tree and a virtual preview list");
    year=find_date(tree,TreeView_GetRoot(tree),20240000);
    month=find_date(tree,TreeView_GetRoot(tree),20240100);
    day=find_date(tree,TreeView_GetRoot(tree),20240103);
    require(year && month && day && TreeView_GetParent(tree,day)==month && TreeView_GetParent(tree,month)==year,
            "archive tree groups notes by creation year, month and day");
    archive_selection_colors(tree,list,year);
    archive_selection_colors(tree,list,month);
    {
        TVITEMW label={0};
        label.mask=TVIF_TEXT; label.hItem=month; label.pszText=preview; label.cchTextMax=256;
        require(TreeView_GetItem(tree,&label) && !wcscmp(preview,L"January"),
                "archive month names stay English independently of the Windows locale");
    }
    TreeView_SelectItem(tree,day); pump(40);
    require(ListView_GetItemCount(list)==1,"selecting a day filters the virtual list");
    ListView_GetItemText(list,0,0,preview,256);
    require(!wcscmp(preview,L"Spesa è urgente"),"bounded lazy previews preserve UTF-8 accents");
    ListView_GetItemRect(list,0,&row,LVIR_BOUNDS);
    require(row.bottom-row.top>=MulDiv(110,(int)GetDpiForWindow(window),96),"previews have post-it sized rows");
    before=note_count();
    click.hdr.hwndFrom=list; click.hdr.code=NM_CLICK; click.iItem=0;
    SendMessageW(window,WM_NOTIFY,2,(LPARAM)&click);
    note=find_note(first);
    require(note && note_count()==before+1 && note->title && !wcscmp(note->title,L"Spesa è urgente"),
            "clicking a preview restores the archived post-it");
    toggle_note(note);
    SendMessageW(window,WM_NOTIFY,2,(LPARAM)&click);
    require(find_note(first)==note && !note->collapsed && note_count()==before+1,
            "opening an existing note expands it without creating duplicates");
    ListView_SetItemState(list,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    require(set_title(note,L"Updated title"),"edit title while its preview is cached");
    replace_text(note,L"Updated body"); pump(650);
    ListView_GetItemText(list,0,0,preview,256);
    require(!wcscmp(preview,L"Updated title"),"autosave updates the open browser without reopening or F5");
    require(TreeView_GetSelection(tree)==day &&
            ListView_GetItemState(list,0,LVIS_SELECTED|LVIS_FOCUSED)==(LVIS_SELECTED|LVIS_FOCUSED),
            "updating a preview preserves date and tile selection");
    require(set_title(note,L""),"remove title while the browser stays open"); pump(650);
    ListView_GetItemText(list,0,0,preview,256);
    require(!wcscmp(preview,L"Updated body"),"preview metadata updates together with the saved body");
    replace_text(note,L"\x2610 Task"); pump(650);
    click_checkbox(note,0); pump(650);
    ListView_GetItemText(list,0,0,preview,256);
    require(!wcscmp(preview,L"\x2611 Task"),"checking a task updates the cached preview");
    {
        Note *added;
        HTREEITEM added_day;
        UINT expansion;
        TreeView_SelectItem(tree,year);
        ListView_GetItemText(list,0,0,preview,256);
        require(!wcscmp(preview,L"Viaggio"),"cache an unchanged neighboring preview");
        swprintf_s(path,PATH_CAP,L"%s\\%s.txt",directory,second);
        lock=CreateFileW(path,GENERIC_READ,0,NULL,OPEN_EXISTING,0,NULL);
        require(lock!=INVALID_HANDLE_VALUE,"deny rereading the unchanged preview during insertion");
        TreeView_Expand(tree,month,TVE_EXPAND);
        expansion=TreeView_GetItemState(tree,month,TVIS_EXPANDED);
        ListView_SetItemState(list,1,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        added=open_note(L"note-2024-01-04_08-00-00-000-001",0,FALSE);
        require(added!=NULL,"create a note while the archive is open");
        replace_text(added,L"A new archived note"); pump(650);
        added_day=find_date(tree,TreeView_GetRoot(tree),20240104);
        require(added_day && ListView_GetItemCount(list)==3 && TreeView_GetSelection(tree)==year,
                "new saved notes join the current archive filter automatically");
        ListView_GetItemText(list,0,0,preview,256);
        require(!wcscmp(preview,L"Viaggio"),"membership refresh reuses unaffected cached previews without disk reads");
        CloseHandle(lock);
        require(find_date(tree,TreeView_GetRoot(tree),20240100)==month &&
                TreeView_GetItemState(tree,month,TVIS_EXPANDED)==expansion &&
                ListView_GetItemState(list,2,LVIS_SELECTED|LVIS_FOCUSED)==(LVIS_SELECTED|LVIS_FOCUSED),
                "insertion preserves existing tree nodes, expansion and the selected note's identity");
        TreeView_SelectItem(tree,added_day);
        replace_text(added,L""); pump(650);
        require(!find_date(tree,TreeView_GetRoot(tree),20240104) &&
                TreeView_GetSelection(tree)==month && ListView_GetItemCount(list)==1 && !added->persisted,
                "clearing the last note of a day removes the date and selects its surviving month");
        destroy_note(added);
        TreeView_SelectItem(tree,day);
        {
            const wchar_t *new_month=L"note-2024-03-01_08-00-00-000-001";
            const wchar_t *new_year=L"note-2023-07-06_08-00-00-000-001";
            added=open_note(new_month,0,FALSE);
            require(added!=NULL,"create a note in a new month");
            replace_text(added,L"New month"); require(save_note(added),"save new month");
            added=open_note(new_year,0,FALSE);
            require(added!=NULL,"create a note in a new year");
            replace_text(added,L"New year"); require(save_note(added),"save new year"); pump(40);
            require(find_date(tree,TreeView_GetRoot(tree),20240300) &&
                    find_date(tree,TreeView_GetRoot(tree),20230000) && TreeView_GetSelection(tree)==day &&
                    ListView_GetItemCount(list)==1,"batched additions merge new months and years without disturbing the current date");
            require(delete_archived_note(new_month) && delete_archived_note(new_year),"remove new date groups"); pump(40);
            require(!find_date(tree,TreeView_GetRoot(tree),20240300) &&
                    !find_date(tree,TreeView_GetRoot(tree),20230000) && TreeView_GetSelection(tree)==day,
                    "deleting a last note prunes empty month and year branches");
        }
    }
    {
        Note *legacy=open_note(L"nota-legacy-test",0,FALSE);
        int index, count;
        require(legacy!=NULL,"create a legacy filename fixture");
        replace_text(legacy,L"Legacy preview"); require(save_note(legacy),"save the legacy fixture"); pump(40);
        TreeView_SelectItem(tree,TreeView_GetRoot(tree));
        count=ListView_GetItemCount(list);
        for (index=0;index<count;++index) {
            ListView_GetItemText(list,index,0,preview,256);
            if (!wcscmp(preview,L"Legacy preview")) break;
        }
        require(index<count,"legacy filenames remain browsable using their file creation date");
        replace_text(legacy,L"Updated legacy preview"); pump(650);
        ListView_GetItemText(list,index,0,preview,256);
        require(!wcscmp(preview,L"Updated legacy preview"),"live invalidation also supports legacy filenames");
        require(delete_archived_note(legacy->id),"remove the legacy fixture"); pump(40);
        TreeView_SelectItem(tree,day);
    }
    require(set_title(note,L"Spesa è urgente"),"restore the fixture title");
    replace_text(note,L"Pane, caffè e latte"); require(save_note(note),"create formatting companion for deletion check");
    ListView_GetItemText(list,0,0,preview,256);
    lock=CreateFileW(note->path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    require(lock!=INVALID_HANDLE_VALUE && set_title(note,L"Pending title") && !save_note(note),
            "block replacement while a changed title is pending");
    ListView_GetItemText(list,0,0,preview,256);
    require(!wcscmp(preview,L"Spesa è urgente"),"a failed save leaves the last saved preview intact");
    CloseHandle(lock); pump(1150);
    ListView_GetItemText(list,0,0,preview,256);
    require(!wcscmp(preview,L"Pending title"),"a successful autosave retry updates the browser");
    require(set_title(note,L"Spesa è urgente") && save_note(note),"restore saved title after the retry test");
    swprintf_s(path,PATH_CAP,L"%s\\%s.rtf",directory,first);
    lock=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    require(lock!=INVALID_HANDLE_VALUE && !delete_archived_note(first),"locked formatting blocks deletion of the whole note");
    swprintf_s(path,PATH_CAP,L"%s\\%s.txt",directory,first);
    require(GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES && find_note(first)==note,
            "a refused deletion preserves the body and open post-it");
    CloseHandle(lock);
    swprintf_s(path,PATH_CAP,L"%s\\%s.rtf",directory,first);
    require(SetFileAttributesW(path,FILE_ATTRIBUTE_READONLY) && !delete_archived_note(first),
            "read-only formatting refuses a partially completed deletion");
    require(SetFileAttributesW(path,FILE_ATTRIBUTE_NORMAL),"restore test companion permissions");
    swprintf_s(path,PATH_CAP,L"%s\\%s.txt",directory,first);
    require(GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES && find_note(first)==note,
            "a failed second disposition rolls back deletion of the TXT");
    TreeView_SelectItem(tree,year);
    require(ListView_GetItemCount(list)==2,"year filter includes both months");
    {
        RECT other;
        LOGFONTW font;
        require((HICON)GetClassLongPtrW(window,GCLP_HICON)==app_icon,"archive title bar uses the Jotilla icon");
        require(GetObjectW((HFONT)SendMessageW(tree,WM_GETFONT,0,0),sizeof(font),&font) &&
                !wcscmp(font.lfFaceName,L"Consolas"),"archive uses a monospaced font");
        ListView_GetItemRect(list,0,&row,LVIR_ICON); ListView_GetItemRect(list,1,&other,LVIR_ICON);
        require(row.top==other.top && row.right<=other.left,"post-it previews form a native mosaic");
    }
    archive_resize_checks(window);
    {
        Note view={0};
        view.window=window;
        screenshot(&view,L"build\\archive.bmp");
        ShowWindow(window,SW_SHOWNORMAL);
    }
    ListView_SetItemState(list,-1,LVIS_SELECTED,LVIS_SELECTED);
    if (interactive_tests) {
        INPUT clicks[2]={{0},{0}};
        POINT previous,point;
        UINT_PTR choose_timer=SetTimer(NULL,0,60,choose_archive_delete);
        UINT_PTR confirm_timer=SetTimer(NULL,0,60,confirm_archive_delete);
        require(choose_timer && confirm_timer,"schedule deletion of isolated test fixtures through the real menu");
        SetForegroundWindow(window); pump(80);
        ListView_GetItemRect(list,0,&row,LVIR_BOUNDS);
        point.x=row.left+40; point.y=(row.top+row.bottom)/2;
        ClientToScreen(list,&point); GetCursorPos(&previous); SetCursorPos(point.x,point.y);
        clicks[0].type=clicks[1].type=INPUT_MOUSE;
        clicks[0].mi.dwFlags=MOUSEEVENTF_RIGHTDOWN; clicks[1].mi.dwFlags=MOUSEEVENTF_RIGHTUP;
        require(SendInput(2,clicks,sizeof(INPUT))==2,"right-click the selected archive previews");
        pump(500); KillTimer(NULL,choose_timer); KillTimer(NULL,confirm_timer);
        SetCursorPos(previous.x,previous.y);
        require(archive_menu_seen,"native right-click exposes archive deletion");
    } else {
        NMLVKEYDOWN key={0};
        key.hdr.hwndFrom=list; key.hdr.code=LVN_KEYDOWN; key.wVKey=VK_DELETE;
        require(SetTimer(NULL,0,60,confirm_archive_delete)!=0,"confirm deletion of isolated test fixtures");
        SendMessageW(window,WM_NOTIFY,2,(LPARAM)&key);
    }
    require(!find_note(first) && GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES,"multi-delete removes an open note and its TXT");
    swprintf_s(path,PATH_CAP,L"%s\\%s.rtf",directory,first);
    require(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES,"multi-delete removes the RTF companion too");
    swprintf_s(path,PATH_CAP,L"%s\\%s.txt",directory,second);
    require(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES && setting(first,L"has_title",-1)==-1,
            "multi-delete removes the second note and obsolete metadata");
    swprintf_s(path,PATH_CAP,L"%s\\%s.txt",directory,third);
    require(GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES,"multi-delete preserves unselected dates");
    require(delete_archived_note(third),"delete a single archived note without an RTF");
    wcscpy_s(id,80,notes->id);
    browse_notes();
    require(thread_window(L"Jotilla.Archive",NULL)==window && !wcscmp(notes->id,id),"archive refresh reuses its window without changing notes");
    archive_close();
    {
        wchar_t empty[PATH_CAP];
        const ArchiveActions actions={save_all,open_archived_note,delete_archived_note,choose_archive};
        swprintf_s(empty,PATH_CAP,L"%s\\empty",directory);
        require(CreateDirectoryW(empty,NULL),"create an empty archive fixture");
        window=archive_show(controller,empty,state_path,&actions);
        require(window && ListView_GetItemCount(GetDlgItem(window,2))==0,"empty archives open normally");
        archive_close(); require(RemoveDirectoryW(empty),"remove empty archive fixture");
    }
    puts("PASS: live archive previews, date-tree reconciliation, stable selection, Unicode, reopening and safe multi-delete.");
}

static SearchResult *wait_search(SearchWorker *worker) {
    ULONGLONG deadline = GetTickCount64() + 10000;
    SearchResult *result;
    do {
        result = search_take(worker);
        if (result) return result;
        pump(5);
    } while (GetTickCount64() < deadline);
    require(FALSE, "search worker completes without blocking the UI"); return NULL;
}

static void wait_search_ui(HWND window) {
    ULONGLONG deadline = GetTickCount64() + 10000;
    wchar_t status[128];
    do {
        pump(10); GetWindowTextW(window, status, 128);
        if (!wcsstr(status, L"Searching")) return;
    } while (GetTickCount64() < deadline);
    require(FALSE, "archive search completes");
}

static HTREEITEM tag_label(HWND tree, const wchar_t *label) {
    HTREEITEM node = TreeView_GetChild(tree, TreeView_GetRoot(tree));
    while (node) {
        wchar_t text[128];
        TVITEMW item = {0};
        item.mask = TVIF_TEXT; item.hItem = node; item.pszText = text; item.cchTextMax = 128;
        require(TreeView_GetItem(tree, &item) && !wcschr(text, L'#'), "sidebar tag labels contain names and counts without hash prefixes");
        if (!wcscmp(text, label)) return node;
        node = TreeView_GetNextSibling(tree, node);
    }
    return NULL;
}

static void sidebar_checks(HWND window, Note *note) {
    HWND dates = GetDlgItem(window, 1), list = GetDlgItem(window, 2), edit = GetDlgItem(window, 3);
    HWND tags = GetDlgItem(window, 6), tag_button = GetDlgItem(window, 7), date_button = GetDlgItem(window, 8);
    HTREEITEM month = find_date(dates, TreeView_GetRoot(dates), 20310200), work;
    BYTE previous[256], keys[256];
    RECT button, tree, card, bounds;
    int dpi = (int)GetDpiForWindow(window), i;
    Note view = {0};
    require(tags && tag_button && date_button && !GetDlgItem(window, 5), "sidebar mode selectors replace the search description");
    SetWindowTextW(edit, L""); TreeView_SelectItem(dates, month);
    require(ListView_GetItemCount(list) == 2, "remember a date range before changing sidebar mode");
    SendMessageW(tag_button, BM_CLICK, 0, 0);
    require(IsWindowVisible(tags) && !IsWindowVisible(dates), "Tags displays its native tag list");
    work = tag_label(tags, L"work (2)");
    require(work && tag_label(tags, L"café (1)") && tag_label(tags, L"project alpha (1)"),
            "sidebar counts include open and closed notes across all dates");
    TreeView_SelectItem(tags, work); wait_search_ui(window);
    require(ListView_GetItemCount(list) == 2 && GetWindowTextLengthW(edit) == 0, "tag selection filters without changing the query field");
    SetWindowTextW(edit, L"budget"); wait_search_ui(window);
    require(ListView_GetItemCount(list) == 1, "sidebar tag and free text intersect");
    {
        HANDLE metadata = CreateFileW(state_path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        require(metadata != INVALID_HANDLE_VALUE, "lock cached tag metadata against rereading");
        replace_text(note, L"Updated budget for the November release.");
        require(save_note(note), "body-only autosave does not touch tag metadata");
        wait_search_ui(window);
        {
            wchar_t title[192]; GetWindowTextW(window, title, 192);
            require(!wcscmp(title, L"Notes archive") && ListView_GetItemCount(list) == 1 &&
                    TreeView_GetSelection(tags) == work,
                    "body updates reuse cached tags and preserve the sidebar without reopening metadata");
        }
        CloseHandle(metadata);
    }
    require(set_tags(note, L"café") && save_note(note), "change a sidebar tag's membership");
    wait_search_ui(window);
    require(tag_label(tags, L"work (1)") && ListView_GetItemCount(list) == 0, "sidebar counts and active results update after a tag save");
    require(set_tags(note, L"café;work") && save_note(note), "restore sidebar tag membership");
    wait_search_ui(window);
    require(TreeView_GetSelection(tags) == tag_label(tags, L"work (2)") && ListView_GetItemCount(list) == 1,
            "rebuilding tag counts preserves selection by name");
    SetWindowTextW(edit, L""); wait_search_ui(window);
    SendMessageW(date_button, BM_CLICK, 0, 0);
    require(IsWindowVisible(dates) && !IsWindowVisible(tags) && TreeView_GetSelection(dates) == month &&
            ListView_GetItemCount(list) == 2, "Date restores the same date nodes and selection");
    SendMessageW(tag_button, BM_CLICK, 0, 0); wait_search_ui(window);
    require(TreeView_GetSelection(tags) == tag_label(tags, L"work (2)") && ListView_GetItemCount(list) == 2,
            "returning to Tags restores its independent filter");
    GetKeyboardState(previous); memcpy(keys, previous, sizeof(keys)); keys[VK_CONTROL] = 0x80;
    SetKeyboardState(keys);
    {
        HWND controls[] = {window, tags, tag_button, date_button, edit, GetDlgItem(window, 4), list, GetDlgItem(window, 9)};
        for (i = 0; i < (int)(sizeof(controls) / sizeof(controls[0])); ++i) {
            SetFocus(controls[i]); SendMessageW(controls[i], WM_KEYDOWN, 'F', 0);
            require(GetFocus() == edit, "Ctrl+F focuses search from every archive surface");
        }
    }
    SetKeyboardState(previous);
    GetWindowRect(tag_button, &button); GetWindowRect(tags, &tree);
    require(tree.top - button.bottom >= MulDiv(8, dpi, 96), "sidebar has deliberate spacing below its selectors");
    {
        HDC dc = GetDC(list), memory = CreateCompatibleDC(dc);
        HBITMAP bitmap;
        HGDIOBJ old;
        GetClientRect(list, &bounds); ListView_GetItemRect(list, 0, &card, LVIR_ICON);
        bitmap = CreateCompatibleBitmap(dc, bounds.right, bounds.bottom); old = SelectObject(memory, bitmap);
        require(bitmap && PrintWindow(list, memory, PW_CLIENTONLY), "render tag footers");
        {
            int x = card.left + MulDiv(4, dpi, 96), y = card.top + MulDiv(4, dpi, 96);
            require(GetPixel(memory, x, y + 10) != RGB(0,0,0) &&
                    GetPixel(memory, x + 1, y + 10) != RGB(0,0,0) &&
                    GetPixel(memory, x + 10, y + MulDiv(28, dpi, 96) - 1) != RGB(0,0,0),
                    "archive previews omit the note's black outline and divider");
        }
        require(GetPixel(memory, card.left + MulDiv(14, dpi, 96), card.bottom - MulDiv(22, dpi, 96)) == RGB(0,0,0),
                "tag footer has a vertical black marker");
        SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(list, dc);
    }
    view.window = window; screenshot(&view, L"build\\sidebar-tags.bmp"); ShowWindow(window, SW_SHOWNORMAL);
    SetWindowTextW(edit, L"budget"); wait_search_ui(window);
    {
        NMITEMACTIVATE click = {0};
        wchar_t query[32];
        ListView_GetItemRect(list, 0, &card, LVIR_ICON);
        click.hdr.hwndFrom = list; click.hdr.code = NM_CLICK; click.iItem = 0;
        click.ptAction.x = card.left + MulDiv(26, dpi, 96); click.ptAction.y = card.bottom - MulDiv(22, dpi, 96);
        SendMessageW(window, WM_NOTIFY, 2, (LPARAM)&click); wait_search_ui(window);
        GetWindowTextW(edit, query, 32);
        require(TreeView_GetSelection(tags) == tag_label(tags, L"café (1)") && !wcscmp(query, L"budget") &&
                ListView_GetItemCount(list) == 1, "plain tag footer hit testing selects its sidebar filter and preserves free text");
    }
    SetWindowTextW(edit, L""); wait_search_ui(window);
    {
        RECT original;
        GetWindowRect(window, &original);
        SetWindowPos(window, NULL, 0, 0, MulDiv(520, dpi, 96), MulDiv(360, dpi, 96), SWP_NOMOVE | SWP_NOZORDER);
        screenshot(&view, L"build\\sidebar-compact.bmp"); ShowWindow(window, SW_SHOWNORMAL);
        SetWindowPos(window, NULL, 0, 0, original.right - original.left, original.bottom - original.top, SWP_NOMOVE | SWP_NOZORDER);
    }
    require(set_tags(note, L"café;temporary;work") && save_note(note), "create a single-use sidebar tag");
    pump(30); TreeView_SelectItem(tags, tag_label(tags, L"temporary (1)")); wait_search_ui(window);
    require(ListView_GetItemCount(list) == 1, "single-use tag selects its note");
    require(set_tags(note, L"café;work") && save_note(note), "remove the final use of the selected tag");
    wait_search_ui(window);
    require(!tag_label(tags, L"temporary (1)") && TreeView_GetSelection(tags) == TreeView_GetRoot(tags),
            "removing the last use prunes the tag and returns to All notes");
    SendMessageW(date_button, BM_CLICK, 0, 0);
    TreeView_SelectItem(dates, TreeView_GetRoot(dates));
    GetKeyboardState(previous); memcpy(keys, previous, sizeof(keys)); keys[VK_CONTROL] = 0x80; SetKeyboardState(keys);
    SetFocus(dates); SendMessageW(dates, WM_KEYDOWN, 'F', 0);
    require(GetFocus() == edit, "Ctrl+F also works in date mode"); SetKeyboardState(previous);
    view.window = window; screenshot(&view, L"build\\sidebar-date.bmp"); ShowWindow(window, SW_SHOWNORMAL);
}

static void CALLBACK fill_tags_dialog(HWND unused, UINT message, UINT_PTR timer, DWORD tick) {
    HWND dialog = thread_window(L"#32770", L"Edit tags"), edit, list;
    wchar_t text[TAGS_CAP];
    Note view = {0};
    (void)unused; (void)message; (void)tick;
    if (!dialog) return;
    KillTimer(NULL, timer);
    edit = GetDlgItem(dialog, IDC_TAGS); list = GetDlgItem(dialog, IDC_TAG_SUGGESTIONS);
    SetWindowTextW(edit, L"wo");
    require(SendMessageW(list, LB_GETCOUNT, 0, 0) >= 1, "tag editor suggests names already used in the archive");
    SendMessageW(edit, WM_KEYDOWN, VK_DOWN, 0); SendMessageW(edit, WM_KEYDOWN, VK_RETURN, 0);
    GetWindowTextW(edit, text, TAGS_CAP);
    require(!wcscmp(text, L"work; "), "Down and Enter complete a tag without closing the editor");
    SetWindowTextW(edit, L"work; urgent; café");
    view.window = dialog; screenshot(&view, L"build\\tags.bmp");
    SendMessageW(dialog, WM_COMMAND, IDOK, 0);
}

static void catalog_checks(void) {
    TagStore store={0};
    TagCatalog catalog;
    int mode,i,j;
    require(tags_catalog_build(&store,&catalog) && !catalog.count && !catalog.items,"empty tag catalogs allocate nothing");
    store.count=1000; store.items=(TagRecord *)calloc(store.count,sizeof(*store.items));
    require(store.items!=NULL,"allocate catalog fixtures");
    for(mode=0;mode<4;++mode) {
        for(i=0;i<(int)store.count;++i) {
            wchar_t names[TAGS_CAP]; int used=0;
            for(j=0;j<TAGS_MAX;++j) {
                int tag=mode?i*TAGS_MAX+j:j;
                if(mode==2) tag=24000-1-tag;
                if(mode==3) tag=(int)((unsigned long long)tag*104729%24000);
                used+=swprintf_s(names+used,TAGS_CAP-(size_t)used,L"%stag%06d",j?L";":L"",tag);
            }
            free(store.items[i].tags); store.items[i].tags=_wcsdup(names);
            require(store.items[i].tags!=NULL,"allocate tag assignments");
        }
        require(tags_catalog_build(&store,&catalog) && catalog.count==(mode?24000u:24u),"catalog merges shared tags and retains unique tags");
        for(i=0;i<(int)catalog.count;++i) {
            wchar_t expected[TAG_NAME_CAP]; swprintf_s(expected,TAG_NAME_CAP,L"tag%06d",i);
            require(!wcscmp(catalog.items[i].name,expected) && catalog.items[i].uses==(mode?1u:1000u),
                "every catalog name, order and assignment count is exact");
        }
        tags_catalog_free(&catalog);
    }
    tags_store_free(&store);
    {
        TagRecord records[3]={0};
        wchar_t a[]=L"a;alpha;école;東京", b[]=L"A;ALPHA;ÉCOLE;東京", c[TAG_NAME_CAP];
        wmemset(c,L'z',TAG_NAME_CAP-1); c[TAG_NAME_CAP-1]=0;
        records[0].tags=a; records[1].tags=b; records[2].tags=c;
        store.items=records; store.count=3;
        require(tags_catalog_build(&store,&catalog) && catalog.count==5,"Unicode, case, prefixes and maximum-length names stay distinct as expected");
        for(i=0;i<5;++i) require(catalog.items[i].uses==(!wcscmp(catalog.items[i].name,c)?1u:2u),"Unicode catalog counts remain case-insensitive");
        tags_catalog_free(&catalog);
    }
    puts("PASS: empty, shared, 24000 distinct, Unicode, prefix and maximum-length tag catalogs.");
}

static void tags_search_checks(void) {
    Note *note = new_note();
    wchar_t normalized[TAGS_CAP], id[80], path[PATH_CAP], *saved;
    const wchar_t *long_id = L"note-2031-02-03_10-00-00-000-001";
    const wchar_t *other_id = L"note-2031-02-04_10-00-00-000-001";
    TagStore store;
    TagCatalog catalog;
    const TagCount *suggestions[8];
    SearchWorker *worker;
    SearchItem items[3] = {0};
    SearchResult *result;
    char *long_text = (char *)malloc(20001);
    HANDLE lock;
    HWND window, edit, list, tree;
    int i;
    BYTE previous[256], keyboard[256];
    catalog_checks();
    require(note && long_text, "allocate tag/search fixtures");
    wcscpy_s(id, 80, note->id);
    replace_text(note, L"Budget for the November release.");
    require(save_note(note), "save tag/search note");
    require(tags_normalize(L" Work; CAFÉ; cafe\x0301; work; ", normalized) && !wcscmp(normalized, L"café;work"),
            "tags normalize composed/decomposed Unicode, case, whitespace, duplicates and order");
    {
        wchar_t repeated[TAGS_CAP] = L"", name[16];
        for (i = 0; i < TAGS_MAX; ++i) {
            swprintf_s(name, 16, L"tag%02d;", i); wcscat_s(repeated, TAGS_CAP, name);
        }
        wcscat_s(repeated, TAGS_CAP, L"TAG00;tag23");
        require(tags_normalize(repeated, normalized), "duplicate names do not consume the 24-tag limit");
        wcscat_s(repeated, TAGS_CAP, L";another");
        require(!tags_normalize(repeated, normalized), "a 25th distinct tag is still rejected");
    }
    require(!tags_normalize(L"bad\"tag", normalized), "tag names reject query delimiters");
    require(set_tags(note, L"Work; Café") && save_note(note), "save Unicode tags and migrate metadata");
    require(tags_read(state_path, note->id, &saved) && saved && !wcscmp(saved, L"café;work"), "Unicode tags round trip");
    free(saved);
    require(setting(note->id, L"open", -1) == TRUE && setting(note->id, L"color", -1) == note->color,
            "Unicode INI migration preserves existing note layout");
    {
        wchar_t bom = 0;
        DWORD read;
        lock = CreateFileW(state_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        require(lock != INVALID_HANDLE_VALUE && ReadFile(lock, &bom, 2, &read, NULL) && read == 2 && bom == 0xfeff,
                "metadata has an explicit UTF-16 BOM");
        require(set_tags(note, L"work; urgent") && !save_note(note) && note->tags_dirty,
                "failed tag writes retain dirty metadata for retry");
        CloseHandle(lock);
        require(save_note(note) && !note->tags_dirty, "tag writes retry successfully");
    }
    lock = CreateFileW(note->path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    require(lock != INVALID_HANDLE_VALUE && set_tags(note, L"work; café") && save_note(note),
            "changing tags never rewrites the locked TXT or RTF");
    CloseHandle(lock);
    memset(long_text, 'x', 20000); long_text[20000] = 0;
    memcpy(long_text, "#Long document\r\n", 16);
    memcpy(long_text + 8185, "cross-boundary CAFÉ finish", strlen("cross-boundary CAFÉ finish"));
    memcpy(long_text + 16383, "è😀 needle", strlen("è😀 needle"));
    archive_fixture(long_id, long_text); free(long_text);
    archive_fixture(other_id, "#Other\r\nUnrelated travel plans.");
    require(tags_write(state_path, long_id, L"project alpha;work"), "save tags on a closed archive note");
    require(tags_store_load(directory, state_path, &store) && tags_catalog_build(&store, &catalog), "build derived tag catalog");
    require(tags_suggest(&catalog, L"wo", L"", suggestions, 8) == 1 &&
            !wcscmp(suggestions[0]->name, L"work") && suggestions[0]->uses == 2, "suggestions count notes, including closed notes");
    require(tags_suggest(&catalog, L"wo", L"work", suggestions, 8) == 0, "suggestions exclude assigned tags");
    tags_catalog_free(&catalog); tags_store_free(&store);
    require(SetTimer(NULL, 0, 40, fill_tags_dialog) != 0, "schedule native tag editor input");
    GetKeyboardState(previous); memcpy(keyboard, previous, sizeof(keyboard)); keyboard[VK_CONTROL] = 0x80;
    require(SetKeyboardState(keyboard), "set Ctrl modifier for tag shortcut");
    SendMessageW(note->edit, WM_KEYDOWN, 'T', 0); SetKeyboardState(previous);
    require(note->tags && !wcscmp(note->tags, L"café;urgent;work"), "Ctrl+T opens and saves the real tag editor");
    worker = search_create(controller, WM_APP + 99);
    require(worker != NULL, "create one on-demand search worker");
    wcscpy_s(items[0].id, 80, note->id); items[0].index = 0;
    wcscpy_s(items[1].id, 80, long_id); items[1].index = 1;
    wcscpy_s(items[2].id, 80, other_id); items[2].index = 2;
    require(search_submit(worker, directory, state_path, L"#WORK", items, 3), "submit tag-only search");
    result = wait_search(worker);
    require(!result->error && result->count == 2 && result->files_read == 0 && result->bytes_read == 0,
            "exact tag filtering reads no note bodies"); search_result_free(result);
    require(search_submit_filtered(worker, directory, state_path, L"", L"work", items, 3), "submit independent sidebar tag filter");
    result = wait_search(worker);
    require(result->count == 2 && !result->files_read, "sidebar tag-only filtering never reads note bodies"); search_result_free(result);
    require(search_submit(worker, directory, state_path, L"-#", items, 3), "submit untagged filter");
    result = wait_search(worker);
    require(result->count == 1 && result->hits[0].index == 2 && !result->files_read,
            "untagged filter uses metadata alone"); search_result_free(result);
    require(search_submit(worker, directory, state_path, L"#cafe", items, 3), "submit exact accented-tag distinction");
    result = wait_search(worker);
    require(result->count == 0 && result->files_read == 0, "tag identity retains accents even though text search ignores them");
    search_result_free(result);
    require(search_submit(worker, directory, state_path, L"cafe budget", items, 3), "submit unified tag and text search");
    result = wait_search(worker);
    require(!result->error && result->count == 1 && result->hits[0].index == 0,
            "plain words match tags and body with AND and accent-insensitive semantics"); search_result_free(result);
    require(search_submit(worker, directory, state_path, L"#\"project alpha\" \"cross-boundary cafe\" needle", items, 3), "submit streaming phrase search");
    result = wait_search(worker);
    require(!result->error && !result->unreadable && result->count == 1 && result->hits[0].index == 1 &&
            result->hits[0].offset == 8185 && result->files_read == 1,
            "KMP finds accent-insensitive phrases across blocks beyond previews and filters tags before file reads");
    search_result_free(result);
    require(search_submit(worker, directory, state_path, L"è😀 needle", items, 3), "submit split UTF-8 search");
    result = wait_search(worker);
    require(!result->error && result->count == 1 && result->hits[0].index == 1 && !result->unreadable,
            "streaming decoder preserves split UTF-8 scalars and supplementary characters"); search_result_free(result);
    {
        char repeated[20001];
        wchar_t query[SEARCH_CAP];
        memset(repeated, 'a', 20000); repeated[19999] = 'b'; repeated[20000] = 0;
        for (i = 0; i < SEARCH_CAP - 2; ++i) query[i] = L'a';
        query[SEARCH_CAP - 2] = L'b'; query[SEARCH_CAP - 1] = 0;
        require(remove_note_files(other_id), "replace isolated overlap fixture");
        archive_fixture(other_id, repeated);
        require(search_submit(worker, directory, state_path, query, items + 2, 1), "submit overlapping-prefix adversarial query");
        result = wait_search(worker);
        require(!result->error && result->count == 1 && result->hits[0].offset == 20000 - (SEARCH_CAP - 1),
                "KMP handles long overlapping prefixes without backtracking through the input");
        search_result_free(result);
        require(remove_note_files(other_id), "replace isolated malformed UTF-8 fixture");
        archive_fixture(other_id, "\xc3");
        require(search_submit(worker, directory, state_path, L"absent", items + 2, 1), "submit malformed UTF-8 fixture");
        result = wait_search(worker);
        require(result->unreadable == 1 && !result->count, "incomplete UTF-8 at EOF is reported"); search_result_free(result);
        require(remove_note_files(other_id), "restore isolated travel fixture");
        archive_fixture(other_id, "#Other\r\nUnrelated travel plans.");
        require(search_submit(worker, directory, state_path, L"a b c d e f g h i j k l m n o p q", items, 3), "submit excessive query terms");
        result = wait_search(worker);
        require(result->error != NULL, "query limits produce an explicit error"); search_result_free(result);
    }
    for (i = 0; i < 30; ++i)
        require(search_submit(worker, directory, state_path, i == 29 ? L"travel" : L"missing", items, 3), "supersede queued search");
    result = wait_search(worker);
    require(result->count == 1 && result->hits[0].index == 2, "only the latest search generation is published"); search_result_free(result);
    lock = CreateFileW(note->path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    require(lock != INVALID_HANDLE_VALUE && search_submit(worker, directory, state_path, L"budget", items, 1), "search a locked note");
    result = wait_search(worker);
    require(result->unreadable == 1 && result->count == 0, "unreadable notes are reported rather than silently omitted");
    search_result_free(result); CloseHandle(lock); search_destroy(worker);
    browse_notes(); window = thread_window(L"Jotilla.Archive", NULL);
    edit = GetDlgItem(window, 3); list = GetDlgItem(window, 2); tree = GetDlgItem(window, 1);
    require(edit && list, "archive has a native unified search field");
    SetWindowTextW(edit, L"#work budget"); wait_search_ui(window);
    require(ListView_GetItemCount(list) == 1, "unified archive query filters virtual cards");
    {
        wchar_t query[SEARCH_CAP];
        ListView_SetItemState(list, 0, LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ShowWindow(window, SW_MINIMIZE);
        require(!IsWindowVisible(window), "filtered browser minimizes into the tray");
        browse_notes(); wait_search_ui(window);
        GetWindowTextW(edit, query, SEARCH_CAP);
        require(IsWindowVisible(window) && !wcscmp(query, L"#work budget") && ListView_GetItemCount(list) == 1 &&
                ListView_GetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED) == LVIS_FOCUSED,
                "tray restore preserves query, result identity and unselected focus");
    }
    {
        NMITEMACTIVATE click = {0};
        click.hdr.hwndFrom = list; click.hdr.code = NM_CLICK; click.iItem = 0;
        SendMessageW(window, WM_NOTIFY, 2, (LPARAM)&click);
        require(find_note(id) == note, "filtered row maps to the correct open note");
    }
    require(set_tags(note, L"café") && save_note(note), "remove a matching tag while searching");
    wait_search_ui(window); require(ListView_GetItemCount(list) == 0, "saved metadata updates active search membership");
    require(set_tags(note, L"café;work") && save_note(note), "restore a matching tag while searching");
    wait_search_ui(window); require(ListView_GetItemCount(list) == 1, "saved tags can add a note back to results");
    TreeView_SelectItem(tree, find_date(tree, TreeView_GetRoot(tree), 20310200)); wait_search_ui(window);
    require(ListView_GetItemCount(list) == 0, "date and unified query filters intersect");
    TreeView_SelectItem(tree, TreeView_GetRoot(tree));
    SetWindowTextW(edit, L"#work cafe"); wait_search_ui(window);
    require(ListView_GetItemCount(list) == 2, "query matches tags in one note and full text in another");
    swprintf_s(path, PATH_CAP, L"%s\\%s.txt", directory, long_id);
    lock = CreateFileW(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    require(lock != INVALID_HANDLE_VALUE, "lock an unaffected search result against rereading");
    ListView_SetItemState(list, 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    replace_text(note, L"Updated budget for the November release."); require(save_note(note), "save one result during search");
    wait_search_ui(window);
    {
        wchar_t status[128]; GetWindowTextW(window, status, 128);
        require(ListView_GetItemCount(list) == 2 && !wcsstr(status, L"unreadable") &&
                ListView_GetItemState(list, 1, LVIS_SELECTED | LVIS_FOCUSED) == (LVIS_SELECTED | LVIS_FOCUSED),
                "live updates reevaluate one note, preserve unaffected results and retain selection by identity");
    }
    CloseHandle(lock);
    {
        WIN32_FILE_ATTRIBUTE_DATA metadata;
        HANDLE attributes;
        require(GetFileAttributesExW(state_path, GetFileExInfoStandard, &metadata), "record metadata cache identity");
        require(tags_write(state_path, long_id, L"project alpha;home"), "simulate an external tag edit");
        attributes = CreateFileW(state_path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 NULL, OPEN_EXISTING, 0, NULL);
        require(attributes != INVALID_HANDLE_VALUE && SetFileTime(attributes, NULL, NULL, &metadata.ftLastWriteTime),
                "preserve timestamp on the same-size external edit");
        CloseHandle(attributes);
        SendMessageW(edit, WM_KEYDOWN, VK_F5, 0); wait_search_ui(window);
        require(ListView_GetItemCount(list) == 1, "F5 forcibly reloads metadata even when size and timestamp are unchanged");
        require(tags_write(state_path, long_id, L"project alpha;work"), "restore external tag fixture");
        SendMessageW(edit, WM_KEYDOWN, VK_F5, 0); wait_search_ui(window);
        require(ListView_GetItemCount(list) == 2, "F5 restores refreshed tag matches");
    }
    {
        Note view = {0}; view.window = window; screenshot(&view, L"build\\search.bmp");
        ShowWindow(window, SW_SHOWNORMAL);
    }
    sidebar_checks(window, note);
    SetWindowTextW(edit, L"travel"); wait_search_ui(window);
    require(ListView_GetItemCount(list) == 1, "select a noncontiguous filtered deletion target");
    ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    {
        NMLVKEYDOWN key = {0}; key.hdr.hwndFrom = list; key.hdr.code = LVN_KEYDOWN; key.wVKey = VK_DELETE;
        require(SetTimer(NULL, 0, 40, confirm_archive_delete) != 0, "confirm filtered deletion");
        SendMessageW(window, WM_NOTIFY, 2, (LPARAM)&key); wait_search_ui(window);
    }
    swprintf_s(path, PATH_CAP, L"%s\\%s.txt", directory, other_id);
    require(GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES && find_note(id) == note,
            "filtered deletion removes exactly its source note");
    SetWindowTextW(edit, L"missing"); SetWindowTextW(edit, L"#work"); wait_search_ui(window);
    require(ListView_GetItemCount(list) == 2, "rapid query edits cannot publish stale UI results");
    SendMessageW(edit, WM_KEYDOWN, VK_ESCAPE, 0);
    require(GetWindowTextLengthW(edit) == 0 && ListView_GetItemCount(list) >= 2, "Escape restores the ordinary archive");
    archive_close();
    SendMessageW(note->window, WM_CLOSE, 0, 0);
    note = open_note(id, 0, TRUE);
    require(note && note->tags && !wcscmp(note->tags, L"café;work"), "tag persistence survives reopening");
    require(delete_archived_note(id) && delete_archived_note(long_id), "remove isolated tag/search fixtures");
    puts("PASS: Unicode tags, metadata-only saves, Ctrl+T suggestions, streaming KMP, sidebar modes/counts, Ctrl+F, live search and filtered deletion.");
}

static void empty_note_checks(void) {
    Note *note=new_note(), *other=new_note();
    wchar_t path[PATH_CAP],id[80];
    require(note && other && wcscmp(note->id,other->id),"unsaved blank notes still receive distinct identifiers");
    require(save_note(note) && !note->persisted && GetFileAttributesW(note->path)==INVALID_FILE_ATTRIBUTES &&
            setting(note->id,L"open",-1)==-1,"blank notes create neither a text file nor note metadata");
    replace_text(note,L" \t\r\n");
    require(save_note(note) && GetFileAttributesW(note->path)==INVALID_FILE_ATTRIBUTES,
            "whitespace-only notes are not archived");
    require(set_title(note,L"Title only") && save_note(note) && note->persisted,"a title alone counts as content");
    expect_file(note->path,"#Title only\r\n \t\r\n");
    replace_text(note,L""); require(set_title(note,L"") && save_note(note),"clear all content from a saved note");
    require(!note->persisted && IsWindow(note->window) && GetFileAttributesW(note->path)==INVALID_FILE_ATTRIBUTES,
            "clearing a saved note removes the archive file but keeps the blank post-it usable");
    swprintf_s(path,PATH_CAP,L"%s\\%s.rtf",directory,note->id);
    require(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES && setting(note->id,L"open",-1)==-1,
            "clearing also removes formatting and metadata");
    replace_text(note,L"Reusable"); require(save_note(note) && note->persisted,"a cleared post-it can be saved again");
    wcscpy_s(id,80,other->id); wcscpy_s(path,PATH_CAP,other->path);
    SendMessageW(other->window,WM_CLOSE,0,0);
    require(!find_note(id) && GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES,"closing a blank note leaves no archive entry");
    require(delete_archived_note(note->id),"remove the isolated empty-note test fixture");
    archive_fixture(L"note-2020-01-01_00-00-00-000-001","");
    note=open_note(L"note-2020-01-01_00-00-00-000-001",0,TRUE);
    require(note && !note->persisted && GetFileAttributesW(note->path)==INVALID_FILE_ATTRIBUTES,
            "restoring a legacy empty file removes its saved entry");
    destroy_note(note);
    puts("PASS: blank and whitespace notes stay unsaved, title-only notes persist, cleared notes remove TXT/RTF/metadata.");
}

static BOOL location_dialog_seen, location_picker_seen;

static BOOL CALLBACK inspect_location_button(HWND window, LPARAM flags) {
    wchar_t text[256];
    GetWindowTextW(window, text, 256);
    if (!wcsncmp(text, L"Move current archive", 20)) *(unsigned *)flags |= 1;
    if (!wcsncmp(text, L"Open or create archive", 22)) *(unsigned *)flags |= 2;
    require(!wcsstr(text, L"..."), "location actions have no ellipsis");
    return TRUE;
}

static void CALLBACK cancel_location_dialog(HWND window, UINT message, UINT_PTR timer, DWORD tick) {
    HWND dialog = thread_window(L"#32770", L"Archive");
    unsigned actions = 0;
    (void)window; (void)message; (void)tick;
    if (!dialog) return;
    KillTimer(NULL, timer);
    EnumChildWindows(dialog, inspect_location_button, (LPARAM)&actions);
    require(actions == 3, "archive dialog offers move and open/create");
    location_dialog_seen = TRUE;
    SendMessageW(dialog, TDM_CLICK_BUTTON, IDCANCEL, 0);
}

static void CALLBACK cancel_location_picker(HWND window, UINT message, UINT_PTR timer, DWORD tick) {
    HWND dialog = thread_window(L"#32770", L"Open or create archive");
    (void)window; (void)message; (void)tick;
    if (!dialog) return;
    KillTimer(NULL, timer); location_picker_seen = TRUE;
    SendMessageW(dialog, WM_CLOSE, 0, 0);
}

static void remove_location_fixture(const wchar_t *folder) {
    wchar_t path[PATH_CAP], pattern[PATH_CAP];
    WIN32_FIND_DATAW entry;
    HANDLE scan;
    swprintf_s(pattern, PATH_CAP, L"%s\\*", folder);
    scan = FindFirstFileW(pattern, &entry);
    if (scan != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(entry.cFileName, L".") || !wcscmp(entry.cFileName, L"..")) continue;
            require(!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY), "location fixture cleanup never recurses");
            swprintf_s(path, PATH_CAP, L"%s\\%s", folder, entry.cFileName);
            require(DeleteFileW(path), "remove private location fixture file");
        } while (FindNextFileW(scan, &entry));
        FindClose(scan);
    }
    require(RemoveDirectoryW(folder), "remove private location fixture directory");
}

static void location_checks(void) {
    wchar_t original[PATH_CAP], base[PATH_CAP], first[PATH_CAP], second[PATH_CAP], third[PATH_CAP];
    wchar_t path[PATH_CAP], target[PATH_CAP], loaded[PATH_CAP], id[80], second_id[80];
    const wchar_t *legacy = L"nota-legacy-location";
    Note *note;
    BOOL copies;
    HANDLE lock, file;
    DWORD written;
    WIN32_FILE_ATTRIBUTE_DATA before, after;
    HWND browser, button;
    RECT search_rect, button_rect;
    wcscpy_s(original, PATH_CAP, directory);
    swprintf_s(base, PATH_CAP, L"%s\\locations", original);
    swprintf_s(first, PATH_CAP, L"%s\\first", base);
    swprintf_s(second, PATH_CAP, L"%s\\second", base);
    swprintf_s(third, PATH_CAP, L"%s\\移動 café", base);
    swprintf_s(location_config, PATH_CAP, L"%s\\archive.path", base);
    require(CreateDirectoryW(base, NULL), "create isolated location fixture");
    require(location_load(location_config, original, loaded) && !wcscmp(loaded, original),
            "missing location setting preserves the legacy default");
    require(switch_archive(first, FALSE, &copies) && !notes && !wcscmp(directory, first), "create and activate an empty archive");
    note = new_note(); require(note != NULL, "create a note in the first archive");
    wcscpy_s(id, 80, note->id);
    replace_text(note, L"Notes travel safely. 😀");
    require(set_title(note, L"Archive move") && set_tags(note, L"café;work"), "prepare title and Unicode tags");
    SendMessageW(note->edit, EM_SETSEL, 0, 5); shortcut(note, 'I', FALSE);
    toggle_note(note); require(save_note(note), "persist source note and formatting");
    archive_fixture(legacy, "Legacy creation date");
    swprintf_s(path, PATH_CAP, L"%s\\%s.txt", first, legacy);
    require(GetFileAttributesExW(path, GetFileExInfoStandard, &before), "record the legacy creation date");
    swprintf_s(path, PATH_CAP, L"%s\\keep.txt", first);
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    require(file != INVALID_HANDLE_VALUE && WriteFile(file, "unrelated", 9, &written, NULL), "create unrelated file beside notes");
    CloseHandle(file);
    require(switch_archive(second, FALSE, &copies) && !notes, "switching creates a separate empty archive");
    swprintf_s(path, PATH_CAP, L"%s\\%s.txt", first, id);
    expect_file(path, "#Archive move\r\nNotes travel safely. 😀");
    require(location_load(location_config, original, loaded) && !wcscmp(loaded, second), "new default survives reloading configuration");
    note = new_note(); require(note != NULL, "create independent second-archive note");
    wcscpy_s(second_id, 80, note->id); replace_text(note, L"Independent archive"); require(save_note(note), "save second archive");
    require(switch_archive(first, FALSE, &copies), "reopen the first archive");
    note = find_note(id);
    require(note && !find_note(second_id) && note->collapsed && !wcscmp(note->tags, L"café;work") &&
            (effects_at(note, 1) & CFE_ITALIC), "switching restores open state, tags and rich formatting without mixing archives");
    require(!switch_archive(second, TRUE, &copies) && GetLastError() == ERROR_DIR_NOT_EMPTY && find_note(id) == note,
            "move refuses to overwrite a populated destination and retains open windows");
    swprintf_s(target, PATH_CAP, L"%s\\nested", first);
    require(!switch_archive(target, TRUE, &copies) && GetLastError() == ERROR_INVALID_PARAMETER,
            "move rejects nested archive paths");
    require(RemoveDirectoryW(target), "remove empty rejected destination");
    lock = CreateFileW(location_config, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    require(lock != INVALID_HANDLE_VALUE && !switch_archive(third, TRUE, &copies), "failure to commit the default rolls back the move");
    CloseHandle(lock);
    swprintf_s(path, PATH_CAP, L"%s\\%s.txt", third, id);
    require(GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES && !wcscmp(directory, first) && find_note(id) == note,
            "rollback removes copied files while preserving originals and active windows");
    require(location_load(location_config, original, loaded) && !wcscmp(loaded, first), "failed move preserves previous default");
    swprintf_s(path, PATH_CAP, L"%s\\%s.txt", first, id);
    lock = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    require(lock != INVALID_HANDLE_VALUE && !switch_archive(third, TRUE, &copies), "a locked source refuses migration");
    CloseHandle(lock);
    require(switch_archive(third, TRUE, &copies) && !copies && !wcscmp(directory, third), "move to Unicode location succeeds");
    require(GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES, "successful move removes the original TXT only after committing");
    note = find_note(id);
    require(note && note->collapsed && !wcscmp(note->tags, L"café;work") && (effects_at(note, 1) & CFE_ITALIC),
            "migration retains restored windows, tags and formatting");
    expect_file(note->path, "#Archive move\r\nNotes travel safely. 😀");
    require(location_load(location_config, original, loaded) && !wcscmp(loaded, third), "Unicode default round trips as UTF-8");
    swprintf_s(path, PATH_CAP, L"%s\\%s.txt", third, legacy);
    require(GetFileAttributesExW(path, GetFileExInfoStandard, &after) &&
            !CompareFileTime(&before.ftCreationTime, &after.ftCreationTime), "legacy archive dates survive migration");
    swprintf_s(path, PATH_CAP, L"%s\\keep.txt", first); expect_file(path, "unrelated");
    swprintf_s(path, PATH_CAP, L"%s\\notes.ini", second);
    require(SetFileAttributesW(path, FILE_ATTRIBUTE_READONLY) && !switch_archive(second, FALSE, &copies),
            "opening an archive requires writable metadata");
    require(SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL), "restore metadata fixture permissions");
    browser = browse_notes(); button = GetDlgItem(browser, 9);
    GetWindowTextW(button, loaded, PATH_CAP);
    require(button && !wcscmp(loaded, L"Archive"), "integrated Archive button has no ellipsis");
    GetWindowRect(GetDlgItem(browser, 3), &search_rect); GetWindowRect(button, &button_rect);
    require(search_rect.right < button_rect.left && search_rect.top >= button_rect.top && search_rect.bottom <= button_rect.bottom,
            "compact search and Archive button share the top row without overlap");
    require(SetTimer(NULL, 0, 40, cancel_location_dialog) != 0, "inspect native archive chooser");
    SendMessageW(button, BM_CLICK, 0, 0);
    require(location_dialog_seen && !wcscmp(directory, third), "cancelling the native choice leaves the archive unchanged");
    require(SetTimer(NULL, 0, 80, cancel_location_picker) != 0, "cancel native folder picker");
    require(location_pick(browser, directory, FALSE, loaded) == HRESULT_FROM_WIN32(ERROR_CANCELLED) && location_picker_seen,
            "native folder picker cancellation has no side effects");
    require(switch_archive(original, FALSE, &copies), "restore the outer test archive");
    /* A missing configured folder must never silently become a new archive. */
    require(location_change(location_config, original, second, FALSE, loaded, &copies), "prepare unavailable-archive fixture");
    swprintf_s(target, PATH_CAP, L"%s\\offline", base);
    require(MoveFileExW(second, target, 0) && !location_load(location_config, original, loaded),
            "unavailable configured archive does not silently fall back");
    require(MoveFileExW(target, second, 0), "restore temporarily unavailable fixture");
    file = CreateFileW(location_config, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    require(file != INVALID_HANDLE_VALUE && WriteFile(file, ".\\relative", 10, &written, NULL),
            "prepare a malformed saved location");
    CloseHandle(file);
    require(!location_load(location_config, original, loaded) && GetLastError() == ERROR_INVALID_DATA,
            "a relative saved path is rejected without selecting the fallback");
    location_config[0] = 0;
    remove_location_fixture(first); remove_location_fixture(second); remove_location_fixture(third); remove_location_fixture(base);
    puts("PASS: archive locations, Unicode defaults, isolated switching, locked-file rollback, durable moves, native chooser and cancellation.");
}

#include "palette_checks.c"
#include "text_size_checks.c"
#include "calendar_checks.c"
#include "session_checks.c"
#include "clipboard_checks.c"
#include "scrolling_checks.c"

int main(int argc, char **argv) {
    wchar_t temporary[PATH_CAP], root[PATH_CAP], id[80], path[PATH_CAP], text[256], pattern[PATH_CAP];
    const wchar_t *sample = L"Appunti di oggi\r\nCaffè, perché, € e 😀\r\nSeconda riga.";
    const char *expected = "Appunti di oggi\r\nCaffè, perché, € e 😀\r\nSeconda riga.";
    Note *first, *second;
    RECT rect;
    HANDLE lock, find;
    WIN32_FIND_DATAW entry;
    int color, saved_width, saved_height, i;
    wchar_t overflow[5000];
    HWND old_window;
    if (argc == 2 && !strcmp(argv[1], "--stop-app")) {
        HWND running = FindWindowW(APP_CLASS, NULL);
        DWORD process_id;
        DWORD_PTR reply;
        HANDLE process;
        if (!running) return 0;
        GetWindowThreadProcessId(running, &process_id);
        process = OpenProcess(SYNCHRONIZE, FALSE, process_id);
        require(process != NULL, "observe running application");
        require(SendMessageTimeoutW(running, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 5000, &reply) != 0,
                "save and gracefully close running application");
        require(WaitForSingleObject(process, 5000) == WAIT_OBJECT_0, "running application saved and exited");
        CloseHandle(process);
        return 0;
    }
    instance = GetModuleHandleW(NULL); testing = TRUE;
    interactive_tests=argc==2 && !strcmp(argv[1],"--interactive");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    require(GetTempPathW(PATH_CAP, temporary) != 0, "temporary directory");
    swprintf_s(root, PATH_CAP, L"%sJotilla-test-è-%lu-%llu", temporary, GetCurrentProcessId(), GetTickCount64());
    require(make_paths(root), "create isolated Unicode test directory");
    app_icon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(IDI_APP),IMAGE_ICON,32,32,0);
    require(app_icon != NULL && register_classes(), "register native C window classes");
    controller = CreateWindowExW(WS_EX_TOOLWINDOW, APP_CLASS, L"Jotilla test", WS_OVERLAPPED,
                                 0, 0, 0, 0, NULL, NULL, instance, NULL);
    require(controller != NULL, "create hotkey controller");
    /* Explicit mode: these checks replace the system clipboard contents. */
    if (argc==2 && !strcmp(argv[1],"--clipboard-only")) { clipboard_checks(); goto cleanup; }
    if (argc==2 && !strcmp(argv[1],"--scrolling-only")) { scrolling_checks(); goto cleanup; }
    if (argc==2 && !strcmp(argv[1],"--calendar-only")) { calendar_checks(); shortcut_checks(); goto cleanup; }
    if (argc==2 && !strcmp(argv[1],"--archive-only")) {
        require(new_note()!=NULL,"create archive test owner");
        archive_checks();
        goto cleanup;
    }
    if (argc==2 && !strcmp(argv[1],"--search-only")) {
        tags_search_checks();
        goto cleanup;
    }
    if (argc==2 && !strcmp(argv[1],"--location-only")) {
        location_checks();
        goto cleanup;
    }
    if (argc==2 && !strcmp(argv[1],"--palette-only")) {
        palette_checks(); branding_location_checks(); goto cleanup;
    }
    if (argc==2 && !strcmp(argv[1],"--text-size-only")) {
        text_size_checks(); resizing_checks(); goto cleanup;
    }
    if (argc==2 && !strcmp(argv[1],"--session-only")) { session_checks(); goto cleanup; }
    session_checks();
    tray_checks();
    first = new_note();
    require(first != NULL, "create first note");
    require(RegisterHotKey(controller, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT,
                           (UINT)(VkKeyScanW(L'+') & 0xff)) ||
            (GetLastError() == ERROR_HOTKEY_ALREADY_REGISTERED &&
             RegisterHotKey(controller, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F24)),
            "register creation hotkey without interrupting a running app");
    SendMessageW(controller, WM_HOTKEY, 1, 0);
    second = notes;
    require(note_count() == 2 && second != first && second->color != first->color,
            "hotkey creates independent note with next color");
    require(wcscmp(first->path, second->path) != 0, "unique TXT files for rapid note creation");
    replace_text(first, sample);
    require(first->dirty, "typing schedules autosave");
    pump(650);
    require(!first->dirty && !first->save_failed, "real autosave timer completes");
    require(!(GetWindowLongPtrW(first->edit, GWL_STYLE) & WS_VSCROLL), "short notes have no redundant scrollbar");
    expect_file(first->path, expected);
    require(GetFileAttributesW(first->path) != INVALID_FILE_ATTRIBUTES, "TXT saved in notes folder");
    for (i = 0; i < 4999; ++i) overflow[i] = L'W';
    overflow[4999] = 0;
    replace_text(first, overflow);
    require(text_fits(first) && GetWindowTextLengthW(first->edit) < 4999,
            "oversized pasted text cannot exceed visible capacity");
    require(!(GetWindowLongPtrW(first->edit, GWL_STYLE) & (WS_VSCROLL | WS_HSCROLL | ES_AUTOVSCROLL | ES_AUTOHSCROLL)),
            "no scrollbar or hidden scrolling content");
    replace_text(first, sample);
    require(save_note(first), "save after overflow check");
    puts("PASS: hotkey dispatch, different colors, unique files, timed UTF-8 autosave with emoji.");

    lock = CreateFileW(first->path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    require(lock != INVALID_HANDLE_VALUE, "lock target against replacement");
    replace_text(first, L"Testo nuovo");
    require(!save_note(first) && first->dirty && first->save_failed, "failed save keeps unsaved text");
    expect_file(first->path, expected);
    old_window = first->window;
    SendMessageW(old_window, WM_CLOSE, 0, 0);
    require(IsWindow(old_window), "failed save prevents closing and losing text");
    CloseHandle(lock);
    pump(1150);
    expect_file(first->path, "Testo nuovo");
    require(!first->dirty && !first->save_failed, "failed autosave retries after file becomes writable");
    replace_text(first, sample);
    require(save_note(first), "restore sample text");
    puts("PASS: failed atomic write preserves previous file, blocks close and retries successfully.");

    SetWindowPos(first->window, NULL, 110, 130, 340, 270, SWP_NOZORDER | SWP_NOACTIVATE);
    GetWindowRect(first->window, &rect);
    require(rect.right - rect.left == 340 && rect.bottom - rect.top == 270,
            "post-it accepts arbitrary dimensions");
    SendMessageW(first->window, WM_EXITSIZEMOVE, 0, 0);
    saved_width = first->width; saved_height = first->height; color = first->color;
    screenshot(first, L"build\\expanded.bmp");
    click_button(first, 1);
    GetWindowRect(first->window, &rect);
    require(first->collapsed && rect.bottom - rect.top == bar_height(first), "collapse leaves only title bar");
    require(!(GetWindowLongPtrW(first->edit, GWL_STYLE) & WS_VISIBLE), "collapsed editor is hidden");
    screenshot(first, L"build\\collapsed.bmp");
    click_button(first, 1);
    GetWindowRect(first->window, &rect);
    require(!first->collapsed && rect.bottom - rect.top == saved_height, "expand restores original height");
    GetWindowTextW(first->edit, text, 256);
    require(!wcscmp(text, sample), "collapse and expand retain complete text");
    click_button(first, 1);
    wcscpy_s(id, 80, first->id); wcscpy_s(path, PATH_CAP, first->path);

    replace_text(second, L"Chiusa, ma conservata.");
    wcscpy_s(text, 256, second->id);
    ShowWindow(second->window, SW_SHOWNORMAL);
    SetFocus(second->edit);
    click_button(second, 2);
    require(note_count() == 1 && setting(text, L"open", TRUE) == FALSE,
            "close saves TXT and archives even a focused note");
    puts("PASS: title-bar buttons, collapsed height, restored size, preserved text and close semantics.");

    require(save_all(), "save before application exit");
    while (notes) destroy_note(notes);
    require(restore_notes(), "restore notes after restart");
    first = notes;
    require(note_count() == 1 && !wcscmp(first->id, id), "restart restores only notes left open");
    GetWindowTextW(first->edit, text, 256);
    require(first->collapsed && first->color == color && first->height == saved_height && !wcscmp(text, sample),
            "restart restores collapsed state, color, height and Unicode text");
    GetWindowRect(first->window, &rect);
    require(rect.left == 110 && rect.top == 130 && rect.right - rect.left == saved_width,
            "restart restores window position and custom width");
    expect_file(path, expected);
    puts("PASS: restart restores layout and active notes, preserves closed TXT archives.");
    calendar_checks(); shortcut_checks();
    title_checks();
    border_checks();
    serialization_checks();
    options_checks();
    palette_checks();
    branding_location_checks();
    if (interactive_tests) keyboard_checks();
    editing_checks();
    resizing_checks();
    text_size_checks();
    scrolling_checks();
    archive_checks();
    tags_search_checks();
    empty_note_checks();
    efficiency_checks(!(argc == 2 && !strcmp(argv[1], "--measure")));
    storage_checks();
    location_checks();

cleanup:
    archive_close();
    while (notes) destroy_note(notes);
    DestroyWindow(controller);
    if (app_icon) DestroyIcon(app_icon);
    swprintf_s(pattern, PATH_CAP, L"%s\\*", root);
    find = FindFirstFileW(pattern, &entry);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                swprintf_s(path, PATH_CAP, L"%s\\%s", root, entry.cFileName);
                require(DeleteFileW(path), "remove isolated test output");
            }
        } while (FindNextFileW(find, &entry));
        FindClose(find);
    }
    require(RemoveDirectoryW(root), "remove isolated test directory");
    puts("All integration checks passed. UI snapshots: build/expanded.bmp and build/collapsed.bmp");
    return 0;
}
