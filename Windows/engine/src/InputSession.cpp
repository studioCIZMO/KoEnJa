#include "InputSession.h"

#include <algorithm>
#include <tuple>
#include <unordered_set>

#include "EnglishModel.h"
#include "JapaneseModel.h"
#include "JapaneseSegmenter.h"
#include "Jamo.h"
#include "MixedWord.h"
#include "RomajiComposer.h"
#include "Sebeolsik.h"
#include "Text.h"

namespace rea {

using Kind = InputSession::Key::Kind;

static bool isHangul(char16_t c) { return (c >= u'가' && c <= u'힣') || (c >= u'ㄱ' && c <= u'ㅣ'); }

std::u16string InputSession::Conversion::display() const { return current().text + drop(kana, current().kanaCount); }

InputSession::Committed InputSession::Committed::withoutBoundary() const {
    Committed copy = *this;
    copy.text = dropLast(text, boundary.size());
    copy.boundary = u"";
    copy.typedBoundary.reset();
    return copy;
}

InputSession::InputSession(std::shared_ptr<DictionaryEngine> engine, std::shared_ptr<JapaneseConverter> converter,
                           bool japaneseEnabled, bool liveConversion, bool returnConfirmsOnly)
    : engine(std::move(engine)), converter(std::move(converter)), japaneseEnabled(japaneseEnabled),
      liveConversion(liveConversion), returnConfirmsOnly(returnConfirmsOnly) {
    if (japaneseEnabled && JapaneseModel::shared()) {
        segmenter = std::make_shared<JapaneseSegmenter>(EnglishModel::shared(), *JapaneseModel::shared());
    }
}

bool InputSession::digitsFullWidth() const { return japaneseEnabled && fullWidthDigits && policy.fullWidth; }

void InputSession::setPreviousLanguage(std::optional<Language> value) {
    previousLanguage = value;
    if (!value) openBrackets.clear(); // a new context
}

void InputSession::activate() {
    closeFlipWindow();
    setPreviousLanguage(std::nullopt);
    passedRun.reset();
    resetRun();
}

bool InputSession::handle(Key key, TextClient& client) {
    bool inKoreanSymbols = afterKoreanSymbol;
    afterKoreanSymbol = false;
    if (key.kind == Kind::ShiftSpace) {
        // With nothing being typed, the other width: full-width unless Space already types that.
        if (!(keys.empty() && !conversion && japaneseEnabled && policy.fullWidth)) {
            return handle(Key::printable(u' '), client);
        }
        closeFlipWindow();
        if (fullWidthSpace && previousLanguage == Language::JAPANESE) { noteRun(u" "); return false; } // the app types " "
        client.insertText(u"　", std::nullopt);
        noteRun(u"　");
        return true;
    }
    if (conversion) return handleConverting(key, client);
    // On 3-set a digit or symbol key with a jamo goes on with the word where the jamo fits
    // (갔 is kf2, 밥 starts on ;); elsewhere it is the symbol. Not in a URL or path, though the
    // word itself doesn't make one: 밝 is ;f@, not an address.
    if (key.kind == Kind::Printable && (keys.empty() || !inURLRun) &&
        Sebeolsik::continuesWord(keys, key.ch, afterDigit, [](char16_t s) { return koreanModel().knowsSyllableIncludingRare(s); }) &&
        !hasURLMarker(run(client))) {
        return handle(Key::letter(key.ch), client);
    }
    // On 3-set a key with a digit or symbol there (⇧B ?, ⇧/ !, ⇧L 3, [ () types it right after
    // a Korean word and after the symbols that follow one; anywhere else it is QWERTY's (Bob),
    // also after Hangul with stray jamo (kgb, 그ㅜ), which isn't a word.
    if (isSebeolsik() && (key.kind == Kind::Letter || key.kind == Kind::Printable) && !inURLRun) {
        auto symbol = Sebeolsik::symbol(key.ch);
        if (symbol && (keys.empty() ? inKoreanSymbols : currentLanguage() == Language::KOREAN && readsAsKoreanWord())) {
            typeKoreanSymbol(*symbol, key.ch, client);
            return true;
        }
    }

    if (!(key.kind == Kind::Printable && keys.empty())) afterDigit = false;
    if (!(key.kind == Kind::Letter || key.kind == Kind::Printable || key.kind == Kind::Escape || key.kind == Kind::Cycle)) passedRun.reset();
    auto pending = lastCommit;
    lastCommit.reset();
    // Esc switches languages where it is a switch key; the others arrive as Cycle.
    bool switchesLanguage = key.kind == Kind::Cycle || (key.kind == Kind::Escape && policy.escapeSwitches);
    if (switchesLanguage && keys.empty() && pending) {
        if (auto cycled = cycle(*pending, client)) {
            lastCommit = cycled;
            return true;
        }
    }
    // Delete right after the commit: the app takes back the space or punctuation, and the
    // word itself can still be changed (Space before looking, then Delete and the switch key).
    if (key.kind == Kind::Delete && keys.empty() && pending && !pending->boundary.empty()) {
        lastCommit = pending->withoutBoundary();
        noteDeleted();
        return false;
    }
    if (pending) finalize(*pending);
    if (switchesLanguage && keys.empty()) {
        if (auto run = passedRunBeforeCaret(client)) {
            passedRun.reset();
            startSymbol(run->first, client, run->second);
            return true;
        }
    }

    switch (key.kind) {
        case Kind::Letter: {
            // A symbol on its own is confirmed by the next word, which is judged by itself.
            if (!keys.empty() && all(keys, [](char16_t c) { return RomajiComposer::symbols().count(c) > 0; }) &&
                !(isSebeolsik() && all(keys, [](char16_t c) { return Sebeolsik::jamo(c).has_value(); }))) {
                commit(&client); // (3-set's ; is ㅂ, a word on its way)
            }
            if (keys.empty()) { // a new word
                lastRecorded.reset();
                liveSuppressed = false;
                liveKept.reset();
                auto cursor = client.selectedRange();
                afterNumberInJapanese = japaneseEnabled && previousLanguage == Language::JAPANESE
                    && passedRun && !passedRun->first.empty() && isNumber(passedRun->first.back())
                    && cursor.location != TextRange::NOT_FOUND && passedRunRange(cursor.location, client).has_value();
                auto before = run(client);
                inURLRun = hasURLMarker(before);
                startsAfterHangul = before.empty() || isHangul(before.back());
            }
            addedNMarker = false;
            pinned.reset();
            keys += key.ch;
            updatePreview(client);
            return true;
        }

        case Kind::Delete: {
            if (keys.empty()) { noteDeleted(); return false; }
            auto shown = forced ? forced : pinned ? pinned : previewLanguage;
            if (!forced) pinned = shown;
            if (shown == Language::JAPANESE && isLive()) deleteLive();
            else if (shown == Language::JAPANESE) deleteKana();
            else if (shown == Language::KOREAN) deleteJamoOrSyllable();
            else keys = dropLast(keys, 1);
            addedNMarker = false;
            if (keys.empty()) { forced.reset(); forcedByUser = false; pinned.reset(); syllableStart.reset(); }
            updatePreview(client);
            return true;
        }

        case Kind::Escape:
        case Kind::Cycle: {
            // The switch key is the keyboard's own: with nothing to change it goes nowhere.
            if (keys.empty()) return key.kind == Kind::Cycle;
            // A live-converted phrase goes back to its kana first, also with an Esc that isn't a
            // switch key, as in macOS's live conversion. Only with a conversion on screen: a phrase
            // showing its kana anyway (たけ, whose first candidate is the kana) switches or cancels
            // at once, not after a press that changes nothing.
            if (isLive() && (forced ? forced : previewLanguage) == Language::JAPANESE && japaneseDisplay() != japaneseReading(true)) {
                liveSuppressed = true;
                updatePreview(client);
                return true;
            }
            // Kana on screen: the app's Esc cancels the phrase, as in macOS's Japanese input,
            // rather than committing it and closing a window in the app.
            if (!switchesLanguage && (forced ? forced : previewLanguage) == Language::JAPANESE) {
                cancelComposition(client);
                return true;
            }
            if (!switchesLanguage) {
                // The app's Esc: the word goes in as shown, then the app gets the key.
                commit(&client);
                return false;
            }
            // Cycle from what the user sees.
            Language shown = forced ? *forced : pinned ? *pinned : previewLanguage ? *previewLanguage : decide(true).language;
            Language next = nextLanguage(shown, keys, japaneseEnabled, engineSplits(keys));
            forced = next;
            pinned.reset();
            forcedByUser = true;
            if (next != Language::JAPANESE) dropNMarker();
            updatePreview(client);
            return true;
        }

        case Kind::Printable: {
            char16_t ch = key.ch;
            if (keys.empty()) {
                bool wasAfterDigit = afterDigit;
                afterDigit = isNumber(ch);
                // Inside a URL or path symbols stay half-width, whatever came before: ほ@gmail.com.
                if (hasURLMarker(run(client) + ch)) {
                    passedRun.reset();
                    noteRun(std::u16string(1, ch));
                    return false;
                }
                if (previousLanguage == Language::JAPANESE) {
                    if (ch == u' ' && fullWidthSpace && japaneseEnabled && policy.fullWidth) {
                        client.insertText(u"　", std::nullopt);
                        return true;
                    }
                    // A number after Japanese text starts a Japanese word: １２３, ３がつ → ３月.
                    if (isAsciiDigit(ch) && digitsFullWidth()) {
                        startSymbol(std::u16string(1, ch), client);
                        return true;
                    }
                }
                // After Japanese text a symbol is Japanese too (、。？！「」・…), underlined until Return
                // or the next word confirms it, as in a Japanese IME; Space shows its other forms.
                // Not inside a number (3.5, 10:30), but a bracket closes the way it was opened.
                if (RomajiComposer::symbols().count(ch) && ((previousLanguage == Language::JAPANESE && !wasAfterDigit) || closesFullWidth(ch))) {
                    startSymbol(std::u16string(1, ch), client);
                    return true;
                }
                notePassed(ch, client);
                noteRun(std::u16string(1, ch));
                afterKoreanSymbol = inKoreanSymbols && ch != u' ';
                return false;
            }
            // A symbol that makes the run a URL or path (ho@, www.) ends the word, half-width.
            bool endsInURL = inURLRun || hasURLMarker(run(client) + keys + ch);
            if (japaneseEnabled && !endsInURL && belongsToJapanesePhrase(ch)) {
                keys += ch;
                // Digits don't read as anything: the phrase stays Japanese, as it was shown.
                if (isNumber(ch) && !forced) { forced = Language::JAPANESE; forcedByUser = false; }
                updatePreview(client);
                return true;
            }
            if (ch == u' ' && currentLanguage() == Language::JAPANESE && converter) {
                startConversion(client);
                return true;
            }
            if (!endsInURL && closesFullWidth(ch)) { // 日本語（English）
                commit(&client);
                startSymbol(std::u16string(1, ch), client);
                return true;
            }
            auto marked = client.markedRange();
            int location = marked.location != TextRange::NOT_FOUND ? marked.location : client.selectedRange().location;
            auto previous = previousLanguage;
            passedRun.reset();
            auto word = finishWord();
            // Only the word goes in; the app types the space or punctuation itself, as with the
            // system's Korean input.
            client.insertText(word.text, std::nullopt);
            lastCommit = Committed{word.keys, word.decided, word.language, word.language, word.text + ch, std::u16string(1, ch),
                                   location, previous, !inURLRun};
            noteRun(word.text + ch);
            afterKoreanSymbol = isSebeolsik() && sentenceLanguage(word.language) == Language::KOREAN && ch != u' ' && !inURLRun;
            return false;
        }

        case Kind::Enter: {
            if (keys.empty()) { noteRun(u"\n"); return false; }
            // Like a Japanese IME, Return on an underlined word only confirms it; the next Return
            // is the app's. A Japanese phrase is only confirmed either way.
            bool japanese = currentLanguage() == Language::JAPANESE;
            commit(&client);
            noteRun(u"\n");
            return returnConfirmsOnly || japanese;
        }

        case Kind::Katakana:
        case Kind::Hiragana: {
            // Only for a Japanese phrase; otherwise they stay the app's.
            if (!(!keys.empty() && japaneseEnabled && (forced == Language::JAPANESE || previewLanguage == Language::JAPANESE))) {
                commit(&client);
                return false;
            }
            wordEnds(Language::JAPANESE, committedLanguage());
            showKanaForms(key.kind == Kind::Katakana, std::nullopt, std::nullopt, client);
            return true;
        }

        case Kind::Up:
        case Kind::Down:
        case Kind::Left:
        case Kind::Right:
        case Kind::Other:
            commit(&client);
            setPreviousLanguage(std::nullopt);
            resetRun();
            return false;

        case Kind::ShiftSpace:
            return false; // handled above
    }
    return false;
}

void InputSession::commit(TextClient* client) {
    closeFlipWindow();
    if (conversion) {
        auto display = conversion->display();
        if (client) client->insertText(display, std::nullopt);
        noteRun(display);
        noteBrackets(display);
        if (converter) converter->learn(conversion->current(), conversion->kana);
        conversion.reset();
        keys = u"";
        setPreviousLanguage(Language::JAPANESE);
        return;
    }
    if (keys.empty()) return;
    auto word = finishWord();
    if (client) client->insertText(word.text, std::nullopt);
    noteRun(word.text);
}

/** The keys make whole syllables, or jamo on their own (ㅎㅎ, and ㅋㅋ on 0). */
bool InputSession::readsAsKoreanWord() {
    auto composed = HangulComposer::compose(keys);
    return composed && (composed->strayJamo().empty() || composed->isJamoAbbreviation());
}

/** 3-set's [symbol] for the key [typed], after a Korean word: the word goes in with it, and a
 *  switch key right after takes both back to the keys as typed (지냈어? → ldhr2jtB). After the
 *  symbols that follow the word, it goes in alone. */
void InputSession::typeKoreanSymbol(char16_t symbol, char16_t typed, TextClient& client) {
    afterKoreanSymbol = true;
    afterDigit = isNumber(symbol); // ⇧L⇧H is 30, and 0 after it a digit too
    passedRun.reset();
    if (keys.empty()) {
        client.insertText(std::u16string(1, symbol), std::nullopt);
        noteRun(std::u16string(1, symbol));
        return;
    }
    auto marked = client.markedRange();
    int location = marked.location != TextRange::NOT_FOUND ? marked.location : client.selectedRange().location;
    auto previous = previousLanguage;
    auto word = finishWord();
    auto text = word.text + symbol;
    // Not left for the app to type as with QWERTY's symbols: its key types something else.
    client.insertText(text, std::nullopt);
    lastCommit = Committed{word.keys, word.decided, word.language, word.language, text, std::u16string(1, symbol),
                           location, previous, true, std::u16string(1, typed)};
    noteRun(text);
}

/** Drops the word being typed: nothing goes in, as when Delete takes its last key. */
void InputSession::cancelComposition(TextClient& client) {
    keys = u"";
    forced.reset();
    forcedByUser = false;
    pinned.reset();
    syllableStart.reset();
    addedNMarker = false;
    liveSuppressed = false;
    liveKept.reset();
    updatePreview(client);
}

void InputSession::commitForNewContext(TextClient* client) {
    commit(client);
    setPreviousLanguage(std::nullopt);
    resetRun();
}

std::vector<ConversionCandidate> InputSession::typingCandidates() {
    if (conversion || !converter || keys.empty() ||
        !(forced == Language::JAPANESE || (!forced && previewLanguage == Language::JAPANESE))) return {};
    auto kana = japaneseReading(false);
    return kana.empty() ? std::vector<ConversionCandidate>{} : conversionCandidates(kana);
}

void InputSession::choose(const ConversionCandidate& candidate, TextClient& client) {
    startConversion(client);
    if (!conversion) return;
    auto& candidates = conversion->candidates;
    auto it = std::find(candidates.begin(), candidates.end(), candidate);
    if (it == candidates.end()) return;
    choose(int(it - candidates.begin()), client);
}

void InputSession::choose(int index, TextClient& client) {
    if (!conversion) return;
    if (index < 0 || index >= int(conversion->candidates.size())) return;
    conversion->selected = index;
    commitCandidate(client);
}

// URLs and paths

bool InputSession::hasURLMarker(const std::u16string& run) {
    return run.find(u"://") != std::u16string::npos || run.find(u"localhost:") != std::u16string::npos
        || startsWith(run, u"www.") || startsWith(run, u"~/") || startsWith(run, u"./") || startsWith(run, u"../")
        || (startsWith(run, u"/") && contains(drop(run, 1), u'/'))
        || contains(drop(run, 1), u'@')
        || run == u"." || run == u"!."; // REA: a dotfile (.env, !.env in .gitignore): one dot starts the run
}

/** The run of text before the word being typed: what went in since the last space, with
 *  what the field had before the caret when that isn't known (a pasted URL, a click). */
std::u16string InputSession::run(TextClient& client) {
    if (runKnown) return runText;
    runKnown = true;
    auto marked = client.markedRange(), cursor = client.selectedRange();
    // Up to the word being typed, if there is one on screen.
    int end = marked.location != TextRange::NOT_FOUND ? marked.location : cursor.length == 0 ? cursor.location : TextRange::NOT_FOUND;
    if (end == TextRange::NOT_FOUND) return runText;
    int start = std::max(0, end - RUN_LOOK_BEHIND);
    auto before = client.text(TextRange{start, end - start});
    if (!before) return runText;
    size_t from = before->size();
    while (from > 0 && !isWhitespace((*before)[from - 1])) from--;
    auto field = before->substr(from);
    if (endsWith(field, runText)) runText = field;
    return runText;
}

/** Keeps the run up to date with text going in; Space, Tab and Return end it. A URL or path
 *  counts as English for the next word. */
void InputSession::noteRun(const std::u16string& text) {
    for (char16_t ch : text) {
        if (isWhitespace(ch)) {
            if (hasURLMarker(runText)) setPreviousLanguage(Language::ENGLISH);
            runText.clear();
            runKnown = true;
        } else {
            runText += ch;
        }
    }
}

/** The app deleted the character before the caret, nothing being typed. */
void InputSession::noteDeleted() {
    if (runText.empty()) runKnown = false; else runText.pop_back();
}

/** The caret moved or the field changed: what is before the caret is read again when needed. */
void InputSession::resetRun() {
    runText.clear();
    runKnown = false;
}

// Japanese conversion

/** A symbol typed as its own Japanese word, shown full-width (or as the 「」・ a Japanese IME types).
 *  With [range], it takes the place of what the app typed there, in one step: an empty
 *  insertText to delete it first is dropped on the way to the app (1 became 1１). */
void InputSession::startSymbol(const std::u16string& symbol, TextClient& client, std::optional<TextRange> range) {
    lastRecorded.reset();
    liveSuppressed = false;
    liveKept.reset();
    keys = symbol;
    forced = Language::JAPANESE;
    forcedByUser = false;
    if (!range) return updatePreview(client);
    previewLanguage = Language::JAPANESE;
    client.setMarkedText(japaneseDisplay(), *range);
}

/** Keeps [passedRun] up to date with a key the app is about to type. */
void InputSession::notePassed(char16_t ch, TextClient& client) {
    auto cursor = client.selectedRange();
    if (!(japaneseEnabled && policy.fullWidth && isAscii(ch) && (isNumber(ch) || RomajiComposer::symbols().count(ch)) &&
          cursor.location != TextRange::NOT_FOUND && cursor.length == 0)) {
        passedRun.reset();
        return;
    }
    if (passedRun && passedRun->second + int(passedRun->first.size()) == cursor.location) {
        passedRun->first += ch;
    } else {
        passedRun = {std::u16string(1, ch), cursor.location};
    }
}

/** The range of [passedRun] when it is still in the text, unchanged, ending at [end]. */
std::optional<TextRange> InputSession::passedRunRange(int end, TextClient& client) {
    if (!passedRun) return std::nullopt;
    TextRange range{passedRun->second, int(passedRun->first.size())};
    if (range.end() == end && client.text(range) == passedRun->first) return range;
    return std::nullopt;
}

/** The digits and symbols just typed, and where they are, when the caret is right after them. */
std::optional<std::pair<std::u16string, TextRange>> InputSession::passedRunBeforeCaret(TextClient& client) {
    auto cursor = client.selectedRange();
    if (!passedRun) return std::nullopt;
    if (!japaneseEnabled || conversion || cursor.location == TextRange::NOT_FOUND || cursor.length != 0) return std::nullopt;
    auto range = passedRunRange(cursor.location, client);
    if (!range) return std::nullopt;
    return std::pair(passedRun->first, *range);
}

bool InputSession::closesFullWidth(char16_t ch) const {
    auto opener = closing().find(ch);
    if (opener == closing().end()) return false;
    return !openBrackets.empty() && openBrackets.back() == opener->second;
}

/** Keeps [openBrackets] up to date with text going in. */
void InputSession::noteBrackets(const std::u16string& text) {
    for (char16_t ch : text) {
        bool isOpener = std::any_of(pairs().begin(), pairs().end(), [&](auto& pair) { return pair.second == ch; });
        if (isOpener) {
            openBrackets.push_back(ch);
        } else if (auto opener = pairs().find(ch); opener != pairs().end() && !openBrackets.empty() && openBrackets.back() == opener->second) {
            openBrackets.pop_back();
        }
    }
}

/** Whether a symbol typed after the current word continues a Japanese phrase.
 *  Sentence punctuation (、。？！) never changes the word's language: it joins only a
 *  word already read as Japanese ("hello." and "dho?" = 왜? stay what they were).
 *  The in-word marks ー and ' may also turn an English-looking start into Japanese
 *  ("ra" → "ra-" → らー), but never take a word away from Korean. */
bool InputSession::belongsToJapanesePhrase(char16_t ch) {
    auto pendingIsEmptyOrN = [&] {
        auto pending = lowercase(RomajiComposer::compose(keys, true).pending);
        return pending.empty() || pending == u"n";
    };
    if (isAsciiDigit(ch)) {
        if (!digitsFullWidth()) return false;
        return forced == Language::JAPANESE || (!forced && previewLanguage == Language::JAPANESE && pendingIsEmptyOrN());
    }
    if (!JapaneseModel::inPhraseSymbols().count(ch)) return false;
    if (forced == Language::JAPANESE) return true;
    // On the way to a Japanese text replacement shortcut with a symbol in it: gu- is ぐー,
    // even though gu alone read as 혀.
    if (japaneseEnabled && !forced) {
        auto kana = RomajiComposer::compose(keys + ch, true);
        if (kana.isClean() && std::any_of(replacements.begin(), replacements.end(), [&](auto& r) { return startsWith(r.first, kana.text); })) return true;
    }
    // A letter still waiting for its vowel says the word may not be Japanese after all
    // (baz) is baz), not ばz）); a final n is ん (nihon. is にほん。).
    if (previewLanguage == Language::JAPANESE) {
        return ch == u'-' || ch == u'\'' || pendingIsEmptyOrN();
    }
    // An English word before the mark keeps it English (to-do, co-op); ra-men isn't one.
    if (!(!forced && (ch == u'-' || ch == u'\'') && previewLanguage == Language::ENGLISH &&
          !EnglishModel::shared().isKnownWord(keys) && RomajiComposer::compose(keys, true).strayLetters == 0)) return false;
    DecisionContext context{previousLanguage, true};
    return engine->decide(keys + ch, context).language == Language::JAPANESE;
}

/** The Korean or Japanese reading of the keys that is a text replacement shortcut (주소, ㅈㅅ,
 *  よろ), unless the user taught the engine otherwise for these keys. English shortcuts are left
 *  to the engine: "go" may be one, and still be ご in a Japanese sentence. */
std::optional<std::pair<Language, std::u16string>> InputSession::shortcutReading(bool partial) {
    if (replacements.empty() || keys.empty() || engine->learned().count(keys)) return std::nullopt;
    if (auto h = hangul(); h && replacements.count(*h)) return std::pair(Language::KOREAN, *h);
    if (japaneseEnabled) {
        auto kana = RomajiComposer::compose(keys, partial);
        if (kana.isClean() && replacements.count(kana.text)) return std::pair(Language::JAPANESE, kana.text);
    }
    return std::nullopt;
}

/** Kana for the keys, with any English words inside the phrase kept as typed. */
std::u16string InputSession::japaneseReading(bool partial) {
    // An English word with a particle (redde → redで) even when every key also makes kana,
    // as the engine split it; a reading the user picked by hand stays kana (れっで).
    if (!(forced == Language::JAPANESE && forcedByUser) && segmenter) {
        if (auto split = segmenter->englishWithParticle(keys)) return split->first + split->second;
    }
    if (!partial) {
        // Letters still waiting for their vowel when the phrase ends go in as shown
        // (watashihag → わたしはg, not わたしhag); only n becomes ん.
        auto typed = RomajiComposer::compose(keys, true);
        if (typed.strayLetters == 0 && !typed.pending.empty()) return RomajiComposer::compose(keys).text;
        if (segmenter) if (auto segments = segmenter->segment(keys)) return JapaneseSegmenter::text(*segments);
    }
    // While typing, a consonant at the end is just waiting for its vowel (watashihag → わたしはg),
    // not a stray letter to split an English word ("hag") out of.
    if (partial && RomajiComposer::compose(keys, true).strayLetters > 0 && segmenter) {
        if (auto segments = segmenter->segment(keys)) {
            // While typing, the last segment may still be growing; keep its pending letters visible.
            auto last = segments->back();
            segments->pop_back();
            auto japanese = std::get_if<JapaneseSegmenter::Japanese>(&last);
            return JapaneseSegmenter::text(*segments) +
                (japanese ? RomajiComposer::compose(japanese->keys, true).text : JapaneseSegmenter::text({last}));
        }
    }
    return RomajiComposer::compose(keys, partial).text;
}

// Live conversion

bool InputSession::isLive() const { return liveConversion && !liveSuppressed && converter; }

/** The top candidate covering all of [kana] (cached: the preview asks on every key). */
std::optional<ConversionCandidate> InputSession::liveCandidate(const std::u16string& kana) {
    if (liveCache && liveCache->first == kana) return liveCache->second;
    std::optional<ConversionCandidate> candidate;
    for (auto& c : conversionCandidates(kana)) {
        if (c.kanaCount == int(kana.size()) && !c.isPrediction) { candidate = c; break; }
    }
    // A short reading only turns into kanji (私, 今日), not into katakana mid-word
    // (と → ト, こんに → コンに): those flicker while the phrase is still being typed.
    if (kana.size() <= 3 && candidate && !any(candidate->text, [](char16_t c) { return c >= 0x4E00 && c <= 0x9FFF; })) candidate.reset();
    liveCache = {kana, candidate};
    return candidate;
}

/** The Japanese phrase as shown while typing: kana, or with live conversion the top
 *  candidate followed by letters still waiting for their vowel (今日はいい天気でs). */
std::u16string InputSession::japaneseDisplay() {
    auto reading = japaneseReading(true);
    auto live = isLive() ? liveReading() : std::nullopt;
    if (!live) return reading;
    auto& [kana, pending] = *live;
    if (auto kept = keptOnScreen(kana)) return *kept + pending;
    if (kana.empty()) return reading;
    auto candidate = liveCandidate(kana);
    if (!candidate) return reading;
    return candidate->text + pending;
}

/** The phrase's reading without the letters still waiting for their vowel, and those letters. */
std::optional<std::pair<std::u16string, std::u16string>> InputSession::liveReading() {
    auto reading = japaneseReading(true);
    auto pending = RomajiComposer::compose(keys, true).pending;
    if (!endsWith(reading, pending)) return std::nullopt;
    return std::pair(dropLast(reading, pending.size()), pending);
}

/** What Delete left of a live-converted [kana] on screen ([liveKept]), if that is still it. */
std::optional<std::u16string> InputSession::keptOnScreen(const std::u16string& kana) {
    if (!liveKept || liveKept->reading != kana) return std::nullopt;
    return liveKept->prefix + drop(kana, liveKept->prefixLength);
}

/** Delete with live conversion, as macOS's: the last clause on screen goes back to kana, less
 *  its last kana (今日はいい天気 → 今日はいいてん, 漢字k → かんじ), the clauses before it stay
 *  as they were shown, and further Deletes take kana off the end. [shown] (text, kana) is a
 *  conversion on screen instead of live conversion's. */
void InputSession::deleteLive(std::optional<std::pair<std::u16string, std::u16string>> shown) {
    auto typed = liveReading();
    if (!typed) return deleteKana();
    auto kana = shown ? shown->second : typed->first;
    std::u16string prefix;
    int length = 0;
    if (shown) {
        prefix = shown->first; length = int(kana.size());
    } else if (liveKept && liveKept->reading == kana) {
        prefix = liveKept->prefix; length = liveKept->prefixLength;
    } else if (auto candidate = kana.empty() ? std::nullopt : liveCandidate(kana)) {
        prefix = candidate->text; length = int(kana.size());
    }
    deleteKana();
    auto now = liveReading();
    auto after = now ? now->first : std::u16string();
    // Nothing shown as kana after the conversion: this Delete reaches its last clause.
    if (length >= int(kana.size()) || int(after.size()) < length) {
        do {
            std::tie(prefix, length) = dropLastClause(prefix, kana.substr(0, length));
        } while (int(after.size()) < length);
    }
    if (after.empty()) liveKept.reset(); else liveKept = LiveKept{prefix, length, after};
}

std::pair<std::u16string, int> InputSession::dropLastClause(const std::u16string& text, const std::u16string& reading) {
    // Kana and letters show as typed; anything else converted some of the reading.
    auto literal = [](char16_t c) { return c < 0x80 || (c >= 0x3041 && c <= 0x309F); };
    int start = int(text.size()) - 1;
    while (start >= 0 && literal(text[start])) start--;
    if (start < 0) return {u"", 0};
    while (start > 0 && !literal(text[start - 1])) start--;
    const std::u16string prefix = text.substr(0, start);
    const int p_end = int(prefix.size()), k_end = int(reading.size());
    // How much of the reading the prefix covers, its conversions as short as they can be
    // (今日 is きょう in きょうははこ, so 箱 is はこ); the last clause keeps at least one kana.
    auto covered = [&](auto& self, int p, int r) -> std::optional<int> {
        if (p >= p_end) return r < k_end ? std::optional<int>(r) : std::nullopt;
        if (literal(prefix[p])) return r < k_end && reading[r] == prefix[p] ? self(self, p + 1, r + 1) : std::nullopt;
        int end = p;
        while (end < p_end && !literal(prefix[end])) end++;
        for (int r2 = r + 1; r2 <= k_end; r2++) if (auto found = self(self, end, r2)) return found;
        return std::nullopt;
    };
    if (prefix.empty()) return {u"", 0};
    auto length = covered(covered, 0, 0);
    if (!length) return {u"", 0};
    return {prefix, *length};
}

/** What a Japanese phrase commits as without the candidate window: the kana, or with
 *  live conversion the candidate that was on screen (which the converter learns). */
std::u16string InputSession::committedJapanese() {
    auto reading = japaneseReading(false);
    // Letters still waiting for their vowel follow the candidate, as japaneseDisplay shows
    // them (今日はいい天気でs, not a candidate for the whole reading with the s in it).
    auto pending = RomajiComposer::compose(keys, true).pending;
    auto kana = endsWith(reading, pending) ? dropLast(reading, pending.size()) : reading;
    auto letters = drop(reading, kana.size());
    // What Delete left on screen goes in as shown; nothing was chosen to learn.
    if (isLive()) if (auto kept = keptOnScreen(kana)) return *kept + letters;
    if (!isLive() || !converter || kana.empty()) return reading;
    auto candidate = liveCandidate(kana);
    if (!candidate) return reading;
    converter->learn(*candidate, kana);
    return candidate->text + letters;
}

void InputSession::startConversion(TextClient& client) {
    auto kana = japaneseReading(false);
    if (!converter || kana.empty()) return;
    // After a Delete live conversion shows the end as kana (へんかn), not a candidate: Space
    // shows the first one (変換), as macOS does.
    auto live0 = isLive() ? liveReading() : std::nullopt;
    bool kept = live0 && keptOnScreen(live0->first);
    // Nor is one on screen with a letter waiting for its vowel: 変化n was へんか's, so Space on
    // henkan shows へんかん's first, 変換, as macOS does, not the one after it.
    bool pendingShown = live0 && !live0->second.empty();
    bool userForced = forced == Language::JAPANESE && forcedByUser;
    wordEnds(Language::JAPANESE, committedLanguage());
    // A number the app typed just before the phrase is part of it: 3ji is 3時, not 3 and 時
    // picked out of 地, 字, 自…
    auto marked = client.markedRange();
    std::optional<TextRange> prefixRange;
    auto run = passedRun;
    if (marked.location != TextRange::NOT_FOUND && run && !run->first.empty() && isNumber(run->first.back()) &&
        all(run->first, isNumberCharacter)) {
        if (auto range = passedRunRange(marked.location, client)) {
            prefixRange = TextRange{range->location, range->length + marked.length};
            kana = run->first + kana;
        }
    }
    std::u16string numberPrefix = prefixRange ? run->first : u"";
    passedRun.reset();
    auto candidates = conversionCandidates(kana);
    if (!numberPrefix.empty()) {
        // The number as it was typed (3時) before other widths and spellings (３時, 三時).
        std::stable_partition(candidates.begin(), candidates.end(), [&](auto& c) { return startsWith(c.text, numberPrefix); });
    }
    // Live conversion showed the phrase without the number, so no candidate is on screen yet.
    auto live = isLive() && numberPrefix.empty() && !kept && !pendingShown ? liveCandidate(kana) : std::nullopt;
    auto phrases = replacements.find(kana);
    if (phrases != replacements.end() && !phrases->second.empty()) {
        // Text replacements come first, or right after the live candidate already on screen.
        size_t at = 0;
        if (live) if (auto it = std::find(candidates.begin(), candidates.end(), *live); it != candidates.end()) at = it - candidates.begin() + 1;
        auto isPhrase = [&](const ConversionCandidate& c) {
            return std::find(phrases->second.begin(), phrases->second.end(), c.text) != phrases->second.end();
        };
        std::vector<ConversionCandidate> merged;
        for (size_t i = 0; i < at; i++) if (!isPhrase(candidates[i])) merged.push_back(candidates[i]);
        for (auto& phrase : phrases->second) merged.push_back({phrase, int(kana.size())});
        for (size_t i = at; i < candidates.size(); i++) if (!isPhrase(candidates[i])) merged.push_back(candidates[i]);
        candidates = std::move(merged);
    }
    // Every other homophone the engine has follows, for the candidate window (しょう → 硝).
    if (converter) {
        std::unordered_set<std::u16string> listed;
        for (auto& c : candidates) listed.insert(c.text);
        for (auto& c : converter->moreCandidates(kana)) if (!listed.count(c.text)) candidates.push_back(c);
    }
    if (candidates.empty()) {
        commit(&client);
        return;
    }
    // With live conversion the top candidate is already on screen: Space shows the next one.
    int selected = 0;
    int i = -1;
    if (live) if (auto it = std::find(candidates.begin(), candidates.end(), *live); it != candidates.end()) i = int(it - candidates.begin());
    if (i >= 0 && candidates.size() > 1) {
        selected = (i + 1) % int(candidates.size());
    } else if (liveConversion && numberPrefix.empty() && candidates.size() > 1 && candidates[0].text == kana) {
        // The kana is on screen (a short reading kept as kana, or brought back with Esc) and the
        // engine put it first (しょう): Space shows a conversion, 翔, as macOS does, not the kana again.
        selected = 1;
    }
    liveSuppressed = false;
    liveKept.reset();
    conversion = Conversion{kana, keys, candidates, selected, userForced, numberPrefix};
    keys = u"";
    forced.reset();
    forcedByUser = false;
    pinned.reset();
    syllableStart.reset();
    enginePreview.reset();
    setPreviousLanguage(Language::JAPANESE);
    if (prefixRange) {
        client.setMarkedText(conversion->display(), *prefixRange); // the number, then the phrase
    } else {
        client.setMarkedText(conversion->display());
    }
}

/** Shows the reading as katakana (or hiragana) as the selected "candidate": Return
 *  commits it, Space flips to the other form, typing on commits it and continues. */
void InputSession::showKanaForms(bool katakanaFirst, std::optional<std::u16string> kana, std::optional<std::u16string> romaji, TextClient& client) {
    auto reading = kana ? *kana : japaneseReading(false);
    std::vector<ConversionCandidate> forms = {{RomajiComposer::katakana(reading), int(reading.size())},
                                              {reading, int(reading.size())}};
    if (!katakanaFirst) std::reverse(forms.begin(), forms.end());
    conversion = Conversion{reading, romaji ? *romaji : keys, forms, 0};
    keys = u"";
    forced.reset();
    forcedByUser = false;
    pinned.reset();
    syllableStart.reset();
    enginePreview.reset();
    setPreviousLanguage(Language::JAPANESE);
    client.setMarkedText(conversion->display());
}

/** The conversion with its first clause [step] kana shorter or longer (今日は|いいてんき):
 *  candidates for exactly that much of the reading, the rest shown as kana. A boundary
 *  never falls inside an English word (google). Nullopt when it can't move further. */
std::optional<InputSession::Conversion> InputSession::resizeFirstClause(const Conversion& conversion, int step) {
    if (!converter) return std::nullopt;
    const auto& reading = conversion.kana;
    const int size = int(reading.size());
    int length = conversion.current().kanaCount + step;
    while (length >= 1 && length < size && isAsciiLetter(reading[length - 1]) && isAsciiLetter(reading[length])) {
        length += step;
    }
    if (length < 1 || length > size || length == conversion.current().kanaCount) return std::nullopt;
    auto clause = reading.substr(0, length);
    std::vector<ConversionCandidate> candidates;
    for (auto& c : conversionCandidates(clause)) if (c.kanaCount == int(clause.size())) candidates.push_back(c);
    if (candidates.empty()) return std::nullopt;
    return Conversion{conversion.kana, conversion.keys, candidates, 0, conversion.userForced, conversion.numberPrefix};
}

bool InputSession::handleConverting(Key key, TextClient& client) {
    if (!conversion) return false;
    const Conversion current = *conversion;
    const int count = int(current.candidates.size());
    if (key == Key::printable(u' ') || key.kind == Kind::Down || key.kind == Kind::ShiftSpace) {
        conversion->selected = (current.selected + 1) % count;
        client.setMarkedText(conversion->display());
        return true;
    }
    if (key.kind == Kind::Up) {
        conversion->selected = (current.selected + count - 1) % count;
        client.setMarkedText(conversion->display());
        return true;
    }
    if (key.kind == Kind::Left || key.kind == Kind::Right) {
        if (auto resized = resizeFirstClause(current, key.kind == Kind::Left ? -1 : 1)) {
            conversion = resized;
            client.setMarkedText(conversion->display());
        }
        return true;
    }
    if (key.kind == Kind::Printable && isNumber(key.ch)) {
        int n = numericValue(key.ch);
        if (n >= 1 && n <= std::min(9, count)) {
            choose(n - 1, client);
            return true;
        }
        commit(&client);
        return handle(key, client);
    }
    if (key.kind == Kind::Enter) {
        commitCandidate(client);
        return true;
    }
    if (key.kind == Kind::Katakana || key.kind == Kind::Hiragana) {
        conversion.reset();
        showKanaForms(key.kind == Kind::Katakana, current.kana, current.keys, client);
        return true;
    }
    if (key.kind == Kind::Escape || key.kind == Kind::Cycle || key.kind == Kind::Delete) {
        conversion.reset();
        bool deletes = key.kind == Kind::Delete;
        // Delete with live conversion off: the conversion on screen goes in less its last
        // character (変換 → 変), as in macOS's Japanese input.
        if (deletes && !liveConversion) {
            auto text = dropLast(current.display(), 1);
            client.insertText(text, std::nullopt);
            noteRun(text);
            noteBrackets(text);
            setPreviousLanguage(Language::JAPANESE);
            return true;
        }
        // "Back to kana" means kana, also with live conversion; Delete goes on with it.
        liveSuppressed = !deletes;
        // A number taken in from before the phrase goes back as it was typed.
        if (!current.numberPrefix.empty()) {
            client.insertText(current.numberPrefix, std::nullopt);
            noteRun(current.numberPrefix);
        }
        auto kana = drop(current.kana, current.numberPrefix.size());
        // Back to editing the romaji — when it still spells the reading. After part of a
        // phrase with English in it was committed it may not; keep the kana rather than lose it.
        if (JapaneseSegmenter::reading(current.keys, segmenter.get()) != kana) {
            auto text = deletes ? dropLast(kana, 1) : kana;
            client.insertText(text, std::nullopt);
            noteRun(text);
            setPreviousLanguage(Language::JAPANESE);
            return true;
        }
        keys = current.keys;
        syllableStart.reset();
        if (deletes) {
            // With live conversion, as macOS's: typing the phrase again, its last clause back
            // to kana less the last one (今日はいい天気 → 今日はいいてん), the clauses before it as shown.
            forced = Language::JAPANESE;
            forcedByUser = current.userForced;
            deleteLive(std::pair(current.numberPrefix.empty() ? current.display() : u"", kana));
            if (keys.empty()) { forced.reset(); forcedByUser = false; }
            updatePreview(client);
            return true;
        }
        // A final "n" was shown as ん; keep it ("henkan" → "henkan'") instead of
        // letting it wait for a vowel again.
        if (endsWith(keys, u"n") && !endsWith(keys, u"nn")) { keys += u"'"; addedNMarker = true; }
        forced = Language::JAPANESE;
        forcedByUser = current.userForced;
        updatePreview(client);
        return true;
    }
    if (key.kind == Kind::Letter || key.kind == Kind::Printable) {
        commit(&client);
        return handle(key, client);
    }
    // Other
    commit(&client);
    return false;
}

/** Commits the selected candidate; if it covers only the start of the reading,
 *  keeps converting the rest. */
void InputSession::commitCandidate(TextClient& client) {
    if (!conversion || !converter) return;
    const Conversion current = *conversion;
    auto candidate = current.current();
    converter->learn(candidate, current.kana);
    client.insertText(candidate.text, std::nullopt);
    noteRun(candidate.text);
    noteBrackets(candidate.text);
    auto rest = drop(current.kana, candidate.kanaCount);
    conversion.reset();
    if (rest.empty()) return;
    auto restKeys = keysAfter(current.keys, std::max(0, candidate.kanaCount - int(current.numberPrefix.size())));
    auto candidates = conversionCandidates(rest);
    if (candidates.empty()) {
        client.insertText(rest, std::nullopt);
        noteRun(rest);
    } else {
        // The rest is the engine's Japanese, not something the user corrected.
        conversion = Conversion{rest, restKeys, candidates, 0};
        client.setMarkedText(conversion->display());
    }
}

/** The converter's candidates, with a number kept as typed: a reading that is only a number
 *  offers itself (１２３) and its half-width form (123) first; otherwise the candidates that keep
 *  the digits come before those that spell them out (３月, 3月, then 三月; 東京都１, not 東京都一). */
std::vector<ConversionCandidate> InputSession::conversionCandidates(const std::u16string& kana) {
    auto candidates = converter ? converter->candidates(kana) : std::vector<ConversionCandidate>{};
    auto kanaDigits = digits(kana);
    if (kanaDigits.empty()) return candidates;
    const int whole = int(kana.size());
    std::vector<ConversionCandidate> ordered;
    if (all(kana, isNumberCharacter)) {
        ordered.push_back({kana, whole});
        ordered.push_back({halfWidth(kana), whole});
    }
    for (auto& c : candidates) if (c.kanaCount == whole && digits(c.text) == kanaDigits) ordered.push_back(c);
    for (auto& c : candidates) if (c.kanaCount == whole && digits(c.text) != kanaDigits) ordered.push_back(c);
    for (auto& c : candidates) if (c.kanaCount != whole) ordered.push_back(c);
    std::unordered_set<std::u16string> shown;
    std::vector<ConversionCandidate> out;
    for (auto& c : ordered) if (shown.insert(c.text).second) out.push_back(c);
    return out;
}

// Private

Decision InputSession::decide(bool partial) {
    // A segment of a URL or path goes in as typed, before the dictionary is asked (so in a
    // URL is so even where it was taught to be 내).
    if (inURLRun) {
        keptEnglishByStrict = false;
        Decision decision{Language::ENGLISH, 1.0, keys, u"inside a URL or path"};
        // REA: a case particle glued to the last part of an address is still Korean
        // (netmaru@gmail.comdmfh is netmaru@gmail.com으로), as before the URL run.
        if (!runText.empty() && runText.back() == u'.') {
            auto mixed = MixedWord::split(keys);
            if (mixed && all(mixed->english, isLetter) && MixedWord::singleLetterParticles().count(mixed->korean)) {
                decision = Decision{Language::MIXED, 1.0, mixed->text(), u"address + particle"};
            }
        }
        lastTrace = Trace{keys, previousLanguage, decision, partial};
        return decision;
    }
    if (auto shortcut = shortcutReading(partial)) {
        // As sure as a split word, so a Strict app shows it too.
        keptEnglishByStrict = false;
        Decision decision{shortcut->first, SplittingEngine::CONFIDENCE, shortcut->second, u"text replacement shortcut"};
        lastTrace = Trace{keys, previousLanguage, decision, partial};
        return decision;
    }
    // A word typed straight after a number in Japanese text is a counter when it is romaji
    // (3ji, 5fun, 3hon); a unit like km or pm has letters that aren't (5km stays English).
    if (afterNumberInJapanese && all(keys, isLetter) && RomajiComposer::compose(keys, partial).strayLetters == 0) {
        keptEnglishByStrict = false;
        Decision decision{Language::JAPANESE, SplittingEngine::CONFIDENCE, RomajiComposer::compose(keys, partial).text,
                          u"counter after a number"};
        lastTrace = Trace{keys, previousLanguage, decision, partial};
        return decision;
    }
    // 3-set's 0 is ㅋ: after Korean, 0000 is ㅋㅋㅋㅋ (a number to the engine, which a switch key gives).
    if (isSebeolsik() && previousLanguage == Language::KOREAN && startsAfterHangul && Sebeolsik::isLaughter(keys)) {
        keptEnglishByStrict = false;
        auto composed = HangulComposer::compose(keys);
        Decision decision{Language::KOREAN, SplittingEngine::CONFIDENCE, composed ? composed->text() : keys, u"ㅋ on 3-set's 0 after Korean"};
        lastTrace = Trace{keys, previousLanguage, decision, partial};
        return decision;
    }
    auto d = engine->decide(keys, DecisionContext{previousLanguage, partial});
    // A word learned as Japanese while Japanese was on: with it off, the engine without what
    // was learned decides it (sushi stays sushi, not すし). Turned on again, it is learned again.
    if (d.language == Language::JAPANESE && !japaneseEnabled) {
        d = engine->base->decide(keys, DecisionContext{previousLanguage, partial});
    }
    // While typing, a consonant at the end may still be waiting for its vowel, which keeps
    // Japanese close and a Korean word under a Strict app's threshold until the word ends
    // (qud: 0.60 while typed, 0.98 at Space). The word as it would go in now decides, so the
    // preview shows what Space commits (병), not English that turns Korean on Space.
    if (partial && d.language == Language::KOREAN && d.confidence < policy.conversionThreshold) {
        auto now = engine->decide(keys, DecisionContext{previousLanguage, false});
        if (now.language == Language::KOREAN && now.confidence >= policy.conversionThreshold) d = now;
    }
    bool kept = d.language != Language::ENGLISH && d.confidence < policy.conversionThreshold;
    keptEnglishByStrict = kept && policy.conversionThreshold >= AppPolicy::STRICT_THRESHOLD && keys.size() <= 3;
    Decision decision = kept
        ? Decision{Language::ENGLISH, 1 - d.confidence, keys,
                   format("kept English (%.2f < %.2f); ", d.confidence, policy.conversionThreshold) + d.explanation}
        : d;
    lastTrace = Trace{keys, previousLanguage, decision, partial};
    return decision;
}

/** The language the current word will be committed in. */
Language InputSession::currentLanguage() { return forced ? *forced : pinned ? *pinned : committedLanguage(); }

/** What Space/Return commits without the switch key: the full-word decision, except that a hesitant
 *  change away from the reading on screen doesn't win (unless Strict kept it English). The
 *  same rule as the preview, so what you see is what you get. */
Language InputSession::committedLanguage() {
    auto d = decide(false);
    // REA: what is committed is what was shown: letters the short-prefix rule kept on screen
    // go in as letters (zi, zio), not as the kana the engine read.
    if (enginePreview == Language::JAPANESE && previewLanguage == Language::ENGLISH) return Language::ENGLISH;
    auto shown = enginePreview;
    if (!shown || *shown == d.language) return d.language;
    // Japanese on screen, kana or kanji: Space converts it, unless the keys are an English
    // word. A short word reads as the start of a longer Japanese one while typed and as
    // English once it ends (zi showed 地 and zio じお, then went in as "zi", "zio"), and a
    // Strict app's short token kept English would go in as letters under its kana (ka under か).
    if (*shown == Language::JAPANESE && d.language == Language::ENGLISH &&
        !EnglishModel::shared().isKnownWord(keys) && RomajiComposer::compose(keys).isClean()) {
        return *shown;
    }
    return isWeakFlipAllowed(d, *shown, false) ? d.language : *shown; // REA: runnerUp
}

/** Whether [decision] may replace the reading on screen: when it's confident, when it's
 *  a dictionary English word (which is what Space/Return will commit), or when a Strict app
 *  kept a short token English: there the typed letters win over a reading shown along the
 *  way (gs showed ㅎㄴ, but gst stays gst). Only a close second is held: Th showed てぇ, and
 *  The (Korean against English, kana far behind) went in as 手ェ. REA: on screen only
 *  ([runnerUp]); Return commits what was shown (ufos showed うふぉs). */
bool InputSession::isWeakFlipAllowed(const Decision& decision, Language shown, bool runnerUp) {
    return decision.confidence >= PREVIEW_FLIP_CONFIDENCE
        || (runnerUp && decision.runnerUp && *decision.runnerUp != shown)
        || (decision.language == Language::ENGLISH && (keptEnglishByStrict || EnglishModel::shared().isKnownWord(keys)))
        || !preview(shown);
}

void InputSession::updatePreview(TextClient& client) {
    auto forcedOrPinned = forced ? forced : pinned;
    if (keys.empty()) {
        previewLanguage.reset();
        enginePreview.reset();
        client.setMarkedText(u"");
    } else if (forcedOrPinned) {
        previewLanguage = forcedOrPinned;
        std::u16string text;
        if (*forcedOrPinned == Language::JAPANESE) text = japaneseDisplay();
        else if (*forcedOrPinned == Language::KOREAN) text = hangul().value_or(keys);
        else text = engine->render(keys, *forcedOrPinned);
        show(text, client);
    } else {
        if (!inURLRun) {
            splitOffKoreanBeforeOther(client);
            splitOffJapaneseBeforeKorean(client);
        }
        auto decision = decide(true);
        auto language = decision.language;
        // Hysteresis: a weak flip on one keystroke (わた → "wat" → わたし) isn't shown,
        // except into a dictionary English word, which is what Space/Return will commit.
        auto shown = previewLanguage;
        if (shown && *shown != language && keys.size() > 1 && !isWeakFlipAllowed(decision, *shown)) {
            language = *shown;
        }
        enginePreview = language;
        // The first letters of an English word often read as kana for a key or two (u is う
        // before up, ta is た before table, ch is ちゃ before chair). Outside Japanese text
        // they stay as typed until the kana are near-certain (ka, wa) or the word is longer:
        // letters are neutral, a flip that flips back looks wrong. REA: Space commits them as
        // shown too (committedLanguage).
        if (language == Language::JAPANESE && keys.size() <= SHORT_PREFIX_LENGTH &&
            shown.value_or(Language::ENGLISH) == Language::ENGLISH &&
            !(previousLanguage && sentenceLanguage(*previousLanguage) == Language::JAPANESE) &&
            decision.confidence < SHORT_PREFIX_FLIP_CONFIDENCE && !shortcutReading(true)) { // a shortcut shows (gu- is ぐー)
            language = Language::ENGLISH;
        }
        previewLanguage = language;
        show(language == Language::JAPANESE ? japaneseDisplay() : preview(language).value_or(keys), client);
    }
}

/** The word being composed as the marked text. */
void InputSession::show(const std::u16string& text, TextClient& client) { client.setMarkedText(text); }

/** Korean with another language typed straight after it reads as broken Hangul: 이력 then
 *  rireki (dlfurrireki, 이력갹다ㅑ), 오늘은 then google에서 (dhsmfdmsgoogledptj, 오늘은해ㅐ힏에서).
 *  When the keys only make Hangul with stray jamo, but a front of two or more ordinary
 *  syllables is followed by a known English word (or one with a particle) or by romaji the
 *  engine is sure is Japanese, the Korean goes in as typed and the rest carries on as its own
 *  word (live conversion and Space for candidates when it's Japanese: 이력履歴). */
void InputSession::splitOffKoreanBeforeOther(TextClient& client) {
    if (keys.size() < 7) return;
    auto whole = HangulComposer::compose(keys);
    if (!whole || !whole->isBroken() ||
        // Only what reads as Korean on its own: a Japanese phrase whose start happens to
        // spell Hangul (chanwo…, 초무をころした) stays whole. Judged without context, since
        // right after Japanese everything leans Japanese.
        engine->decide(keys, DecisionContext{std::nullopt, true}).language != Language::KOREAN) return;
    for (size_t cut = 4; cut <= keys.size() - 3; cut++) {
        auto front = keys.substr(0, cut);
        auto back = keys.substr(cut);
        auto korean = HangulComposer::compose(front);
        if (!korean) continue;
        auto syllables = korean->syllables();
        if (!korean->strayJamo().empty() || syllables.size() < 2 ||
            !all(syllables, [](char16_t s) { return koreanModel().syllableLogProbability(s) > KoreanModel::UNKNOWN_SYLLABLE_LOG_P; }) ||
            !isOtherLanguageWord(back)) continue;
        client.insertText(korean->text(), std::nullopt);
        noteRun(korean->text());
        keys = back;
        syllableStart.reset();
        setPreviousLanguage(Language::KOREAN);
        previewLanguage.reset();
        enginePreview.reset();
        return;
    }
}

/** A known English word, one with a Korean particle (google에서), or romaji the engine is
 *  sure is Japanese (no word starts with っ or ん: that cut takes a letter from the Korean). */
bool InputSession::isOtherLanguageWord(const std::u16string& keys) {
    auto decision = engine->decide(keys, DecisionContext{Language::KOREAN, true});
    switch (decision.language) {
        case Language::ENGLISH: return keys.size() >= 3 && EnglishModel::shared().isKnownWord(keys);
        case Language::MIXED: return true;
        case Language::JAPANESE: {
            if (!japaneseEnabled || decision.confidence < 0.9 || keys.size() < 4) return false;
            auto romaji = RomajiComposer::compose(keys, true);
            return romaji.strayLetters == 0 && !romaji.text.empty() && romaji.text[0] != u'っ' && romaji.text[0] != u'ん';
        }
        case Language::KOREAN: return false;
    }
    return false;
}

/** Japanese with Korean typed straight after it reads as broken Hangul: a particle
 *  (rirekifmf, 履歴를, not 갹다ㅑ를) or a word (kyouhaehtl…, 今日は도시락은). Once the Korean
 *  part is a particle or two ordinary syllables, the Japanese goes in (kanji with live
 *  conversion) and the rest carries on as Korean, so 를요, 에서 or 도시락은 can follow. */
void InputSession::splitOffJapaneseBeforeKorean(TextClient& client) {
    if (!japaneseEnabled || keys.size() < 5) return;
    auto whole = HangulComposer::compose(keys);
    if (!(whole ? whole->isBroken() : true) ||
        // Keys that still spell romaji are more Japanese (kyouhago is きょうはご, not きょうは해).
        RomajiComposer::compose(keys, true).strayLetters <= 0) return;
    // The longest Japanese front first: きょうは|도시, not きょう|하ㄷ….
    for (size_t cut = keys.size() - 1; cut >= 3; cut--) {
        auto front = keys.substr(0, cut);
        auto back = keys.substr(cut);
        if (!DubeolsikLayout::startsSyllable(back[0])) continue;
        auto korean = HangulComposer::compose(back);
        if (!korean || !korean->strayJamo().empty()) continue;
        auto syllables = korean->syllables();
        if (!(MixedWord::suffixes().count(korean->text()) ||
              (syllables.size() >= 2 &&
               all(syllables, [](char16_t s) { return koreanModel().syllableLogProbability(s) > KoreanModel::UNKNOWN_SYLLABLE_LOG_P; })))) continue;
        if (!RomajiComposer::compose(front).isClean()) continue;
        // Not an English word with a particle (linderman isn't linで그무).
        if (segmenter && segmenter->englishWithParticle(front)) continue;
        auto decision = engine->decide(front, DecisionContext{previousLanguage});
        if (decision.language != Language::JAPANESE || decision.confidence < 0.9) continue;
        keys = front;
        auto japanese = committedJapanese();
        client.insertText(japanese, std::nullopt);
        noteRun(japanese);
        keys = back;
        syllableStart.reset();
        setPreviousLanguage(Language::JAPANESE);
        forced = Language::KOREAN;
        forcedByUser = false;
        previewLanguage.reset();
        enginePreview.reset();
        return;
    }
}

/** The word so far rendered in [language], or nullopt when it has no such reading. */
std::optional<std::u16string> InputSession::preview(Language language) {
    switch (language) {
        case Language::ENGLISH: return keys;
        case Language::KOREAN: return hangul();
        case Language::MIXED: {
            auto mixed = engine->render(keys, Language::MIXED);
            if (mixed == keys) return std::nullopt;
            return mixed;
        }
        case Language::JAPANESE:
            // Only っ and letters (ss: っs) is no Japanese word, so it isn't a reading to hold on to.
            if (!japaneseEnabled || RomajiComposer::compose(keys, true).isOnlySokuon()) return std::nullopt;
            return japaneseReading(true);
    }
    return std::nullopt;
}

/** The word being typed ends as [language]: it goes in, or its conversion or kana forms start.
 *  A correction the user made to it is learned; otherwise it was typed again (a weak entry
 *  saying so stays). Inside a URL or path it isn't a word: neither. */
void InputSession::wordEnds(Language language, Language engineChoice) {
    if (inURLRun) return;
    if (forced && forcedByUser) record(keys, language, engineChoice);
    else engine->used(keys, language);
}

/** Decides the current word, learns a switch-key override, and resets the buffer. */
InputSession::Word InputSession::finishWord() {
    Language engineChoice = committedLanguage();
    Language language = forced ? *forced : pinned ? *pinned : engineChoice;
    if (language != Language::JAPANESE) dropNMarker();
    // "Back to kana" without the user's switch, or a reading kept while deleting, isn't a
    // correction: report it as the engine's.
    Language decided = ((forced && !forcedByUser) || (!forced && pinned)) ? language : engineChoice;
    wordEnds(language, decided);
    std::u16string text;
    if (language == Language::JAPANESE) text = committedJapanese();
    else if (language == Language::KOREAN) { auto h = hangul(); text = h ? *h : engine->render(keys, language); }
    else text = engine->render(keys, language);
    liveSuppressed = false;
    liveKept.reset();
    noteBrackets(text);
    Word word{keys, text, language, decided};
    setPreviousLanguage(sentenceLanguage(language));
    keys = u"";
    forced.reset();
    forcedByUser = false;
    pinned.reset();
    syllableStart.reset();
    addedNMarker = false;
    previewLanguage.reset();
    enginePreview.reset();
    return word;
}

/** Takes the last kana off a Japanese phrase, as a Japanese IME does (かんこく → かんこ, not
 *  かんこk): the keys become a spelling of the shorter reading, keeping as much of what was
 *  typed as composes to it. A letter still waiting for its vowel goes alone (かんこk → かんこ),
 *  and so does anything in a phrase with English in it. */
void InputSession::deleteKana() {
    auto composed = RomajiComposer::compose(keys, true);
    if (!composed.pending.empty() || composed.strayLetters != 0 || composed.text.empty()) {
        keys = dropLast(keys, 1);
        return;
    }
    keys = keysSpelling(dropLast(composed.text, 1), keys).value_or(dropLast(keys, 1));
}

/** Korean as the system's 2-set input deletes it: jamo by jamo in the syllable being typed
 *  (한국 → 한구 → 한ㄱ → 한), then whole syllables, which it had already committed (한 → nothing). */
void InputSession::deleteJamoOrSyllable() {
    if (!syllableStart) {
        auto composed = HangulComposer::compose(keys);
        if (!composed) { keys = dropLast(keys, 1); return; }
        auto units = composed->units;
        units.pop_back();
        syllableStart = cut(keys, units);
    }
    if (*syllableStart < int(keys.size())) {
        keys = dropLast(keys, 1);
        return;
    }
    // Nothing left being typed: a whole committed syllable goes.
    auto composed = HangulComposer::compose(keys);
    if (!composed) { keys = dropLast(keys, 1); return; }
    auto units = composed->units;
    if (!units.empty()) units.pop_back();
    int at = cut(keys, units);
    keys = keys.substr(0, at);
    syllableStart = at;
}

/** The word as Hangul, with the syllable being typed kept apart from those before it
 *  after a Delete ([syllableStart]). */
std::optional<std::u16string> InputSession::hangul() {
    if (!syllableStart || *syllableStart >= int(keys.size())) {
        auto composed = HangulComposer::compose(keys);
        if (!composed) return std::nullopt;
        return composed->text();
    }
    auto front = HangulComposer::compose(keys.substr(0, *syllableStart));
    if (!front) return std::nullopt;
    auto back = HangulComposer::compose(keys.substr(*syllableStart));
    if (!back) return std::nullopt;
    return front->text() + back->text();
}

void InputSession::dropNMarker() {
    if (addedNMarker && endsWith(keys, u"'")) keys = dropLast(keys, 1);
    addedNMarker = false;
}

void InputSession::record(const std::u16string& keys, Language language, Language engineChoice) {
    if (lastRecorded && lastRecorded->first == keys && lastRecorded->second == language) return;
    lastRecorded = {keys, language};
    engine->learn(keys, language, engineChoice);
}

/** Whether the keys have a split reading to cycle to: the engine's (x에, hello월드), which the
 *  particle split alone doesn't find. */
bool InputSession::engineSplits(const std::u16string& keys) { return engine->render(keys, Language::MIXED) != keys; }

/** Replaces a just-committed word with its next rendering, but only if the caret
 *  still sits right after it and the text there is what we inserted. */
std::optional<InputSession::Committed> InputSession::cycle(const Committed& commit, TextClient& client) {
    TextRange range{commit.location, int(commit.text.size())};
    auto cursor = client.selectedRange();
    if (cursor.location == TextRange::NOT_FOUND || cursor.length != 0 || cursor.location != range.end() ||
        client.text(range) != commit.text) return std::nullopt;

    Language language = nextLanguage(commit.language, commit.keys, japaneseEnabled, engineSplits(commit.keys));
    auto reading = language == Language::JAPANESE ? JapaneseSegmenter::reading(commit.keys, segmenter.get())
                                                  : engine->render(commit.keys, language);
    auto text = reading + commit.boundaryIn(language);
    client.insertText(text, range);
    if (endsWith(runText, commit.text)) runText = dropLast(runText, commit.text.size()) + text;
    setPreviousLanguage(sentenceLanguage(language));
    Committed cycled = commit;
    cycled.language = language;
    cycled.text = text;
    return cycled;
}

void InputSession::closeFlipWindow() {
    if (lastCommit) finalize(*lastCommit);
    lastCommit.reset();
}

/** The undo window closed: learn where the word ended up (once, not every reading the switch
 *  passed through on the way). */
void InputSession::finalize(const Committed& commit) {
    if (commit.learns && commit.language != commit.committedAs) record(commit.keys, commit.language, commit.decided);
}

const std::map<char16_t, char16_t>& InputSession::closing() {
    static const std::map<char16_t, char16_t> map = {{u')', u'（'}, {u']', u'「'}, {u'}', u'｛'}};
    return map;
}

const std::map<char16_t, char16_t>& InputSession::pairs() {
    static const std::map<char16_t, char16_t> map = {{u'）', u'（'}, {u'」', u'「'}, {u'｝', u'｛'}, {u'』', u'『'}, {u'】', u'【'}};
    return map;
}

const KoreanModel& InputSession::koreanModel() {
    static const KoreanModel model;
    return model;
}

bool InputSession::isNumberCharacter(char16_t c) { return contains(u"0123456789０１２３４５６７８９－．，／：-.,/:", c); }

std::u16string InputSession::digits(const std::u16string& text) { return filter(halfWidth(text), isAsciiDigit); }

std::u16string InputSession::halfWidth(const std::u16string& text) {
    std::u16string out = text;
    for (auto& c : out) if (c >= 0xFF01 && c <= 0xFF5E) c -= 0xFEE0;
    return out;
}

std::u16string InputSession::keysAfter(const std::u16string& keys, int kanaCount) {
    auto rest = drop(RomajiComposer::compose(keys).text, kanaCount);
    for (int i = int(keys.size()); i >= 0; i--) {
        if (RomajiComposer::compose(keys.substr(i)).text == rest) return keys.substr(i);
    }
    return u"";
}

std::optional<std::u16string> InputSession::keysSpelling(const std::u16string& spelling, const std::u16string& from) {
    for (int cut = int(from.size()) - 1; cut >= 0; cut--) {
        auto front = from.substr(0, cut);
        auto composed = RomajiComposer::compose(front, true);
        if (!composed.pending.empty() || composed.strayLetters != 0 || !startsWith(spelling, composed.text)) continue;
        auto rest = RomajiComposer::romaji(drop(spelling, composed.text.size()));
        if (!rest) continue;
        auto respelled = RomajiComposer::compose(front + *rest, true);
        if (respelled.pending.empty() && respelled.text == spelling) return front + *rest;
    }
    return std::nullopt;
}

int InputSession::cut(const std::u16string& keys, const std::vector<HangulComposer::Unit>& earlier) {
    for (int i = int(keys.size()) - 1; i >= 0; i--) {
        auto composed = HangulComposer::compose(keys.substr(0, i));
        if (composed && composed->units == earlier) return i;
    }
    return int(keys.size()) - 1;
}

} // namespace rea
