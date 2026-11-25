// MozcConverterTest.kt's cases against the C++ converter, plus a dump for the parity check.
//
// Usage: mozc_converter_test <mozc.dic>
//        mozc_converter_test <mozc.dic> <readings.txt> <out.tsv>   (reading, then text/kanaCount per candidate, tab-separated)

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>

#include "../MozcConverter.h"
#include "../../engine/src/Text.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#else
#include <mach/mach.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace rea;

static int failures = 0;

static void check(bool ok, const char* what, const std::u16string& detail = u"") {
    if (ok) return;
    failures++;
    std::printf("FAIL %s %s\n", what, utf8(detail).c_str());
}

/** Resident memory, MB: the working set on Windows. */
static double residentMb() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof counters);
    return counters.WorkingSetSize / 1048576.0;
#else
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    task_info(mach_task_self(), MACH_TASK_BASIC_INFO, task_info_t(&info), &count);
    return info.resident_size / 1048576.0;
#endif
}

static double msSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

static std::vector<std::u16string> split(const std::u16string& s, char16_t separator) {
    std::vector<std::u16string> out;
    size_t start = 0;
    for (size_t at; (at = s.find(separator, start)) != std::u16string::npos; start = at + 1) out.push_back(s.substr(start, at - start));
    out.push_back(s.substr(start));
    return out;
}

