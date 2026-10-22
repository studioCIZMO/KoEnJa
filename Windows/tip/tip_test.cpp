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

    // Created through its registration, as TSF would.
    ComPtr<ITfTextInputProcessorEx> tip;
    ComPtr<ITfClientId> clientIds;
    TfClientId tipId;
    ULONGLONG started = GetTickCount64();
    if (!check(CoCreateInstance(CLSID_KoEnJa, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&tip)), "create KoEnJa (registered?)") ||
        !check(host.real.As(&clientIds), "client ids") || !check(clientIds->GetClientId(CLSID_KoEnJa, &tipId), "KoEnJa's client id") ||
        !check(tip->ActivateEx(&host, tipId, 0), "activate KoEnJa"))
        return 1;
    printf("activated in %llu ms (engine data loaded)\n", GetTickCount64() - started);

    ComPtr<ITfContextComposition> compositions;
    context.As(&compositions);
    auto composing = [&] {
        ComPtr<IEnumITfCompositionView> views;
        ComPtr<ITfCompositionView> view;
        ULONG n = 0;
        return SUCCEEDED(compositions->EnumCompositions(&views)) && views->Next(1, &view, &n) == S_OK && n == 1;
    };

    // keys: letters, ' ', '\n' Enter, '\b' Backspace, '\x1b' Esc, '\f' focus to another field and
    // back (commits what's being typed). Each step checks the whole text.
    ComPtr<ITfDocumentMgr> elsewhere;
    host.real->CreateDocumentMgr(&elsewhere);
    struct Step { const char* keys; const wchar_t* text; bool composing; };
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
    };
    int failures = 0;
    for (auto& step : steps) {
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
            WPARAM vk = c == ' ' ? VK_SPACE : c == '\n' ? VK_RETURN : c == '\b' ? VK_BACK : c == '\x1b' ? VK_ESCAPE : WPARAM(toupper(c));
            // Capitals the way a keyboard types them: Shift held in this thread's key state.
            BYTE keys[256];
            GetKeyboardState(keys);
            keys[VK_SHIFT] = isupper(c) ? 0x80 : 0;
            SetKeyboardState(keys);
            LPARAM lParam = 1 | (LPARAM(MapVirtualKeyW(UINT(vk), MAPVK_VK_TO_VSC)) << 16);
            BOOL eaten = FALSE;
            if (!check(host.KeyDown(vk, lParam, &eaten), "KeyDown")) return 1;
            if (!eaten && c != '\x1b') store.type(c == '\n' ? L'\n' : wchar_t(c));
            host.KeyUp(vk, lParam | 0xC0000000, &eaten);
        }
        bool ok = store.text == step.text && composing() == step.composing;
        failures += !ok;
        std::wstring keys;
        for (const char* k = step.keys; *k; k++) keys += *k == '\n' ? L"⏎" : *k == '\b' ? L"⌫" : *k == '\x1b' ? L"⎋" : *k == '\f' ? L"↹" : std::wstring(1, *k);
        printf("%s %-10s -> \"%s\"%s\n", ok ? "ok  " : "FAIL", utf8(keys).c_str(), utf8(store.text).c_str(), composing() ? " (composing)" : "");
        if (!ok) printf("     expected \"%s\"%s\n", utf8(step.text).c_str(), step.composing ? " (composing)" : "");
    }

    tip->Deactivate();
    host.real->Deactivate();
    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
