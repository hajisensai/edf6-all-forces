r"""Source guard of the game folder lookup: python tests/gamedir_literal_guard.py, exit 1 on a failure.

Every Python / cmd / CMake file in the repository finds an EDF install through pylib/gamedir.py (find(), find_or_dev(),
find_other()), so none may carry a developer machine's path or its own environment variable:
  - no `steamapps\common\` literal (either slash, any case) outside pylib/gamedir.py (the one lookup) and
    pylib/xm_pack.py (X-Morph's own install, not an EDF's);
  - nothing reads $EDF6_GAME_DIR / $EDF41_GAME_DIR / $EDF5_GAME_DIR (os.environ.get / os.environ[...] / os.getenv);
    gamedir answers to $EDF6_DIR / $EDF41_DIR / $EDF5_DIR. CMake's cache variable ${EDF6_GAME_DIR} and plain comments
    are fine.
"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ALLOWED = {'pylib/gamedir.py', 'pylib/xm_pack.py', 'tests/gamedir_literal_guard.py'}
LITERAL = re.compile(r'steamapps[\\/]+common[\\/]', re.IGNORECASE)
ENV_READ = re.compile(r'''(?:environ\s*\.\s*get\s*\(|environ\s*\[|getenv\s*\()\s*[rbf]?['"]EDF(?:6|41|5)_GAME_DIR\b''')


def tracked() -> list[str]:
    out = subprocess.run(['git', 'ls-files'], cwd=ROOT, capture_output=True, check=True).stdout
    names = out.decode('utf-8').split('\n')
    return [n for n in names if n and (n.endswith(('.py', '.cmd')) or n == 'CMakeLists.txt')]


def main() -> int:
    failures: list[str] = []
    for name in tracked():
        if name in ALLOWED:
            continue
        path = ROOT / name
        if not path.is_file():
            continue
        for number, line in enumerate(path.read_text(encoding='utf-8', errors='replace').splitlines(), 1):
            if LITERAL.search(line):
                failures.append(f'{name}:{number}: a Steam library path literal (use pylib/gamedir.py)')
            if ENV_READ.search(line):
                failures.append(f'{name}:{number}: reads a *_GAME_DIR environment variable (gamedir reads EDF6_DIR / EDF41_DIR / EDF5_DIR)')
    for failure in failures:
        print('FAIL', failure)
    if failures:
        return 1
    print('gamedir literal guard: ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
