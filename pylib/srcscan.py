"""C/C++ source scanning for the source guards (tools/selftest.py, tests/*_guard.py): one reader instead of a copy in
each guard (docs/reviews/design-review-1126.md P7, docs/reviews/guards-conversion-1127.md).

What a guard asks of a source is structural: is this symbol defined, which functions call that one, what does this
function's body call, in which order. So the text is read the way the compiler reads it, not as lines:
  - code(): comments out, string and character literals (raw strings too) kept whole: a // inside "http://..." is
    not a comment, a brace or a call's name inside a literal or a comment is not code;
  - body(): the braces-balanced body of a definition (a declaration, `;` before its `{`, is passed over), braces
    inside literals not counted;
  - definitions() / functions(): the function definitions of a file (namespace / class level), each with its body;
  - calls() / callers(): a call of a name (a word followed by its `(`), and the functions whose bodies make one;
  - defines(): a name defined (constexpr / enum value / #define / function / struct), its value when it has one.
Whitespace and formatting do not matter to calls(), callers() and defines(); body() and code() keep the text so a
guard can still look for a phrase in it."""
from __future__ import annotations

import functools
import os
import re
from dataclasses import dataclass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# One pass over the literals and comments: a comment is only a comment outside a literal, and a literal's quote only a
# quote outside a comment. A character literal needs a non-word before it (1'000 is a digit separator, u8'x' rare).
_TOKENS = re.compile(
    r'(?P<line>//[^\n]*)'
    r'|(?P<block>/\*.*?\*/)'
    r'|(?P<raw>\bu?8?[LuU]?R"(?P<delim>[^(\s"\\]{0,16})\(.*?\)(?P=delim)")'
    r'|(?P<str>"(?:\\.|[^"\\\n])*")'
    r'|(?P<chr>(?<![\w])\'(?:\\.|[^\'\\\n])+\')',
    re.S)


def read(rel: str, root: str = ROOT, expand: bool = True) -> str:
    """A source's text (`rel` from the repo root, / separated); crew.h's and plugin.cpp's with the config.inc sections
    they include written out (tools/gen_config.py expand), as the compiler sees them, unless expand=False."""
    with open(os.path.join(root, *rel.split('/')), encoding='utf-8-sig') as f:
        text = f.read()
    if expand and '#include "config.inc"' in text:
        import sys
        tools = os.path.join(root, 'tools')
        if tools not in sys.path:
            sys.path.insert(0, tools)
        import gen_config
        text = gen_config.expand(text)
    return text


@functools.lru_cache(maxsize=256)
def code(text: str) -> str:
    """`text` with its comments out (a line comment to its line's end; a block comment kept as its line breaks, or one
    space), string and character literals kept as they are."""
    def keep(m: re.Match) -> str:
        if m.group('line') is not None:
            return ''
        if m.group('block') is not None:
            breaks = m.group('block').count('\n')
            return '\n' * breaks if breaks else ' '
        return m.group(0)
    return _TOKENS.sub(keep, text)


@functools.lru_cache(maxsize=256)
def masked(text: str) -> str:
    """code(text) with every literal's inside blanked (same length, line breaks kept): braces, parentheses and names in
    literals are not code. Positions in it are positions in code(text)."""
    def blank(m: re.Match) -> str:
        s = m.group(0)
        if m.group('line') is not None or m.group('block') is not None:   # code() left none; a stray one blanks too
            return re.sub(r'[^\n]', ' ', s)
        q = s.index('"') if m.group('raw') is not None or m.group('str') is not None else s.index("'")
        return s[:q + 1] + re.sub(r'[^\n]', ' ', s[q + 1:-1]) + s[-1]
    return _TOKENS.sub(blank, code(text))


def _close(mask: str, at: int) -> int:
    """The index of the `}` closing the `{` at mask[at]; -1 when unbalanced."""
    depth = 0
    for m in re.finditer(r'[{}]', mask[at:]):
        depth += 1 if m.group(0) == '{' else -1
        if depth == 0:
            return at + m.start()
    return -1


def body(text: str, head: str, regex: bool = False, braces: bool = True) -> str:
    """The body of the first definition in code(text) whose head is `head` (a literal; a regex with regex=True), with
    its braces (braces=False: without); a head followed by `;` before its `{` (a declaration, a call) is passed over.
    '' when there is none."""
    c, mask = code(text), masked(text)
    pattern = re.compile(head if regex else re.escape(head))
    pos = 0
    while (m := pattern.search(mask if not regex else c, pos)) is not None:
        start = max(m.end() - 1, m.start())
        open_at = mask.find('{', start)
        if open_at < 0:
            return ''
        if ';' not in mask[m.end():open_at]:
            end = _close(mask, open_at)
            if end < 0:
                return ''
            return c[open_at:end + 1] if braces else c[open_at + 1:end]
        pos = m.start() + 1
    return ''


