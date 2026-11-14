"""fetch.py - the Shadowgreat launcher's downloads.

Run by the launcher with the Python it downloaded first (tools/python):

    python -I fetch.py <destination folder> <item> [<item> ...]

Items (each is a free, openly licensed download from its own project):
    compiler  llvm-mingw, the C compiler           (Apache-2.0 with LLVM exception)
    ninja     Ninja, runs the compiler              (Apache-2.0)
    sdl2      SDL2 for MinGW, the game's window     (zlib)
    cmake     CMake, plans the build                (BSD-3-Clause)
    sherpa    sherpa-onnx, runs the voice model     (Apache-2.0)
    kokoro    the Kokoro voice model                (Apache-2.0)

Every download is checked against its SHA-256 fingerprint before it is
unpacked, so a damaged or changed file is never used. Nothing from the game
is downloaded. Output lines starting with '@' are read by the launcher:
    @step <n> <count> <name>      starting the n-th item
    @bytes <done> <total>         downloading
    @unpack <done> <total>        unpacking (files)
    @error <message>              stopped (exit code 1)
"""
import hashlib
import os
import shutil
import sys
import tarfile
import time
import urllib.request
import zipfile

GH = "https://github.com"
GH_RAW = "https://raw.githubusercontent.com"

ITEMS = {
    "compiler": dict(
        name="the compiler (llvm-mingw)",
        url=GH + "/mstorsjo/llvm-mingw/releases/download/20261020/llvm-mingw-20261020-ucrt-x86_64.zip",
        size=190612575,
        sha256="fd5462b7f5d93b2ff981fcd3f9c7e427e47f3203c64ea91c27bcc444eebd74cd"),
    "ninja": dict(
        name="Ninja",
        url=GH + "/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip",
        size=291570,
        sha256="07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65",
        into="ninja"),
    "sdl2": dict(
        name="SDL2",
        url=GH + "/libsdl-org/SDL/releases/download/release-2.30.0/SDL2-devel-2.30.0-mingw.zip",
        size=16027075,
        sha256="83a97f6e53d0ca78e71502efb9a704db6a5b5abca016a17ce66bdb099fa2f925"),
    "cmake": dict(
        name="CMake",
        url=GH + "/Kitware/CMake/releases/download/v4.4.4/cmake-4.4.4-windows-x86_64.zip",
        size=54400562,
        sha256="bace36e94b31c68ab6fa295f26dfa11219e0701cf7c94b0284a7d1cb13dac536"),
    "sherpa": dict(
        name="sherpa-onnx (runs the voice)",
        url=(GH + "/k2-fsa/sherpa-onnx/releases/download/v2.0.1/sherpa-onnx-v2.0.1-win-x64-shared-MT-Release-lib.tar.bz2"
             if sys.platform == "win32" else
             GH + "/k2-fsa/sherpa-onnx/releases/download/v2.0.1/sherpa-onnx-v2.0.1-linux-x64-shared-lib.tar.bz2"),
        size=7899712 if sys.platform == "win32" else 9710131,
        sha256=("afa4de3ca4ab0afd53bb4aab40d28ee15608364b549ffc0fe0caf6489bfc39a5" if sys.platform == "win32" else
                "532c45bbd41036d83cd70606797529aa8086c2053395a58967736c727625b120"),
        take=("sherpa-onnx-v2.0.1-win-x64-shared-MT-Release-lib/lib/" if sys.platform == "win32" else
              "sherpa-onnx-v2.0.1-linux-x64-shared-lib/lib/"),
        only=".dll" if sys.platform == "win32" else ".so"),
    "kokoro": dict(
        name="the Kokoro voice model",
        url=GH + "/k2-fsa/sherpa-onnx/releases/download/tts-models/kokoro-multi-lang-v1_0.tar.bz2",
        size=349886476,
        sha256="60703707bc05ccd48d60e1eb723f38b07d6e474795a0b6a588d7220400e0f7a4"),
    "dict-fr": dict(
        name="French pronunciation dictionary",
        url=GH_RAW + "/open-dict-data/ipa-dict/972cd67dd5f96bc5660e28070cde26e3928abcd7/data/fr_FR.txt",
        size=6152841,
        sha256="4b4133e294dd3f49f98fcb02bbce1da3f1ed481d7c768e8b4ed93936464dc4cf",
        raw=True),
    "dict-de": dict(
        name="German pronunciation dictionary",
        url=GH_RAW + "/open-dict-data/ipa-dict/972cd67dd5f96bc5660e28070cde26e3928abcd7/data/de.txt",
        size=30726400,
        sha256="d36d11dbcc226468a22f0fb3a0794ee0ce142bf277e26e517a8bfd0de1ec2653",
        raw=True),
    "dict-sv-mfa": dict(
        name="Swedish MFA pronunciation dictionary",
        url="https://huggingface.co/MontrealCorpusTools/swedish_mfa/resolve/fdc6edcd905a0c89075f93e408cdd4b2294adf11/dictionary/swedish_mfa.dict",
        size=3028835,
        sha256="54761ac5e8699ede9daa13358c45718e54148daeaaae6fb291613fa99096a07d",
        raw=True),
    "dict-sv-ipa": dict(
        name="Swedish IPA pronunciation dictionary",
        url=GH_RAW + "/open-dict-data/ipa-dict/972cd67dd5f96bc5660e28070cde26e3928abcd7/data/sv.txt",
        size=574869,
        sha256="acd508aaa5a883dd54e6fafcb886ee7494d71c101c7e4341410fe289ec46bdb2",
        raw=True),
}


