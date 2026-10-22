// KoEnJa's text input processor: the C++ engine's InputSession typing into any TSF text field.
// One COM class is the TIP (TSF makes one per thread manager, so one session per UI thread: the
// engine is single-threaded), the display attribute provider for the composition's underline and
// the candidate list (its own window, or drawn by a full-screen app through the UI element).

#include <windows.h>
#include <dwmapi.h>
#include <msctf.h>
#include <olectl.h>
#include <shellscalingapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include "../engine/src/Engines.h"
#include "../engine/src/Resources.h"
#include "../engine/src/RomajiComposer.h"
#include "../japanese/MozcConverter.h"

#pragma comment(linker, "/EXPORT:DllGetClassObject,PRIVATE")
#pragma comment(linker, "/EXPORT:DllCanUnloadNow,PRIVATE")
#pragma comment(linker, "/EXPORT:DllRegisterServer,PRIVATE")
#pragma comment(linker, "/EXPORT:DllUnregisterServer,PRIVATE")

using Microsoft::WRL::ComPtr;
using rea::InputSession;
using rea::TextRange;
using Kind = InputSession::Key::Kind;

// {113C852D-C4E7-4C6E-B713-3F79874DE867}
static const CLSID CLSID_KoEnJa = {0x113c852d, 0xc4e7, 0x4c6e, {0xb7, 0x13, 0x3f, 0x79, 0x87, 0x4d, 0xe8, 0x67}};
// {56DCC211-5E23-42CE-9CF1-20EBA011028E}: the one profile, under each language below.
static const GUID PROFILE_KoEnJa = {0x56dcc211, 0x5e23, 0x42ce, {0x9c, 0xf1, 0x20, 0xeb, 0xa0, 0x11, 0x02, 0x8e}};
// {984A32FD-0B9D-4EA6-A0A8-AF9AE0F4CFC6}: the composition's underline.
static const GUID ATTRIBUTE_INPUT = {0x984a32fd, 0x0b9d, 0x4ea6, {0xa0, 0xa8, 0xaf, 0x9a, 0xe0, 0xf4, 0xcf, 0xc6}};
// {6E3C1F0A-5B7D-4C2E-9A41-2D8F0B6C7E53}: the segment being converted, underlined thick.
static const GUID ATTRIBUTE_TARGET = {0x6e3c1f0a, 0x5b7d, 0x4c2e, {0x9a, 0x41, 0x2d, 0x8f, 0x0b, 0x6c, 0x7e, 0x53}};
// {D89F1C7F-0686-4898-B222-C523F8060883}: the candidate list, as a UI element.
static const GUID CANDIDATE_UI = {0xd89f1c7f, 0x0686, 0x4898, {0xb2, 0x22, 0xc5, 0x23, 0xf8, 0x06, 0x08, 0x83}};
/** The input indicator's mode button (the SDK documents it but doesn't declare it). */
static const GUID GUID_LBI_INPUTMODE = {0x2c77a81e, 0x41cc, 0x4178, {0xa3, 0xa7, 0x5f, 0x8a, 0x98, 0x75, 0x68, 0xe6}};
/** ko-KR and ja-JP: Win+Space lists it under whichever the user has. */
static const LANGID LANGUAGES[] = {MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN), MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN)};
static const GUID* const CATEGORIES[] = {&GUID_TFCAT_TIP_KEYBOARD, &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
                                         &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT, &GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
                                         &GUID_TFCAT_TIPCAP_UIELEMENTENABLED};
static const wchar_t NAME[] = L"KoEnJa";
/** Tip.rc's icons; the first is the profile's (index 0). */
enum { ICON_ON = 1, ICON_OFF = 2 };
static const wchar_t CANDIDATE_CLASS[] = L"KoEnJaCandidates";
/** Candidates per page of the window: 1–9 pick from the page shown. */
static constexpr int PAGE = 9;

static HINSTANCE module;
/** Live objects and LockServer locks, for DllCanUnloadNow. */
static std::atomic<long> references;

struct Live {
    Live() { ++references; }
    ~Live() { --references; }
};

#define COUNTED                                                                       \
    std::atomic<ULONG> refs{1};                                                       \
    Live live;                                                                        \
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs; }                         \
    STDMETHODIMP_(ULONG) Release() override {                                         \
        ULONG n = --refs;                                                             \
        if (n == 0) delete this;                                                      \
        return n;                                                                     \
    }

/** Runs [body] with the document locked: TSF lets a TIP read or change text only inside one. */
struct EditSession : ITfEditSession {
    COUNTED
    std::function<void(TfEditCookie)> body;

    explicit EditSession(std::function<void(TfEditCookie)> body) : body(std::move(body)) {}
    virtual ~EditSession() = default;
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
        if (iid != IID_IUnknown && iid != IID_ITfEditSession) { *out = nullptr; return E_NOINTERFACE; }
        *out = this;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP DoEditSession(TfEditCookie ec) override {
        // An engine exception must not unwind into the host app.
        try { body(ec); } catch (...) { return E_FAIL; }
        return S_OK;
    }
};

static HRESULT edit(ITfContext* context, TfClientId client, DWORD flags, std::function<void(TfEditCookie)> body) {
    auto session = new EditSession(std::move(body));
    HRESULT result = E_FAIL;
    HRESULT hr = context->RequestEditSession(client, session, flags | TF_ES_READWRITE, &result);
    session->Release();
    return FAILED(hr) ? hr : result;
}

/** How the composition looks: [which] 0 the reading (a solid underline), 1 the segment being
 *  converted (a thick one); and an enumerator over both ([next]). */
struct DisplayAttribute : ITfDisplayAttributeInfo, IEnumTfDisplayAttributeInfo {
    COUNTED
    int which = 0, next = 0;

    explicit DisplayAttribute(int which = 0) : which(which) {}
    virtual ~DisplayAttribute() = default;
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
        if (iid == IID_IUnknown || iid == IID_ITfDisplayAttributeInfo) *out = static_cast<ITfDisplayAttributeInfo*>(this);
        else if (iid == IID_IEnumTfDisplayAttributeInfo) *out = static_cast<IEnumTfDisplayAttributeInfo*>(this);
        else { *out = nullptr; return E_NOINTERFACE; }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP GetGUID(GUID* guid) override { *guid = which ? ATTRIBUTE_TARGET : ATTRIBUTE_INPUT; return S_OK; }
    STDMETHODIMP GetDescription(BSTR* description) override {
        *description = SysAllocString(L"KoEnJa composition");
        return *description ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* attribute) override {
        *attribute = {};
        attribute->crText.type = TF_CT_NONE;
        attribute->crBk.type = TF_CT_NONE;
        attribute->crLine.type = TF_CT_NONE;
        attribute->lsStyle = TF_LS_SOLID;
        attribute->fBoldLine = which == 1;
        attribute->bAttr = which ? TF_ATTR_TARGET_CONVERTED : TF_ATTR_INPUT;
        return S_OK;
    }
    STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE*) override { return E_NOTIMPL; }
    /** Both interfaces' Reset: nothing to restore, and the enumeration starts over. */
    STDMETHODIMP Reset() override { next = 0; return S_OK; }
    STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** out) override {
        auto copy = new DisplayAttribute;
        copy->next = next;
        *out = copy;
        return S_OK;
    }
    STDMETHODIMP Next(ULONG count, ITfDisplayAttributeInfo** infos, ULONG* fetched) override {
        ULONG n = 0;
        while (n < count && next < 2) infos[n++] = new DisplayAttribute(next++);
        if (fetched) *fetched = n;
        return n == count ? S_OK : S_FALSE;
    }
    STDMETHODIMP Skip(ULONG count) override {
        bool all = next + count <= 2;
        next = std::min(2, int(next + count));
        return all ? S_OK : S_FALSE;
    }
};

/** tip_test's stand-in for a dictionary (KOENJA_TEST_CONVERTER set): the reading, then 候補1…候補11,
 *  two pages of the window. */
struct TestConverter : rea::JapaneseConverter {
    std::vector<rea::ConversionCandidate> candidates(const std::u16string& kana) override {
        std::vector<rea::ConversionCandidate> out{{kana, int(kana.size())}};
        for (int i = 1; i <= 11; i++) {
            std::u16string text = u"候補";
            for (char c : std::to_string(i)) text += char16_t(c);
            out.push_back({text, int(kana.size())});
        }
        return out;
    }
    void learn(const rea::ConversionCandidate&, const std::u16string&) override {}
    void save() override {}
};

