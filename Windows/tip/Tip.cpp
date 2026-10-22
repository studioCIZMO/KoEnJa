// KoEnJa's text input processor: the C++ engine's InputSession typing into any TSF text field.
// One COM class is the TIP (TSF makes one per thread manager, so one session per UI thread: the
// engine is single-threaded) and the display attribute provider for the composition's underline.

#include <windows.h>
#include <msctf.h>
#include <olectl.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include "../engine/src/Engines.h"
#include "../engine/src/Resources.h"

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
/** ko-KR and ja-JP: Win+Space lists it under whichever the user has. */
static const LANGID LANGUAGES[] = {MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN), MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN)};
static const GUID* const CATEGORIES[] = {&GUID_TFCAT_TIP_KEYBOARD, &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
                                         &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT, &GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT};
static const wchar_t NAME[] = L"KoEnJa";

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

/** The engine's languages, most used first, as in the golden test; no kana→kanji converter yet. */
static std::unique_ptr<InputSession> makeSession() {
    static const bool located = [] {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(module, path, MAX_PATH);
        rea::Resources::directory = std::filesystem::path(path).parent_path() / L"data";
        return true;
    }();
    (void)located;
    using rea::Language;
    return rea::Engines::makeSession({Language::KOREAN, Language::ENGLISH, Language::JAPANESE}, nullptr);
}

class Tip : public ITfTextInputProcessorEx,
            public ITfThreadMgrEventSink,
            public ITfKeyEventSink,
            public ITfCompositionSink,
            public ITfDisplayAttributeProvider {
public:
    COUNTED
    ComPtr<ITfThreadMgr> threadMgr;
    TfClientId client = TF_CLIENTID_NULL;
    DWORD threadMgrCookie = TF_INVALID_COOKIE;
    bool keySinkAdvised = false;
    TfGuidAtom underline = TF_INVALID_GUIDATOM;
    std::unique_ptr<InputSession> session;
    /** Where the word being typed is shown (the engine's marked text). */
    ComPtr<ITfComposition> composition;

    virtual ~Tip() = default;

    STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
        if (iid == IID_IUnknown || iid == IID_ITfTextInputProcessor || iid == IID_ITfTextInputProcessorEx)
            *out = static_cast<ITfTextInputProcessorEx*>(this);
        else if (iid == IID_ITfThreadMgrEventSink) *out = static_cast<ITfThreadMgrEventSink*>(this);
        else if (iid == IID_ITfKeyEventSink) *out = static_cast<ITfKeyEventSink*>(this);
        else if (iid == IID_ITfCompositionSink) *out = static_cast<ITfCompositionSink*>(this);
        else if (iid == IID_ITfDisplayAttributeProvider) *out = static_cast<ITfDisplayAttributeProvider*>(this);
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
        return S_OK;
    }

    STDMETHODIMP Deactivate() override {
        finish();
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
    STDMETHODIMP OnTestKeyDown(ITfContext*, WPARAM vk, LPARAM, BOOL* eaten) override {
        *eaten = keyFor(vk).has_value();
        return S_OK;
    }
    STDMETHODIMP OnKeyDown(ITfContext* context, WPARAM vk, LPARAM, BOOL* eaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* eaten) override { *eaten = FALSE; return S_OK; }
    STDMETHODIMP OnKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* eaten) override { *eaten = FALSE; return S_OK; }
    STDMETHODIMP OnPreservedKey(ITfContext*, REFGUID, BOOL* eaten) override { *eaten = FALSE; return S_OK; }

    // ITfCompositionSink

    /** The app ended the composition (a click elsewhere, say): its text stays as shown. */
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ec, ITfComposition* ended) override {
        ComPtr<ITfRange> range;
        ComPtr<ITfContext> context;
        if (SUCCEEDED(ended->GetRange(&range)) && SUCCEEDED(range->GetContext(&context))) setUnderline(context.Get(), ec, range.Get(), false);
        composition.Reset();
        if (session) session->commitForNewContext(nullptr);
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
    bool typing() const { return session && (!session->keys.empty() || session->conversion); }

    /** What a key-down means to the session; nullopt leaves it to the app. Letters start or extend a
     *  word; while one is being typed every other key goes to the session too (most commit it and
     *  then reach the app); with nothing being typed they are all the app's. */
    std::optional<InputSession::Key> keyFor(WPARAM vk) const {
        if (!session) return std::nullopt;
        // Modifiers alone never commit: Shift is half of typing a capital.
        if ((vk >= VK_SHIFT && vk <= VK_CAPITAL) || vk == VK_LWIN || vk == VK_RWIN || (vk >= VK_LSHIFT && vk <= VK_RMENU))
            return std::nullopt;
        bool chord = GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_MENU) < 0;
        if (!chord && vk >= 'A' && vk <= 'Z') {
            bool upper = (GetKeyState(VK_SHIFT) < 0) != ((GetKeyState(VK_CAPITAL) & 1) != 0);
            return InputSession::Key::letter(char16_t(upper ? vk : vk + ('a' - 'A')));
        }
        if (!typing()) return std::nullopt;
        if (chord) return InputSession::Key{Kind::Other};
        switch (vk) {
            case VK_BACK: return InputSession::Key{Kind::Delete};
            case VK_RETURN: return InputSession::Key{Kind::Enter};
            case VK_ESCAPE: return InputSession::Key{Kind::Escape};
            case VK_SPACE: return InputSession::Key::printable(u' ');
        }
        // Digits and punctuation as this keyboard layout types them (flag 4: leave dead-key state alone).
        BYTE state[256];
        wchar_t ch[4];
        if (GetKeyboardState(state) &&
            ToUnicode(UINT(vk), MapVirtualKeyW(UINT(vk), MAPVK_VK_TO_VSC), state, ch, 4, 4) == 1 && ch[0] >= 0x20)
            return InputSession::Key::printable(char16_t(ch[0]));
        return InputSession::Key{Kind::Other};
    }

    void finish();
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

STDMETHODIMP Tip::OnKeyDown(ITfContext* context, WPARAM vk, LPARAM, BOOL* eaten) {
    *eaten = FALSE;
    auto key = keyFor(vk);
    if (!key || !context) return S_OK;
    bool handled = false;
    edit(context, client, TF_ES_SYNC, [&](TfEditCookie ec) {
        Client field(*this, ec, context);
        handled = session->handle(*key, field);
    });
    // Not consumed (Space after a word, Tab, a shortcut…): the app gets the key after the commit.
    *eaten = handled;
    return S_OK;
}

/** Commits what's being typed where it is shown: focus moved away, or the TIP was switched off. */
void Tip::finish() {
    if (!session) return;
    ComPtr<ITfRange> range;
    ComPtr<ITfContext> context;
    if (!composition || FAILED(composition->GetRange(&range)) || FAILED(range->GetContext(&context))) {
        composition.Reset();
        session->commitForNewContext(nullptr);
        return;
    }
    ComPtr<Tip> self(this);
    edit(context.Get(), client, TF_ES_ASYNCDONTCARE, [self, context](TfEditCookie ec) {
        Client field(*self.Get(), ec, context.Get());
        self->session->commitForNewContext(&field);
    });
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
        module = instance;
        DisableThreadLibraryCalls(instance);
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
