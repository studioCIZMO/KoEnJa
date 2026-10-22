// KoEnJaSettings.exe: picks KoEnJa's switch key (HKCU\Software\KoEnJa\SwitchKey, the names Tip.cpp
// reads). The TIP reads it again when an app gets the focus back, so nothing needs restarting.

#include <windows.h>

#include <algorithm>

#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
// Common controls 6: themed radio buttons.
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

static const wchar_t* const NAMES[] = {L"RightAlt", L"LeftCtrl", L"Hangul", L"Henkan", L"CtrlShift", L"ShiftSpace", L"CtrlSpace"};
static constexpr int COUNT = int(std::size(NAMES));
enum { ID_FIRST = 100, ID_APPLY = 200, ID_CLOSE = 201 };

/** The window's text in the user's UI language: Korean, Japanese, else English. */
struct Text {
    const wchar_t *title, *heading, *options[COUNT], *ctrlShiftNote, *hint, *apply, *close, *applied;
};
static const Text KO = {
    L"KoEnJa 설정", L"언어 전환 키 (입력 중: 단어의 언어 바꾸기 / 입력 없을 때: KoEnJa 켜기·끄기)",
    {L"오른쪽 Alt 단독 탭 (기본)", L"왼쪽 Ctrl 단독 탭", L"한/영 키", L"変換 키", L"Ctrl+Shift 탭", L"Shift+Space", L"Ctrl+Space"},
    L"Ctrl+Shift를 쓰려면 Windows의 Ctrl+Shift 키보드 레이아웃 전환 단축키를 꺼야 합니다 (설정 > 시간 및 언어 > 입력 > 고급 키보드 설정 > 입력 언어 바로 가기 키).",
    L"Win+Space 없이 KoEnJa 고르기: 작업 표시줄 오른쪽의 언어 표시(한/あ/ENG)를 클릭하거나, 설정 > 시간 및 언어 > 언어 및 지역에서 키보드를 추가하세요.",
    L"적용", L"닫기", L"적용했습니다. 앱을 다시 클릭하면 바로 반영됩니다."};
static const Text JA = {
    L"KoEnJa 設定", L"切り替えキー (入力中: 単語の言語を切り替え / 入力していないとき: KoEnJa のオン・オフ)",
    {L"右 Alt を単独で押す (既定)", L"左 Ctrl を単独で押す", L"한/영 キー", L"変換キー", L"Ctrl+Shift を押して離す", L"Shift+Space", L"Ctrl+Space"},
    L"Ctrl+Shift を使うには、Windows の Ctrl+Shift によるキーボード レイアウト切り替えをオフにしてください (設定 > 時刻と言語 > 入力 > キーボードの詳細設定 > 入力言語のホットキー)。",
    L"Win+Space を使わずに KoEnJa を選ぶ: タスクバー右の言語表示 (あ/한/ENG) をクリックするか、設定 > 時刻と言語 > 言語と地域 でキーボードを追加します。",
    L"適用", L"閉じる", L"適用しました。アプリをクリックするとすぐに反映されます。"};
static const Text EN = {
    L"KoEnJa Settings", L"Switch key (while typing: the word's language / otherwise: KoEnJa on and off)",
    {L"Right Alt, tapped alone (default)", L"Left Ctrl, tapped alone", L"한/영 key", L"変換 key", L"Ctrl+Shift, tapped", L"Shift+Space", L"Ctrl+Space"},
    L"For Ctrl+Shift, turn off Windows' Ctrl+Shift keyboard layout hotkey (Settings > Time & language > Typing > Advanced keyboard settings > Input language hot keys).",
    L"To pick KoEnJa without Win+Space: click the language indicator (ENG/한/あ) on the taskbar, or add it in Settings > Time & language > Language & region.",
    L"Apply", L"Close", L"Applied. Click back into an app and it takes effect."};

static const Text* text;
static HFONT font;
static HWND controls[COUNT + 5];

static int current() {
    wchar_t name[32];
    DWORD size = sizeof name;
    if (!RegGetValueW(HKEY_CURRENT_USER, L"Software\\KoEnJa", L"SwitchKey", RRF_RT_REG_SZ, nullptr, name, &size))
        for (int i = 0; i < COUNT; i++)
            if (!_wcsicmp(name, NAMES[i])) return i;
    return 0;
}

