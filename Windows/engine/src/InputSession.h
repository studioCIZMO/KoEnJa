#pragma once

#include <climits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "AppPolicy.h"
#include "DecisionEngine.h"
#include "HangulComposer.h"
#include "JapaneseConverter.h"
#include "UserDictionary.h"

namespace rea {

/** NSRange: UTF-16 offsets, like TSF's and the Kotlin engine's. */
struct TextRange {
    /** NSNotFound. */
    static constexpr int NOT_FOUND = INT_MAX;

    int location;
    int length;

    long long end() const { return (long long)location + length; }
    bool operator==(const TextRange&) const = default;
};

/** The text field the input method is typing into. Ranges are UTF-16, like NSString. */
class TextClient {
public:
    virtual ~TextClient() = default;
    virtual void setMarkedText(const std::u16string& text) = 0;
    /** Marked text in place of [range], text the app already has (the marked text, if any, goes too). */
    virtual void setMarkedText(const std::u16string& text, TextRange replacing) = 0;
    /** [replacing] nullopt replaces the marked text (or the selection if nothing is marked). */
    virtual void insertText(const std::u16string& text, std::optional<TextRange> replacing) = 0;
    virtual TextRange selectedRange() = 0;
    virtual TextRange markedRange() = 0;
    virtual std::optional<std::u16string> text(TextRange range) = 0;
};

class JapaneseSegmenter;

/**
 * Per-client typing state: the word being composed, the language of the previous
 * word, the last committed word (so the switch key right after a commit can change it), and
 * an in-progress kana→kanji conversion.
 *
 * While a word is being composed:
 *   letters        extend it; the underlined preview shows the likely language
 *   switch key     cycle it: english → english+particle (github에) → korean → japanese
 *                  (Key::Cycle, or Esc where `policy.escapeSwitches`; otherwise Esc commits
 *                  and goes to the app)
 *   Delete         remove the last kana of a Japanese phrase; in Korean the last jamo of the
 *                  syllable being typed, then whole syllables (as the system's 2-set does);
 *                  else the last keystroke. The word keeps its language until the next letter.
 *                  With live conversion the last clause goes back to kana (今日はいい天気 →
 *                  今日はいいてん) until a letter completes a kana, as in macOS's.
 *   Space          English/Korean: commit, then a space. Japanese: convert to kanji
 *                  (with `fullWidthSpace`, a space after Japanese text is full-width; ⇧Space
 *                  types the other width)
 *   0–9            with `fullWidthDigits`, after Japanese text or inside a phrase they are part
 *                  of it, full-width (１２３, ０３－１２３４); Space offers the half-width form
 *                  Otherwise the app types them; a switch key right after digits or symbols it
 *                  typed makes them Japanese (123 → １２３, [ → 「), and in Japanese text a romaji
 *                  word straight after a number is a counter, converted with it (3ji → 3時)
 *   symbols        , . ? ! - ' inside a Japanese phrase are part of it (、。？！ー); otherwise
 *                  a symbol commits the word, then types the key
 *   Return         commit the word as shown, and nothing else (the next Return is the app's);
 *                  with `returnConfirmsOnly` off the app gets it too, except after Japanese
 *   anything else  commit and let the app handle the key
 * While converting to kanji (candidate window open):
 *   Space / ↓      next candidate          ↑      previous candidate
 *   1–9            commit that candidate   Return commit the selected candidate
 *   Esc            back to kana            letters/punctuation  commit, then keep typing
 *   Delete         as macOS's Japanese input: without live conversion, commit what is shown less
 *                  its last character (変換 → 変); with it, back to typing the phrase, its last
 *                  clause as kana less one (今日はいい天気 → 今日はいいてん)
 *   A candidate that covers only the start of the reading commits that part and
 *   continues converting the rest.
 * Right after a word was committed with space/punctuation:
 *   switch keys    cycle that committed word the same way.
 *   Delete         the app deletes the space or punctuation; the word can still be changed.
 * A Key::Cycle with nothing to change is dropped, never passed to the app.
 *                  The same correction made twice is learned for good.
 *
 * In a URL or path (`hasURLMarker`) words go in as typed and symbols stay half-width.
 *
 * Not ported (macOS only): vim mode, pausing (the pause key), `holdSpace` (terminals) and ⌘
 * (`commandPressed`), per-app policies and dictionary entries, and Colemak.
 */
class InputSession {
public:
    struct Key {
        enum class Kind {
            Letter,
            Delete,
            Escape,
            /** A switch key other than Esc (the keyboard's switch key). */
            Cycle,
            /** Space, digit or punctuation typed by the input method. */
            Printable,
            /** ⇧Space: a space of the other width (full-width where Space types a half-width one). */
            ShiftSpace,
            Enter,
            Up,
            Down,
            /** ←/→: while converting, shorten or lengthen the first clause; otherwise like Other. */
            Left,
            Right,
            /** The Japanese reading as katakana / hiragana. */
            Katakana,
            Hiragana,
            /** Tab, shortcuts…: commit and let the app handle the key. */
            Other,
        };
        Kind kind;
        /** The character of a Letter or Printable. */
        char16_t ch = 0;