/**
 * Kana → kanji over Mozc's dictionary (Windows/japanese), mapped read-only from the data folder so
 * every app shares its pages. Learned words go to %LOCALAPPDATA%\KoEnJa (each app saves its own
 * copy over the file; ponytail: last writer wins, a merge on save if two apps' learning matters).
 * tip_test sets KOENJA_TEST_CONVERTER for a fixed candidate list.
 */
static std::shared_ptr<rea::JapaneseConverter> makeConverter() {
    if (GetEnvironmentVariableW(L"KOENJA_TEST_CONVERTER", nullptr, 0)) return std::make_shared<TestConverter>();
    std::optional<std::filesystem::path> learning;
    wchar_t local[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH)) {
        std::error_code error;
        auto folder = std::filesystem::path(local) / L"KoEnJa";
        std::filesystem::create_directories(folder, error);
        if (!error) learning = folder / L"japanese.txt";
    }
    try {
        return std::make_shared<rea::MozcConverter>(rea::Resources::directory / L"mozc.dic", learning);
    } catch (...) {
        return nullptr; // Japanese then goes in as kana
    }
}

/** The engine over [order], the user's languages most used first. */
static std::unique_ptr<InputSession> makeSession(const std::vector<rea::Language>& order) {
    static const bool located = [] {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(module, path, MAX_PATH);
        rea::Resources::directory = std::filesystem::path(path).parent_path() / L"data";
        return true;
    }();
    (void)located;
    return rea::Engines::makeSession(order, makeConverter());
}

static const wchar_t SETTINGS_KEY[] = L"Software\\KoEnJa";

/** The physical keyboard (HKCU\Software\KoEnJa\KeyboardType "Korean" or "JIS"): a US-style
 *  101/103 (Korean) or a Japanese 106/109. Unset: what Windows says is plugged in. */
enum class Keyboard { Korean, JIS };

static Keyboard readKeyboard() {
    wchar_t name[16];
    DWORD size = sizeof name;
    if (!RegGetValueW(HKEY_CURRENT_USER, SETTINGS_KEY, L"KeyboardType", RRF_RT_REG_SZ, nullptr, name, &size))
        return _wcsicmp(name, L"JIS") ? Keyboard::Korean : Keyboard::JIS;
    DWORD type = 0, bytes = sizeof type; // Windows' hardware keyboard setting; GetKeyboardType follows the thread's layout
    RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\i8042prt\\Parameters", L"OverrideKeyboardType", RRF_RT_REG_DWORD,
                 nullptr, &type, &bytes);
    return (type ? type : GetKeyboardType(0)) == 7 ? Keyboard::JIS : Keyboard::Korean;
}

/** The switch key picked in KoEnJaSettings (HKCU\Software\KoEnJa\SwitchKey; these names). Unset:
 *  right Alt (한/영) on a Korean keyboard, 無変換 on a Japanese one. */
enum class Switch { RightAlt, LeftCtrl, Hangul, Henkan, Muhenkan, CtrlShift, ShiftSpace, CtrlSpace };
static const wchar_t* const SWITCH_NAMES[] = {L"RightAlt", L"LeftCtrl", L"Hangul", L"Henkan", L"Muhenkan", L"CtrlShift", L"ShiftSpace", L"CtrlSpace"};

static Switch readSwitch(Keyboard keyboard) {
    wchar_t name[32];
    DWORD size = sizeof name;
    if (!RegGetValueW(HKEY_CURRENT_USER, SETTINGS_KEY, L"SwitchKey", RRF_RT_REG_SZ, nullptr, name, &size))
        for (int i = 0; i < int(std::size(SWITCH_NAMES)); i++)
            if (!_wcsicmp(name, SWITCH_NAMES[i])) return Switch(i);
    return keyboard == Keyboard::JIS ? Switch::Muhenkan : Switch::RightAlt;
}

/** HKCU\Software\KoEnJa\LanguageOrder ("ko,ja,en": most used first); English is always in.
 *  Unset: Korean, English, Japanese. */
static std::vector<rea::Language> readOrder() {
    using rea::Language;
    wchar_t text[64];
    DWORD size = sizeof text;
    std::vector<Language> order;
    if (!RegGetValueW(HKEY_CURRENT_USER, SETTINGS_KEY, L"LanguageOrder", RRF_RT_REG_SZ, nullptr, text, &size))
        for (wchar_t *rest = nullptr, *code = wcstok(text, L", ", &rest); code; code = wcstok(nullptr, L", ", &rest)) {
            auto language = !wcscmp(code, L"ko") ? Language::KOREAN : !wcscmp(code, L"ja") ? Language::JAPANESE : Language::ENGLISH;
            if (std::find(order.begin(), order.end(), language) == order.end()) order.push_back(language);
        }
    if (order.empty()) return {Language::KOREAN, Language::ENGLISH, Language::JAPANESE};
    if (std::find(order.begin(), order.end(), Language::ENGLISH) == order.end()) order.push_back(Language::ENGLISH);
    return order;
}

/** A symbol or digit key (set-1 scan code) and what it types, unshifted and shifted (0: nothing). */
struct Printed { BYTE scan; char16_t plain, shifted; };
/** A US 101 / Korean 103 keyboard (its \ key reads ₩ in Korean fonts). */
static const Printed US_KEYS[] = {
    {0x02, '1', '!'}, {0x03, '2', '@'}, {0x04, '3', '#'}, {0x05, '4', '$'}, {0x06, '5', '%'}, {0x07, '6', '^'},
    {0x08, '7', '&'}, {0x09, '8', '*'}, {0x0A, '9', '('}, {0x0B, '0', ')'}, {0x0C, '-', '_'}, {0x0D, '=', '+'},
    {0x1A, '[', '{'}, {0x1B, ']', '}'}, {0x27, ';', ':'}, {0x28, '\'', '"'}, {0x29, '`', '~'}, {0x2B, '\\', '|'},
    {0x33, ',', '<'}, {0x34, '.', '>'}, {0x35, '/', '?'}};
/** A Japanese 106/109 keyboard (its ¥ key, 0x7D, types \, which reads ¥ in Japanese fonts, as Windows has it). */
static const Printed JIS_KEYS[] = {
    {0x02, '1', '!'}, {0x03, '2', '"'}, {0x04, '3', '#'}, {0x05, '4', '$'}, {0x06, '5', '%'}, {0x07, '6', '&'},
    {0x08, '7', '\''}, {0x09, '8', '('}, {0x0A, '9', ')'}, {0x0B, '0', 0}, {0x0C, '-', '='}, {0x0D, '^', '~'},
    {0x7D, '\\', '|'}, {0x1A, '@', '`'}, {0x1B, '[', '{'}, {0x27, ';', '+'}, {0x28, ':', '*'}, {0x2B, ']', '}'},
    {0x33, ',', '<'}, {0x34, '.', '>'}, {0x35, '/', '?'}, {0x73, '\\', '_'}};

/**
 * What a symbol or digit key types on the [keyboard] the user has, from its scan code: Windows
 * maps scan codes with the profile language's layout (a Japanese 106 under ja-JP, US under ko-KR)
 * whatever keyboard is plugged in, so Shift+2 would be " under the Japanese profile on a Korean
 * keyboard. nullopt: no such key on it (the layout's character goes); 0: it types nothing.
 */
static std::optional<char16_t> printed(Keyboard keyboard, LPARAM lParam, bool shift) {
    BYTE scan = BYTE(lParam >> 16);
    if (lParam & (1 << 24)) return std::nullopt; // the numeric keypad's / and Enter share scan codes
    auto keys = keyboard == Keyboard::JIS ? std::begin(JIS_KEYS) : std::begin(US_KEYS);
    auto end = keyboard == Keyboard::JIS ? std::end(JIS_KEYS) : std::end(US_KEYS);
    for (; keys != end; keys++)
        if (keys->scan == scan) return shift ? keys->shifted : keys->plain;
    return std::nullopt;
}

/** HKCU\Software\KoEnJa\KeyLog = 1: every key the TIP gets goes to %LOCALAPPDATA%\KoEnJa\keys.log. */
static bool readKeyLog() {
    DWORD on = 0, size = sizeof on;
    RegGetValueW(HKEY_CURRENT_USER, SETTINGS_KEY, L"KeyLog", RRF_RT_REG_DWORD, nullptr, &on, &size);
    return on == 1;
}

/** Bit 24 of a key's lParam: the right Alt or Ctrl (on an AltGr layout the left Ctrl that comes
 *  with right Alt is a plain one). */
static bool extended(LPARAM lParam) { return lParam & (1 << 24); }

/** The key's virtual-key code as the [keyboard] the user has would send it: the profile's layout
 *  reads a JIS keyboard's 半角/全角 (scan 0x29) as `, and a US one's ` as 半角/全角. 半角/全角 is
 *  VK_OEM_AUTO here, whichever of its codes the layout sends. */