@dataclass(frozen=True)
class Definition:
    name: str        # the function's own name (Foo of ns::Type::Foo)
    qualified: str   # as its head writes it (ns::Type::Foo, operator(), ~Type)
    head: str        # the text from the end of the previous statement to its `{`
    start: int       # code(text) index of its `{`
    end: int         # code(text) index of its `}`
    body: str        # code(text)[start:end + 1]


_SCOPE = re.compile(r'\b(namespace|struct|class|union|enum)\b')
_CONTROL = {'if', 'for', 'while', 'switch', 'catch', 'do', 'else', 'try', 'return', 'sizeof', 'decltype',
            '__try', '__except', 'alignas', 'static_assert'}


def _fn_name(head: str) -> str | None:
    """The function's name a definition head writes (the name before its parameters' parenthesis), None when the head
    is no function's (a control statement, an initializer, a lambda)."""
    head = re.sub(r'(?m)^\s*#.*$', '', head).strip()
    if not head or '(' not in head:
        return None
    if re.search(r'=\s*[\[{(]|\]\s*\(|\[[^\]]*\]\s*(?:\(|mutable|noexcept|->|\{)', head) and not re.search(
            r'\boperator\b', head):
        return None   # an initializer or a lambda
    depth, i = 0, 0
    for i, ch in enumerate(head):   # the first ( at depth 0 of the template brackets
        if ch == '<':
            depth += 1
        elif ch == '>':
            depth = max(0, depth - 1)
        elif ch == '(' and depth == 0:
            break
    else:
        return None
    m = re.search(r'((?:[A-Za-z_]\w*\s*::\s*)*~?(?:operator\s*(?:\(\)|[^\s(]+)|[A-Za-z_]\w*))\s*(?:<[^()]*>)?\s*$',
                  head[:i])
    if not m:
        return None
    qualified = re.sub(r'\s+', '', m.group(1))
    name = qualified.rsplit('::', 1)[-1]
    if name in _CONTROL or name in ('define', 'pragma'):
        return None
    return qualified


@functools.lru_cache(maxsize=256)
def definitions(text: str) -> tuple[Definition, ...]:
    """The function definitions in code(text) at namespace, class or file level (not the lambdas and blocks inside a
    function), in file order."""
    c, mask = code(text), masked(text)
    out: list[Definition] = []
    stack: list[str] = []   # the kinds of the open braces: 'scope' (namespace / class / extern), 'fn', 'other'
    last = 0                # where the current statement began
    for m in re.finditer(r'[{};]', mask):
        ch, at = m.group(0), m.start()
        if ch == ';':
            last = at + 1
            continue
        if ch == '}':
            if stack:
                stack.pop()
            last = at + 1
            continue
        head = mask[last:at]
        inside = all(k == 'scope' for k in stack)
        kind = 'other'
        if inside:
            bare = re.sub(r'(?m)^\s*#.*$', '', head)
            if re.search(r'\bextern\s*"\s*"\s*$', bare.strip()) or (_SCOPE.search(bare) and '(' not in bare.split(
                    _SCOPE.search(bare).group(0), 1)[1].split('{')[0] and '=' not in bare):
                kind = 'scope'
            else:
                q = _fn_name(bare)
                if q is not None:
                    end = _close(mask, at)
                    if end >= 0:
                        out.append(Definition(q.rsplit('::', 1)[-1], q, c[last:at].strip(), at, end, c[at:end + 1]))
                    kind = 'fn'
        stack.append(kind)
        last = at + 1
    return tuple(out)


def functions(text: str) -> dict[str, str]:
    """name -> body of the function definitions in `text` (the first of an overloaded name; definitions() for all)."""
    out: dict[str, str] = {}
    for d in definitions(text):
        out.setdefault(d.name, d.body)
    return out


def function(text: str, name: str) -> str:
    """The body (braces kept) of the first definition named `name` (own name, or qualified: ns::Type::Foo); ''."""
    for d in definitions(text):
        if name in (d.name, d.qualified) or d.qualified.endswith('::' + name):
            return d.body
    return ''


def _word(name: str) -> str:
    return rf'(?<![\w]){re.escape(name)}(?![\w])'


