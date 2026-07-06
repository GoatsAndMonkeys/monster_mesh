#!/usr/bin/env python3
"""Generate DaycareLore.h (per-species Pokedex flavor + trivia, dex 1-386).

Reads the offline Pokedex dataset at /tmp/pokedex/pokedex/pokemon/NNN-name.txt
and writes two identical C++ headers:
  meshtastic-firmware/src/modules/monstermesh/DaycareLore.h
  monster_mesh_pi/src/shared/DaycareLore.h

Stdlib only. Output is ASCII-clean, each string <=150 chars at a word boundary.
"""
import os
import re
import sys

SRC_DIR = "/tmp/pokedex/pokedex/pokemon"
OUT_PATHS = [
    "/Users/goatsandmonkeys/Documents/pokemesh/meshtastic-firmware/src/modules/monstermesh/DaycareLore.h",
    "/Users/goatsandmonkeys/Documents/pokemesh/monster_mesh_pi/src/shared/DaycareLore.h",
]

MAXLEN = 150

# Map common non-ASCII characters to plain ASCII.
UNI_MAP = {
    "é": "e", "É": "E",          # e-acute
    "è": "e", "à": "a", "ñ": "n", "ü": "u",
    "’": "'", "‘": "'", "ʼ": "'",
    "“": '"', "”": '"',
    "—": "-", "–": "-", "−": "-", "‐": "-",
    "…": "...",
    "°": " deg", "×": "x", "½": "1/2",
    " ": " ",
    "′": "'", "″": '"',
    "ū": "u", "ō": "o", "ā": "a", "ī": "i",
}


def clean(s):
    if not s:
        return ""
    for k, v in UNI_MAP.items():
        s = s.replace(k, v)
    # Drop any remaining non-ASCII bytes.
    s = s.encode("ascii", "ignore").decode("ascii")
    # Collapse whitespace.
    s = re.sub(r"\s+", " ", s).strip()
    return s


def truncate(s, n=MAXLEN):
    s = s.strip()
    if len(s) <= n:
        return s
    cut = s[:n]
    # Prefer ending on a sentence boundary within the window.
    m = max(cut.rfind(". "), cut.rfind("! "), cut.rfind("? "))
    if m >= 50:
        return cut[: m + 1].strip()
    # Otherwise cut at the last word boundary (never mid-word).
    sp = cut.rfind(" ")
    if sp <= 0:
        return cut.rstrip()
    return cut[:sp].rstrip(" ,;:-")


def cescape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


# --- Parsing -------------------------------------------------------------

def parse_entries(text):
    """Return dict {Game: entry_text} from the POKEDEX ENTRIES block."""
    entries = {}
    in_block = False
    for line in text.splitlines():
        if line.startswith("POKEDEX ENTRIES"):
            in_block = True
            continue
        if in_block:
            if line.startswith("LEARNSET") or line.startswith("BULBAPEDIA"):
                break
            m = re.match(r"\[([A-Za-z]+)\]\s*(.+)", line.strip())
            if m:
                game, txt = m.group(1), m.group(2).strip()
                if game not in entries and txt:
                    entries[game] = txt
    return entries


def parse_biology(text):
    """Return list of paragraph strings from the == Biology == section."""
    lines = text.splitlines()
    paras = []
    in_bio = False
    for line in lines:
        st = line.strip()
        if st == "== Biology ==":
            in_bio = True
            continue
        if in_bio:
            if st.startswith("== ") or st.startswith("=== "):
                break
            if st and not st.startswith("="):
                paras.append(st)
    return paras


SENT_SPLIT = re.compile(r"(?<=[.!?])\s+(?=[A-Z0-9\"'])")


def split_sentences(para):
    # Protect a few common abbreviations from the naive splitter.
    protected = para
    for ab in ["Mr.", "Mrs.", "Ms.", "Dr.", "Mt.", "St.", "vs.", "etc.",
               "e.g.", "i.e.", "No.", "Jr.", "approx."]:
        protected = protected.replace(ab, ab.replace(".", "\x00"))
    parts = SENT_SPLIT.split(protected)
    return [p.replace("\x00", ".").strip() for p in parts if p.strip()]


GOOD = [
    "lives", "live ", "inhabit", "habitat", "found in", "dwell", "roams",
    "eats", "feeds", "feed on", "prey", "hunt", "sleeps", "nest", "burrow",
    "flies", " fly", "swims", " swim", "digs", " dig ", "emits", "produces",
    "releases", "sprays", "breathes", "spits", "leaps", "jump", "climbs",
    "muscles", "power", "strength", "warm", "cold", "poison", "venom",
    "forest", "mountain", "ocean", " sea ", "cave", "grass", "immune",
    "attack", "protect", "territory", "pack", "herd", "flock", "wander",
    "senses", "detect", "sensitive", "able to", "capable", "can lift",
    "never", "always", "loves", "known to", "said to", "night", "day",
    "electric", "heat", "flame", "fire", "water", "psychic", "evolve",
]
BAD = [
    "episode", "manga", "anime", " movie", "tcg", "generation ",
    "signature move", "trainer", " ash ", "the series", "pokemon sleep",
    "pokemon go", "pokemon snap", "pokemon origins", "detective pikachu",
    "as seen", "according to", "journal", "professor", "researcher",
    "translated", "unused", "english release", " version", "illustrated",
    "carddass", "official artwork", "ride pokemon", "in pokemon ",
    "manor", "mansion", "team rocket", "hoppy town", "in the ",
    "in alola", "in hisui", "in johto", "in kanto", "in hoenn",
]


