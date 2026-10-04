"""Prints the top-level values of OBJECT SGO / DSGO files in the game's Root.cpk (a developer's probe).

  python -B tools/dump.py NAME.SGO ...
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'pylib'))
import rootcpk  # noqa: E402
import sgo  # noqa: E402

for n in sys.argv[1:]:
    print('=====', n)
    v = sgo.load(data=rootcpk.default().read('OBJECT', n))
    for k, x in v.items():
        s = repr(x)
        print(' ', k, '=', s[:400])
