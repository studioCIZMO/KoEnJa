// Types into the registered KoEnJa TIP as an app's keyboard would, with no one at the keyboard: hosts TSF
// on a text store of its own, activates the TIP, feeds it key-downs and checks the text and the
// composition (real TSF ranges, compositions and edit sessions; only key routing is the harness's).
// Run after `regsvr32 KoEnJa.dll`, in a desktop session: TSF is off in session 0, so over SSH start it
// with `schtasks /create ... /it` (the logged-on user's session). Exits non-zero on a mismatch.

#include <windows.h>
#include <msctf.h>
#include <textstor.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <string>

using Microsoft::WRL::ComPtr;

static const CLSID CLSID_KoEnJa = {0x113c852d, 0xc4e7, 0x4c6e, {0xb7, 0x13, 0x3f, 0x79, 0x87, 0x4d, 0xe8, 0x67}};

/** A plain-text field: the text, one selection, and locks granted on the spot. */
struct Store : ITextStoreACP {
    ULONG refs = 1;
    std::wstring text;
    LONG selStart = 0, selEnd = 0;
    ComPtr<ITextStoreACPSink> sink;
    DWORD lock = 0, pending = 0;

    STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
        if (iid != IID_IUnknown && iid != IID_ITextStoreACP) { *out = nullptr; return E_NOINTERFACE; }
        *out = this;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs; }
    STDMETHODIMP_(ULONG) Release() override { return --refs; } // lives as long as main

    STDMETHODIMP AdviseSink(REFIID iid, IUnknown* unknown, DWORD) override {
        if (iid != IID_ITextStoreACPSink) return E_INVALIDARG;
        sink.Reset();
        return unknown->QueryInterface(IID_PPV_ARGS(&sink));
    }
    STDMETHODIMP UnadviseSink(IUnknown*) override { sink.Reset(); return S_OK; }
    STDMETHODIMP RequestLock(DWORD flags, HRESULT* result) override {
        if (!sink) return E_UNEXPECTED;
        if (lock) {
            if (flags & TS_LF_SYNC) { *result = TS_E_SYNCHRONOUS; return S_OK; }
            pending |= flags & TS_LF_READWRITE;
            *result = TS_S_ASYNC;
            return S_OK;
        }
        lock = flags & TS_LF_READWRITE;
        *result = sink->OnLockGranted(lock);
        lock = 0;
        while (pending) {
            lock = pending;
            pending = 0;
            sink->OnLockGranted(lock);
            lock = 0;
        }
        return S_OK;
    }
    STDMETHODIMP GetStatus(TS_STATUS* status) override {
        status->dwDynamicFlags = 0;
        status->dwStaticFlags = TS_SS_NOHIDDENTEXT;
        return S_OK;
    }
    STDMETHODIMP QueryInsert(LONG start, LONG end, ULONG, LONG* resultStart, LONG* resultEnd) override {
        *resultStart = start;
        *resultEnd = end;
        return S_OK;
    }
    STDMETHODIMP GetSelection(ULONG, ULONG count, TS_SELECTION_ACP* selection, ULONG* fetched) override {
        *fetched = 0;
        if (count == 0) return S_OK;
        selection[0].acpStart = selStart;
        selection[0].acpEnd = selEnd;
        selection[0].style.ase = TS_AE_END;
        selection[0].style.fInterimChar = FALSE;
        *fetched = 1;
        return S_OK;
    }
    STDMETHODIMP SetSelection(ULONG count, const TS_SELECTION_ACP* selection) override {
        if (count) { selStart = selection[0].acpStart; selEnd = selection[0].acpEnd; }
        return S_OK;
    }
    STDMETHODIMP GetText(LONG start, LONG end, WCHAR* plain, ULONG plainWanted, ULONG* plainGot, TS_RUNINFO* runs, ULONG runsWanted,
                         ULONG* runsGot, LONG* next) override {
        LONG size = LONG(text.size());
        if (end == -1) end = size;
        if (start < 0 || start > end || end > size) return TS_E_INVALIDPOS;
        ULONG n = ULONG(end - start);
        if (plainWanted) n = std::min(n, plainWanted);
        if (plainWanted) text.copy(plain, n, start);
        *plainGot = plainWanted ? n : 0;
        *runsGot = 0;
        if (runsWanted && n) { runs[0] = {n, TS_RT_PLAIN}; *runsGot = 1; }
        *next = start + LONG(n);
        return S_OK;
    }
    STDMETHODIMP SetText(DWORD, LONG start, LONG end, const WCHAR* chars, ULONG count, TS_TEXTCHANGE* change) override {
        if (start < 0 || start > end || end > LONG(text.size())) return TS_E_INVALIDPOS;
        text.replace(start, end - start, chars, count);
        auto moved = [&](LONG& at) {
            if (at >= end) at += LONG(count) - (end - start);
            else if (at > start) at = start + LONG(count);
        };
        moved(selStart);
        moved(selEnd);
        *change = {start, end, start + LONG(count)};
        return S_OK;
    }
    STDMETHODIMP GetFormattedText(LONG, LONG, IDataObject**) override { return E_NOTIMPL; }
    STDMETHODIMP GetEmbedded(LONG, REFGUID, REFIID, IUnknown**) override { return E_NOTIMPL; }
    STDMETHODIMP QueryInsertEmbedded(const GUID*, const FORMATETC*, BOOL* insertable) override { *insertable = FALSE; return S_OK; }
    STDMETHODIMP InsertEmbedded(DWORD, LONG, LONG, IDataObject*, TS_TEXTCHANGE*) override { return E_NOTIMPL; }
    STDMETHODIMP InsertTextAtSelection(DWORD flags, const WCHAR* chars, ULONG count, LONG* start, LONG* end, TS_TEXTCHANGE* change) override {
        LONG from = selStart;
        if (!(flags & TF_IAS_QUERYONLY)) {
            TS_TEXTCHANGE ignored;
            SetText(0, selStart, selEnd, chars, count, change ? change : &ignored);
            selStart = selEnd = from + LONG(count);
        }
        if (start) *start = from;
        if (end) *end = flags & TF_IAS_QUERYONLY ? selEnd : from + LONG(count);
        return S_OK;
    }
    STDMETHODIMP InsertEmbeddedAtSelection(DWORD, IDataObject*, LONG*, LONG*, TS_TEXTCHANGE*) override { return E_NOTIMPL; }
    STDMETHODIMP RequestSupportedAttrs(DWORD, ULONG, const TS_ATTRID*) override { return S_OK; }
    STDMETHODIMP RequestAttrsAtPosition(LONG, ULONG, const TS_ATTRID*, DWORD) override { return S_OK; }
    STDMETHODIMP RequestAttrsTransitioningAtPosition(LONG, ULONG, const TS_ATTRID*, DWORD) override { return S_OK; }
    STDMETHODIMP FindNextAttrTransition(LONG, LONG halt, ULONG, const TS_ATTRID*, DWORD, LONG* next, BOOL* found, LONG* offset) override {
        *next = halt;
        *found = FALSE;
        *offset = 0;
        return S_OK;
    }
    STDMETHODIMP RetrieveRequestedAttrs(ULONG, TS_ATTRVAL*, ULONG* fetched) override { *fetched = 0; return S_OK; }
    STDMETHODIMP GetEndACP(LONG* end) override { *end = LONG(text.size()); return S_OK; }
    STDMETHODIMP GetActiveView(TsViewCookie* view) override { *view = 1; return S_OK; }
    STDMETHODIMP GetACPFromPoint(TsViewCookie, const POINT*, DWORD, LONG*) override { return E_NOTIMPL; }
    STDMETHODIMP GetTextExt(TsViewCookie, LONG, LONG, RECT* rect, BOOL* clipped) override { *rect = {}; *clipped = FALSE; return S_OK; }
    STDMETHODIMP GetScreenExt(TsViewCookie, RECT* rect) override { *rect = {}; return S_OK; }
    STDMETHODIMP GetWnd(TsViewCookie, HWND* window) override { *window = nullptr; return S_OK; }

    /** The user selects all and deletes it, so each test starts on an empty field. */
    void clear() {
        TS_TEXTCHANGE change{0, LONG(text.size()), 0};
        text.clear();
        selStart = selEnd = 0;
        if (sink) { sink->OnTextChange(0, &change); sink->OnSelectionChange(); }
    }

    /** What the app does with a key the TIP left alone: types it, outside any lock, and tells TSF. */
    void type(wchar_t c) {
        LONG start = selStart, end = selEnd;
        TS_TEXTCHANGE change;
        if (c == L'\b') {
            if (start == end && start > 0) start--;
            SetText(0, start, end, L"", 0, &change);
        } else {
            SetText(0, start, end, &c, 1, &change);
        }
        if (sink) { sink->OnTextChange(0, &change); sink->OnSelectionChange(); }
    }
};