static WPARAM physicalKey(Keyboard keyboard, WPARAM vk, LPARAM lParam) {
    BYTE scan = BYTE(lParam >> 16);
    if (keyboard == Keyboard::Korean && (scan == 0x72 || scan == 0xF2)) return VK_HANGUL;
    if (extended(lParam)) return vk;
    if (keyboard == Keyboard::Korean) return scan == 0x29 ? VK_OEM_3 : vk;
    return scan == 0x29 ? VK_OEM_AUTO : scan == 0x79 ? VK_CONVERT : scan == 0x7B ? VK_NONCONVERT : vk;
}

/** A tap switch key's part: 1 the right Alt, 2 the left Ctrl, 4 a Shift; 0 any other key. */
static int tapPart(WPARAM vk, LPARAM lParam) {
    if (vk == VK_RMENU || (vk == VK_MENU && extended(lParam))) return 1;
    if (vk == VK_LCONTROL || (vk == VK_CONTROL && !extended(lParam))) return 2;
    if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) return 4;
    return 0;
}

/** [text] in its other width, for F8–F10: [flags] LCMAP_HALFWIDTH or LCMAP_FULLWIDTH. */
static std::u16string mapped(const std::u16string& text, DWORD flags) {
    std::u16string out(text.size() * 2 + 1, u'\0');
    int n = LCMapStringEx(L"ja-JP", flags, reinterpret_cast<LPCWSTR>(text.c_str()), int(text.size()), reinterpret_cast<LPWSTR>(out.data()),
                          int(out.size()), nullptr, nullptr, 0);
    out.resize(std::max(0, n));
    return out;
}

/** Window calls in this scope are per-monitor DPI aware, whatever the host app is: the window is
 *  sized and placed in real pixels on every monitor, never stretched by DPI virtualization. */
struct PerMonitorDpi {
    DPI_AWARENESS_CONTEXT old = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ~PerMonitorDpi() { if (old) SetThreadDpiAwarenessContext(old); }
};

/** Windows' app theme (Settings > Personalization > Colors). */
static bool darkMode() {
    DWORD light = 1, size = sizeof light;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
                 RRF_RT_REG_DWORD, nullptr, &light, &size);
    return light == 0;
}

/** The candidates under the composition, a page of 9 numbered rows: topmost, and never takes the
 *  focus from the app. */
struct CandidateWindow {
    HWND hwnd = nullptr;
    HFONT font = nullptr;
    std::vector<std::wstring> items;
    int selected = 0;
    /** Pixels at the monitor's DPI: a row's height, the number column's width, the margin. */
    int row = 0, number = 0, pad = 0;

    ~CandidateWindow() { destroy(); }

    static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
        if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        auto self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_PAINT && self) { self->paint(); return 0; }
        return DefWindowProcW(hwnd, message, w, l);
    }

    /** [list] at [selection]'s page, under [text] (the composition, in screen pixels), or above it
     *  at the bottom of the screen. */
    void show(const std::vector<std::wstring>& list, int selection, RECT text) {
        PerMonitorDpi aware;
        if (!hwnd) {
            static const ATOM registered = [] {
                WNDCLASSEXW c{sizeof c};
                c.style = CS_DROPSHADOW;
                c.lpfnWndProc = proc;
                c.hInstance = module;
                c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
                c.lpszClassName = CANDIDATE_CLASS;
                return RegisterClassExW(&c);
            }();
            if (!registered) return;
            hwnd = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, CANDIDATE_CLASS, L"", WS_POPUP, 0, 0, 0, 0,
                                   nullptr, nullptr, module, nullptr);
            if (!hwnd) return;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, LONG_PTR(this));
            DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUNDSMALL; // Windows 11; ignored before
            DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof corner);
        }
        items = list;
        selected = selection;
        HMONITOR monitor = MonitorFromPoint({text.left, text.bottom}, MONITOR_DEFAULTTONEAREST);
        UINT dpi = 96, unused;
        GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi, &unused);

        // The system's UI font, a third larger (kanji need the room), in the script's own face.
        bool hangul = std::any_of(items.begin(), items.end(), [](auto& item) {
            return std::any_of(item.begin(), item.end(), [](wchar_t c) { return c >= 0xAC00 && c <= 0xD7A3; });
        });
        NONCLIENTMETRICSW metrics{sizeof metrics};
        SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof metrics, &metrics, 0, dpi);
        LOGFONTW face = metrics.lfMessageFont;
        face.lfHeight = face.lfHeight * 4 / 3;
        wcscpy_s(face.lfFaceName, hangul ? L"Malgun Gothic" : L"Yu Gothic UI");
        if (font) DeleteObject(font);
        font = CreateFontIndirectW(&face);

        HDC dc = GetDC(hwnd);
        auto old = SelectObject(dc, font);
        TEXTMETRICW tm;
        GetTextMetricsW(dc, &tm);
        pad = MulDiv(6, int(dpi), 96);
        row = tm.tmHeight + pad;
        SIZE size;
        GetTextExtentPoint32W(dc, L"9", 1, &size);
        number = size.cx + 2 * pad;
        int first = selected / PAGE * PAGE, count = std::min(PAGE, int(items.size()) - first), width = MulDiv(80, int(dpi), 96);
        for (int i = first; i < first + count; i++) {
            GetTextExtentPoint32W(dc, items[i].c_str(), int(items[i].size()), &size);
            width = std::max(width, int(size.cx));
        }
        SelectObject(dc, old);
        ReleaseDC(hwnd, dc);
        int w = number + width + 2 * pad, h = (count + (items.size() > PAGE)) * row + pad; // a row for "2/3" when paged

        MONITORINFO screen{sizeof screen};
        GetMonitorInfoW(monitor, &screen);
        int x = std::max(int(screen.rcWork.left), std::min(int(text.left), int(screen.rcWork.right) - w));
        int y = text.bottom + pad / 2;
        if (y + h > screen.rcWork.bottom) y = text.top - h - pad / 2;
        SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    void hide() {
        if (hwnd) ShowWindow(hwnd, SW_HIDE);
    }

    void destroy() {
        if (hwnd) DestroyWindow(hwnd);
        if (font) DeleteObject(font);
        hwnd = nullptr;
        font = nullptr;
    }

    void paint() {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        bool dark = darkMode();
        COLORREF back = dark ? RGB(43, 43, 43) : RGB(249, 249, 249), text = dark ? RGB(255, 255, 255) : RGB(0, 0, 0),
                 dim = dark ? RGB(160, 160, 160) : RGB(110, 110, 110);
        RECT all;
        GetClientRect(hwnd, &all);
        HBRUSH brush = CreateSolidBrush(back);
        FillRect(dc, &all, brush);
        DeleteObject(brush);
        auto old = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        int first = selected / PAGE * PAGE, count = std::min(PAGE, int(items.size()) - first);
        for (int i = 0; i < count; i++) {
            RECT line{pad / 2, pad / 2 + i * row, all.right - pad / 2, pad / 2 + (i + 1) * row};
            bool chosen = first + i == selected;
            if (chosen) FillRect(dc, &line, GetSysColorBrush(COLOR_HIGHLIGHT));
            wchar_t digit = wchar_t(L'1' + i);
            SetTextColor(dc, chosen ? GetSysColor(COLOR_HIGHLIGHTTEXT) : dim);
            TextOutW(dc, pad, line.top + pad / 2, &digit, 1);
            SetTextColor(dc, chosen ? GetSysColor(COLOR_HIGHLIGHTTEXT) : text);
            auto& item = items[first + i];
            TextOutW(dc, number, line.top + pad / 2, item.c_str(), int(item.size()));
        }
        if (items.size() > PAGE) {
            auto page = std::to_wstring(first / PAGE + 1) + L"/" + std::to_wstring((items.size() + PAGE - 1) / PAGE);
            RECT line{pad, pad / 2 + count * row, all.right - pad, pad / 2 + (count + 1) * row};
            SetTextColor(dc, dim);
            DrawTextW(dc, page.c_str(), int(page.size()), &line, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            DrawTextW(dc, L"\u25B2\u25BC", 2, &line, DT_LEFT | DT_VCENTER | DT_SINGLELINE); // ▲▼: PageUp/PageDown
        }
        SelectObject(dc, old);
        EndPaint(hwnd, &ps);
    }
};

/** The input indicator's mode button (taskbar, by the clock): KoEnJa's icon while it is on, "A"
 *  while it is off (keys go to the app as plain English). A click switches, as 한/영 does. */
struct ModeButton : ITfLangBarItemButton, ITfSource {
    COUNTED
    std::function<bool()> on;
    std::function<void()> clicked;
    ComPtr<ITfLangBarItemSink> sink;