def calls(text: str, name: str) -> bool:
    """`text` (a body, a file) calls `name` (as name(, ns::name(, obj.name(, name<T>(), outside literals and
    comments."""
    return re.search(_word(name) + r'\s*(?:<[^;(){}]*>\s*)?\(', masked(text)) is not None


def mentions(text: str, name: str) -> bool:
    """`name` as a word of the code of `text` (outside literals and comments): a use, a call, a reference."""
    return re.search(_word(name), masked(text)) is not None


def callers(text: str, callee: str) -> set[str]:
    """The names of the functions defined in `text` whose bodies call `callee`."""
    return {d.name for d in definitions(text) if calls(d.body, callee)}


def order(text: str, *names: str) -> bool:
    """The first calls of `names` come in `text` in this order (each after the one before it)."""
    mask = masked(text)
    at = -1
    for n in names:
        m = re.compile(_word(n) + r'\s*(?:<[^;(){}]*>\s*)?\(').search(mask, at + 1)
        if m is None:
            return False
        at = m.start()
    return True


def before(text: str, *parts: str) -> bool:
    """The phrases `parts` come in `text` as this sequence (a literal search, each found after the one before it)."""
    at = -1
    for p in parts:
        nxt = text.find(p, at + 1)
        if nxt < 0:
            return False
        at = nxt
    return True


def precedes(text: str, first: str, then: str) -> bool:
    """`first` is in `text` before any `then` (both literal): the first place of `then` comes after the first of
    `first` (a gate before the work it guards: no work ahead of the gate)."""
    a, b = text.find(first), text.find(then)
    return 0 <= a < b


def defines(text: str, name: str) -> bool:
    """`text` defines `name`: a constexpr / const / #define / enum value / function / struct / class / using of it."""
    mask = masked(text)
    w = re.escape(name)
    return any(re.search(p, mask, re.M) for p in (
        rf'\b(?:constexpr|const)\b[^;={{()]*\b{w}\s*(?:\[[^\]]*\]\s*)?[={{]',
        rf'^\s*#\s*define\s+{w}\b',
        rf'\b(?:struct|class|union|enum(?:\s+class)?)\s+{w}\b\s*(?:final\b)?\s*[:{{]',
        rf'\busing\s+{w}\s*=',
        rf'[{{,]\s*{w}\s*(?:=[^,}}]*)?(?=[,}}])',
    )) or any(d.name == name for d in definitions(text))


def _split(mask: str, a: int, b: int) -> list[tuple[int, int]]:
    """mask[a:b] cut at its commas outside parentheses, brackets and braces: (start, end) of each piece."""
    out, depth, at = [], 0, a
    for i in range(a, b):
        ch = mask[i]
        if ch in '([{':
            depth += 1
        elif ch in ')]}':
            depth -= 1
        elif ch == ',' and depth == 0:
            out.append((at, i))
            at = i + 1
    out.append((at, b))
    return out


def value(text: str, name: str) -> str | None:
    """The initializer text of `name`'s constexpr / const / #define / enum value in `text`, whitespace squeezed; None.
    A declarator after the first of its statement (constexpr float a=1.0f,b=2.0f;) is found too, its initializer
    ending at the declarator's comma."""
    c, mask = code(text), masked(text)
    w = re.escape(name)
    for m in re.finditer(r'\b(?:constexpr|const)\b[^;{}]*;', mask):
        for a, b in _split(mask, m.start(), m.end() - 1):
            d = re.match(rf'(?:.*[\s*&])?{w}\s*=\s*', mask[a:b], re.S)
            if d:
                return re.sub(r'\s+', ' ', c[a + d.end():b]).strip()
    for p in (rf'\b(?:constexpr|const)\b[^;={{()]*\b{w}\s*\{{([^;]*)\}}\s*;',
              rf'^\s*#\s*define\s+{w}\s+(.+)$',
              rf'[{{,]\s*{w}\s*=\s*([^,}}]+)'):
        m = re.search(p, c, re.M)
        if m:
            return re.sub(r'\s+', ' ', m.group(1)).strip()
    return None


def takes(text: str, name: str, source: str) -> bool:
    """`text`'s `name` is `source` (a qualified name, pyconst::kX): its initializer is that name alone, or `name` is
    brought in by `using source;` (an array cannot be initialized from another). How a guard asks that a C++ copy is
    the generated one (src/py_consts.inc) rather than a hand-kept literal."""
    if source.rsplit('::', 1)[-1] == name and re.search(rf'\busing\s+{re.escape(source)}\s*;', masked(text)):
        return True
    return value(text, name) == source
