// KoEnJaSettings.exe: the keyboard (HKCU\Software\KoEnJa\KeyboardType), the usual languages
// (LanguageOrder), the Hangul layout (KoreanLayout, Araso's Layouts > Hangul menu) and the switch key
// (SwitchKey), the names Tip.cpp reads. The TIP reads them again when an app gets the focus back,
// so nothing needs restarting. Until the first Apply (no
// Configured value) it opens as the first-run page: a welcome line over the same choices.
// "Report a problem" opens the GitHub issue form filled with the version, Windows and these settings (never typed text).

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <string>
#include <vector>

#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
// Common controls 6: themed radio buttons.
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

static const wchar_t SETTINGS_KEY[] = L"Software\\KoEnJa";
static const wchar_t* const KEYBOARDS[] = {L"Korean", L"JIS"};
/** iOS's LanguagePreferences.presets: who you talk to decides the pair. With all three the switch key
 *  picks which of Korean and Japanese goes first (the TIP writes LanguageOrder then); English is second. */
static const wchar_t* const ORDERS[] = {L"ko,en", L"ja,en", L"ko,en,ja", L"ja,en,ko"};
static const wchar_t* const ORDER_TITLES[] = {L"한국어 + English", L"日本語 + English", L"한국어 + 日本語 + English (한국어 우선 ↔ 日本語優先)",
                                              L"日本語 + 한국어 + English (日本語優先 ↔ 한국어 우선)"};
/** The Hangul layouts, in the order of Araso's menu: 2-set, 3-set final, 3-set 390. */
static const wchar_t* const LAYOUTS[] = {L"dubeolsik", L"sebeolsikFinal", L"sebeolsik390"};
static const wchar_t* const NAMES[] = {L"RightAlt", L"LeftCtrl", L"Hangul", L"Henkan", L"Muhenkan", L"CtrlShift", L"ShiftSpace", L"CtrlSpace"};
/** Each keyboard's switch key until the user picks one: right Alt (한/영), 無変換. */
static const int DEFAULT_SWITCH[] = {0, 4};
static constexpr int KEYBOARD_COUNT = 2, ORDER_COUNT = int(std::size(ORDERS)), LAYOUT_COUNT = int(std::size(LAYOUTS)), COUNT = int(std::size(NAMES));
enum { ID_KEYBOARD = 100, ID_ORDER = 110, ID_FIRST = 120, ID_LAYOUT = 130, ID_APPLY = 200, ID_CLOSE = 201, ID_REPORT = 202 };

/** The window's text in the user's UI language: Korean, Japanese, else English. */
struct Text {
    const wchar_t *title, *welcome, *keyboardHeading, *keyboards[KEYBOARD_COUNT], *orderHeading, *layoutHeading, *layouts[LAYOUT_COUNT], *layoutNote,
        *heading, *options[COUNT], *ctrlShiftNote, *hint, *apply, *close, *applied, *report;
};
static const Text KO = {
    L"KoEnJa 설정", L"KoEnJa를 설치해 주셔서 감사합니다. 쓰시는 키보드와 언어를 골라 주세요.", L"키보드 종류",
    {L"한국어 / 미국식 키보드 (한/영 키)", L"일본어 키보드 (半角/全角, 変換, 無変換 키)"},
    L"자주 쓰는 언어 (앞쪽일수록 애매한 단어가 그 언어로 됩니다)",
    L"한글 자판", {L"두벌식", L"세벌식 최종", L"세벌식 390"}, L"세벌식은 숫자·기호 키로도 자모를 칩니다. 숫자만 친 것은 숫자로 둡니다.",
    L"언어 전환 키: 우선 언어 바꾸기 (한국어 우선 ↔ 日本語優先), 입력 중에는 단어 언어 바꾸기",
    {L"오른쪽 Alt 단독 탭 (= 한/영, 한국어 키보드 기본)", L"왼쪽 Ctrl 단독 탭", L"한/영 키", L"変換 키", L"無変換 키 (일본어 키보드 기본)", L"Ctrl+Shift 탭",
     L"Shift+Space", L"Ctrl+Space"},
    L"Ctrl+Shift를 쓰려면 Windows의 Ctrl+Shift 키보드 레이아웃 전환 단축키를 꺼야 합니다 (설정 > 시간 및 언어 > 입력 > 고급 키보드 설정 > 입력 언어 바로 가기 키).",
    L"한국어 + English나 日本語 + English만 고르면 바꿀 우선 언어가 없으므로, 입력 없을 때 전환 키는 KoEnJa 켜기·끄기입니다. "
    L"켜기·끄기는 작업 표시줄의 한/あ/A 버튼 클릭으로 (일본어 키보드는 半角/全角로도) 합니다. 変換은 일본어 변환입니다. "
    L"Win+Space 없이 KoEnJa 고르기: 작업 표시줄 오른쪽의 언어 표시(한/あ/ENG)를 클릭하세요.",
    L"적용", L"닫기", L"적용했습니다. 앱을 다시 클릭하면 바로 반영됩니다.", L"문제 보고…"};