        bool operator==(const Key&) const = default;
        static Key letter(char16_t ch) { return {Kind::Letter, ch}; }
        static Key printable(char16_t ch) { return {Kind::Printable, ch}; }
    };

    /** A kana→kanji conversion in progress. */
    struct Conversion {
        /** The reading still being converted. */
        std::u16string kana;
        /** The romaji that produced [kana], so Esc can go back to editing it. */
        std::u16string keys;
        std::vector<ConversionCandidate> candidates;
        int selected;
        /** The user chose Japanese for these keys with the switch key (going back to kana keeps that). */
        bool userForced = false;
        /** Digits the app had already typed just before the phrase, taken into the reading
         *  (3ji → 3じ → 3時); not part of [keys]. */
        std::u16string numberPrefix;

        const ConversionCandidate& current() const { return candidates[selected]; }
        /** What the user sees: the selected candidate, then any reading it doesn't cover. */
        std::u16string display() const;
    };

    /** The last decision and what it was made from, for a typing test. */
    struct Trace {
        std::u16string keys;
        std::optional<Language> previous;
        Decision decision;
        /** Still being typed (the preview) rather than committed. */
        bool partial;
    };

    /** The preview only switches language mid-word when the new reading is at least this likely. */
    static constexpr double PREVIEW_FLIP_CONFIDENCE = 0.7;
    /** Up to this many keys, the preview only turns Latin letters into kana at this confidence. */
    static constexpr size_t SHORT_PREFIX_LENGTH = 3;
    static constexpr double SHORT_PREFIX_FLIP_CONFIDENCE = 0.99;

    std::shared_ptr<DictionaryEngine> engine;
    std::shared_ptr<JapaneseConverter> converter;
    bool japaneseEnabled;
    /** Live conversion: a Japanese phrase is shown as its top kanji candidate while it is typed,
     *  and committed that way. */
    const bool liveConversion;
    /** Return on an underlined word only confirms it. */
    const bool returnConfirmsOnly;

    /** The word being typed (read-only outside the session). */
    std::u16string keys;
    AppPolicy policy;
    /** The open conversion (read-only outside the session). */
    std::optional<Conversion> conversion;
    /** Digits after Japanese text, and inside a Japanese phrase, are part of it and full-width,
     *  as addresses in Japan often need. */
    bool fullWidthDigits = false;
    /** A space after Japanese text is full-width. */
    bool fullWidthSpace = false;
    /** Splits English words out of a Japanese phrase (google in kyouhagoogledekensaku). */
    std::shared_ptr<JapaneseSegmenter> segmenter;
    /** The user's text replacements (shortcut → phrases; a shortcut may be there more than once).
     *  A word whose reading is a shortcut shows that way, so the app replaces it on Space as it
     *  would with the system's input; a Japanese one also offers every phrase as the first candidates. */
    std::map<std::u16string, std::vector<std::u16string>> replacements;
    std::optional<Trace> lastTrace;

