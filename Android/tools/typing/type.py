#!/usr/bin/env python3
"""Types iOS/UITests' checks and corpus on the 한영일 keyboard in an emulator by tapping key
positions (KeyLayout's math), reads the try field back, and prints TYPING ok/MISS like iOS.

  type.py -s emulator-5560 [checks|corpus|all]

The app must be installed and the keyboard selected (adb shell ime set com.netmaru.rea/.ReaKeyboardService).
"""
import html, os, re, shutil, subprocess, sys, time
from pathlib import Path

SERIAL = sys.argv[sys.argv.index("-s") + 1] if "-s" in sys.argv else "emulator-5560"
WHAT = sys.argv[-1] if sys.argv[-1] in ("checks", "corpus", "all") else "all"
ROOT = Path(__file__).resolve().parents[3]
ADB = shutil.which("adb") or os.path.expanduser("~/Library/Android/sdk/platform-tools/adb")

def adb(*args):
    return subprocess.run([ADB, "-s", SERIAL, *args], capture_output=True, text=True).stdout

# Key centers, in px (KeyLayout: dp; the keyboard sits above the 48dp system bar under it).
w, h = map(int, re.search(r"(\d+)x(\d+)", adb("shell", "wm", "size")).groups())
d = int(re.search(r"(\d+)\s*$", adb("shell", "wm", "density").strip()).group(1)) / 160
GAP, MARGIN, KEY_H = 6, 20 / 3, 127 / 3
HEIGHT = 36 + 4 * KEY_H + 3 * (54 - KEY_H) - 11 / 3
W = w / d
pitch = (W - 2 * MARGIN + GAP) / 10
top = h - 48 * d - int(HEIGHT * d)
keys = {}
for r, row in enumerate(["qwertyuiop", "asdfghjkl", "zxcvbnm"]):
    x0 = (W - len(row) * pitch + GAP) / 2
    for i, c in enumerate(row):
        keys[c] = (x0 + i * pitch + (pitch - GAP) / 2, 36 + r * 54 + KEY_H / 2)
keys["⇧"] = (MARGIN + (1.3 * pitch - GAP) / 2, 36 + 108 + KEY_H / 2)
keys["⌫"] = (W - keys["⇧"][0], keys["⇧"][1])
x = MARGIN
for name, units in [("123", 1.25), ("※", 1.25), (" ", 4.5), (".", 1), ("⏎", 2)]:
    keys[name] = (x + (units * pitch - GAP) / 2, 36 + 162 + 19)
    x += units * pitch
SYMBOLS = dict(zip("1234567890-/:;()₩&@,?!'\"~", "qwertyuiopasdfghjklxcvbnm"))