    virtual ~ModeButton() = default;
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
        if (iid == IID_IUnknown || iid == IID_ITfLangBarItem || iid == IID_ITfLangBarItemButton) *out = static_cast<ITfLangBarItemButton*>(this);
        else if (iid == IID_ITfSource) *out = static_cast<ITfSource*>(this);
        else { *out = nullptr; return E_NOINTERFACE; }
        AddRef();
        return S_OK;
    }
    /** On or off changed: the indicator asks for the icon and text again. */
    void update() {
        if (sink) sink->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_TOOLTIP);
    }
    bool isOn() const { return !on || on(); }

    STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* info) override {
        info->clsidService = CLSID_KoEnJa;
        info->guidItem = GUID_LBI_INPUTMODE;
        info->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;
        info->ulSort = 0;
        wcscpy_s(info->szDescription, NAME);
        return S_OK;
    }
    STDMETHODIMP GetStatus(DWORD* status) override { *status = 0; return S_OK; }
    STDMETHODIMP Show(BOOL) override { return E_NOTIMPL; }
    STDMETHODIMP GetTooltipString(BSTR* tip) override {
        *tip = SysAllocString(isOn() ? L"KoEnJa" : L"KoEnJa: off (English)");
        return *tip ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP OnClick(TfLBIClick click, POINT, const RECT*) override {
        if (click == TF_LBI_CLK_LEFT && clicked) clicked();
        return S_OK;
    }
    STDMETHODIMP InitMenu(ITfMenu*) override { return E_NOTIMPL; }
    STDMETHODIMP OnMenuSelect(UINT) override { return E_NOTIMPL; }
    STDMETHODIMP GetIcon(HICON* icon) override {
        int size = GetSystemMetrics(SM_CXSMICON);
        *icon = HICON(LoadImageW(module, MAKEINTRESOURCEW(isOn() ? ICON_ON : ICON_OFF), IMAGE_ICON, size, size, 0));
        return *icon ? S_OK : E_FAIL;
    }
    STDMETHODIMP GetText(BSTR* text) override {
        *text = SysAllocString(isOn() ? NAME : L"A");
        return *text ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP AdviseSink(REFIID iid, IUnknown* unknown, DWORD* cookie) override {
        if (iid != IID_ITfLangBarItemSink) return CONNECT_E_CANNOTCONNECT;
        if (sink) return CONNECT_E_ADVISELIMIT;
        if (FAILED(unknown->QueryInterface(IID_PPV_ARGS(&sink)))) return E_NOINTERFACE;
        *cookie = 1;
        return S_OK;
    }
    STDMETHODIMP UnadviseSink(DWORD cookie) override {
        if (cookie != 1 || !sink) return CONNECT_E_NOCONNECTION;
        sink.Reset();
        return S_OK;
    }
};

class Tip : public ITfTextInputProcessorEx,
            public ITfThreadMgrEventSink,
            public ITfKeyEventSink,
            public ITfCompositionSink,
            public ITfDisplayAttributeProvider,
            public ITfCompartmentEventSink,
            public ITfCandidateListUIElement {
public:
    COUNTED
    ComPtr<ITfThreadMgr> threadMgr;
    TfClientId client = TF_CLIENTID_NULL;
    DWORD threadMgrCookie = TF_INVALID_COOKIE;
    DWORD openCloseCookie = TF_INVALID_COOKIE;
    bool keySinkAdvised = false;
    TfGuidAtom underline = TF_INVALID_GUIDATOM, thick = TF_INVALID_GUIDATOM;
    std::unique_ptr<InputSession> session;
    /** Where the word being typed is shown (the engine's marked text). */
    ComPtr<ITfComposition> composition;
    ComPtr<ModeButton> button;

    /** The candidates listed (the conversion's, or the Japanese word's as it is typed), as the
     *  engine gave them, and the window's or app's copy. Empty: no list. */
    std::vector<rea::ConversionCandidate> candidates;
    std::vector<std::wstring> listed;
    int selection = 0;
    ComPtr<ITfContext> listContext;
    RECT listAnchor{};
    DWORD uiElement = TF_INVALID_UIELEMENTID;
    /** Whether KoEnJa draws the list; false while a full-screen app draws it from the UI element. */
    BOOL drawsList = TRUE;
    CandidateWindow window;
    /** A switch key whose key-down was eaten: its key-up goes too (a lone Alt up opens the app's menu). */
    WPARAM eatenSwitch = 0;
    /** Re-read on activation and focus, so a change in KoEnJaSettings needs no restart. */
    Switch switchKey = Switch::RightAlt;
    Keyboard keyboard = Keyboard::Korean;
    /** The session's languages (LanguageOrder); a new one is made when they change. */
    std::vector<rea::Language> order;
    bool keyLog = false;
    /** The tap switch key's parts (tapPart) pressed since the last other key; full means a tap. */
    int tapped = 0;
    /** Whether the open conversion's list shows: the first Space converts in place, the next opens it. */
    bool listOpen = false;

    virtual ~Tip() = default;

    STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
        if (iid == IID_IUnknown || iid == IID_ITfTextInputProcessor || iid == IID_ITfTextInputProcessorEx)
            *out = static_cast<ITfTextInputProcessorEx*>(this);
        else if (iid == IID_ITfThreadMgrEventSink) *out = static_cast<ITfThreadMgrEventSink*>(this);
        else if (iid == IID_ITfKeyEventSink) *out = static_cast<ITfKeyEventSink*>(this);
        else if (iid == IID_ITfCompositionSink) *out = static_cast<ITfCompositionSink*>(this);
        else if (iid == IID_ITfDisplayAttributeProvider) *out = static_cast<ITfDisplayAttributeProvider*>(this);
        else if (iid == IID_ITfCompartmentEventSink) *out = static_cast<ITfCompartmentEventSink*>(this);
        else if (iid == IID_ITfUIElement || iid == IID_ITfCandidateListUIElement) *out = static_cast<ITfCandidateListUIElement*>(this);
        else { *out = nullptr; return E_NOINTERFACE; }
        AddRef();
        return S_OK;
    }

    // ITfTextInputProcessorEx

    STDMETHODIMP Activate(ITfThreadMgr* manager, TfClientId id) override { return ActivateEx(manager, id, 0); }

    STDMETHODIMP ActivateEx(ITfThreadMgr* manager, TfClientId id, DWORD) override {
        threadMgr = manager;
        client = id;
        readSettings();
        ComPtr<ITfSource> source;
        ComPtr<ITfKeystrokeMgr> keystrokes;
        HRESULT hr;
        if (FAILED(hr = threadMgr.As(&source)) ||
            FAILED(hr = source->AdviseSink(IID_ITfThreadMgrEventSink, static_cast<ITfThreadMgrEventSink*>(this), &threadMgrCookie)) ||
            FAILED(hr = threadMgr.As(&keystrokes)) || FAILED(hr = keystrokes->AdviseKeyEventSink(client, this, TRUE))) {
            Deactivate();
            return hr;
        }
        keySinkAdvised = true;
        ComPtr<ITfCategoryMgr> categories;
        if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&categories)))) {
            categories->RegisterGUID(ATTRIBUTE_INPUT, &underline);
            categories->RegisterGUID(ATTRIBUTE_TARGET, &thick);
        }
        try {
            if (!session) session = makeSession(order);
        } catch (...) { // the engine's data is missing or unreadable
            Deactivate();
            return E_UNEXPECTED;
        }
        session->activate();
        // On whenever it is picked; then the switch key (or the indicator) switches it off and on.
        setOpen(true);
        ComPtr<ITfSource> openClose;
        if (auto compartment = openCloseCompartment(); compartment && SUCCEEDED(compartment.As(&openClose)))
            openClose->AdviseSink(IID_ITfCompartmentEventSink, static_cast<ITfCompartmentEventSink*>(this), &openCloseCookie);
        ComPtr<ITfLangBarItemMgr> items;
        if (SUCCEEDED(threadMgr.As(&items))) {
            button.Attach(new ModeButton);
            button->on = [this] { return isOpen(); };
            button->clicked = [this] { setOpen(!isOpen()); };
            if (FAILED(items->AddItem(button.Get()))) button.Reset();
        }
        return S_OK;
    }

    STDMETHODIMP Deactivate() override {
        finish();
        closeList();
        window.destroy();
        ComPtr<ITfLangBarItemMgr> items;
        if (button && SUCCEEDED(threadMgr.As(&items))) items->RemoveItem(button.Get());
        if (button) { button->on = nullptr; button->clicked = nullptr; }
        button.Reset();
        ComPtr<ITfSource> openClose;
        if (auto compartment = openCloseCompartment();
            openCloseCookie != TF_INVALID_COOKIE && compartment && SUCCEEDED(compartment.As(&openClose)))
            openClose->UnadviseSink(openCloseCookie);
        openCloseCookie = TF_INVALID_COOKIE;
        ComPtr<ITfKeystrokeMgr> keystrokes;
        if (keySinkAdvised && SUCCEEDED(threadMgr.As(&keystrokes))) keystrokes->UnadviseKeyEventSink(client);
        keySinkAdvised = false;
        ComPtr<ITfSource> source;
        if (threadMgrCookie != TF_INVALID_COOKIE && SUCCEEDED(threadMgr.As(&source))) source->UnadviseSink(threadMgrCookie);
        threadMgrCookie = TF_INVALID_COOKIE;
        threadMgr.Reset();
        client = TF_CLIENTID_NULL;
        return S_OK;
    }

    // ITfThreadMgrEventSink: only focus matters.

    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
    STDMETHODIMP OnSetFocus(ITfDocumentMgr*, ITfDocumentMgr*) override {
        finish();
        readSettings();
        if (session) session->activate();
        return S_OK;
    }
    STDMETHODIMP OnPushContext(ITfContext*) override { return S_OK; }
    STDMETHODIMP OnPopContext(ITfContext*) override { return S_OK; }

    // ITfKeyEventSink

    /** The app's thread got the keyboard back (from KoEnJaSettings, say). */
    STDMETHODIMP OnSetFocus(BOOL foreground) override {
        if (foreground) readSettings();
        tapped = 0;
        return S_OK;
    }
    STDMETHODIMP OnTestKeyDown(ITfContext* context, WPARAM vk, LPARAM lParam, BOOL* eaten) override {
        trackTap(vk, lParam);
        *eaten = actionFor(context, physicalKey(keyboard, vk, lParam), lParam).has_value();
        log("TestDown", vk, lParam, *eaten);
        return S_OK;
    }
    STDMETHODIMP OnKeyDown(ITfContext* context, WPARAM vk, LPARAM lParam, BOOL* eaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* context, WPARAM vk, LPARAM lParam, BOOL* eaten) override {
        *eaten = (eatenSwitch && physicalKey(keyboard, vk, lParam) == eatenSwitch) || isTap(context, vk, lParam);
        if (!*eaten && tapPart(vk, lParam)) tapped = 0;
        log("TestUp", vk, lParam, *eaten);
        return S_OK;
    }
    STDMETHODIMP OnKeyUp(ITfContext* context, WPARAM vk, LPARAM lParam, BOOL* eaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext*, REFGUID, BOOL* eaten) override { *eaten = FALSE; return S_OK; }

    void readSettings() {
        keyboard = readKeyboard();
        switchKey = readSwitch(keyboard);
        keyLog = readKeyLog();
        auto languages = readOrder();
        if (languages == order) return;
        // A new order, a new session: not while a word is being typed into it (the next focus then).
        if (session && (composition || typing())) return;
        try {
            if (session) {
                session = makeSession(languages);
                session->activate();
            }
            order = languages;
        } catch (...) {} // the engine's data unreadable: the old session goes on
    }

    /** The parts of the tap switch key (0: the switch key is no tap). */
    int tapParts() const {
        return switchKey == Switch::RightAlt ? 1 : switchKey == Switch::LeftCtrl ? 2 : switchKey == Switch::CtrlShift ? 2 | 4 : 0;
    }
    /** A key-down: a part of the tap key adds to it (a repeat, bit 30, doesn't), any other key spoils it. */
    void trackTap(WPARAM vk, LPARAM lParam) {
        int part = tapPart(vk, lParam) & tapParts();
        if (!part) tapped = 0;
        else if (!(lParam & (1 << 30))) tapped |= part;
    }
    /** Whether this key-up ends a tap of the switch key: all its parts went down with nothing else. */
    bool isTap(ITfContext* context, WPARAM vk, LPARAM lParam) const {
        return session && tapParts() && (tapPart(vk, lParam) & tapParts()) && tapped == tapParts() && !disabled(context);
    }

    /** One line in keys.log per key event, when KeyLog is on: what reached the TIP in a real app. */
    void log(const char* event, WPARAM vk, LPARAM lParam, BOOL eaten) const {
        if (!keyLog) return;
        wchar_t local[MAX_PATH];
        if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH)) return;
        auto folder = std::wstring(local) + L"\\KoEnJa";
        CreateDirectoryW(folder.c_str(), nullptr);
        HANDLE file = CreateFileW((folder + L"\\keys.log").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        char exe[MAX_PATH] = "";
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        const char* name = strrchr(exe, '\\') ? strrchr(exe, '\\') + 1 : exe;
        SYSTEMTIME t;
        GetLocalTime(&t);
        char line[400];
        int n = snprintf(line, sizeof line,
                         "%02d:%02d:%02d.%03d %s %-8s vk=%02X scan=%02X ext=%d context=%d repeat=%d up=%d lParam=%08X eaten=%d composing=%d open=%d "
                         "ctrl=%d shift=%d alt=%d switch=%ls\r\n",
                         t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, name, event, unsigned(vk), unsigned((lParam >> 16) & 0xFF),
                         extended(lParam), int((lParam >> 29) & 1), int((lParam >> 30) & 1), int((lParam >> 31) & 1), unsigned(lParam),
                         int(eaten), int(typing()), int(isOpen()), GetKeyState(VK_CONTROL) < 0, GetKeyState(VK_SHIFT) < 0,
                         GetKeyState(VK_MENU) < 0, SWITCH_NAMES[int(switchKey)]);
        DWORD written;
        WriteFile(file, line, DWORD(std::max(0, n)), &written, nullptr);
        CloseHandle(file);
    }

    // ITfCompositionSink

    /** The app ended the composition (a click elsewhere, say): its text stays as shown. */
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ec, ITfComposition* ended) override {
        ComPtr<ITfRange> range;
        ComPtr<ITfContext> context;
        if (SUCCEEDED(ended->GetRange(&range)) && SUCCEEDED(range->GetContext(&context))) setUnderline(context.Get(), ec, range.Get(), false);
        drop();
        return S_OK;
    }

    // ITfDisplayAttributeProvider

    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** out) override {
        *out = new DisplayAttribute;
        return S_OK;
    }
    STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** out) override {
        if (guid != ATTRIBUTE_INPUT && guid != ATTRIBUTE_TARGET) { *out = nullptr; return E_INVALIDARG; }
        *out = new DisplayAttribute(guid == ATTRIBUTE_TARGET);
        return S_OK;
    }

    // ITfCompartmentEventSink: switched off (by 한/영, the indicator or the app), what's being
    // typed goes in as shown.

    STDMETHODIMP OnChange(REFGUID guid) override {
        if (guid != GUID_COMPARTMENT_KEYBOARD_OPENCLOSE) return S_OK;
        if (!isOpen()) finish();
        if (button) button->update();
        return S_OK;
    }

    // ITfCandidateListUIElement: the list for an app that draws it itself (full-screen games).

    STDMETHODIMP GetDescription(BSTR* description) override {
        *description = SysAllocString(L"KoEnJa candidates");
        return *description ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP GetGUID(GUID* guid) override { *guid = CANDIDATE_UI; return S_OK; }
    /** The app says whether KoEnJa's own window shows. */
    STDMETHODIMP Show(BOOL show) override {
        drawsList = show;
        if (show && !listed.empty()) window.show(listed, selection, listAnchor);
        else window.hide();
        return S_OK;
    }
    STDMETHODIMP IsShown(BOOL* shown) override {
        *shown = window.hwnd && IsWindowVisible(window.hwnd);
        return S_OK;
    }
    STDMETHODIMP GetUpdatedFlags(DWORD* flags) override {
        *flags = TF_CLUIE_DOCUMENTMGR | TF_CLUIE_COUNT | TF_CLUIE_SELECTION | TF_CLUIE_STRING | TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE;
        return S_OK;
    }
    STDMETHODIMP GetDocumentMgr(ITfDocumentMgr** document) override {
        *document = nullptr;
        return listContext ? listContext->GetDocumentMgr(document) : E_FAIL;
    }
    STDMETHODIMP GetCount(UINT* count) override { *count = UINT(listed.size()); return S_OK; }
    STDMETHODIMP GetSelection(UINT* index) override { *index = UINT(selection); return S_OK; }
    STDMETHODIMP GetString(UINT index, BSTR* text) override {
        if (index >= listed.size()) { *text = nullptr; return E_INVALIDARG; }
        *text = SysAllocString(listed[index].c_str());
        return *text ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP GetPageIndex(UINT* starts, UINT size, UINT* pages) override {
        *pages = UINT((listed.size() + PAGE - 1) / PAGE);
        if (starts) for (UINT i = 0; i < std::min(size, *pages); i++) starts[i] = i * PAGE;
        return S_OK;
    }
    STDMETHODIMP SetPageIndex(UINT*, UINT) override { return E_NOTIMPL; } // pages are fixed at 9
    STDMETHODIMP GetCurrentPage(UINT* page) override { *page = UINT(selection / PAGE); return S_OK; }

    /** The thread's on/off switch, the one the system's IMEs use (so apps and the indicator see it). */
    ComPtr<ITfCompartment> openCloseCompartment() const {
        ComPtr<ITfCompartmentMgr> compartments;
        ComPtr<ITfCompartment> compartment;
        if (threadMgr && SUCCEEDED(threadMgr.As(&compartments))) compartments->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &compartment);
        return compartment;
    }
    bool isOpen() const {
        auto compartment = openCloseCompartment();
        VARIANT value{};
        return !compartment || compartment->GetValue(&value) != S_OK || value.vt != VT_I4 || value.lVal != 0;
    }
    void setOpen(bool open) {
        VARIANT value{};
        value.vt = VT_I4;
        value.lVal = open;
        if (auto compartment = openCloseCompartment()) compartment->SetValue(client, &value);
    }

    /** Lists the session's candidates under the composition (in the key's edit session), or
     *  closes the list when there are none. As iOS's candidate bar: the open conversion's, else
     *  the Japanese word's as it is typed. */
    void listCandidates(TfEditCookie ec, ITfContext* context) {
        if (session->conversion) {
            candidates = session->conversion->candidates;
            selection = session->conversion->selected;
        } else {
            candidates = session->typingCandidates();
            if (candidates.size() > 30) candidates.resize(30);
            selection = 0;
        }
        if (candidates.empty() || (session->conversion && !listOpen)) return closeList();
        listed.clear();
        for (auto& c : candidates) listed.emplace_back(c.text.begin(), c.text.end());
        listContext = context;
        listAnchor = {};
        ComPtr<ITfRange> range;
        ComPtr<ITfContextView> view;
        BOOL clipped;
        HWND app = nullptr;
        if (composition && SUCCEEDED(composition->GetRange(&range)) && SUCCEEDED(context->GetActiveView(&view)) &&
            SUCCEEDED(view->GetTextExt(ec, range.Get(), &listAnchor, &clipped)) && SUCCEEDED(view->GetWnd(&app)) && app) {
            // A DPI-unaware app measures in its scaled pixels; the window is in real ones.
            LogicalToPhysicalPointForPerMonitorDPI(app, reinterpret_cast<POINT*>(&listAnchor.left));
            LogicalToPhysicalPointForPerMonitorDPI(app, reinterpret_cast<POINT*>(&listAnchor.right));
        }
        ComPtr<ITfUIElementMgr> elements;
        if (uiElement == TF_INVALID_UIELEMENTID) {
            drawsList = TRUE;
            if (SUCCEEDED(threadMgr.As(&elements)) && FAILED(elements->BeginUIElement(this, &drawsList, &uiElement))) {
                uiElement = TF_INVALID_UIELEMENTID;
                drawsList = TRUE;
            }
        } else if (SUCCEEDED(threadMgr.As(&elements))) {
            elements->UpdateUIElement(uiElement);
        }
        if (drawsList) window.show(listed, selection, listAnchor);
    }

    void closeList() {
        window.hide();
        candidates.clear();
        listed.clear();
        listContext.Reset();
        ComPtr<ITfUIElementMgr> elements;
        if (uiElement != TF_INVALID_UIELEMENTID && SUCCEEDED(threadMgr.As(&elements))) elements->EndUIElement(uiElement);
        uiElement = TF_INVALID_UIELEMENTID;
    }

    void setUnderline(ITfContext* context, TfEditCookie ec, ITfRange* range, bool on) { setAttribute(context, ec, range, on ? underline : TF_INVALID_GUIDATOM); }
    /** [atom]'s look on [range]; TF_INVALID_GUIDATOM: none. */
    void setAttribute(ITfContext* context, TfEditCookie ec, ITfRange* range, TfGuidAtom atom) {
        ComPtr<ITfProperty> property;
        if (FAILED(context->GetProperty(GUID_PROP_ATTRIBUTE, &property))) return;
        if (atom == TF_INVALID_GUIDATOM) { property->Clear(ec, range); return; }
        VARIANT value;
        value.vt = VT_I4;
        value.lVal = LONG(atom);
        property->SetValue(ec, range, &value);
    }

private:
    /** TSF's flags for a field the keyboard mustn't compose in (a password, IME off) or no field at all. */
    static bool disabled(ITfContext* context) {
        ComPtr<ITfCompartmentMgr> compartments;
        if (!context || FAILED(context->QueryInterface(IID_PPV_ARGS(&compartments)))) return true;
        for (auto guid : {&GUID_COMPARTMENT_KEYBOARD_DISABLED, &GUID_COMPARTMENT_EMPTYCONTEXT}) {
            ComPtr<ITfCompartment> compartment;
            VARIANT value{};
            if (SUCCEEDED(compartments->GetCompartment(*guid, &compartment)) && compartment->GetValue(&value) == S_OK &&
                value.vt == VT_I4 && value.lVal)
                return true;
        }
        return false;
    }

    bool typing() const { return session && (!session->keys.empty() || session->conversion); }

    /** What a key-down does: keys for the session, a listed candidate picked, KoEnJa off/on, an F8–F10
     *  form of the reading, or nothing (the key just eaten). */
    struct Action {
        enum { Type, Pick, Toggle, Form, Nothing } what;
        InputSession::Key key{Kind::Other};
        /** Pick: the candidate; Form: the F key; Type: how many times (-1: until the conversion is done). */
        int index = 1;
        /** Whether the conversion's list shows afterwards; nullopt leaves it as it is. */
        std::optional<bool> showList;
        /** A printable key the app's layout types otherwise: if the session leaves it to the app, KoEnJa types it. */
        bool insert = false;
    };
    static Action type(InputSession::Key key, int times = 1, std::optional<bool> showList = std::nullopt) {
        return {Action::Type, key, times, showList};
    }

    /** The switch key, if it acts on its key-down (the others are taps, done on their key-up). On
     *  Korean layouts right Alt arrives as 한/영, so that picks right Alt too. */
    bool isSwitchDown(WPARAM vk) const {
        bool shift = GetKeyState(VK_SHIFT) < 0, ctrl = GetKeyState(VK_CONTROL) < 0, alt = GetKeyState(VK_MENU) < 0;
        switch (switchKey) {
            case Switch::RightAlt:
            case Switch::Hangul: return vk == VK_HANGUL;
            case Switch::Henkan: return vk == VK_CONVERT;
            case Switch::Muhenkan: return vk == VK_NONCONVERT;
            case Switch::ShiftSpace: return vk == VK_SPACE && shift && !ctrl && !alt;
            case Switch::CtrlSpace: return vk == VK_SPACE && ctrl && !shift && !alt;
            default: return false;
        }
    }
    /** The switch key: the word being typed in its next language (the iOS keyboard's 한あA key),
     *  or with nothing being typed, KoEnJa off (plain English) and on. */
    Action switched() const { return typing() ? type({Kind::Cycle}) : Action{Action::Toggle}; }

    /** What a key-down does; nullopt leaves it to the app. Letters start or extend a word; while
     *  one is being typed every other key goes to the session too (most commit it and then reach
     *  the app); with nothing being typed they are all the app's, except the switch key. Japanese
     *  converts as Microsoft's IME does: Space or 変換 converts in place, the next one opens the
     *  list; Space/Tab/↓ and Shift+Space/↑ move in it, PageDown/PageUp by pages, 1–9 pick from the
     *  page, Return commits it all, Esc and Backspace go back to the kana; Shift+←/→ make the first
     *  segment shorter or longer, → commits it and converts the rest. F6/F7 hiragana and katakana
     *  (無変換 flips between them), F8 half-width katakana, F9/F10 full- and half-width romaji.
     *  On a Japanese keyboard 半角/全角 switches KoEnJa off and on. [vk] is physicalKey's. */
    std::optional<Action> actionFor(ITfContext* context, WPARAM vk, LPARAM lParam) const {
        if (!session || disabled(context)) return std::nullopt;
        if (isSwitchDown(vk)) return switched();
        if (vk == VK_OEM_AUTO && keyboard == Keyboard::JIS) return Action{Action::Toggle};
        if (!isOpen()) return std::nullopt;
        // Modifiers alone never commit: Shift is half of typing a capital.
        if ((vk >= VK_SHIFT && vk <= VK_CAPITAL) || vk == VK_LWIN || vk == VK_RWIN || (vk >= VK_LSHIFT && vk <= VK_RMENU))
            return std::nullopt;
        bool chord = GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_MENU) < 0;
        bool shift = GetKeyState(VK_SHIFT) < 0;
        if (!chord && vk >= 'A' && vk <= 'Z') {
            bool upper = shift != ((GetKeyState(VK_CAPITAL) & 1) != 0);
            return type(InputSession::Key::letter(char16_t(upper ? vk : vk + ('a' - 'A'))));
        }
        if (!typing()) return std::nullopt;
        if (chord) return type({Kind::Other});
        auto& converting = session->conversion;
        // A Japanese phrase: converting, or typed with its candidates listed.
        bool japanese = converting || !candidates.empty();
        if (vk >= VK_F6 && vk <= VK_F10) {
            if (vk == VK_F6) return type({Kind::Hiragana}, 1, false);
            if (vk == VK_F7) return type({Kind::Katakana}, 1, false);
            return Action{Action::Form, {}, int(vk - VK_F1 + 1), false};
        }
        if (vk == VK_NONCONVERT) {
            bool katakana = converting && converting->current().text == rea::RomajiComposer::katakana(converting->kana);
            return type({katakana ? Kind::Hiragana : Kind::Katakana}, 1, false);
        }
        if (vk == VK_CONVERT && japanese) vk = VK_SPACE;
        if (!candidates.empty()) {
            int digit = vk >= '1' && vk <= '9' && !shift ? int(vk - '0') : vk >= VK_NUMPAD1 && vk <= VK_NUMPAD9 ? int(vk - VK_NUMPAD0) : 0;
            int index = selection / PAGE * PAGE + digit - 1;
            if (digit && index < int(candidates.size())) return Action{Action::Pick, {}, index};
        }
        if (japanese && vk == VK_TAB) vk = shift ? VK_UP : VK_DOWN;
        // Into the list from the word as typed: the conversion opens on its first candidate, as Space does.
        if (!converting && !candidates.empty() && vk == VK_DOWN) return type(InputSession::Key::printable(u' '), 1, true);
        if (converting) switch (vk) {
            case VK_SPACE: return type(shift ? InputSession::Key{Kind::Up} : InputSession::Key::printable(u' '), 1, true);
            case VK_UP: return type({Kind::Up}, 1, true);
            case VK_DOWN: return type({Kind::Down}, 1, true);
            case VK_PRIOR: return type({Kind::Up}, PAGE, true);
            case VK_NEXT: return type({Kind::Down}, PAGE, true);
            case VK_LEFT:
            case VK_RIGHT:
                if (shift) return type({vk == VK_LEFT ? Kind::Left : Kind::Right});
                // The first segment is the one being converted: → commits it, the rest converts next.
                if (vk == VK_RIGHT && converting->current().kanaCount < int(converting->kana.size())) return type({Kind::Enter});
                return Action{Action::Nothing};
            case VK_RETURN: return type({Kind::Enter}, -1);
        }
        switch (vk) {
            case VK_BACK: return type({Kind::Delete});
            case VK_RETURN: return type({Kind::Enter});
            case VK_ESCAPE: return type({Kind::Escape});
            // The first Space converts in place; the list opens with the next.
            case VK_SPACE: return type(InputSession::Key::printable(u' '), 1, false);
        }
        // Digits and punctuation as this keyboard layout types them (flag 4: leave dead-key state alone),
        // or as the keyboard the user has does, where the layout reads it as another.
        BYTE state[256];
        wchar_t ch[4];
        char16_t typed = GetKeyboardState(state) && ToUnicode(UINT(vk), MapVirtualKeyW(UINT(vk), MAPVK_VK_TO_VSC), state, ch, 4, 4) == 1 && ch[0] >= 0x20
                       ? char16_t(ch[0]) : 0;
        char16_t key = printed(keyboard, lParam, shift).value_or(typed);
        if (!key) return type({Kind::Other});
        auto action = type(InputSession::Key::printable(key));
        action.insert = key != typed;
        return action;
    }

    bool perform(ITfContext* context, const Action& action);
    void finish();

    /** Forgets the word being typed, its text left as shown. */
    void drop() {
        closeList();
        composition.Reset();
        try { if (session) session->commitForNewContext(nullptr); } catch (...) {}
    }
};