class Stop(Exception):
    pass


def say(*words):
    print(*words, flush=True)


def download(item, archive):
    """Download to <archive>, checking size and fingerprint as it arrives."""
    for attempt in range(3):
        try:
            request = urllib.request.Request(item["url"], headers={"User-Agent": "Shadowgreat-launcher"})
            digest, done, last = hashlib.sha256(), 0, 0.0
            with urllib.request.urlopen(request, timeout=60) as response, open(archive, "wb") as out:
                while True:
                    chunk = response.read(1 << 18)
                    if not chunk:
                        break
                    out.write(chunk)
                    digest.update(chunk)
                    done += len(chunk)
                    if time.monotonic() - last > 0.2:
                        say("@bytes", done, item["size"])
                        last = time.monotonic()
            say("@bytes", done, item["size"])
            if done != item["size"] or digest.hexdigest() != item["sha256"]:
                raise Stop("The download of %s was damaged or has changed (its fingerprint doesn't match), "
                           "so it was not used. Please try again." % item["name"])
            return
        except Stop:
            raise
        except OSError as e:
            say("couldn't download %s (try %d of 3): %s" % (item["url"], attempt + 1, e))
            if attempt == 2:
                raise Stop("Couldn't download %s. Check the internet connection and try again.\n(%s)"
                           % (item["name"], e))
            time.sleep(3)


def unpack(item, archive, staging):
    """Unpack into <staging>, keeping only the part under item['take'] (and
    only names ending in item['only']) if set."""
    take, only = item.get("take", ""), item.get("only", "")
    if archive.endswith(".zip"):
        with zipfile.ZipFile(archive) as z:
            members = [m for m in z.infolist() if m.filename.startswith(take) and m.filename != take
                       and m.filename.endswith(only)]
            for n, m in enumerate(members):
                m.filename = m.filename[len(take):]
                z.extract(m, staging)              # zipfile keeps paths inside staging
                if n % 200 == 0:
                    say("@unpack", n, len(members))
    else:
        with tarfile.open(archive) as t:
            members = [m for m in t.getmembers() if m.name.startswith(take) and m.name != take
                       and m.name.endswith(only)]
            for n, m in enumerate(members):
                m.name = m.name[len(take):]
                t.extract(m, staging, filter="data")   # refuses anything outside staging
                if n % 50 == 0:
                    say("@unpack", n, len(members))
    say("@unpack", 1, 1)


def _load_tokens(voice_dir):
    tokens_file = os.path.join(voice_dir, "kokoro-multi-lang-v1_0", "tokens.txt")
    if not os.path.exists(tokens_file):
        tokens_file = os.path.join(voice_dir, "tokens.txt")
    if not os.path.exists(tokens_file):
        raise Stop("Kokoro tokens.txt is missing in %s; install the Kokoro voice model first." % voice_dir)
    tokens_str = set()
    with open(tokens_file, "r", encoding="utf-8") as f:
        for line in f:
            p = line.strip().split()
            if p:
                tokens_str.add(p[0])
    return tokens_str


def _write_lexicon(path, out_lines, label):
    if not out_lines:
        raise Stop("Could not build %s voice lexicon (no entries)." % label)
    tmp_out = path + ".tmp"
    with open(tmp_out, "w", encoding="utf-8") as f:
        f.writelines(out_lines)
    os.replace(tmp_out, path)
    say("prepared %s voice lexicon (%d words) in %s" % (label, len(out_lines), path))