static const Text JA = {
    L"KoEnJa 設定", L"KoEnJa をインストールしていただきありがとうございます。お使いのキーボードと言語を選んでください。", L"キーボードの種類",
    {L"韓国語 / 英語 (US) キーボード (한/영 キー)", L"日本語キーボード (半角/全角、変換、無変換キー)"},
    L"よく使う言語 (先にあるほど、あいまいな単語がその言語になります)",
    L"ハングルの配列", {L"2ボル式 (두벌식)", L"3ボル式 最終 (세벌식 최종)", L"3ボル式 390 (세벌식 390)"},
    L"3ボル式では数字・記号のキーでも字母を入力します。数字だけのときは数字のままです。",
    L"切り替えキー: 優先言語の切り替え (한국어 우선 ↔ 日本語優先)、入力中は単語の言語を切り替え",
    {L"右 Alt を単独で押す (= 한/영、韓国語キーボードの既定)", L"左 Ctrl を単独で押す", L"한/영 キー", L"変換キー", L"無変換キー (日本語キーボードの既定)",
     L"Ctrl+Shift を押して離す", L"Shift+Space", L"Ctrl+Space"},
    L"Ctrl+Shift を使うには、Windows の Ctrl+Shift によるキーボード レイアウト切り替えをオフにしてください (設定 > 時刻と言語 > 入力 > キーボードの詳細設定 > 入力言語のホットキー)。",
    L"한국어 + English か 日本語 + English だけなら切り替える優先言語がないため、入力していないときの切り替えキーは KoEnJa のオン・オフです。"
    L"オン・オフはタスクバーの あ/한/A ボタンのクリック (日本語キーボードでは 半角/全角 も) で行います。変換 で変換します。"
    L"Win+Space を使わずに KoEnJa を選ぶ: タスクバー右の言語表示 (あ/한/ENG) をクリックします。",
    L"適用", L"閉じる", L"適用しました。アプリをクリックするとすぐに反映されます。", L"問題を報告…"};
static const Text EN = {
    L"KoEnJa Settings", L"Thanks for installing KoEnJa. Pick your keyboard and the languages you type.", L"Keyboard",
    {L"Korean / US keyboard (한/영 key)", L"Japanese keyboard (半角/全角, 変換, 無変換 keys)"},
    L"Your languages (the first wins an ambiguous word)",
    L"Hangul layout", {L"2-set", L"3-set final", L"3-set 390"}, L"3-set types jamo on the digit and symbol keys too; a number on its own stays a number.",
    L"Switch key: which language goes first (한국어 우선 ↔ 日本語優先); while typing, the word's language",
    {L"Right Alt, tapped alone (= 한/영; default on Korean keyboards)", L"Left Ctrl, tapped alone", L"한/영 key", L"変換 key",
     L"無変換 key (default on Japanese keyboards)", L"Ctrl+Shift, tapped", L"Shift+Space", L"Ctrl+Space"},
    L"For Ctrl+Shift, turn off Windows' Ctrl+Shift keyboard layout hotkey (Settings > Time & language > Typing > Advanced keyboard settings > Input language hot keys).",
    L"With only 한국어 + English or 日本語 + English there is no language to put first, so the switch key turns KoEnJa on and off when nothing is "
    L"being typed. On and off: click the 한/あ/A button on the taskbar (or 半角/全角 on a Japanese keyboard). 変換 converts. "
    L"To pick KoEnJa without Win+Space: click the language indicator (ENG/한/あ) on the taskbar.",
    L"Apply", L"Close", L"Applied. Click back into an app and it takes effect.", L"Report a problem…"};

static const Text* text;
static HFONT font;
static bool firstRun;
/** The controls top to bottom, with their height, indent and width (0: the window's) in 96-DPI units (the buttons last, on one row). */
struct Placed { HWND control; int height, indent, gap, width = 0; };
static std::vector<Placed> controls;
static HWND buttons[3];

static bool read(const wchar_t* name, wchar_t* out, DWORD size) {
    return !RegGetValueW(HKEY_CURRENT_USER, SETTINGS_KEY, name, RRF_RT_REG_SZ, nullptr, out, &size);
}

/** As Tip.cpp's readKeyboard: the saved one, else what Windows says is plugged in. */
static int currentKeyboard() {
    wchar_t name[16];
    if (read(L"KeyboardType", name, sizeof name)) return _wcsicmp(name, L"JIS") ? 0 : 1;
    DWORD type = 0, bytes = sizeof type; // Windows' hardware keyboard setting; GetKeyboardType follows the thread's layout
    RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\i8042prt\\Parameters", L"OverrideKeyboardType", RRF_RT_REG_DWORD,
                 nullptr, &type, &bytes);
    return (type ? type : GetKeyboardType(0)) == 7 ? 1 : 0;
}