/**
 * The text field as the engine sees it, during one edit session: UTF-16 offsets from the start of
 * the document (ITfRangeACP), and the composition as the marked text.
 * ponytail: offsets need an ACP-based text store, which edit controls, RichEdit, Notepad and the
 * CUAS bridge for IMM32 apps all are; for an anchor-based one, measure with ITfRange::ShiftStart.
 */
struct Client : rea::TextClient {
    Tip& tip;
    TfEditCookie ec;
    ITfContext* context;

    Client(Tip& tip, TfEditCookie ec, ITfContext* context) : tip(tip), ec(ec), context(context) {}

    ComPtr<ITfRange> range(TextRange r) {
        ComPtr<ITfRange> out;
        ComPtr<ITfRangeACP> acp;
        if (r.location < 0 || r.location == TextRange::NOT_FOUND || FAILED(context->GetStart(ec, &out)) ||
            FAILED(out.As(&acp)) || FAILED(acp->SetExtent(r.location, r.length)))
            return nullptr;
        return out;
    }

    static TextRange extent(ITfRange* r) {
        ComPtr<ITfRangeACP> acp;
        LONG start, length;
        if (!r || FAILED(r->QueryInterface(IID_PPV_ARGS(&acp))) || FAILED(acp->GetExtent(&start, &length)))
            return {TextRange::NOT_FOUND, 0};
        return {int(start), int(length)};
    }