def build_french_lexicon(voice_dir, src_path):
    fr_lex = os.path.join(voice_dir, "lexicon-fr.txt")
    tokens_str = _load_tokens(voice_dir)
    with open(src_path, "r", encoding="utf-8") as f:
        lines = f.read().splitlines()

    import unicodedata

    def strip_accents(s):
        return "".join(c for c in unicodedata.normalize("NFD", s) if unicodedata.category(c) != "Mn")

    out_lines = []
    seen = set()

    for line in lines:
        parts = line.strip().split("\t")
        if len(parts) != 2:
            continue
        raw_word = parts[0].strip().lower()
        if " " in raw_word:
            continue
        ipa = parts[1].strip("/[] ")
        tokens = []
        i = 0
        while i < len(ipa):
            if i + 1 < len(ipa) and ipa[i:i+2] in tokens_str:
                tokens.append(ipa[i:i+2])
                i += 2
            elif ipa[i] in tokens_str:
                tokens.append(ipa[i])
                i += 1
            elif ipa[i] in ("\u0303", "\u02c8", "\u02cc", "\u02d0"):
                tokens.append(ipa[i])
                i += 1
            else:
                i += 1
        if not tokens:
            continue
        tok_str = " ".join(tokens)
        for w in (raw_word, strip_accents(raw_word)):
            if w and w not in seen:
                seen.add(w)
                out_lines.append(w + " " + tok_str + "\n")

    _write_lexicon(fr_lex, out_lines, "French")


def build_german_lexicon(voice_dir, src_path):
    de_lex = os.path.join(voice_dir, "lexicon-de.txt")
    tokens_str = _load_tokens(voice_dir)
    with open(src_path, "r", encoding="utf-8") as f:
        lines = f.read().splitlines()

    replacements = [
        ("t͡s", "ʦ"), ("t͡ʃ", "ʧ"), ("d͡ʒ", "ʤ"), ("p͡f", "p f"),
        ("tʃ", "ʧ"), ("dʒ", "ʤ"), ("ts", "ʦ"), ("pf", "p f"),
        ("ʏ", "ɪ"), ("l̩", "l"), ("n̩", "n"), ("m̩", "m"), ("r̩", "r"),
        ("\u032f", ""), ("\u0329", ""), ("\u0361", ""),
    ]

    out_lines = []
    seen = set()

    for line in lines:
        parts = line.strip().split("\t")
        if len(parts) != 2:
            continue
        raw_word = parts[0].strip().lower()
        if " " in raw_word:
            continue
        ipa = parts[1].split(",")[0].strip("/[] ")
        for a, b in replacements:
            ipa = ipa.replace(a, b)
        tokens = []
        i = 0
        while i < len(ipa):
            if i + 1 < len(ipa) and ipa[i:i+2] in tokens_str:
                tokens.append(ipa[i:i+2])
                i += 2
            elif ipa[i] in tokens_str:
                tokens.append(ipa[i])
                i += 1
            elif ipa[i] in ("\u0303", "\u02c8", "\u02cc", "\u02d0"):
                tokens.append(ipa[i])
                i += 1
            else:
                i += 1
        if not tokens:
            continue
        tok_str = " ".join(tokens)
        variants = [raw_word]
        if "ß" in raw_word:
            variants.append(raw_word.replace("ß", "ss"))
        if "ss" in raw_word:
            variants.append(raw_word.replace("ss", "ß"))
        trans = raw_word.replace("ä", "ae").replace("ö", "oe").replace("ü", "ue").replace("ß", "ss")
        if trans != raw_word:
            variants.append(trans)
        for w in variants:
            if w and w not in seen:
                seen.add(w)
                out_lines.append(w + " " + tok_str + "\n")

    _write_lexicon(de_lex, out_lines, "German")