    InputSession(std::shared_ptr<DictionaryEngine> engine, std::shared_ptr<JapaneseConverter> converter = nullptr,
                 bool japaneseEnabled = false, bool liveConversion = false, bool returnConfirmsOnly = true);

    /** A new text field became active: forget the sentence context. */
    void activate();

    /** Returns true when the key was consumed. */
    bool handle(Key key, TextClient& client);

    /** Commits whatever is in progress as-is (focus change, shortcut, deactivation). */
    void commit(TextClient* client);

    /** Commits what's in progress after the caret was moved elsewhere (a click; the app asks):
     *  the next word doesn't continue the previous sentence. */
    void commitForNewContext(TextClient* client);

    /** The candidates for the Japanese phrase being typed, before Space, for a
     *  candidate bar that shows them as you type, like the system's Japanese keyboard. */
    std::vector<ConversionCandidate> typingCandidates();

    /** Picks one of [typingCandidates], as Space then a click on it would. */
    void choose(const ConversionCandidate& candidate, TextClient& client);

    /** Commits candidate [index] of the open conversion (e.g. clicked in the candidate window). */
    void choose(int index, TextClient& client);

    /** Whether a run of text without spaces is a URL or path from here on: `://`, `www.`, `~/`,
     *  `./` or `../` at its start, a path that starts with `/` and has another, `@` after
     *  something (an email; `@홍길동` is a mention) or `localhost:`. A lone `.` or `/` isn't one:
     *  L.A.로, TV/라디오 and 남/여 are Korean. */
    static bool hasURLMarker(const std::u16string& run);

    /** What a number reading is made of: digits and the punctuation inside numbers, either width. */
    static bool isNumberCharacter(char16_t c);

    /** The digits of [text] in order, as ASCII ("東京都１－２" → "12"). */
    static std::u16string digits(const std::u16string& text);

    /** Full-width ASCII (！ to ～) as ASCII. */
    static std::u16string halfWidth(const std::u16string& text);

    /** The romaji left after the part that produced the first [kanaCount] kana: the
     *  shortest tail that composes to exactly the remaining kana ("kitte" after きっ is
     *  "te"; "konnichiha" after こん is "nichiha"). Kotlin's keys(keys, kanaCount). */
    static std::u16string keysAfter(const std::u16string& keys, int kanaCount);

    /** The longest start of [from] that composes to a start of [spelling], followed by a spelling of
     *  the rest ("kya" for き is "ki"; "kitte" for きっ is "kiltu"). Kotlin's keys(spelling, from). */
    static std::optional<std::u16string> keysSpelling(const std::u16string& spelling, const std::u16string& from);

    /** The clauses before the last of [text], a conversion of [reading], and how much of the
     *  reading they cover. The last clause is the last run of kanji (or katakana, or anything not
     *  typed as it shows) with the kana after it: 天気です in 今日はいい天気です. ("", 0) when
     *  there is nothing before it, or the text can't be matched to the reading. */
    static std::pair<std::u16string, int> dropLastClause(const std::u16string& text, const std::u16string& reading);

private:
    struct Committed {
        std::u16string keys;
        /** What the engine chose before any switch. */
        Language decided;
        /** What it went in as (the switch key after the commit changes [language]). */
        Language committedAs;
        Language language;
        /** Exactly what was inserted: the word plus its boundary character. */
        std::u16string text;
        std::u16string boundary;
        int location;
        std::optional<Language> previous;
        /** A segment of a URL or path: a switch there isn't a correction to learn. */
        bool learns = true;
        /** The key as typed when [boundary] is 3-set's symbol for it (B for ?): what the boundary
         *  goes back to when the word is switched out of Korean (지냈어? → ldhr2jtB). */
        std::optional<std::u16string> typedBoundary;