    TextRange markedRange() override {
        ComPtr<ITfRange> r;
        if (!tip.composition || FAILED(tip.composition->GetRange(&r))) return {TextRange::NOT_FOUND, 0};
        return extent(r.Get());
    }

    TextRange selectedRange() override {
        TF_SELECTION selection;
        ULONG fetched = 0;
        if (FAILED(context->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &selection, &fetched)) || fetched == 0)
            return {TextRange::NOT_FOUND, 0};
        auto out = extent(selection.range);
        selection.range->Release();
        return out;
    }

    std::optional<std::u16string> text(TextRange r) override {
        auto found = range(r);
        if (!found) return std::nullopt;
        std::u16string out(size_t(r.length), u'\0');
        ULONG got = 0;
        if (r.length > 0 && (FAILED(found->GetText(ec, 0, reinterpret_cast<WCHAR*>(out.data()), ULONG(r.length), &got)) ||
                             got != ULONG(r.length)))
            return std::nullopt;
        return out;
    }

    void setMarkedText(const std::u16string& text) override { replace(text, std::nullopt, true); }
    void setMarkedText(const std::u16string& text, TextRange replacing) override { replace(text, replacing, true); }
    void insertText(const std::u16string& text, std::optional<TextRange> replacing) override { replace(text, replacing, false); }

    /** [text] in place of [replacing] and the marked text (or the selection, if neither), then the
     *  caret after it; [marked] keeps it the composition, underlined, else the composition ends. */
    void replace(const std::u16string& text, std::optional<TextRange> replacing, bool marked) {
        TextRange target = replacing ? *replacing : markedRange();
        if (target.location == TextRange::NOT_FOUND) target = selectedRange();
        TextRange shown = markedRange();
        if (shown.location != TextRange::NOT_FOUND) {
            int start = std::min(target.location, shown.location);
            target = {start, int(std::max(target.end(), shown.end()) - start)};
        }
        auto r = range(target);
        if (!r || FAILED(r->SetText(ec, 0, reinterpret_cast<const WCHAR*>(text.data()), LONG(text.size())))) return;
        auto done = range({target.location, int(text.size())});
        if (!done) return;
        if (marked && !text.empty()) {
            if (tip.composition) {
                tip.composition->ShiftStart(ec, done.Get());
                tip.composition->ShiftEnd(ec, done.Get());
            } else {
                ComPtr<ITfContextComposition> compositions;
                if (SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&compositions))))
                    compositions->StartComposition(ec, done.Get(), static_cast<ITfCompositionSink*>(&tip), &tip.composition);
            }
            tip.setUnderline(context, ec, done.Get(), true);
        } else {
            tip.setUnderline(context, ec, done.Get(), false);
            if (tip.composition) tip.composition->EndComposition(ec);
            tip.composition.Reset();
        }
        if (auto caret = range({target.location + int(text.size()), 0})) {
            TF_SELECTION selection{};
            selection.range = caret.Get();
            selection.style.ase = TF_AE_NONE;
            context->SetSelection(ec, 1, &selection);
        }
    }
};