def score_sentence(s):
    low = clean(s).lower()
    sc = 0
    for g in GOOD:
        if g in low:
            sc += 1
    for b in BAD:
        if b in low:
            sc -= 3
    # Prefer moderate length; penalize very short/very long.
    n = len(low)
    if n < 30:
        sc -= 2
    if n > 240:
        sc -= 1
    return sc


def pick_trivia(paras, exclude=""):
    """Choose the most flavorful behavioral sentence from Biology prose."""
    if not paras:
        return ""
    # Candidate sentences: skip the first (appearance) paragraph if we can.
    if len(paras) >= 2:
        cand_paras = paras[1:]
    else:
        # Single paragraph: skip its first sentence (usually appearance).
        sents = split_sentences(paras[0])
        cand_paras = [" ".join(sents[1:])] if len(sents) > 1 else paras
    sentences = []
    for p in cand_paras:
        sentences.extend(split_sentences(p))
    if not sentences:
        return ""
    best = None
    best_score = -10**9
    for i, s in enumerate(sentences):
        cl = clean(s)
        if not cl or cl == exclude:
            continue
        sc = score_sentence(s) - i * 0.01  # tie-break: earlier wins
        if sc > best_score:
            best_score = sc
            best = s
    return clean(best) if best else clean(sentences[0])


FLAVOR_ORDER = ["Emerald", "Crystal", "Ruby", "Firered", "Sapphire",
                "Leafgreen", "Gold", "Silver", "Yellow", "Red", "Blue"]


def pick_flavor(entries):
    for g in FLAVOR_ORDER:
        if g in entries:
            return entries[g], g
    if entries:
        k = next(iter(entries))
        return entries[k], k
    return "", None


def alt_entry(entries, used_game, exclude_text):
    """A different Pokedex entry than the flavor one (for trivia fallback)."""
    order = FLAVOR_ORDER + [g for g in entries if g not in FLAVOR_ORDER]
    for g in order:
        if g in entries and g != used_game:
            t = clean(entries[g])
            if t and t != exclude_text:
                return t
    return ""


def build():
    files = {}
    for fn in os.listdir(SRC_DIR):
        m = re.match(r"(\d{3})-.*\.txt$", fn)
        if m:
            files[int(m.group(1))] = os.path.join(SRC_DIR, fn)

    flavor = [""] * 387
    trivia = [""] * 387
    problems = []

    for dex in range(1, 387):
        path = files.get(dex)
        if not path:
            problems.append((dex, "no file"))
            continue
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            text = f.read()
        entries = parse_entries(text)
        paras = parse_biology(text)

        raw_flavor, used_game = pick_flavor(entries)
        fl = truncate(clean(raw_flavor))
        flavor[dex] = fl

        tr = truncate(pick_trivia(paras, exclude=clean(raw_flavor)))
        if not tr:
            tr = truncate(alt_entry(entries, used_game, clean(raw_flavor)))
        if not tr:
            tr = fl  # last resort
        trivia[dex] = tr

        if not fl:
            problems.append((dex, "no flavor"))
        if not tr:
            problems.append((dex, "no trivia"))

    return flavor, trivia, problems


def names():
    nm = {}
    for fn in os.listdir(SRC_DIR):
        m = re.match(r"(\d{3})-(.*)\.txt$", fn)
        if m:
            nm[int(m.group(1))] = m.group(2)
    return nm


def emit(flavor, trivia):
    nm = names()
    out = []
    out.append("#pragma once")
    out.append("#include <stdint.h>")
    out.append("// Per-species Pokedex flavor + trivia for the daycare, dex 1-386.")
    out.append("// Source: Bulbapedia / PokeAPI (Gen 1-3). ASCII-cleaned, <=150 chars each.")
    out.append("// Generated by monster_mesh_pi/scripts/gen_daycare_lore.py -- do not edit by hand.")
    out.append("static const char *const kDaycareFlavor[387] = {")
    out.append('    "",  // 0 unused')
    for dex in range(1, 387):
        nmn = nm.get(dex, "")
        out.append('    "%s",  // %d %s' % (cescape(flavor[dex]), dex, nmn))
    out.append("};")
    out.append("static const char *const kDaycareTrivia[387] = {")
    out.append('    "",  // 0 unused')
    for dex in range(1, 387):
        nmn = nm.get(dex, "")
        out.append('    "%s",  // %d %s' % (cescape(trivia[dex]), dex, nmn))
    out.append("};")
    out.append("// Return the flavor/trivia string for a national dex number, or \"\" if out of range.")
    out.append("static inline const char *daycareFlavor(uint16_t dex) {")
    out.append("    return (dex >= 1 && dex <= 386) ? kDaycareFlavor[dex] : \"\";")
    out.append("}")
    out.append("static inline const char *daycareTrivia(uint16_t dex) {")
    out.append("    return (dex >= 1 && dex <= 386) ? kDaycareTrivia[dex] : \"\";")
    out.append("}")
    out.append("")
    return "\n".join(out)


def main():
    flavor, trivia, problems = build()
    content = emit(flavor, trivia)
    # Safety: assert ASCII.
    nonascii = [c for c in content if ord(c) > 127]
    if nonascii:
        print("ERROR: non-ASCII bytes present:", set(nonascii), file=sys.stderr)
        sys.exit(1)
    for p in OUT_PATHS:
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "w", encoding="ascii") as f:
            f.write(content)
    nfl = sum(1 for i in range(1, 387) if flavor[i])
    ntr = sum(1 for i in range(1, 387) if trivia[i])
    print("flavor entries filled: %d/386" % nfl)
    print("trivia entries filled: %d/386" % ntr)
    print("header size: %d bytes" % len(content))
    if problems:
        print("problems:")
        for dex, why in problems:
            print("  #%03d %s" % (dex, why))
    else:
        print("no problems")


if __name__ == "__main__":
    main()
