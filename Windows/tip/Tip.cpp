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

/** How the composition looks (a solid underline), and an enumerator over it, the only one. */
struct DisplayAttribute : ITfDisplayAttributeInfo, IEnumTfDisplayAttributeInfo {
    COUNTED
    bool enumerated = false;

    virtual ~DisplayAttribute() = default;
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
        if (iid == IID_IUnknown || iid == IID_ITfDisplayAttributeInfo) *out = static_cast<ITfDisplayAttributeInfo*>(this);
        else if (iid == IID_IEnumTfDisplayAttributeInfo) *out = static_cast<IEnumTfDisplayAttributeInfo*>(this);
        else { *out = nullptr; return E_NOINTERFACE; }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP GetGUID(GUID* guid) override { *guid = ATTRIBUTE_INPUT; return S_OK; }
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
        attribute->bAttr = TF_ATTR_INPUT;
        return S_OK;
    }
    STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE*) override { return E_NOTIMPL; }
    /** Both interfaces' Reset: nothing to restore, and the enumeration starts over. */
    STDMETHODIMP Reset() override { enumerated = false; return S_OK; }
    STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** out) override {
        auto copy = new DisplayAttribute;
        copy->enumerated = enumerated;
        *out = copy;
        return S_OK;
    }
    STDMETHODIMP Next(ULONG count, ITfDisplayAttributeInfo** infos, ULONG* fetched) override {
        ULONG n = 0;
        if (count > 0 && !enumerated) { infos[0] = new DisplayAttribute; enumerated = true; n = 1; }
        if (fetched) *fetched = n;
        return n == count ? S_OK : S_FALSE;
    }
    STDMETHODIMP Skip(ULONG count) override {
        if (count == 0) return S_OK;
        bool had = !enumerated;
        enumerated = true;
        return had && count == 1 ? S_OK : S_FALSE;
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

/** The engine's languages, most used first, as in the golden test. */
static std::unique_ptr<InputSession> makeSession() {
    static const bool located = [] {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(module, path, MAX_PATH);
        rea::Resources::directory = std::filesystem::path(path).parent_path() / L"data";
        return true;
    }();
    (void)located;
    using rea::Language;
    return rea::Engines::makeSession({Language::KOREAN, Language::ENGLISH, Language::JAPANESE}, makeConverter());
}

/** The keyboard's language key: 한/영 (Korean keyboards), 変換 (Japanese), else right Alt where it
 *  isn't AltGr: on the layouts KoEnJa runs on (Korean, Japanese) and US English. */
static bool isSwitchKey(WPARAM vk, LPARAM lParam) {
    if (vk == VK_HANGUL || vk == VK_CONVERT) return true;
    if (!(vk == VK_RMENU || (vk == VK_MENU && (lParam & (1 << 24))))) return false; // bit 24: the right one
    LANGID layout = LOWORD(HandleToUlong(GetKeyboardLayout(0)));
    return PRIMARYLANGID(layout) == LANG_KOREAN || PRIMARYLANGID(layout) == LANG_JAPANESE ||
           layout == MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
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
    TfGuidAtom underline = TF_INVALID_GUIDATOM;
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
        if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&categories))))
            categories->RegisterGUID(ATTRIBUTE_INPUT, &underline);
        try {
            if (!session) session = makeSession();
        } catch (...) { // the engine's data is missing or unreadable
            Deactivate();
            return E_UNEXPECTED;
        }
        session->activate();
        // On whenever it is picked; then 한/영 (or the indicator) switches it off and on.
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
        if (session) session->activate();
        return S_OK;
    }
    STDMETHODIMP OnPushContext(ITfContext*) override { return S_OK; }
    STDMETHODIMP OnPopContext(ITfContext*) override { return S_OK; }

    // ITfKeyEventSink

    STDMETHODIMP OnSetFocus(BOOL) override { return S_OK; }
    STDMETHODIMP OnTestKeyDown(ITfContext* context, WPARAM vk, LPARAM lParam, BOOL* eaten) override {
        *eaten = actionFor(context, vk, lParam).has_value();
        return S_OK;
    }
    STDMETHODIMP OnKeyDown(ITfContext* context, WPARAM vk, LPARAM lParam, BOOL* eaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext*, WPARAM vk, LPARAM, BOOL* eaten) override {
        *eaten = eatenSwitch && vk == eatenSwitch;
        return S_OK;
    }
    STDMETHODIMP OnKeyUp(ITfContext*, WPARAM vk, LPARAM, BOOL* eaten) override {
        *eaten = eatenSwitch && vk == eatenSwitch;
        if (*eaten) eatenSwitch = 0;
        return S_OK;
    }
    STDMETHODIMP OnPreservedKey(ITfContext*, REFGUID, BOOL* eaten) override { *eaten = FALSE; return S_OK; }

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
        if (guid != ATTRIBUTE_INPUT) { *out = nullptr; return E_INVALIDARG; }
        *out = new DisplayAttribute;
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
        if (candidates.empty()) return closeList();
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

    void setUnderline(ITfContext* context, TfEditCookie ec, ITfRange* range, bool on) {
        ComPtr<ITfProperty> property;
        if (FAILED(context->GetProperty(GUID_PROP_ATTRIBUTE, &property))) return;
        if (!on || underline == TF_INVALID_GUIDATOM) { property->Clear(ec, range); return; }
        VARIANT value;
        value.vt = VT_I4;
        value.lVal = LONG(underline);
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

    /** What a key-down does: a key for the session, a listed candidate picked, or KoEnJa off/on. */
    struct Action {
        enum { Type, Pick, Toggle } what;
        InputSession::Key key{Kind::Other};
        int index = 0;
    };
    static Action type(InputSession::Key key) { return {Action::Type, key}; }

    /** What a key-down does; nullopt leaves it to the app. Letters start or extend a word; while
     *  one is being typed every other key goes to the session too (most commit it and then reach
     *  the app); with nothing being typed they are all the app's, but 한/영 switches KoEnJa off
     *  and on. While a list shows, Tab/↓ and ↑ move in it and 1–9 pick from its page. */
    std::optional<Action> actionFor(ITfContext* context, WPARAM vk, LPARAM lParam) const {
        if (!session || disabled(context)) return std::nullopt;
        if (isSwitchKey(vk, lParam)) {
            // The iOS keyboard's 한あA key: the word being typed in its next language.
            if (typing()) return type({Kind::Cycle});
            if (vk == VK_HANGUL) return Action{Action::Toggle};
            return std::nullopt;
        }
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
        if (!candidates.empty()) {
            int digit = vk >= '1' && vk <= '9' && !shift ? int(vk - '0') : vk >= VK_NUMPAD1 && vk <= VK_NUMPAD9 ? int(vk - VK_NUMPAD0) : 0;
            int index = selection / PAGE * PAGE + digit - 1;
            if (digit && index < int(candidates.size())) return Action{Action::Pick, {}, index};
            if (vk == VK_TAB) vk = shift ? VK_UP : VK_DOWN;
            // Into the list from the word as typed: the conversion opens on its first candidate, as Space does.
            if (!session->conversion && vk == VK_DOWN) return type(InputSession::Key::printable(u' '));
        }
        if (session->conversion) switch (vk) {
            case VK_UP: return type({Kind::Up});
            case VK_DOWN: return type({Kind::Down});
            case VK_LEFT: return type({Kind::Left}); // the first clause shorter
            case VK_RIGHT: return type({Kind::Right});
        }
        switch (vk) {
            case VK_BACK: return type({Kind::Delete});
            case VK_RETURN: return type({Kind::Enter});
            case VK_ESCAPE: return type({Kind::Escape});
            case VK_SPACE: return type(InputSession::Key::printable(u' '));
        }
        // Digits and punctuation as this keyboard layout types them (flag 4: leave dead-key state alone).
        BYTE state[256];
        wchar_t ch[4];
        if (GetKeyboardState(state) &&
            ToUnicode(UINT(vk), MapVirtualKeyW(UINT(vk), MAPVK_VK_TO_VSC), state, ch, 4, 4) == 1 && ch[0] >= 0x20)
            return type(InputSession::Key::printable(char16_t(ch[0])));
        return type({Kind::Other});
    }

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

STDMETHODIMP Tip::OnKeyDown(ITfContext* context, WPARAM vk, LPARAM lParam, BOOL* eaten) {
    *eaten = FALSE;
    auto action = actionFor(context, vk, lParam);
    if (!action) return S_OK;
    if (action->what == Action::Toggle) {
        setOpen(!isOpen());
        eatenSwitch = vk;
        *eaten = TRUE;
        return S_OK;
    }
    bool handled = false;
    edit(context, client, TF_ES_SYNC, [&](TfEditCookie ec) {
        Client field(*this, ec, context);
        if (action->what == Action::Pick) {
            if (session->conversion) session->choose(action->index, field);
            else session->choose(candidates[action->index], field);
            handled = true;
        } else {
            handled = session->handle(action->key, field);
        }
        listCandidates(ec, context);
    });
    // Not consumed (Space after a word, Tab, a shortcut…): the app gets the key after the commit.
    *eaten = handled;
    if (handled && isSwitchKey(vk, lParam)) eatenSwitch = vk;
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
