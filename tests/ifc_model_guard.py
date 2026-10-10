"""The Proteus shield's round carries its model into the IFC (src/ifc_model.h) on every path that fires it.

A BarrierBullet01 fired by a DemoIndirectFire throws in its ctor unless the IFC's InitParam +0x1B0 holds the model
(test hub report #6, docs/feedback-2026-10-10-proteus-shield-crash.md). This checks the wiring in src/jet_bay.cpp:
the shield's EmcFile is marked as a model round, EmcFire hands the model over right after ShellMake and returns no
round when it was not handed over, the kind is not ready without the model contract's signatures, and the shield SGO
generator writes the member the plugin reads.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
bay = (ROOT / 'src/jet_bay.cpp').read_text(encoding='utf-8')
header = (ROOT / 'src/ifc_model.h').read_text(encoding='utf-8')
gen = (ROOT / 'tools/make_proteus.py').read_text(encoding='utf-8')
failures = []


def check(ok: bool, what: str) -> None:
    if not ok:
        failures.append(what)


shield = re.search(r'\{L"app:/object/edf6vc_proteus_shield\.sgo",L"EDF6VC_PROTEUS_SHIELD\.SGO","Proteus shield",(\w+)\}', bay)
check(bool(shield) and shield.group(1) == 'true', 'the Proteus shield EmcFile is a model round (model=true)')
others = re.findall(r'\{L"app:/object/edf6vc_(?!proteus_shield)\w+\.sgo",L"\w+\.SGO","[^"]+"(,true)?\}', bay)
check(others and not any(others), 'no other EMC round is a model round (their classes take no model)')

fire = re.search(r'RoundObj EmcFire\(.*?\n\}', bay, re.S)
check(bool(fire), 'EmcFire found')
if fire:
    body = fire.group(0)
    make = body.find('ShellMake(')
    carry = body.find('if(kEmcFiles[i].model && !EmcCarryModel(o,i))return RoundObj{};')
    check(make >= 0 and carry > make, 'EmcFire hands the model over after ShellMake and fires nothing when it was not')
    check(body.find('return RoundObj{o,') > carry, '...before it returns the round')

helper = re.search(r'bool EmcCarryModel\(unsigned char\* o,int i\) noexcept \{.*?\n\}', bay, re.S)
check(bool(helper), 'EmcCarryModel found')
if helper:
    h = helper.group(0)
    check('ifcmodel::CarryModel(image,o+ifcmodel::kDemoSgo,o+kDemoIfc+ifcmodel::kIfcModel)' in h,
          'it carries the round\'s own SGO into its IFC\'s InitParam +0x1B0')
    check('image+kDelete)(o)' in h and 'emcReady[i]=false' in h, 'not carried: the round is deleted before it fires, the kind off')
    check(h.find('return true') < h.find('image+kDelete'), 'only a carried round survives')

check('(!kEmcFiles[i].model || ifcModelOk)' in bay, 'a model round is preloaded only with the model contract matched')
check(bay.count('{0x28FCEC,') == 1 and bay.count('{0x100321,') == 1 and bay.count('{0x5B56F9,') == 1,
      'the model contract\'s signatures are checked')
member = re.search(r'kModelMember\[\]=L"(\w+)"', header)
check(bool(member) and f"m['{member.group(1)}'] = barrier_model(weapon)" in gen,
      'make_proteus.py writes the member ifc_model.h reads')

for f in failures:
    print('FAIL', f)
print(f'ifc_model_guard: {"FAILED" if failures else "ok"}')
sys.exit(1 if failures else 0)