def build_swedish_lexicon(voice_dir, mfa_path, ipa_path):
    sv_lex = os.path.join(voice_dir, "lexicon-sv.txt")
    tokens_str = _load_tokens(voice_dir)

    tones = ["˥˧", "˥˩", "˧˩", "˩", "\u032a"]
    replacements = [
        ("ɧ", "ʃ"), ("ɭ", "r l"), ("ʈ", "r t"), ("ɖ", "r d"), ("ɳ", "r n"), ("ʂ", "r s"),
        ("ɵ", "ø"), ("ʉ", "u"), ("ʋ", "v"), ("ʏ", "ɪ"),
        ("ä", "ɛ"), ("g", "ɡ"), ("²", ""), ("ʰ", "")
    ]
    overrides = {
        "och": ["ɔ"],
        "en": ["e", "n"],
        "du": ["d", "u"],
        "i": ["i"],
        "det": ["d", "e", "ː"],
    }

    def to_tokens(ipa_str):
        for t in tones:
            ipa_str = ipa_str.replace(t, "")
        for a, b in replacements:
            ipa_str = ipa_str.replace(a, b)
        toks = []
        i = 0
        while i < len(ipa_str):
            if i + 1 < len(ipa_str) and ipa_str[i:i+2] in tokens_str:
                toks.append(ipa_str[i:i+2])
                i += 2
            elif ipa_str[i] in tokens_str:
                toks.append(ipa_str[i])
                i += 1
            elif ipa_str[i] in ("\u02d0",):
                toks.append(ipa_str[i])
                i += 1
            else:
                i += 1
        # Collapse duplicate r
        collapsed = []
        for t in toks:
            if t == "r" and collapsed and collapsed[-1] == "r":
                continue
            collapsed.append(t)
        return collapsed

    seen = set()
    out_lines = []

    with open(mfa_path, "r", encoding="utf-8") as f:
        lines = f.read().splitlines()

    for line in lines:
        p = line.strip().split("\t")
        if len(p) >= 2 and not p[0].startswith("<") and not p[0].startswith("["):
            w = p[0].strip().lower()
            if " " in w or w in seen:
                continue
            if w in overrides:
                toks = overrides[w]
            else:
                toks = []
                for ph in p[-1].split():
                    toks.extend(to_tokens(ph))
            if toks:
                seen.add(w)
                out_lines.append(w + " " + " ".join(toks) + "\n")

    with open(ipa_path, "r", encoding="utf-8") as f:
        raw_lines = f.read().splitlines()

    for line in raw_lines:
        p = line.strip().split("\t")
        if len(p) == 2:
            w = p[0].strip().lower()
            if " " in w or w in seen:
                continue
            toks = to_tokens(p[1].strip("/[] "))
            if toks:
                seen.add(w)
                out_lines.append(w + " " + " ".join(toks) + "\n")

    _write_lexicon(sv_lex, out_lines, "Swedish")


def install(key, dest, number, count):
    item = ITEMS[key]
    say("@step", number, count, item["name"])
    work = os.path.join(dest, ".downloads")
    os.makedirs(work, exist_ok=True)
    archive = os.path.join(work, item["url"].rsplit("/", 1)[1])
    download(item, archive)
    if item.get("raw"):
        target = os.path.join(dest, item.get("into", ""))
        os.makedirs(target, exist_ok=True)
        if key == "dict-fr":
            build_french_lexicon(target, archive)
            os.remove(archive)
        elif key == "dict-de":
            build_german_lexicon(target, archive)
            os.remove(archive)
        elif key in ("dict-sv-mfa", "dict-sv-ipa"):
            mfa_path = os.path.join(work, "swedish_mfa.dict")
            ipa_path = os.path.join(work, "sv.txt")
            if os.path.exists(mfa_path) and os.path.exists(ipa_path):
                build_swedish_lexicon(target, mfa_path, ipa_path)
                os.remove(mfa_path)
                os.remove(ipa_path)
        return
    staging = os.path.join(work, "unpack-" + key)
    shutil.rmtree(staging, ignore_errors=True)
    unpack(item, archive, staging)
    # Move what was unpacked into place, replacing an older copy.
    target = os.path.join(dest, item.get("into", ""))
    os.makedirs(target, exist_ok=True)
    for name in os.listdir(staging):
        there = os.path.join(target, name)
        if os.path.isdir(there):
            shutil.rmtree(there)
        elif os.path.exists(there):
            os.remove(there)
        os.replace(os.path.join(staging, name), there)
    shutil.rmtree(staging, ignore_errors=True)
    os.remove(archive)
    say("installed %s in %s" % (item["name"], target))


def main(argv):
    if len(argv) < 3 or any(k not in ITEMS for k in argv[2:]):
        say("@error usage: fetch.py <destination> <item>...; items: " + " ".join(ITEMS))
        return 1
    dest, keys = argv[1], argv[2:]
    try:
        for n, key in enumerate(keys, 1):
            install(key, dest, n, len(keys))
    except Stop as e:
        say("@error " + str(e).replace("\n", " "))
        return 1
    except Exception as e:                       # anything unexpected: say what it was
        say("@error Something went wrong: %s: %s" % (type(e).__name__, e))
        return 1
    shutil.rmtree(os.path.join(dest, ".downloads"), ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