/** Runs [action] in an edit session, then lists the candidates; whether the key was consumed. */
bool Tip::perform(ITfContext* context, const Action& action) {
    if (action.what == Action::Toggle) { setOpen(!isOpen()); return true; }
    if (action.what == Action::Nothing) return true;
    bool handled = false;
    edit(context, client, TF_ES_SYNC, [&](TfEditCookie ec) {
        Client field(*this, ec, context);
        if (action.what == Action::Pick) {
            if (session->conversion) session->choose(action.index, field);
            else session->choose(candidates[action.index], field);
            handled = true;
        } else if (action.what == Action::Form) {
            // The engine's katakana form, then this one in its place (Return commits it, as F7's).
            handled = session->handle({Kind::Katakana}, field);
            if (auto& c = session->conversion; handled && c) {
                auto form = action.index == 8 ? mapped(c->current().text, LCMAP_HALFWIDTH)
                          : mapped(c->keys, action.index == 9 ? LCMAP_FULLWIDTH : LCMAP_HALFWIDTH);
                c->candidates = {{form, int(c->kana.size())}};
                c->selected = 0;
                field.setMarkedText(c->display());
            }
        } else {
            bool converting = session->conversion.has_value();
            handled = session->handle(action.key, field);
            // The word went in and the app would type the key, as its layout reads it: KoEnJa types it.
            if (!handled && action.insert) {
                field.insertText(std::u16string(1, action.key.ch), std::nullopt);
                handled = true;
            }
            // Space and a resized segment convert to kanji, as Microsoft's IME: the reading (often the
            // engine's first candidate, for iOS's bar) is no conversion.
            bool converts = (!converting && action.showList == false && action.key == InputSession::Key::printable(u' ')) ||
                            action.key.kind == Kind::Left || action.key.kind == Kind::Right;
            if (auto& c = session->conversion; converts && c && c->candidates.size() > 1 &&
                                                  c->current().text == c->kana.substr(0, c->current().kanaCount))
                session->handle({Kind::Down}, field);
            // Return: every segment, each as converted (64: a reading is never that long).
            for (int i = 1; action.index < 0 ? session->conversion && i < 64 : i < action.index; i++) session->handle(action.key, field);
        }
        if (action.showList) listOpen = *action.showList;
        // The segment being converted underlined thick, the rest of the reading thin.
        if (auto& c = session->conversion; c && composition) {
            auto marked = field.markedRange();
            if (auto segment = field.range({marked.location, std::min(marked.length, int(c->current().text.size()))}))
                setAttribute(context, ec, segment.Get(), thick == TF_INVALID_GUIDATOM ? underline : thick);
        }
        listCandidates(ec, context);
    });
    return handled;
}