/** The preset for LanguageOrder: its languages, and with both Korean and Japanese which goes first. */
static int currentOrder() {
    wchar_t order[64];
    if (!read(L"LanguageOrder", order, sizeof order)) wcscpy_s(order, L"ko,en,ja"); // unset: the TIP's Korean, English, Japanese
    const wchar_t *ko = wcsstr(order, L"ko"), *ja = wcsstr(order, L"ja");
    return ko && ja ? (ja < ko ? 3 : 2) : ja ? 1 : 0;
}

static int currentLayout() {
    wchar_t name[32];
    if (read(L"KoreanLayout", name, sizeof name))
        for (int i = 0; i < LAYOUT_COUNT; i++)
            if (!_wcsicmp(name, LAYOUTS[i])) return i;
    return 0;
}

static int current(int keyboard) {
    wchar_t name[32];
    if (read(L"SwitchKey", name, sizeof name))
        for (int i = 0; i < COUNT; i++)
            if (!_wcsicmp(name, NAMES[i])) return i;
    return DEFAULT_SWITCH[keyboard];
}

static int checked(HWND window, int first, int count) {
    for (int i = 0; i < count; i++)
        if (IsDlgButtonChecked(window, first + i) == BST_CHECKED) return i;
    return -1;
}

/** Opens the issue form with what helps to reproduce a problem: this build, Windows and the saved settings. */
static void report() {
    std::wstring version = L"?";
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::vector<BYTE> info(GetFileVersionInfoSizeW(path, nullptr));
    VS_FIXEDFILEINFO* fixed;
    UINT size;
    if (!info.empty() && GetFileVersionInfoW(path, 0, DWORD(info.size()), info.data()) && VerQueryValueW(info.data(), L"\\", (void**)&fixed, &size))
        version = std::to_wstring(HIWORD(fixed->dwProductVersionMS)) + L"." + std::to_wstring(LOWORD(fixed->dwProductVersionMS)) + L"." +
                  std::to_wstring(HIWORD(fixed->dwProductVersionLS));
    auto value = [](HKEY root, const wchar_t* key, const wchar_t* name) {
        wchar_t out[128] = L"-";
        DWORD bytes = sizeof out;
        if (RegGetValueW(root, key, name, RRF_RT_REG_SZ, nullptr, out, &bytes)) wcscpy_s(out, L"-");
        return std::wstring(out);
    };
    const wchar_t* nt = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    SYSTEM_INFO system;
    GetNativeSystemInfo(&system);
    std::wstring environment = L"Windows " + value(HKEY_LOCAL_MACHINE, nt, L"DisplayVersion") + L" (build " + value(HKEY_LOCAL_MACHINE, nt, L"CurrentBuild") +
                               L", " + (system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? L"x64" : L"other") + L")\nUI language: " +
                               std::to_wstring(GetUserDefaultUILanguage()) + L"\nKeyboardType: " + value(HKEY_CURRENT_USER, SETTINGS_KEY, L"KeyboardType") +
                               L"\nLanguageOrder: " + value(HKEY_CURRENT_USER, SETTINGS_KEY, L"LanguageOrder") +
                               L"\nKoreanLayout: " + value(HKEY_CURRENT_USER, SETTINGS_KEY, L"KoreanLayout") + L"\nSwitchKey: " +
                               value(HKEY_CURRENT_USER, SETTINGS_KEY, L"SwitchKey");
    auto encode = [](const std::wstring& s) {
        std::string utf8(WideCharToMultiByte(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0, nullptr, nullptr), 0);
        WideCharToMultiByte(CP_UTF8, 0, s.c_str(), int(s.size()), utf8.data(), int(utf8.size()), nullptr, nullptr);
        std::wstring out;
        for (unsigned char c : utf8) {
            if (isalnum(c) || strchr("-._~", c)) out += wchar_t(c);
            else { wchar_t hex[4]; swprintf_s(hex, L"%%%02X", c); out += hex; }
        }
        return out;
    };
    std::wstring url = L"https://github.com/studioCIZMO/KoEnJa/issues/new?template=bug.yml&platform=Windows&version=" + encode(version) +
                       L"&environment=" + encode(environment);
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static bool save(HWND window) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, SETTINGS_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr)) return false;
    auto set = [&](const wchar_t* name, const wchar_t* value) {
        return !RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value), DWORD((wcslen(value) + 1) * sizeof(wchar_t)));
    };
    int order = checked(window, ID_ORDER, ORDER_COUNT);
    int hangul = int(SendDlgItemMessageW(window, ID_LAYOUT, CB_GETCURSEL, 0, 0));
    DWORD one = 1;
    bool ok = set(L"KeyboardType", KEYBOARDS[std::max(0, checked(window, ID_KEYBOARD, KEYBOARD_COUNT))]) &&
              set(L"SwitchKey", NAMES[std::max(0, checked(window, ID_FIRST, COUNT))]) && (order < 0 || set(L"LanguageOrder", ORDERS[order])) &&
              set(L"KoreanLayout", LAYOUTS[hangul >= 0 && hangul < LAYOUT_COUNT ? hangul : 0]) &&
              !RegSetValueExW(key, L"Configured", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof one);
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
    int y = px(12), width = px(560);
    for (auto& c : controls) {
        y += px(c.gap);
        SendMessageW(c.control, WM_SETFONT, WPARAM(font), TRUE);
        SetWindowPos(c.control, nullptr, px(12 + c.indent), y, c.width ? px(c.width) : width - px(c.indent), px(c.height), SWP_NOZORDER | SWP_NOACTIVATE);
        y += px(c.height);
    }
    y += px(8);
    for (int i = 0; i < 3; i++) {
        SendMessageW(buttons[i], WM_SETFONT, WPARAM(font), TRUE);
        SetWindowPos(buttons[i], nullptr, i == 2 ? px(12) : px(12) + width - px(i ? 88 : 184), y, px(i == 2 ? 140 : 88), px(28), SWP_NOZORDER | SWP_NOACTIVATE);
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
            auto label = [&](const wchar_t* s, int height, int indent = 0, int gap = 8) { controls.push_back({make(L"STATIC", s, WS_GROUP, 0), height, indent, gap}); };
            auto radios = [&](const wchar_t* const* labels, int count, int first) {
                for (int i = 0; i < count; i++)
                    controls.push_back({make(L"BUTTON", labels[i], BS_AUTORADIOBUTTON | WS_TABSTOP | (i ? 0 : WS_GROUP), first + i), 24, 8, 0});
            };
            if (firstRun) label(text->welcome, 36, 0, 0);
            label(text->keyboardHeading, 20);
            radios(text->keyboards, KEYBOARD_COUNT, ID_KEYBOARD);
            label(text->orderHeading, 20);
            radios(ORDER_TITLES, ORDER_COUNT, ID_ORDER);
            label(text->layoutHeading, 20);
            HWND layouts = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL | WS_GROUP, ID_LAYOUT);
            for (auto name : text->layouts) SendMessageW(layouts, CB_ADDSTRING, 0, LPARAM(name));
            SendMessageW(layouts, CB_SETCURSEL, currentLayout(), 0);
            controls.push_back({layouts, 28, 8, 2, 240});
            label(text->layoutNote, 36, 8, 4);
            label(text->heading, 36);
            radios(text->options, COUNT, ID_FIRST);
            label(text->ctrlShiftNote, 56, 24, 4);
            label(text->hint, 92);
            buttons[0] = make(L"BUTTON", text->apply, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP, ID_APPLY);
            buttons[1] = make(L"BUTTON", text->close, BS_PUSHBUTTON | WS_TABSTOP, ID_CLOSE);
            buttons[2] = make(L"BUTTON", text->report, BS_PUSHBUTTON | WS_TABSTOP, ID_REPORT);
            int keyboard = currentKeyboard();
            CheckRadioButton(window, ID_KEYBOARD, ID_KEYBOARD + KEYBOARD_COUNT - 1, ID_KEYBOARD + keyboard);
            CheckRadioButton(window, ID_ORDER, ID_ORDER + ORDER_COUNT - 1, ID_ORDER + currentOrder());
            CheckRadioButton(window, ID_FIRST, ID_FIRST + COUNT - 1, ID_FIRST + current(keyboard));
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
            // Another keyboard: its switch key (the user can still pick another).
            if (LOWORD(w) >= ID_KEYBOARD && LOWORD(w) < ID_KEYBOARD + KEYBOARD_COUNT && HIWORD(w) == BN_CLICKED)
                CheckRadioButton(window, ID_FIRST, ID_FIRST + COUNT - 1, ID_FIRST + DEFAULT_SWITCH[LOWORD(w) - ID_KEYBOARD]);
            if (LOWORD(w) == ID_APPLY) {
                bool ok = save(window);
                MessageBoxW(window, ok ? text->applied : L"RegSetValueEx failed", text->title, MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
            } else if (LOWORD(w) == ID_REPORT) {
                report();
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
    DWORD configured = 0, size = sizeof configured;
    firstRun = RegGetValueW(HKEY_CURRENT_USER, SETTINGS_KEY, L"Configured", RRF_RT_REG_DWORD, nullptr, &configured, &size) != ERROR_SUCCESS;
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