        /** The same word after Delete took back its boundary character. */
        Committed withoutBoundary() const;
        /** The boundary as it goes in after the word in [in]. */
        std::u16string boundaryIn(Language in) const { return in == Language::KOREAN ? boundary : typedBoundary.value_or(boundary); }
    };

    struct Word {
        std::u16string keys;
        std::u16string text;
        Language language;
        Language decided;
    };

    std::optional<Language> forced;
    /** Whether [forced] is the user's choice (the switch key) rather than "back to kana" after converting
     *  a phrase the engine read as Japanese anyway: only the user's choices are corrections. */
    bool forcedByUser = false;
    /** The language the engine itself showed last, before any switch: what a commit compares
     *  against, so a forced reading is never mistaken for the engine's own choice. */
    std::optional<Language> enginePreview;
    /** The last `decide` kept a short token (3 keys or fewer, like a command) English only
     *  because the app is set to Strict. Longer words keep the hysteresis: applied to them it
     *  left 8% of Korean words English, against 5% this way, for the same English kept. */
    bool keptEnglishByStrict = false;
    /** "henkan" came back from conversion as "henkan'" (the final n stays ん); dropped again
     *  if the word leaves Japanese, so English doesn't get an apostrophe nobody typed. */
    bool addedNMarker = false;
    /** Set through setPreviousLanguage: a new context (nullopt) also forgets open brackets. */
    std::optional<Language> previousLanguage;
    std::optional<Committed> lastCommit;
    /** Language of the current preview, so in-phrase symbols know where they belong. */
    std::optional<Language> previewLanguage;
    /** The last key passed to the app was a digit (so "." there is a decimal point). */
    bool afterDigit = false;
    /** On 3-set, the keys since the last Korean word are symbols typed straight after it (지냈어?,
     *  그래...): 3-set's symbols go on there (⇧/ is !), as they do right after the word itself. */
    bool afterKoreanSymbol = false;
    /** The word being typed starts after a space or straight after Hangul: on 3-set its 0s may
     *  be ㅋㅋ there, not in 1,000 or 10:00. */
    bool startsAfterHangul = true;
    /** Digits and symbols the app just typed for us, one after another, and where they start:
     *  a switch key right after them makes them Japanese (123 → １２３, [ → 「), and a Japanese
     *  phrase typed right after digits converts with them (3ji → 3時). */
    std::optional<std::pair<std::u16string, int>> passedRun;
    /** The word being typed started right after a number, in Japanese text: a counter (3ji → 3時). */
    bool afterNumberInJapanese = false;
    /** The text gone in since the last Space, Tab or Return ([runKnown]), or since the caret moved
     *  or the field changed, when the text before the caret is read the first time it matters. */
    std::u16string runText;
    bool runKnown = false;
    /** The word being typed is a segment of a URL or path ([hasURLMarker]): it goes in as typed. */
    bool inURLRun = false;
    /** How far back the field is read for the run (UTF-16 units). */
    static constexpr int RUN_LOOK_BEHIND = 256;
    /** The language shown when Delete was pressed, held until the next letter: a shorter word
     *  may read differently (한국 → 한구, not 한rn), but it shouldn't change under the user. */
    std::optional<Language> pinned;
    /** Where the Korean syllable being typed starts once Delete was used in the word: the system's
     *  2-set input has committed everything before it, so those syllables delete whole and new
     *  jamo don't join them (세요 → 세ㅇ, not 셍). Nullopt until then: the word composes as one. */
    std::optional<int> syllableStart;
    /** Esc showed the current phrase's kana instead (the next switch changes language as usual). */
    bool liveSuppressed = false;
    std::optional<std::pair<std::u16string, std::optional<ConversionCandidate>>> liveCache;
    /** What live conversion keeps on screen after Delete, as macOS's does: the clauses before the
     *  last as they were shown ([prefix], a conversion of the first [prefixLength] kana of
     *  [reading]), the rest as kana. Only while the reading stays [reading]: a letter that
     *  completes a kana converts the whole phrase again. */
    struct LiveKept {
        std::u16string prefix;
        int prefixLength;
        std::u16string reading;
    };
    std::optional<LiveKept> liveKept;
    /** Full-width openers not closed yet in this sentence, so ) ] } close them full-width
     *  after an English word too. */
    std::vector<char16_t> openBrackets;
    /** The last correction handed to the learning engine, for the word being typed or just
     *  committed: going back and forth (Space, switch, Space while converting) is one correction. */
    std::optional<std::pair<std::u16string, Language>> lastRecorded;