STDMETHODIMP Tip::OnKeyDown(ITfContext* context, WPARAM vk, LPARAM lParam, BOOL* eaten) {
    trackTap(vk, lParam);
    WPARAM key = physicalKey(keyboard, vk, lParam);
    auto action = actionFor(context, key, lParam);
    // Not consumed (Space after a word, Tab, a shortcut…): the app gets the key after the commit.
    *eaten = action && perform(context, *action);
    if (*eaten && isSwitchDown(key)) eatenSwitch = key;
    log("Down", vk, lParam, *eaten);
    return S_OK;
}

/** A tap of the switch key switches on its key-up; the key-up goes (Alt's would open the app's menu). */
STDMETHODIMP Tip::OnKeyUp(ITfContext* context, WPARAM vk, LPARAM lParam, BOOL* eaten) {
    *eaten = FALSE;
    if (eatenSwitch && physicalKey(keyboard, vk, lParam) == eatenSwitch) {
        eatenSwitch = 0;
        *eaten = TRUE;
    } else if (isTap(context, vk, lParam)) {
        perform(context, switched());
        *eaten = TRUE;
    }
    if (tapPart(vk, lParam)) tapped = 0;
    log("Up", vk, lParam, *eaten);
    return S_OK;
}

/** Commits what's being typed where it is shown: focus moved away, or the TIP was switched off. */
void Tip::finish() {
    if (!session) return;
    closeList();
    ComPtr<ITfRange> range;
    ComPtr<ITfContext> context;
    if (!composition || FAILED(composition->GetRange(&range)) || FAILED(range->GetContext(&context))) return drop();
    ComPtr<Tip> self(this);
    ComPtr<ITfComposition> ended = composition;
    HRESULT hr = edit(context.Get(), client, TF_ES_ASYNCDONTCARE, [self, context, ended](TfEditCookie ec) {
        if (self->composition == ended) {
            Client field(*self.Get(), ec, context.Get());
            self->session->commitForNewContext(&field);
        } else { // the lock came late (below): just end it, as shown
            ComPtr<ITfRange> shown;
            if (SUCCEEDED(ended->GetRange(&shown))) self->setUnderline(context.Get(), ec, shown.Get(), false);
            ended->EndComposition(ec);
        }
    });
    // Async or refused: the next field's keys must not reach this word or its offsets.
    if (hr != S_OK) drop();
}

/** CoCreateInstance's way in; static, so it only counts LockServer. */
struct Factory : IClassFactory {
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
        if (iid != IID_IUnknown && iid != IID_IClassFactory) { *out = nullptr; return E_NOINTERFACE; }
        *out = this;
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID iid, void** out) override {
        *out = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        auto tip = new (std::nothrow) Tip;
        if (!tip) return E_OUTOFMEMORY;
        HRESULT hr = tip->QueryInterface(iid, out);
        tip->Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL lock) override {
        lock ? ++references : --references;
        return S_OK;
    }
} factory;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH) {
        module = instance; // no DisableThreadLibraryCalls: the static CRT (/MT) needs thread detach
    } else if (reason == DLL_PROCESS_DETACH) {
        // A class outlives its DLL: loaded again, the old one would still point at this copy's code.
        UnregisterClassW(CANDIDATE_CLASS, module);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** out) {
    if (clsid != CLSID_KoEnJa) { *out = nullptr; return CLASS_E_CLASSNOTAVAILABLE; }
    return factory.QueryInterface(iid, out);
}

STDAPI DllCanUnloadNow() { return references == 0 ? S_OK : S_FALSE; }

static std::wstring classKey() {
    wchar_t clsid[40];
    StringFromGUID2(CLSID_KoEnJa, clsid, 40);
    return std::wstring(L"Software\\Classes\\CLSID\\") + clsid;
}

STDAPI DllUnregisterServer() {
    ComPtr<ITfCategoryMgr> categories;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&categories))))
        for (auto category : CATEGORIES) categories->UnregisterCategory(CLSID_KoEnJa, *category, CLSID_KoEnJa);
    ComPtr<ITfInputProcessorProfileMgr> profiles;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles))))
        for (LANGID language : LANGUAGES) profiles->UnregisterProfile(CLSID_KoEnJa, language, PROFILE_KoEnJa, 0);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, classKey().c_str());
    return S_OK;
}

/** The COM class (HKLM, so every user's apps can load it), the profiles and the categories; needs admin. */
STDAPI DllRegisterServer() {
    wchar_t path[MAX_PATH];
    DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    auto key = classKey();
    HKEY server;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, (key + L"\\InprocServer32").c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &server, nullptr))
        return SELFREG_E_CLASS;
    static const wchar_t apartment[] = L"Apartment";
    bool ok = !RegSetValueExW(server, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(path), (length + 1) * sizeof(wchar_t)) &&
              !RegSetValueExW(server, L"ThreadingModel", 0, REG_SZ, reinterpret_cast<const BYTE*>(apartment), sizeof apartment) &&
              !RegSetKeyValueW(HKEY_LOCAL_MACHINE, key.c_str(), nullptr, REG_SZ, NAME, sizeof NAME);
    RegCloseKey(server);

    ComPtr<ITfInputProcessorProfileMgr> profiles;
    ok = ok && SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles)));
    for (LANGID language : LANGUAGES)
        ok = ok && SUCCEEDED(profiles->RegisterProfile(CLSID_KoEnJa, language, PROFILE_KoEnJa, NAME, ULONG(wcslen(NAME)), path, length, 0,
                                                       nullptr, 0, TRUE, 0));
    ComPtr<ITfCategoryMgr> categories;
    ok = ok && SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&categories)));
    for (auto category : CATEGORIES) ok = ok && SUCCEEDED(categories->RegisterCategory(CLSID_KoEnJa, *category, CLSID_KoEnJa));
    if (!ok) {
        DllUnregisterServer();
        return SELFREG_E_CLASS;
    }
    return S_OK;
}