/**
 * The thread manager as the TIP sees it. TSF activates a keyboard TIP, and lets it advise a key
 * sink, only on a thread whose window has the focus, which a run without a desktop (or one that
 * mustn't take the user's focus) doesn't have. So the TIP gets this instead: its key sink is kept
 * here and fed as TSF would (the test, then the key); other sinks go to the real thread manager.
 */
struct Host : ITfThreadMgr, ITfSource, ITfKeystrokeMgr {
    ComPtr<ITfThreadMgr> real;
    ComPtr<ITfKeyEventSink> keys;
    ITfContext* context = nullptr;

    STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
        if (iid == IID_IUnknown || iid == IID_ITfThreadMgr) *out = static_cast<ITfThreadMgr*>(this);
        else if (iid == IID_ITfSource) *out = static_cast<ITfSource*>(this);
        else if (iid == IID_ITfKeystrokeMgr) *out = static_cast<ITfKeystrokeMgr*>(this);
        else return real->QueryInterface(iid, out);
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; } // lives as long as main
    STDMETHODIMP_(ULONG) Release() override { return 1; }

    STDMETHODIMP AdviseSink(REFIID iid, IUnknown* sink, DWORD* cookie) override {
        ComPtr<ITfSource> source;
        real.As(&source);
        return source->AdviseSink(iid, sink, cookie);
    }
    STDMETHODIMP UnadviseSink(DWORD cookie) override {
        ComPtr<ITfSource> source;
        real.As(&source);
        return source->UnadviseSink(cookie);
    }

    STDMETHODIMP AdviseKeyEventSink(TfClientId, ITfKeyEventSink* sink, BOOL) override { keys = sink; return S_OK; }
    STDMETHODIMP UnadviseKeyEventSink(TfClientId) override { keys.Reset(); return S_OK; }
    STDMETHODIMP KeyDown(WPARAM vk, LPARAM lParam, BOOL* eaten) override {
        *eaten = FALSE;
        if (!keys) return S_OK;
        keys->OnTestKeyDown(context, vk, lParam, eaten);
        return *eaten ? keys->OnKeyDown(context, vk, lParam, eaten) : S_OK;
    }
    STDMETHODIMP KeyUp(WPARAM vk, LPARAM lParam, BOOL* eaten) override {
        *eaten = FALSE;
        if (!keys) return S_OK;
        keys->OnTestKeyUp(context, vk, lParam, eaten);
        return *eaten ? keys->OnKeyUp(context, vk, lParam, eaten) : S_OK;
    }

    // The TIP uses nothing else.
    STDMETHODIMP Activate(TfClientId*) override { return E_NOTIMPL; }
    STDMETHODIMP Deactivate() override { return E_NOTIMPL; }
    STDMETHODIMP CreateDocumentMgr(ITfDocumentMgr**) override { return E_NOTIMPL; }
    STDMETHODIMP EnumDocumentMgrs(IEnumTfDocumentMgrs**) override { return E_NOTIMPL; }
    STDMETHODIMP GetFocus(ITfDocumentMgr**) override { return E_NOTIMPL; }
    STDMETHODIMP SetFocus(ITfDocumentMgr*) override { return E_NOTIMPL; }
    STDMETHODIMP AssociateFocus(HWND, ITfDocumentMgr*, ITfDocumentMgr**) override { return E_NOTIMPL; }
    STDMETHODIMP IsThreadFocus(BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP GetFunctionProvider(REFCLSID, ITfFunctionProvider**) override { return E_NOTIMPL; }
    STDMETHODIMP EnumFunctionProviders(IEnumTfFunctionProviders**) override { return E_NOTIMPL; }
    STDMETHODIMP GetGlobalCompartment(ITfCompartmentMgr**) override { return E_NOTIMPL; }
    STDMETHODIMP GetForeground(CLSID*) override { return E_NOTIMPL; }
    STDMETHODIMP TestKeyDown(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP TestKeyUp(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP GetPreservedKey(ITfContext*, const TF_PRESERVEDKEY*, GUID*) override { return E_NOTIMPL; }
    STDMETHODIMP IsPreservedKey(REFGUID, const TF_PRESERVEDKEY*, BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP PreserveKey(TfClientId, REFGUID, const TF_PRESERVEDKEY*, const WCHAR*, ULONG) override { return E_NOTIMPL; }
    STDMETHODIMP UnpreserveKey(REFGUID, const TF_PRESERVEDKEY*) override { return E_NOTIMPL; }
    STDMETHODIMP SetPreservedKeyDescription(REFGUID, const WCHAR*, ULONG) override { return E_NOTIMPL; }
    STDMETHODIMP GetPreservedKeyDescription(REFGUID, BSTR*) override { return E_NOTIMPL; }
    STDMETHODIMP SimulatePreservedKey(ITfContext*, REFGUID, BOOL*) override { return E_NOTIMPL; }
};

static std::string utf8(const std::wstring& s) {
    std::string out(WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), out.data(), int(out.size()), nullptr, nullptr);
    return out;
}

static bool check(HRESULT hr, const char* what) {
    if (SUCCEEDED(hr)) return true;
    printf("FAIL %s: 0x%08lx\n", what, hr);
    return false;
}

static const wchar_t SETTINGS[] = L"Software\\KoEnJa";

static void setSetting(const wchar_t* name, DWORD type, const void* data, DWORD size) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, SETTINGS, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr)) return;
    RegSetValueExW(key, name, 0, type, static_cast<const BYTE*>(data), size);
    RegCloseKey(key);
}

