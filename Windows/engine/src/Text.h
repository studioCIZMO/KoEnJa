#pragma once

// What Kotlin's String and Char give the engine for free. Text is UTF-16 (std::u16string),
// like Kotlin strings and TSF: indices, lengths and ranges are code units, so they mean
// exactly what they mean in the Kotlin (and NSString) engine.

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace rea {

inline bool isAscii(char16_t c) { return c < 128; }
inline bool isAsciiDigit(char16_t c) { return c >= u'0' && c <= u'9'; }
inline bool isAsciiLetter(char16_t c) { return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z'); }

/** Char.isUpperCase / isLowerCase.
 *  ponytail: ASCII, Latin-1 and full-width A–Z only (not Greek/Cyrillic/Latin Extended: Ω stays Ω
 *  where Kotlin lowercases it); keys come from a QWERTY layout. */
inline bool isUpperCase(char16_t c) { return (c >= u'A' && c <= u'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7) || (c >= 0xFF21 && c <= 0xFF3A); }
inline bool isLowerCase(char16_t c) { return (c >= u'a' && c <= u'z') || (c >= 0xDF && c <= 0xFF && c != 0xF7) || (c >= 0xFF41 && c <= 0xFF5A); }

/** Char.isLetter.
 *  ponytail: ASCII, Latin, Greek/Cyrillic, kana, Hangul and CJK ranges, not every Unicode letter;
 *  the engine only asks about typed keys. */
inline bool isLetter(char16_t c) {
    if (c < 128) return isAsciiLetter(c);
    return c == 0xAA || c == 0xB5 || c == 0xBA || (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7)
        || (c >= 0x370 && c <= 0x52F && c != 0x37E && c != 0x387) || (c >= 0x1100 && c <= 0x11FF)
        || c == 0x3005 || c == 0x3006 || (c >= 0x3041 && c <= 0x3096) || (c >= 0x309D && c <= 0x309F)
        || (c >= 0x30A1 && c <= 0x30FA) || (c >= 0x30FC && c <= 0x30FF) || (c >= 0x3131 && c <= 0x318E)
        || (c >= 0x3400 && c <= 0x4DBF) || (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7A3)
        || (c >= 0xFF21 && c <= 0xFF3A) || (c >= 0xFF41 && c <= 0xFF5A) || (c >= 0xFF66 && c <= 0xFF9F);
}

/** Swift's Character.isNumber: digits, and other numerals (½, ①).
 *  ponytail: the common ranges, not all of Unicode's Nd/Nl/No. */
inline bool isNumber(char16_t c) {
    return isAsciiDigit(c) || c == 0xB2 || c == 0xB3 || c == 0xB9 || (c >= 0xBC && c <= 0xBE)
        || (c >= 0x2070 && c <= 0x2089 && c != 0x2071 && c != 0x2072 && c != 0x2073 && !(c >= 0x207A && c <= 0x207F))
        || (c >= 0x2150 && c <= 0x2189) || (c >= 0x2460 && c <= 0x249B) || (c >= 0x24EA && c <= 0x24FF)
        || c == 0x3007 || (c >= 0x3021 && c <= 0x3029) || (c >= 0xFF10 && c <= 0xFF19);
}

/** Character.getNumericValue for what a candidate number key can be: 0–9 of either width. */
inline int numericValue(char16_t c) {
    if (isAsciiDigit(c)) return c - u'0';
    if (c >= 0xFF10 && c <= 0xFF19) return c - 0xFF10;
    return -1;
}

/** String.lowercase(). ponytail: the letters isUpperCase knows, as it. */
inline std::u16string lowercase(std::u16string s) {
    for (auto& c : s) if (isUpperCase(c)) c += 0x20;
    return s;
}

inline std::u16string dropLast(const std::u16string& s, size_t n) { return s.substr(0, s.size() - std::min(n, s.size())); }
inline std::u16string drop(const std::u16string& s, size_t n) { return s.substr(std::min(n, s.size())); }
inline bool startsWith(const std::u16string& s, const std::u16string& prefix) { return s.compare(0, prefix.size(), prefix) == 0; }
inline bool endsWith(const std::u16string& s, const std::u16string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
inline bool contains(const std::u16string& s, char16_t c) { return s.find(c) != std::u16string::npos; }
template <class P> bool all(const std::u16string& s, P p) { return std::all_of(s.begin(), s.end(), p); }
template <class P> bool any(const std::u16string& s, P p) { return std::any_of(s.begin(), s.end(), p); }
template <class P> std::u16string filter(const std::u16string& s, P p) {
    std::u16string out;
    for (char16_t c : s) if (p(c)) out += c;
    return out;
}

/** String.toIntOrNull: an optional sign and digits, nothing else. */
inline std::optional<int> toInt(const std::u16string& s) {
    size_t i = (!s.empty() && (s[0] == u'-' || s[0] == u'+')) ? 1 : 0;
    if (i == s.size()) return std::nullopt;
    long long n = 0;
    for (size_t k = i; k < s.size(); k++) {
        if (!isAsciiDigit(s[k])) return std::nullopt;
        n = n * 10 + (s[k] - u'0');
        if (n > 2147483648LL) return std::nullopt;
    }
    if (s[0] == u'-') n = -n;
    if (n > 2147483647LL) return std::nullopt;
    return int(n);
}

/** String.toFloatOrNull for counts (ASCII numbers). */
inline std::optional<float> toFloat(const std::u16string& s) {
    std::string ascii;
    for (char16_t c : s) { if (c >= 128) return std::nullopt; ascii += char(c); }
    if (ascii.empty()) return std::nullopt;
    char* end = nullptr;
    float f = std::strtof(ascii.c_str(), &end);
    if (end != ascii.c_str() + ascii.size()) return std::nullopt;
    return f;
}

/** Whitespace-separated words of a word-list literal. */
inline std::vector<std::u16string> words(const std::u16string& text) {
    std::vector<std::u16string> out;
    std::u16string word;
    for (char16_t c : text) {
        if (c == u' ' || c == u'\n') { if (!word.empty()) out.push_back(word); word.clear(); }
        else word += c;
    }
    if (!word.empty()) out.push_back(word);
    return out;
}

inline std::u16string utf16(const std::string& utf8) {
    std::u16string out;
    out.reserve(utf8.size());
    for (size_t i = 0; i < utf8.size();) {
        unsigned char b = utf8[i];
        char32_t cp;
        int extra;
        if (b < 0x80) { cp = b; extra = 0; }
        else if ((b >> 5) == 6) { cp = b & 0x1F; extra = 1; }
        else if ((b >> 4) == 14) { cp = b & 0x0F; extra = 2; }
        else { cp = b & 0x07; extra = 3; }
        i++;
        for (int k = 0; k < extra && i < utf8.size(); k++, i++) cp = (cp << 6) | (utf8[i] & 0x3F);
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out += char16_t(0xD800 + (cp >> 10));
            out += char16_t(0xDC00 + (cp & 0x3FF));
        } else {
            out += char16_t(cp);
        }
    }
    return out;
}

inline std::string utf8(const std::u16string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        char32_t cp = s[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (s[++i] - 0xDC00);
        }
        if (cp < 0x80) out += char(cp);
        else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
        else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    }
    return out;
}

/** String.format(Locale.ROOT, …) for the explanations (ASCII formats only). */
inline std::u16string format(const char* fmt, ...) {
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof buffer, fmt, args);
    va_end(args);
    return utf16(buffer);
}

} // namespace rea