static bool save(int choice) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\KoEnJa", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr)) return false;
    bool ok = !RegSetValueExW(key, L"SwitchKey", 0, REG_SZ, reinterpret_cast<const BYTE*>(NAMES[choice]),
                              DWORD((wcslen(NAMES[choice]) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return ok;
}

/** Places the controls in 96-DPI units scaled to [dpi], in the system's message font. */
static void layout(HWND window, UINT dpi) {
    auto px = [dpi](int units) { return MulDiv(units, int(dpi), 96); };
    NONCLIENTMETRICSW metrics{sizeof metrics};
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof metrics, &metrics, 0, dpi);
    HFONT old = font;
    font = CreateFontIndirectW(&metrics.lfMessageFont);
    int y = px(12), width = px(520);
    auto place = [&](HWND control, int x, int height) {
        SendMessageW(control, WM_SETFONT, WPARAM(font), TRUE);
        SetWindowPos(control, nullptr, px(12) + x, y, width - x, height, SWP_NOZORDER | SWP_NOACTIVATE);
        y += height;
    };
    place(controls[0], 0, px(36));
    for (int i = 0; i < COUNT; i++) place(controls[1 + i], px(8), px(24));
    y += px(4);
    place(controls[COUNT + 1], px(24), px(56)); // the Ctrl+Shift note
    y += px(8);
    place(controls[COUNT + 2], 0, px(56)); // the hint
    y += px(8);
    for (int i = 0; i < 2; i++) {
        HWND button = controls[COUNT + 3 + i];
        SendMessageW(button, WM_SETFONT, WPARAM(font), TRUE);
        SetWindowPos(button, nullptr, px(12) + width - px(i ? 88 : 184), y, px(88), px(28), SWP_NOZORDER | SWP_NOACTIVATE);
    }
    y += px(28) + px(12);
    if (old) DeleteObject(old);
    RECT frame{0, 0, width + px(24), y};
    AdjustWindowRectExForDpi(&frame, GetWindowLongW(window, GWL_STYLE), FALSE, 0, dpi);
    SetWindowPos(window, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
        case WM_CREATE: {
            auto make = [&](const wchar_t* cls, const wchar_t* label, DWORD style, int id) {
                return CreateWindowExW(0, cls, label, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, window, HMENU(INT_PTR(id)), nullptr, nullptr);
            };
            controls[0] = make(L"STATIC", text->heading, 0, 0);
            for (int i = 0; i < COUNT; i++)
                controls[1 + i] = make(L"BUTTON", text->options[i], BS_AUTORADIOBUTTON | WS_TABSTOP | (i ? 0 : WS_GROUP), ID_FIRST + i);
            controls[COUNT + 1] = make(L"STATIC", text->ctrlShiftNote, 0, 0);
            controls[COUNT + 2] = make(L"STATIC", text->hint, 0, 0);
            controls[COUNT + 3] = make(L"BUTTON", text->apply, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP, ID_APPLY);
            controls[COUNT + 4] = make(L"BUTTON", text->close, BS_PUSHBUTTON | WS_TABSTOP, ID_CLOSE);
            CheckRadioButton(window, ID_FIRST, ID_FIRST + COUNT - 1, ID_FIRST + current());
            layout(window, GetDpiForWindow(window));
            return 0;
        }
        case WM_DPICHANGED: {
            auto r = reinterpret_cast<RECT*>(l);
            SetWindowPos(window, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            layout(window, HIWORD(w));
            return 0;
        }
        case WM_CTLCOLORSTATIC: // the dialog's background behind labels and radio buttons
            SetBkMode(HDC(w), TRANSPARENT);
            return LRESULT(GetSysColorBrush(COLOR_BTNFACE));
        case WM_COMMAND:
            if (LOWORD(w) == ID_APPLY) {
                for (int i = 0; i < COUNT; i++)
                    if (IsDlgButtonChecked(window, ID_FIRST + i) == BST_CHECKED) {
                        bool ok = save(i);
                        MessageBoxW(window, ok ? text->applied : L"RegSetValueEx failed", text->title, MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
                    }
            } else if (LOWORD(w) == ID_CLOSE || LOWORD(w) == IDCANCEL) {
                DestroyWindow(window);
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, w, l);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    LANGID ui = PRIMARYLANGID(GetUserDefaultUILanguage());
    text = ui == LANG_KOREAN ? &KO : ui == LANG_JAPANESE ? &JA : &EN;
    WNDCLASSEXW c{sizeof c};
    c.lpfnWndProc = proc;
    c.hInstance = instance;
    c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    c.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    c.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    c.lpszClassName = L"KoEnJaSettings";
    RegisterClassExW(&c);
    HWND window = CreateWindowExW(0, c.lpszClassName, text->title, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                                  CW_USEDEFAULT, 100, 100, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    ShowWindow(window, show);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0)) {
        if (IsDialogMessageW(window, &message)) continue; // Tab, arrows, Enter, Esc
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