/** Everyday phrases: reading to expected (alternatives split by |). */
static const std::pair<const char16_t*, const char16_t*> PHRASES[] = {
    {u"きょうはいいてんきですね", u"今日はいい天気ですね"},
    {u"てんき", u"天気"},
    {u"あしたのかいぎ", u"明日の会議"},
    {u"かいぎのしりょうをおくります", u"会議の資料を送ります"},
    {u"しりょう", u"資料"},
    {u"でんしゃがおくれています", u"電車が遅れています"},
    {u"でんしゃ", u"電車"},
    {u"えきでまっています", u"駅で待っています"},
    {u"ひるごはんをたべましょう", u"昼ご飯を食べましょう|昼ごはんを食べましょう"},
    {u"しゅくだい", u"宿題"},
    {u"べんきょうする", u"勉強する"},
    {u"にほんごをべんきょうしています", u"日本語を勉強しています"},
    {u"しごと", u"仕事"},
    {u"しごとがおわったられんらくします", u"仕事が終わったら連絡します"},
    {u"れんらく", u"連絡"},
    {u"ありがとうございます", u"ありがとうございます|有難うございます"},
    {u"よろしくおねがいします", u"よろしくお願いします|宜しくお願いします"},
    {u"おねがいします", u"お願いします"},
    {u"じかんがありますか", u"時間がありますか"},
    {u"じかん", u"時間"},
    {u"らいしゅうのよてい", u"来週の予定"},
    {u"よてい", u"予定"},
    {u"でんわばんごう", u"電話番号"},
    {u"じゅうしょをおしえてください", u"住所を教えてください|住所を教えて下さい"},
    {u"としょかん", u"図書館"},
    {u"びょういんにいきます", u"病院に行きます"},
    {u"くすり", u"薬"},
    {u"かいものにいく", u"買い物に行く"},
    {u"やさいとくだもの", u"野菜と果物"},
    {u"たのしいりょこうでした", u"楽しい旅行でした"},
};

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: mozc_converter_test <mozc.dic> [readings.txt out.tsv]\n");
        return 2;
    }
    double before = residentMb();
    auto start = std::chrono::steady_clock::now();
    MozcConverter converter(argv[1]);
    converter.candidates(u"あ");
    double loadMs = msSince(start);
    double loadedMb = residentMb() - before;
    if (!converter.loaded()) {
        std::printf("FAIL can't map %s\n", argv[1]);
        return 1;
    }
    auto top = [&](const std::u16string& kana) { return converter.candidates(kana).front().text; };

    if (argc == 4) { // parity dump
        std::ifstream in(argv[2], std::ios::binary);
        std::ofstream out(argv[3], std::ios::binary);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            out << line;
            auto all = converter.candidates(utf16(line));
            for (auto& c : all) out << '\t' << utf8(c.text) << '/' << c.kanaCount;
            out << '\n';
        }
        return 0;
    }

    // sentences
    check(top(u"きょうはいいてんきですね") == u"今日はいい天気ですね", "sentences", top(u"きょうはいいてんきですね"));
    check(top(u"かいぎのしりょうをおくります") == u"会議の資料を送ります", "sentences", top(u"かいぎのしりょうをおくります"));
    check(top(u"とうきょうにいきます") == u"東京に行きます", "sentences", top(u"とうきょうにいきます"));
    check(top(u"せいかいか") == u"正解か", "sentences", top(u"せいかいか"));

    // candidateShape
    {
        std::u16string kana = u"きょうはいいてんきですね";
        auto all = converter.candidates(kana);
        std::u16string listed;
        for (auto& c : all) listed += c.text + format("(%d) ", c.kanaCount);
        std::printf("%s\n", utf8(listed).c_str());
        check(all.size() >= 10 && all.size() <= 25, "candidateShape size");
        std::set<std::u16string> texts;
        for (auto& c : all) texts.insert(c.text);
        check(texts.size() == all.size(), "candidateShape distinct");
        size_t firstPartial = 0;
        while (firstPartial < all.size() && all[firstPartial].kanaCount >= int(kana.size())) firstPartial++;
        bool partialsAfter = firstPartial > 0 && firstPartial < all.size();
        for (size_t i = firstPartial; i + 2 < all.size(); i++) partialsAfter = partialsAfter && all[i].kanaCount < int(kana.size());
        check(partialsAfter, "candidateShape partials");
        check(std::any_of(all.begin(), all.end(), [](auto& c) { return c.text == u"今日は" && c.kanaCount == 4; }), "candidateShape 今日は");
        check(all.size() >= 2 && all[all.size() - 2].text == kana && all.back().text == u"キョウハイイテンキデスネ", "candidateShape plain last");
        check(top(u"、。") == u"、。", "candidateShape punctuation");
    }

    // phrases
    {
        int misses = 0;
        for (auto& [kana, want] : PHRASES) {
            auto got = top(kana);
            auto options = split(want, u'|');
            if (std::find(options.begin(), options.end(), got) != options.end()) continue;
            misses++;
            std::printf("MISS %s → %s (want %s)\n", utf8(kana).c_str(), utf8(got).c_str(), utf8(want).c_str());
        }
        int total = int(std::size(PHRASES));
        std::printf("top-1 accuracy: %d/%d = %.0f%%\n", total - misses, total, 100.0 * (total - misses) / total);
        check(1.0 - double(misses) / total >= 0.8, "phrases accuracy");
    }

    // learning
    {
        auto file = std::filesystem::temp_directory_path() / "mozc-learning.txt";
        std::filesystem::remove(file);
        {
            MozcConverter learner(argv[1], file);
            auto all = learner.candidates(u"じこ");
            auto alternative = std::find_if(all.begin(), all.end(), [](auto& c) { return c.text == u"自己"; });
            check(alternative != all.end(), "learning 自己 listed");
            if (alternative != all.end()) learner.learn(*alternative, u"じこ");
            check(learner.candidates(u"じこ").front().text == u"自己", "learning learned");
            learner.save();
            check(MozcConverter(argv[1], file).candidates(u"じこ").front().text == u"自己", "learning saved");
            learner.learn({u"事故", 2}, u"じこ");
            check(learner.candidates(u"じこ").front().text == u"事故", "learning latest wins");
        }
        // Two apps on the one file: each save keeps what the other saved (the TIP has a converter per app).
        std::filesystem::remove(file);
        {
            MozcConverter one(argv[1], file), two(argv[1], file);
            auto pick = [&](MozcConverter& c, const std::u16string& kana) {
                auto all = c.candidates(kana);
                auto second = all.size() > 1 ? all[1] : all[0];
                c.learn(second, kana);
                return second.text;
            };
            auto jiko = pick(one, u"じこ"), kisha = pick(two, u"きしゃ");
            one.save();
            two.save();
            MozcConverter fresh(argv[1], file);
            check(fresh.candidates(u"じこ").front().text == jiko && fresh.candidates(u"きしゃ").front().text == kisha,
                  "learning merged across converters");
        }
        // The same word learned in two apps: counted on, not set back (4 + 1 is 5).
        std::filesystem::remove(file);
        {
            MozcConverter a(argv[1], file), b(argv[1], file);
            for (int i = 0; i < 4; i++) a.learn({u"事故", 2}, u"じこ");
            a.save();
            b.learn({u"事故", 2}, u"じこ");
            b.save();
            std::ifstream saved(file, std::ios::binary);
            std::string all((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
            check(all.find("じこ\t事故\t5\n") != std::string::npos, "learning counts added across converters");
        }
#ifndef _WIN32
        // The lock not taken (its file unwritable): nothing written, nothing lost; the next save writes it.
        std::filesystem::remove(file);
        {
            auto lock = file;
            lock += ".lock";
            { std::ofstream{lock}; }
            std::filesystem::permissions(lock, std::filesystem::perms::none);
            MozcConverter a(argv[1], file);
            a.learn({u"事故", 2}, u"じこ");
            a.save();
            bool skipped = !std::filesystem::exists(file);
            std::filesystem::permissions(lock, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
            a.save();
            check(skipped && MozcConverter(argv[1], file).candidates(u"じこ").front().text == u"事故", "learning kept until the lock is taken");
            std::filesystem::remove(lock);
        }
#endif
#ifndef _WIN32
        // Two processes saving at the same instant over a large file: the lock keeps both readings.
        {
            int lost = 0;
            for (int trial = 0; trial < 5; trial++) {
                std::filesystem::remove(file);
                {
                    std::ofstream big(file, std::ios::binary);
                    for (int i = 0; i < 30000; i++) big << "よみ" << i << "\t語" << i << "\t1\n";
                }
                auto go = file;
                go += ".go";
                std::filesystem::remove(go);
                pid_t children[2];
                const char16_t* kanas[2] = {u"じこ", u"きしゃ"};
                for (int c = 0; c < 2; c++) {
                    if ((children[c] = fork()) == 0) {
                        MozcConverter converter(argv[1], file);
                        auto all = converter.candidates(kanas[c]);
                        converter.learn(all.size() > 1 ? all[1] : all[0], kanas[c]);
                        while (!std::filesystem::exists(go)) usleep(100);
                        converter.save();
                        _exit(0);
                    }
                }
                usleep(300000); // both loaded and waiting
                std::ofstream{go};
                for (pid_t child : children) waitpid(child, nullptr, 0);
                std::ifstream saved(file, std::ios::binary);
                std::string all((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
                bool both = all.find("じこ\t") != std::string::npos && all.find("きしゃ\t") != std::string::npos;
                bool kept = all.find("よみ29999\t") != std::string::npos;
                lost += !(both && kept);
                std::filesystem::remove(go);
            }
            check(lost == 0, "learning kept by two processes saving at once");
            std::printf("concurrent saves: %d/5 trials lost a reading\n", lost);
        }
#endif
        std::filesystem::remove(file);
    }

    // performance
    {
        std::u16string kana = u"かいぎのしりょうをおくりますのでかくにん"; // 20 kana
        for (int i = 0; i < 20; i++) converter.candidates(kana);
        auto t = std::chrono::steady_clock::now();
        for (int i = 0; i < 50; i++) converter.candidates(kana);
        double ms = msSince(t) / 50;
        std::printf("candidates(20 kana): %.2f ms; load %.1f ms, resident +%.1f MB after load, %.1f MB in all now\n", ms, loadMs, loadedMb,
                    residentMb());
        check(ms < 50, "performance");
    }

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