shell = subprocess.Popen([ADB, "-s", SERIAL, "shell"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)

def run(lines):
    """Runs the commands in one shell session and waits until they're done."""
    shell.stdin.write("".join(l + "\n" for l in lines) + "echo DONE\n")
    shell.stdin.flush()
    while shell.stdout.readline().strip() != "DONE":
        pass

def taps(text):
    """Letters as keys; " " space, "⏎" return, "⌫" delete, "⇧" shift, "※" the 한あA key."""
    out, numbers = [], False
    def tap(k):
        cx, cy = keys[k]
        out.append(f"input tap {int(cx * d)} {int(top + cy * d)}")
    for c in text:
        if c in SYMBOLS:
            if not numbers: tap("123"); numbers = True
            tap(SYMBOLS[c]); continue
        if numbers and c.isalpha(): tap("123"); numbers = False
        if c.isupper(): tap("⇧"); tap(c.lower())
        else: tap(c)
    if numbers: tap("123")
    return out

def field():
    """The try field's text and center (an empty field reports its hint)."""
    adb("shell", "uiautomator", "dump", "/sdcard/rea.xml")
    xml = adb("shell", "cat", "/sdcard/rea.xml")
    node = re.search(r'<node[^>]*class="android.widget.EditText"[^>]*>', xml).group(0)
    text, hint = (html.unescape(re.search(f' {a}="([^"]*)"', node).group(1)) for a in ("text", "hint"))
    x1, y1, x2, y2 = map(int, re.search(r'bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', node).groups())
    return ("" if text == hint else text), ((x1 + x2) // 2, (y1 + y2) // 2)

def memory(label):
    pss = re.search(r"TOTAL PSS:\s+(\d+)", adb("shell", "dumpsys", "meminfo", "com.netmaru.rea"))
    print(f"MEMORY {label}: {int(pss.group(1)) // 1024 if pss else '?'} MB PSS")

misses = []
def check(keys_, expected, note, accept=None):
    before, _ = field()
    run(taps(keys_))
    time.sleep(0.5)
    after, _ = field()
    added = after[len(before):] if after.startswith(before) else "(changed) " + after
    ok = added == expected or (accept and added in accept)
    print(f"TYPING {'ok' if ok else 'MISS'} {note}: {keys_} → [{added}]" + ("" if ok else f" expected [{expected}]"))
    if not ok: misses.append((note, keys_, added, expected))

def swift_pairs(path):
    """("keys", "expected") pairs from a Swift array literal."""
    lit = r'"((?:[^"\\]|\\.)*)"'
    unescape = lambda s: re.sub(r"\\(.)", r"\1", s)
    return [(unescape(a), unescape(b)) for a, b in re.findall(rf"\(\s*{lit},\s*{lit}\s*\)", path.read_text())]

# The settings app with Korean > Japanese > English, the keyboard up in the try field.
adb("shell", "am", "start", "-n", "com.netmaru.rea/.SettingsActivity")
time.sleep(2)
adb("shell", "uiautomator", "dump", "/sdcard/rea.xml")
xml = adb("shell", "cat", "/sdcard/rea.xml")
x1, y1, x2, y2 = map(int, re.search(r'text="한국어 &gt; 일본어 &gt; 영어"[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', xml).groups())
adb("shell", "input", "tap", str((x1 + x2) // 2), str((y1 + y2) // 2))
_, (fx, fy) = field()
adb("shell", "input", "tap", str(fx), str(fy))
time.sleep(2)
memory("start")
started = time.time()

if WHAT in ("checks", "all"):
    check("dkssudgktpdy ", "안녕하세요 ", "Korean")
    check("hello world ", "hello world ", "English")
    check("githubdp ", "github에 ", "English + Korean particle")
    memory("after Korean/English")
    check("nihongo⏎", "日本語", "Japanese, return takes the first candidate")
    memory("after Japanese loads")
    check("wlagodgksek ", "진행한다 ", "Korean typo 짐행한다")
    check("snffu ", "눌려 ", "Korean word missing from our lists")
    check("teh ", "the ", "English typo")
    check("hellp ", "hello ", "English: p for o, one slip")
    check("dkssudgktpdu ", "안녕하세요 ", "Korean: ㅕ for ㅛ, one slip")
    check("hello⌫⌫ ", "hel ", "delete")
    check("⇧hi ", "Hi ", "one-shot shift")
    check("go ※", "해 ", "switch key", accept=["해 ", "ご "])
    check("dhsmf meetingdptj designdmf reviewgoTek ", "오늘 meeting에서 design을 review했다 ", "Korean + English")
    check("sksms Swiftfh appdmf aksemsek ", "나는 Swift로 app을 만든다 ", "Korean + capitalized English")
    check("toukyouniikimasu⏎", "東京に行きます", "Japanese sentence")
    check("kinou email wo okutta⏎", "昨日email を送った", "Japanese + English, spaces typed")
    check("kinouemailwookutta⏎", "昨日emailを送った", "Japanese + English, no spaces")

if WHAT in ("corpus", "all"):
    corpus = swift_pairs(ROOT / "iOS/UITests/Corpus.swift")
    before = len(misses)
    for i, (k, e) in enumerate(corpus):
        check(k, e, f"corpus {i + 1}")
    print(f"TYPING corpus: {len(corpus) - (len(misses) - before)}/{len(corpus)} sentences, {sum(len(e) for _, e in corpus)} characters")
    memory("after corpus")

print(f"TYPING done in {time.time() - started:.0f}s, {len(misses)} misses")
for note, k, added, expected in misses:
    print(f"  MISS {note}: {k} → [{added}] expected [{expected}]")
