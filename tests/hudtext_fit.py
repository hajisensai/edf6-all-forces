"""Every HUD text (src/hudtext.inc) in its 4 languages, measured offline against where it is drawn.

Measured with the glyph table tools/hud_view.cpp draws with (read from it: kGlyphH px a unit of font scale, a CJK
character a full em, Latin kGlyphW of it in English and Japanese, the Chinese fonts' own advances kChineseLatin), the
format's arguments stood in for (a number two or three digits, a key or word argument "Shift"), so:
  - every text, every language: written into a HUD line it keeps under the line's 127 characters (hud_internal.h
    kLineChars; TextWritten would cut it), a word argument counted WORD_CHARS (8: a key's name, "Left Alt") characters;
  - every text drawn into a slot (SLOTS: the slot's width and font scale read from the code that draws it) fits it at
    its size, or shrunk as FitLine shrinks (hud_text.cpp: as far as text_fit.h kMinScale), or is declared CUT (FitLine
    cuts it with "..." there: the slot is too narrow for it, said why). A CUT that fits is stale and fails too.
Texts not in a slot are free lines (anchored, any width): tools/hud_view.cpp checks them for overlaps in each language.
Source only, no game.

Usage: python hudtext_fit.py [repository root]   (default: the parent of this file's directory)"""
from __future__ import annotations

import ast
import os
import re
import sys

ROOT = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LANGS = ('en', 'zh-CN', 'zh-TW', 'ja')
WORD_CHARS = 8   # a %ls argument's characters for the line's length (a key's name: "Left Alt", "Shift")
checks = 0
failures: list[str] = []


def read(rel: str) -> str:
    with open(os.path.join(ROOT, rel), encoding='utf-8') as f:
        return f.read()


def number(text: str, pattern: str, rel: str) -> float:
    m = re.search(pattern, text)
    assert m, f'{rel}: {pattern} not found'
    return float(m.group(1))


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        failures.append(why)


# --- The texts ---
def texts() -> dict[str, list[str]]:
    inc = read('src/hudtext.inc')
    out: dict[str, list[str]] = {}
    for m in re.finditer(r'^HUDTEXT\((\w+),', inc, re.M):
        j, strs = m.end(), []
        while len(strs) < 4:
            k = inc.index('L"', j)
            e = k + 2
            while inc[e] != '"':
                e += 2 if inc[e] == '\\' else 1
            strs.append(ast.literal_eval(inc[k + 1:e + 1]))
            j = e + 1
        out[m.group(1)] = strs
    assert len(out) > 500, len(out)
    return out


# --- The glyph table (tools/hud_view.cpp Advance) ---
HV = read('tools/hud_view.cpp')
GLYPH_H = number(HV, r'kGlyphH=([0-9.]+)f', 'tools/hud_view.cpp')
GLYPH_W = number(HV, r'kGlyphW=([0-9.]+)f', 'tools/hud_view.cpp')
CHINESE_LATIN = [float(x.strip().rstrip('f')) for x in
                 re.search(r'kChineseLatin\[95\]=\{([^}]*)\}', HV).group(1).split(',')]
assert len(CHINESE_LATIN) == 95, len(CHINESE_LATIN)


def advance(c: str, lang: int) -> float:
    o = ord(c)
    if o >= 0x2E80:
        return 1.0
    if lang in (1, 2) and 0x20 <= o < 0x7F:
        return CHINESE_LATIN[o - 0x20]
    return GLYPH_W


SPEC = re.compile(r'%[-+ #0]*\d*(?:\.(\d+))?(ls|lc|d|u|x|X|f|%)')


def expand(fmt: str, word: str) -> str:
    def one(m: re.Match) -> str:
        kind = m.group(2)
        if kind == '%':
            return '%'
        if kind == 'ls':
            return word
        if kind == 'f':
            return '00.' + '0' * int(m.group(1)) if m.group(1) and m.group(1) != '0' else '000'
        return '00' if kind in ('x', 'X') else '000'
    return SPEC.sub(one, fmt)


def width(fmt: str, lang: int, scale: float) -> float:   # design px at HUD scale 1
    return sum(advance(c, lang) for c in expand(fmt, 'Shift')) * GLYPH_H * scale


# --- The slots: (what, keys, slot width in design px, font scale), read from the code that draws them ---
HI = read('src/hud_internal.h')
MB = read('src/map_buttons.h')
MAP = read('src/hud_map.cpp')
STATS = read('src/hud_stats.inc')
LINE_SCALE = number(HI, r'constexpr float kLineScale=([0-9.]+)f', 'src/hud_internal.h')
MIN_SCALE = number(read('src/text_fit.h'), r'kMinScale=([0-9.]+)f', 'src/text_fit.h')
LINE_CHARS = int(number(HI, r'constexpr int kLineChars=(\d+);', 'src/hud_internal.h'))


def tx_list(text: str, name: str, rel: str) -> list[str]:
    m = re.search(name + r'\[[^\]]*\]=\{([^}]*)\}', text)
    assert m, f'{rel}: {name} not found'
    return re.findall(r'Tx::(\w+)', m.group(1))