/** The user's SwitchKey and KeyLog as they were, put back (or removed) at the end. */
struct Saved {
    struct Value { const wchar_t* name; DWORD type = 0; BYTE data[128]; DWORD size = sizeof data; bool had; };
    Value values[2] = {{L"SwitchKey"}, {L"KeyLog"}};
    Saved() {
        for (auto& v : values) v.had = !RegGetValueW(HKEY_CURRENT_USER, SETTINGS, v.name, RRF_RT_ANY, &v.type, v.data, &v.size);
    }
    ~Saved() {
        for (auto& v : values)
            if (v.had) setSetting(v.name, v.type, v.data, v.size);
            else RegDeleteKeyValueW(HKEY_CURRENT_USER, SETTINGS, v.name);
    }
};

/** A new KoEnJa, created through its registration as TSF would, activated on [host]. */
static ComPtr<ITfTextInputProcessorEx> activate(Host& host) {
    ComPtr<ITfTextInputProcessorEx> tip;
    ComPtr<ITfClientId> clientIds;
    TfClientId tipId;
    if (!check(CoCreateInstance(CLSID_KoEnJa, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&tip)), "create KoEnJa (registered?)") ||
        !check(host.real.As(&clientIds), "client ids") || !check(clientIds->GetClientId(CLSID_KoEnJa, &tipId), "KoEnJa's client id") ||
        !check(tip->ActivateEx(&host, tipId, 0), "activate KoEnJa"))
        return nullptr;
    return tip;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    if (!check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "CoInitializeEx")) return 1;
    Host host;
    TfClientId id;
    if (!check(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&host.real)), "thread manager") ||
        !check(host.real->Activate(&id), "activate TSF"))
        return 1;
    Store store;
    ComPtr<ITfDocumentMgr> document;
    ComPtr<ITfContext> context;
    TfEditCookie storeCookie;
    if (!check(host.real->CreateDocumentMgr(&document), "document") ||
        !check(document->CreateContext(id, 0, static_cast<ITextStoreACP*>(&store), &context, &storeCookie), "context") ||
        !check(document->Push(context.Get()), "push") || !check(host.real->SetFocus(document.Get()), "focus"))
        return 1;
    host.context = context.Get();


    // The TIP's files (keys.log, learned words) go to a folder of the test's own, not the user's.
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring local = std::wstring(exe).substr(0, std::wstring(exe).rfind(L'\\')) + L"\\local";
    CreateDirectoryW(local.c_str(), nullptr);
    CreateDirectoryW((local + L"\\KoEnJa").c_str(), nullptr);
    std::wstring logFile = local + L"\\KoEnJa\\keys.log";
    DeleteFileW(logFile.c_str());
    DeleteFileW((local + L"\\KoEnJa\\japanese.txt").c_str());
    SetEnvironmentVariableW(L"LOCALAPPDATA", local.c_str());
    // The user's settings (the same HKCU the TIP reads) are put back when the test ends.
    Saved saved;

    // Candidates without a dictionary: the TIP's stand-in converter (わたし, 候補1…候補11).
    SetEnvironmentVariableW(L"KOENJA_TEST_CONVERTER", L"1");

    // Created through its registration, as TSF would.
    ComPtr<ITfTextInputProcessorEx> tip;
    ULONGLONG started = GetTickCount64();
    if (!(tip = activate(host))) return 1;
    printf("activated in %llu ms (engine data loaded)\n", GetTickCount64() - started);
    LANGID layout = LOWORD(HandleToUlong(GetKeyboardLayout(0)));
    printf("keyboard layout %04x\n", layout);

    ComPtr<ITfContextComposition> compositions;
    context.As(&compositions);
    auto composing = [&] {
        ComPtr<IEnumITfCompositionView> views;
        ComPtr<ITfCompositionView> view;
        ULONG n = 0;
        return SUCCEEDED(compositions->EnumCompositions(&views)) && views->Next(1, &view, &n) == S_OK && n == 1;
    };
    // The candidate list as a full-screen app reads it, and whether the TIP's window shows it.
    auto listed = [&] {
        ComPtr<ITfCandidateListUIElement> list;
        tip.As(&list);
        UINT count = 0, selected = 0;
        list->GetCount(&count);
        list->GetSelection(&selected);
        std::wstring out;
        for (UINT i = 0; i < count; i++) {
            BSTR text = nullptr;
            list->GetString(i, &text);
            out += (i == selected ? L"[" : L"") + std::wstring(text ? text : L"") + (i == selected ? L"]" : L"") + (i + 1 < count ? L" " : L"");
            SysFreeString(text);
        }
        HWND window = FindWindowW(L"KoEnJaCandidates", nullptr);
        if (count && !(window && IsWindowVisible(window))) out += L" (window hidden!)";
        if (!count && window && IsWindowVisible(window)) out += L" (window still shown!)";
        return out;
    };

    // keys: letters, digits, ' ', '\n' Enter, '\b' Backspace, '\x1b' Esc, '\t' Tab, '\x11' ↑, '\x12' ↓,
    // '\x13' ←, '\x14' →, '\x15' PageDown, '\x01' 한/영, '\x02' 変換, '\x18' 無変換, '\x17' F6, '\x16' F7, '\x19' F8, '\x1c' F9,
    // '\x1a' F10; held keys: '\x04'/'\x05' right Alt down/up, '\x06'/'\x07' left Ctrl, '\x0e'/'\x0f'
    // Shift; '\f' focus to another field and back (commits what's being typed), '\r' empty the field.
    // Each step checks the whole text, the list (L"*": any but none) and, when ups >= 0, how
    // many key-ups the TIP ate. [option], if set, is written to the registry first and the TIP
    // told the app has the focus again, as when the user comes back from KoEnJaSettings.
    struct Key { WPARAM vk; int scan; int held; const wchar_t* name; }; // held: 0 pressed, 1 down, 2 up
    auto keyFor = [](char c) -> Key {
        switch (c) {
            case ' ': return {VK_SPACE, 0x39, 0, L"␣"};
            case '\n': return {VK_RETURN, 0x1C, 0, L"⏎"};
            case '\b': return {VK_BACK, 0x0E, 0, L"⌫"};
            case '\x1b': return {VK_ESCAPE, 0x01, 0, L"⎋"};
            case '\t': return {VK_TAB, 0x0F, 0, L"⇥"};
            case '\x11': return {VK_UP, 0x48, 0, L"↑"};
            case '\x12': return {VK_DOWN, 0x50, 0, L"↓"};
            case '\x15': return {VK_NEXT, 0x51, 0, L"PgDn"};
            case '\x13': return {VK_LEFT, 0x4B, 0, L"←"};
            case '\x14': return {VK_RIGHT, 0x4D, 0, L"→"};
            case '\x01': return {VK_HANGUL, 0x72, 0, L"한영"};
            case '\x02': return {VK_CONVERT, 0x79, 0, L"変換"};
            case '\x18': return {VK_NONCONVERT, 0x7B, 0, L"無変換"};
            case '\x17': return {VK_F6, 0x40, 0, L"F6"};
            case '\x16': return {VK_F7, 0x41, 0, L"F7"};
            case '\x19': return {VK_F8, 0x42, 0, L"F8"};
            case '\x1c': return {VK_F9, 0x43, 0, L"F9"};
            case '\x1a': return {VK_F10, 0x44, 0, L"F10"};
            case '\x04': return {VK_MENU, 0x38, 1, L"RAlt↓"};
            case '\x05': return {VK_MENU, 0x38, 2, L"RAlt↑"};
            case '\x06': return {VK_CONTROL, 0x1D, 1, L"Ctrl↓"};
            case '\x07': return {VK_CONTROL, 0x1D, 2, L"Ctrl↑"};
            case '\x0e': return {VK_SHIFT, 0x2A, 1, L"Shift↓"};
            case '\x0f': return {VK_SHIFT, 0x2A, 2, L"Shift↑"};
        }
        return {WPARAM(toupper(c)), int(MapVirtualKeyW(toupper(c), MAPVK_VK_TO_VSC)), 0, nullptr};
    };
    ComPtr<ITfDocumentMgr> elsewhere;
    host.real->CreateDocumentMgr(&elsewhere);
    struct Step { const char* keys; const wchar_t* text; bool composing; const wchar_t* list = L""; const wchar_t* option = nullptr; int ups = -1; };
    int failures = 0;
    auto run = [&](const auto& steps) {
        for (auto& step : steps) {
            if (step.option) {
                setSetting(L"SwitchKey", REG_SZ, step.option, DWORD((wcslen(step.option) + 1) * sizeof(wchar_t)));
                ComPtr<ITfKeyEventSink> sink;
                tip.As(&sink);
                sink->OnSetFocus(TRUE);
            }
            int ups = 0;
            for (const char* k = step.keys; *k; k++) {
                char c = *k;
                if (c == '\f') {
                    // TSF tells only the TIPs it activated itself, so the harness passes the news on.
                    ComPtr<ITfThreadMgrEventSink> events;
                    tip.As(&events);
                    host.real->SetFocus(elsewhere.Get());
                    events->OnSetFocus(elsewhere.Get(), document.Get());
                    host.real->SetFocus(document.Get());
                    events->OnSetFocus(document.Get(), elsewhere.Get());
                    continue;
                }
                if (c == '\r') { store.clear(); continue; }
                Key key = keyFor(c);
                // The key state as a keyboard leaves it: held modifiers, Shift for capitals.
                BYTE state[256];
                GetKeyboardState(state);
                if (key.held) {
                    BYTE down = key.held == 1 ? 0x80 : 0;
                    state[key.vk] = down;
                    state[key.vk == VK_MENU ? VK_RMENU : key.vk == VK_CONTROL ? VK_LCONTROL : VK_LSHIFT] = down;
                }
                static bool shiftHeld;
                if (key.vk == VK_SHIFT) shiftHeld = key.held == 1;
                if (!key.held) state[VK_SHIFT] = isupper(c) || shiftHeld ? 0x80 : 0;
                SetKeyboardState(state);
                bool chord = state[VK_CONTROL] & 0x80 || state[VK_MENU] & 0x80;
                LPARAM lParam = 1 | (LPARAM(key.scan) << 16) | (c == '\x04' || c == '\x05' ? 1 << 24 : 0) | (state[VK_MENU] & 0x80 ? 1 << 29 : 0);
                BOOL eaten = FALSE;
                if (key.held != 2) {
                    if (!check(host.KeyDown(key.vk, lParam, &eaten), "KeyDown")) exit(1);
                    // What the app does with a key left to it (not a shortcut).
                    if (!eaten && !chord && (c >= ' ' || c == '\n' || c == '\t')) store.type(c == '\n' ? L'\n' : wchar_t(c));
                }
                if (key.held != 1) {
                    BOOL upEaten = FALSE;
                    host.KeyUp(key.vk, lParam | 0xC0000000, &upEaten);
                    ups += upEaten;
                }
            }
            auto shown = listed();
            bool ok = store.text == step.text && composing() == step.composing &&
                      (!wcscmp(step.list, L"*") ? !shown.empty() : shown == step.list) && (step.ups < 0 || ups == step.ups);
            failures += !ok;
            std::wstring keys = step.option ? L"[" + std::wstring(step.option) + L"] " : L"";
            for (const char* k = step.keys; *k; k++) {
                Key key = keyFor(*k);
                keys += *k == '\f' ? L"↹" : *k == '\r' ? L"∅" : key.name ? key.name : std::wstring(1, wchar_t(*k));
            }
            printf("%s %-12s -> \"%s\"%s ups %d%s%s\n", ok ? "ok  " : "FAIL", utf8(keys).c_str(), utf8(store.text).c_str(),
                   composing() ? " (composing)" : "", ups, shown.empty() ? "" : "\n       list: ", utf8(shown).c_str());
            if (!ok) printf("     expected \"%s\"%s ups %d%s%s\n", utf8(step.text).c_str(), step.composing ? " (composing)" : "", step.ups,
                            *step.list ? "\n       list: " : "", utf8(step.list).c_str());
        }
    };

    const Step steps[] = {
        {"dkssud", L"안녕", true},
        {"gktpdy ", L"안녕하세요 ", false},
        {"hello ", L"안녕하세요 hello ", false},
        {"gksrnr", L"안녕하세요 hello 한국", true},
        {"\b", L"안녕하세요 hello 한구", true},
        {"\n", L"안녕하세요 hello 한구", false},
        {"\n", L"안녕하세요 hello 한구\n", false},
        {"Hello\x1b", L"안녕하세요 hello 한구\nHello", false},
        {" dkssud\f", L"안녕하세요 hello 한구\nHello 안녕", false},
        // 한/영 picked: while typing, the word in its next language (iOS's 한あA key); with nothing
        // being typed, KoEnJa off (plain English) and on again. Its key-up goes too.
        {"\r" "dkssud\x01", L"dkssud", true, L"", L"Hangul", 1},
        {"\x01", L"안녕", true, L"", nullptr, 1},
        {" ", L"안녕 ", false},
        {"\r\x01" "dkssud ", L"dkssud ", false, L"", nullptr, 1},
        {"\x01" "dkssud ", L"dkssud 안녕 ", false, L"", nullptr, 1},
        // 変換 isn't the switch key then: with nothing being typed it's the app's.
        {"\r\x02" "dkssud", L"안녕", true, L"", nullptr, 0},
        // 変換 picked.
        {"\n\r" "gksrnr\x02", L"gksrnr", true, L"", L"Henkan", 1},
        {"\n\r\x02" "dkssud\x02 ", L"dkssud ", false, L"", nullptr, 2},
        {"dkssud", L"dkssud 안녕", true},
        // Right Alt tapped (the default): it switches on its key-up, which goes (else the app's menu bar opens).
        {"\n\r" "tkfkd\x04\x05", L"tkfkd", true, L"", L"RightAlt", 1},
        {"\n\r\x04\x05" "dkssud ", L"dkssud ", false, L"", nullptr, 1},
        {"\x04\x05" "dkssud", L"dkssud 안녕", true, L"", nullptr, 1},
        // AltGr+E (left Ctrl, then right Alt, on AltGr layouts) is no tap: still on, nothing eaten.
        {"\n\r\x06\x04" "e" "\x05\x07" "dkssud", L"안녕", true, L"", nullptr, 0},
        // Korean layouts send right Alt as 한/영.
        {"\n\r\x01" "dkssud\x01 ", L"dkssud ", false, L"", nullptr, 2},
        // Left Ctrl tapped; Ctrl+C and AltGr aren't taps.
        {"\n\r" "rkskek\x06\x07", L"rkskek", true, L"", L"LeftCtrl", 1},
        {"\n\r\x06" "c" "\x07" "dkssud", L"안녕", true, L"", nullptr, 0},
        {"\n\r\x06\x04\x05\x07" "dkssud", L"안녕", true, L"", nullptr, 0},
        {"\n\r\x06\x07" "dkssud ", L"dkssud ", false, L"", nullptr, 1},
        {"\x06\x07" "dkssud", L"dkssud 안녕", true, L"", nullptr, 1},
        // Ctrl+Shift tapped together; Ctrl alone or Ctrl+Shift+T isn't.
        {"\n\r" "dlfma\x06\x0e\x0f\x07", L"dlfma", true, L"", L"CtrlShift", 1},
        {"\n\r\x06\x07" "dkssud", L"안녕", true, L"", nullptr, 0},
        {"\n\r\x06\x0e" "t" "\x0f\x07" "dkssud", L"안녕", true, L"", nullptr, 0},
        {"\n\r\x06\x0e\x07\x0f" "dkssud ", L"dkssud ", false, L"", nullptr, 1},
        {"\x0e\x06\x0f\x07" "dkssud", L"dkssud 안녕", true, L"", nullptr, 1},
        // Shift+Space; Space alone still commits and types a space.
        {"\n\r" "dndb\x0e \x0f", L"dndb", true, L"", L"ShiftSpace", 1},
        {" ", L"dndb ", false},
        {"\r\x0e \x0f" "dkssud ", L"dkssud ", false, L"", nullptr, 1},
        {"\x0e \x0f" "dkssud", L"dkssud 안녕", true, L"", nullptr, 1},
        // Ctrl+Space; Shift+Space is a space then.
        {"\n\r" "rhkdrh\x06 \x07", L"rhkdrh", true, L"", L"CtrlSpace", 1},
        {"\n\r" "dkssud\x0e \x0f", L"안녕 ", false, L"", nullptr, 0},
        // The Japanese word's candidates as it is typed; Tab/↓ into them (the conversion), ↑ back, a digit picks.
        {"\f\r" "watashi", L"わたし", true, L"[わたし] 候補1 候補2 候補3 候補4 候補5 候補6 候補7 候補8 候補9 候補10 候補11", L"RightAlt"},
        {"\t", L"わたし", true, L"[わたし] 候補1 候補2 候補3 候補4 候補5 候補6 候補7 候補8 候補9 候補10 候補11"},
        {"\t\x12", L"候補2", true, L"わたし 候補1 [候補2] 候補3 候補4 候補5 候補6 候補7 候補8 候補9 候補10 候補11"},
        {"\x11", L"候補1", true, L"わたし [候補1] 候補2 候補3 候補4 候補5 候補6 候補7 候補8 候補9 候補10 候補11"},
        {"3", L"候補2", false},
        // Space converts in place (to a kanji candidate, not the reading), the next opens the list on
        // the next candidate (Microsoft's IME);
        // Shift+Space back, PageDown a page on, Return commits.
        {"\r" "watashi ", L"候補1", true},
        {" ", L"候補2", true, L"わたし 候補1 [候補2] 候補3 候補4 候補5 候補6 候補7 候補8 候補9 候補10 候補11"},
        {"\x0e \x0f", L"候補1", true, L"わたし [候補1] 候補2 候補3 候補4 候補5 候補6 候補7 候補8 候補9 候補10 候補11"},
        {"\x15", L"候補10", true, L"わたし 候補1 候補2 候補3 候補4 候補5 候補6 候補7 候補8 候補9 [候補10] 候補11"},
        {"\n", L"候補10", false},
        // ↓ on the second page; its 2 is the 11th candidate.
        {"\r" "watashi \x12\x12\x12\x12\x12\x12\x12\x12", L"候補9", true, L"わたし 候補1 候補2 候補3 候補4 候補5 候補6 候補7 候補8 [候補9] 候補10 候補11"},
        {"2", L"候補10", false},
        // 変換 converts as Space does when it isn't the switch key; Esc back to the kana, Esc again cancels.
        {"\r" "watashi\x02\x02", L"候補2", true, L"わたし 候補1 [候補2] 候補3 候補4 候補5 候補6 候補7 候補8 候補9 候補10 候補11"},
        {"\x1b", L"わたし", true, L"[わたし] 候補1 候補2 候補3 候補4 候補5 候補6 候補7 候補8 候補9 候補10 候補11"},
        {"\x1b", L"", false},
        // Backspace while converting: back to the kana; Return commits it as shown.
        {"\r" "watashi \t\b", L"わたし", true, L"[わたし] 候補1 候補2 候補3 候補4 候補5 候補6 候補7 候補8 候補9 候補10 候補11"},
        {"\n", L"わたし", false},
        // F6–F10 and 無変換.
        {"\r" "watashi\x16", L"ワタシ", true},
        {"\x17\n", L"わたし", false},
        {"\r" "watashi\x19\n", L"ﾜﾀｼ", false},
        {"\r" "watashi\x1c\n", L"ｗａｔａｓｈｉ", false},
        {"\r" "watashi\x1a\n", L"watashi", false},
        {"\r" "watashi\x18", L"ワタシ", true},
        {"\x18", L"わたし", true},
        {"\n", L"わたし", false},
    };
    run(steps);

    // KeyLog = 1: a line per key event in keys.log; 0: none.
    auto lines = [&] {
        FILE* f = _wfopen(logFile.c_str(), L"rb");
        std::string all;
        if (f) { char buffer[4096]; size_t n; while ((n = fread(buffer, 1, sizeof buffer, f))) all.append(buffer, n); fclose(f); }
        return all;
    };
    DWORD one = 1, zero = 0;
    setSetting(L"KeyLog", REG_DWORD, &one, sizeof one);
    const Step logged[] = {{"\f\r\x04\x05" "a\x04\x05", L"a", false, L"", nullptr, 2}};
    run(logged);
    auto log = lines();
    size_t count = std::count(log.begin(), log.end(), '\n');
    printf("keys.log: %zu lines\n%s", count, log.c_str());
    bool logOk = count == 8 && log.find(" Up ") != std::string::npos && log.find("vk=12 scan=38 ext=1") != std::string::npos &&
                 log.find("eaten=1") != std::string::npos;
    setSetting(L"KeyLog", REG_DWORD, &zero, sizeof zero);
    run(logged);
    logOk = logOk && lines() == log;
    printf("%s key log on, then off\n", logOk ? "ok  " : "FAIL");
    failures += !logOk;

    // Mozc's dictionary (data\mozc.dic), as the user has it.
    tip->Deactivate();
    SetEnvironmentVariableW(L"KOENJA_TEST_CONVERTER", nullptr);
    started = GetTickCount64();
    if (!(tip = activate(host))) return 1;
    printf("activated with Mozc in %llu ms\n", GetTickCount64() - started);
    const Step mozc[] = {
        {"\f\r" "anatahaitsumo", L"あなたはいつも", true, L"*"},
        {" ", L"貴方はいつも", true},
        {" ", L"アナタはいつも", true, L"*"},
        {"\x12", L"あなたは何時も", true, L"*"},
        {"\n", L"あなたは何時も", false},
        {"\r" "anatahaitsumo  3", L"貴方はいつも", false},
        // Shift+← makes the first segment shorter, → commits it and converts the rest.
        {"\r" "anatahaitsumo \x0e\x13\x13\x13\x0f", L"貴方はいつも", true},
        {"\x14", L"貴方はいつも", true},
        {"\n", L"貴方はいつも", false},
        {"\r" "anatahaitsumo \x1b", L"あなたはいつも", true, L"*"},
        {"\x16", L"アナタハイツモ", true},
        {"\n", L"アナタハイツモ", false},
    };
    run(mozc);

    tip->Deactivate();
    host.real->Deactivate();
    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