    bool digitsFullWidth() const;
    bool isLive() const;
    void setPreviousLanguage(std::optional<Language> value);

    void cancelComposition(TextClient& client);
    bool readsAsKoreanWord();
    void typeKoreanSymbol(char16_t symbol, char16_t typed, TextClient& client);
    std::u16string run(TextClient& client);
    void noteRun(const std::u16string& text);
    void noteDeleted();
    void resetRun();
    void startSymbol(const std::u16string& symbol, TextClient& client, std::optional<TextRange> range = std::nullopt);
    void notePassed(char16_t ch, TextClient& client);
    std::optional<TextRange> passedRunRange(int end, TextClient& client);
    std::optional<std::pair<std::u16string, TextRange>> passedRunBeforeCaret(TextClient& client);
    bool closesFullWidth(char16_t ch) const;
    void noteBrackets(const std::u16string& text);
    bool belongsToJapanesePhrase(char16_t ch);
    std::optional<std::pair<Language, std::u16string>> shortcutReading(bool partial);
    std::u16string japaneseReading(bool partial);
    std::optional<ConversionCandidate> liveCandidate(const std::u16string& kana);
    std::u16string japaneseDisplay();
    std::optional<std::pair<std::u16string, std::u16string>> liveReading();
    std::optional<std::u16string> keptOnScreen(const std::u16string& kana);
    void deleteLive(std::optional<std::pair<std::u16string, std::u16string>> shown = std::nullopt);
    std::u16string committedJapanese();
    void startConversion(TextClient& client);
    void showKanaForms(bool katakanaFirst, std::optional<std::u16string> kana, std::optional<std::u16string> romaji, TextClient& client);
    std::optional<Conversion> resizeFirstClause(const Conversion& conversion, int step);
    bool handleConverting(Key key, TextClient& client);
    void commitCandidate(TextClient& client);
    std::vector<ConversionCandidate> conversionCandidates(const std::u16string& kana);
    Decision decide(bool partial);
    Language currentLanguage();
    Language committedLanguage();
    bool isWeakFlipAllowed(const Decision& decision, Language shown, bool runnerUp = true);
    void updatePreview(TextClient& client);
    void show(const std::u16string& text, TextClient& client);
    void splitOffKoreanBeforeOther(TextClient& client);
    bool isOtherLanguageWord(const std::u16string& keys);
    void splitOffJapaneseBeforeKorean(TextClient& client);
    std::optional<std::u16string> preview(Language language);
    void wordEnds(Language language, Language engineChoice);
    Word finishWord();
    void deleteKana();
    void deleteJamoOrSyllable();
    std::optional<std::u16string> hangul();
    void dropNMarker();
    void record(const std::u16string& keys, Language language, Language engineChoice);
    bool engineSplits(const std::u16string& keys);
    std::optional<Committed> cycle(const Committed& commit, TextClient& client);
    void closeFlipWindow();
    void finalize(const Committed& commit);

    static const std::map<char16_t, char16_t>& closing();
    static const std::map<char16_t, char16_t>& pairs();
    static const KoreanModel& koreanModel();
    /** Where the keys of the last syllable start: the longest front that makes [earlier] alone. */
    static int cut(const std::u16string& keys, const std::vector<HangulComposer::Unit>& earlier);
};

} // namespace rea