def drawn_at(text: str, key_expr: str, rel: str) -> tuple[float, str]:
    """The Label call drawing `key_expr`: its font scale (kLineScale*k) and the FitLine after it (its slot's edges)."""
    i = text.find(key_expr)
    assert i >= 0, f'{rel}: {key_expr} not drawn'
    head = text.rfind('Label(', 0, i)
    m = re.search(r'kLineScale\*([0-9.]+)f', text[head:i])
    assert m, f'{rel}: {key_expr}: its Label has no kLineScale*k'
    fit = re.search(r'FitLine\(text,[^;]*;', text[i:i + 400])
    assert fit, f'{rel}: {key_expr}: no FitLine after its Label'
    return LINE_SCALE * float(m.group(1)), fit.group(0)


SLOTS = []
# The support bar's composition panel (hud_map.cpp MapComposePanel): a kind's button, kComposeKindW wide, 2 px a side.
kind_w = number(MB, r'kComposeKindW=([0-9.]+)f', 'src/map_buttons.h')
scale, fit = drawn_at(MAP, 'KindText(kind));', 'src/hud_map.cpp')
assert 'q.x0+2.0f*s,q.x1-2.0f*s' in fit, fit
SLOTS.append(('composition kind button', tx_list(MAP, 'kKindText', 'src/hud_map.cpp'), kind_w - 4, scale))
# its class label, kComposeLabelW wide, 2 px off its right
label_w = number(MB, r'kComposeLabelW=([0-9.]+)f', 'src/map_buttons.h')
scale, fit = drawn_at(MAP, 'Tr(kClassText[k]));', 'src/hud_map.cpp')
assert 'L.classLabel[k].x1-2.0f*s' in fit, fit
SLOTS.append(('composition class label', tx_list(MAP, 'kClassText', 'src/hud_map.cpp'), label_w - 2, scale))
# The damage statistics page's tabs (hud_stats.inc StatsHead): at most 140 px (the 1920-wide design), 4 px a side.
tab_w = number(STATS, r'tabW=std::fmin\(([0-9.]+)f\*s', 'src/hud_stats.inc')
scale, fit = drawn_at(STATS, 'L"%ls",word);', 'src/hud_stats.inc')
assert 'r.x0+4.0f*s,r.x1-4.0f*s' in fit, fit
SLOTS.append(('statistics tab', tx_list(STATS, 'kStatsTabName', 'src/hud_stats.inc'), tab_w - 8, scale))
# The M map's band (hud_map.cpp MapScreen): the screen's width (1920 design) less 16 px a side, kLineScale*0.75.
band = re.search(r'// The band over the map\'s keys.*?for\(int i=footerFirst;i<\*at;\+\+i\)', MAP, re.S)
assert band, 'src/hud_map.cpp: the band not found'
assert 'FitLine(text,row,16.0f*s,width-16.0f*s)' in MAP, 'src/hud_map.cpp: the band\'s FitLine moved'
band_scale = LINE_SCALE * number(band.group(0), r'kLineScale\*([0-9.]+)f,kWhite,Tr\(Tx::npcMouseKeys\)', 'src/hud_map.cpp')
SLOTS.append(('M map band', sorted(set(re.findall(r'Tx::(\w+)', band.group(0))) - {'selectedAll', 'selectedOne', 'selectedSome'}),
              1920 - 32, band_scale))

# Texts FitLine cuts in their slot (the slot too narrow at kMinScale): key -> (languages, why).
CUT = {
    'kindThunderBow': ('ja', 'サンダーボウ in a 56 px button: the panel\'s geometry (map_buttons.h) is for EN / ZH words'),
    'kindMiddleCannon': ('ja', '中型キャノン, as kindThunderBow'),
    'classWingDiver': ('ja', 'ウイングダイバー in the 66 px class column, as kindThunderBow'),
}


def main() -> int:
    t = texts()
    for key, strs in t.items():
        for li, s in enumerate(strs):
            n = len(expand(s, 'W' * WORD_CHARS))
            check(n < LINE_CHARS, f'{key} [{LANGS[li]}]: {n} characters with its arguments, a HUD line holds {LINE_CHARS - 1}')
    seen = set()
    for what, keys, slot, scale in SLOTS:
        assert keys, what
        for key in keys:
            assert key in t, f'{what}: {key} not in hudtext.inc'
            seen.add(key)
            cut_langs = set(CUT[key][0].split()) if key in CUT else set()
            for li, s in enumerate(t[key]):
                w = width(s, li, scale)
                cut = w * MIN_SCALE > slot
                if LANGS[li] in cut_langs:
                    check(cut, f'{key} [{LANGS[li]}]: declared CUT but fits its {what} ({w:.0f} px, {slot:.0f} px at '
                                f'{MIN_SCALE} its size): drop it from CUT')
                else:
                    check(not cut, f'{key} [{LANGS[li]}] "{s}": {w:.0f} px at its size, {w * MIN_SCALE:.0f} shrunk to '
                                    f'kMinScale, its {what} {slot:.0f} px: FitLine cuts it (widen the slot, shorten '
                                    f'the text, or declare it in CUT with why)')
    for key in CUT:
        check(key in seen, f'CUT {key}: not in any slot')
    for f in failures:
        print('FAIL', f)
    print(f'hudtext_fit: {len(t)} texts x {len(LANGS)} languages, {len(seen)} in {len(SLOTS)} slots, '
          f'{len(CUT)} declared cut; {checks - len(failures)} / {checks} checks passed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
