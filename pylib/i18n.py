"""The installer's language: Chinese or English, for everything it shows the player (prompts, questions, progress, errors).

The maintainer, 2026-11-10: 「同时支持一下英文版本」. The language follows Windows' display language (a Chinese UI gets
Chinese, any other English); `installer.py --lang zh|en` (or EDF6_LANG=zh|en) overrides it. The choice is put in the
environment (ENV) so the processes the installer starts (the model workers, pylib/model_workers.py; the loadout
generator) speak the same language.

Each message is written where it is said, both languages side by side: tr('写入', 'Writing'). Fixed keywords a
developer greps a log for (EDF6VC_PACKAGE_OK, the generator's status words, file names) are not translated.
"""
from __future__ import annotations

import os
import sys

ENV = 'EDF6_LANG'
LANGS = ('zh', 'en')
LANG_CHINESE = 0x04   # PRIMARYLANGID of a Chinese LANGID (zh-CN 0x0804, zh-TW 0x0404, zh-HK 0x0C04, ...)

_lang: str | None = None


def lang_of(langid: int) -> str:
    """'zh' for a Chinese Windows LANGID (any region), else 'en'."""
    return 'zh' if langid & 0x3FF == LANG_CHINESE else 'en'


def system_lang() -> str:
    """'zh' when Windows' display language (GetUserDefaultUILanguage) is Chinese, else 'en' (also off Windows)."""
    if os.name != 'nt':
        return 'en'
    import ctypes
    return lang_of(ctypes.windll.kernel32.GetUserDefaultUILanguage())


def lang() -> str:
    """The language in use: set_lang's, else EDF6_LANG's, else the system's."""
    global _lang
    if _lang is None:
        env = os.environ.get(ENV, '').strip().lower()
        _lang = env if env in LANGS else system_lang()
        os.environ[ENV] = _lang   # the processes this one starts (model workers) say the same
    return _lang


def set_lang(code: str) -> None:
    """Use `code` ('zh' / 'en') from now on, in this process and the ones it starts."""
    global _lang
    if code not in LANGS:
        raise ValueError(f'language {code!r}: one of {", ".join(LANGS)}')
    _lang = code
    os.environ[ENV] = code


def take_lang_arg(argv: list[str]) -> list[str]:
    """`argv` without its --lang zh|en / --lang=zh|en, which is applied (set_lang). An unknown language is refused."""
    out: list[str] = []
    i = 0
    while i < len(argv):
        arg = argv[i]
        if arg == '--lang' or arg.startswith('--lang='):
            value = arg.partition('=')[2] if '=' in arg else (argv[i + 1] if i + 1 < len(argv) else '')
            i += 1 if '=' in arg else 2
            if value.lower() not in LANGS:
                raise SystemExit(f'--lang {value!r}: zh or en')
            set_lang(value.lower())
            continue
        out.append(arg)
        i += 1
    return out


def tr(zh: str, en: str) -> str:
    """The message in the language in use."""
    return zh if lang() == 'zh' else en


def is_zh() -> bool:
    return lang() == 'zh'


if __name__ == '__main__':   # python pylib/i18n.py: what this machine would pick
    print(lang(), file=sys.stdout)
